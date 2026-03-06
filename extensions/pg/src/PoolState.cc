/**
 * @file PoolState.cc
 * @brief PoolState and pool helper functions implementation
 */
#include "PoolState.h"

#include <nitrocoro/pg/PgException.h>
#include <nitrocoro/utils/Debug.h>

namespace nitrocoro::pg
{

void PoolState::returnConnection(const std::weak_ptr<PoolState> & weakState,
                                 std::unique_ptr<PgConnectionImpl> conn) noexcept
{
    auto state = weakState.lock();
    if (!state)
        return;

    state->scheduler->spawn([state, conn = std::move(conn)]() mutable -> Task<> {
        [[maybe_unused]] auto lock = co_await state->mutex.scoped_lock();

        if (!conn->isAlive())
        {
            NITRO_ERROR("PgPool: connection dead, discarding");
            --state->totalCount;
            if (!state->waiters.empty())
            {
                state->waiters.front().set_exception(
                    std::make_exception_ptr(PgConnectionError("PgPool: connection dead")));
                state->waiters.pop();
            }
        }
        else if (!state->waiters.empty())
        {
            state->waiters.front().set_value(std::move(conn));
            state->waiters.pop();
        }
        else
        {
            state->idle.push(std::move(conn));
        }
    });
}

void PoolState::detachConnection(const std::weak_ptr<PoolState> & weakState) noexcept
{
    auto state = weakState.lock();
    if (!state)
        return;
    state->scheduler->spawn([state]() -> Task<> {
        [[maybe_unused]] auto lock = co_await state->mutex.scoped_lock();
        --state->totalCount;
    });
}

} // namespace nitrocoro::pg
