/**
 * @file PgPool.cc
 * @brief PgPool implementation
 */
#include <nitrocoro/pg/PgPool.h>

#include "PgConnectionImpl.h"
#include "PoolState.h"
#include "PooledConnection.h"
#include <nitrocoro/pg/PgTransaction.h>

namespace nitrocoro::pg
{

PgPool::PgPool(PgPoolConfig config, Scheduler * scheduler)
    : state_(std::make_shared<PoolState>(scheduler, config.maxSize))
    , config_(std::move(config))
    , connStr_(config_.connect.toConnStr())
{
}

PgPool::~PgPool() = default;

size_t PgPool::idleCount() const
{
    return state_->idle.size();
}

Task<std::unique_ptr<PgConnection>> PgPool::acquire()
{
    std::unique_ptr<PgConnectionImpl> conn;
    {
        [[maybe_unused]] auto lock = co_await state_->mutex.scoped_lock();
        if (!state_->idle.empty())
        {
            conn = std::move(state_->idle.front());
            state_->idle.pop();
        }
        else if (state_->totalCount < state_->maxSize)
        {
            ++state_->totalCount;
        }
        else
        {
            Promise<std::unique_ptr<PgConnectionImpl>> promise(state_->scheduler);
            auto future = promise.get_future();
            state_->waiters.push(std::move(promise));
            lock.unlock();
            conn = co_await future.get();
        }
    }

    if (!conn)
    {
        std::exception_ptr err;
        try
        {
            conn = co_await PgConnectionImpl::connect(connStr_, state_->scheduler);
        }
        catch (...)
        {
            err = std::current_exception();
        }

        if (err)
        {
            [[maybe_unused]] auto lock = co_await state_->mutex.scoped_lock();
            --state_->totalCount;
            std::rethrow_exception(err);
        }
    }

    co_return std::make_unique<PooledConnection>(std::move(conn), std::weak_ptr<PoolState>(state_));
}

Task<std::unique_ptr<PgTransaction>> PgPool::newTransaction()
{
    auto conn = co_await acquire();
    co_return co_await PgTransaction::begin(std::move(conn));
}

} // namespace nitrocoro::pg
