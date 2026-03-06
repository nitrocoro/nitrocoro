/**
 * @file PgPool.cc
 * @brief PgPool implementation
 */
#include <nitrocoro/pg/PgPool.h>

#include "PgConnectionImpl.h"
#include "PoolState.h"
#include "PooledConnection.h"
#include <nitrocoro/pg/PgException.h>
#include <nitrocoro/pg/PgTransaction.h>

namespace nitrocoro::pg
{

PgPool::PgPool(PgPoolConfig config, Scheduler * scheduler)
    : config_(std::move(config))
{
    auto state = std::make_shared<PoolState>();
    state->scheduler = scheduler;
    state->maxSize = config_.maxSize;
    state->connStr = config_.connect.toConnStr();
    state->connectTimeoutMs = config_.connect.connectTimeoutMs;
    state_ = std::move(state);
}

PgPool::~PgPool() = default;

size_t PgPool::idleCount() const
{
    return state_->idle.size();
}

Task<std::unique_ptr<PgConnection>> PgPool::acquire(CancelToken cancelToken)
{
    co_await state_->scheduler->switch_to();

    CancelSource defaultSrc(state_->scheduler);
    if (!cancelToken && state_->connectTimeoutMs > 0)
    {
        defaultSrc.cancelAfter(std::chrono::milliseconds(state_->connectTimeoutMs));
        cancelToken = defaultSrc.token();
    }

    auto waiter = std::make_shared<PoolState::Waiter>();
    waiter->cancelReg = cancelToken.onCancel([w = std::weak_ptr(waiter)] {
        if (auto p = w.lock(); p && !p->cancelled)
        {
            p->cancelled = true;
            p->promise.set_exception(
                std::make_exception_ptr(PgTimeoutError("PgPool: acquire timed out")));
        }
    });

    auto future = waiter->promise.get_future();
    state_->waiters.push(waiter);
    PoolState::dispatch(state_);
    auto conn = co_await future.get();
    co_return std::make_unique<PooledConnection>(std::move(conn), std::weak_ptr<PoolState>(state_));
}

Task<std::unique_ptr<PgTransaction>> PgPool::newTransaction()
{
    auto conn = co_await acquire();
    co_return co_await PgTransaction::begin(std::move(conn));
}

} // namespace nitrocoro::pg
