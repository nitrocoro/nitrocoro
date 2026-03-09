/**
 * @file PoolState.cc
 * @brief PoolState and pool helper functions implementation
 */
#include "PoolState.h"

#include <nitrocoro/core/CancelToken.h>
#include <nitrocoro/pg/PgException.h>
#include <nitrocoro/utils/Debug.h>

namespace nitrocoro::pg
{

void PoolState::dispatch(const std::shared_ptr<PoolState> & state)
{
    // skip cancelled waiters
    while (!state->waiters.empty() && state->waiters.front()->cancelled)
        state->waiters.pop();

    if (state->waiters.empty())
        return;

    if (!state->idle.empty())
    {
        state->waiters.front()->promise.set_value(std::move(state->idle.front()));
        state->idle.pop();
        state->waiters.pop();
    }
    else if (state->totalCount < state->maxSize)
    {
        ++state->totalCount;
        auto waiter = std::move(state->waiters.front());
        state->waiters.pop();
        state->scheduler->spawn([state, waiter = std::move(waiter)]() mutable -> Task<> {
            std::unique_ptr<PgConnectionImpl> conn;
            std::exception_ptr exPtr;
            try
            {
                CancelSource src(state->scheduler);
                if (state->connectTimeoutMs > 0)
                    src.cancelAfter(std::chrono::milliseconds(state->connectTimeoutMs));
                conn = co_await PgConnectionImpl::connect(state->connStr, src.token(), state->scheduler);
            }
            catch (...)
            {
                exPtr = std::current_exception();
            }

            if (exPtr)
            {
                --state->totalCount;
                if (!waiter->cancelled)
                    waiter->promise.set_exception(exPtr);
                dispatch(state);
            }
            else if (!waiter->cancelled)
            {
                waiter->promise.set_value(std::move(conn));
            }
            else
            {
                state->idle.push(std::move(conn));
                dispatch(state);
            }
        });
    }
}

void PoolState::returnConnection(const std::weak_ptr<PoolState> & weakState,
                                 std::unique_ptr<PgConnectionImpl> conn) noexcept
{
    auto state = weakState.lock();
    if (!state)
        return;

    state->scheduler->dispatch([state, conn = std::move(conn)]() mutable {
        if (!conn->isAlive())
        {
            NITRO_DEBUG("PgPool: connection dead, discarding");
            --state->totalCount;
        }
        else
        {
            state->idle.push(std::move(conn));
        }
        dispatch(state);
    });
}

void PoolState::detachConnection(const std::weak_ptr<PoolState> & weakState) noexcept
{
    auto state = weakState.lock();
    if (!state)
        return;
    state->scheduler->dispatch([state]() {
        --state->totalCount;
        dispatch(state);
    });
}

} // namespace nitrocoro::pg
