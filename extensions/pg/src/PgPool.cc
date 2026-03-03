/**
 * @file PgPool.cc
 * @brief PgPool implementation
 */
#include "nitrocoro/pg/PgPool.h"
#include "nitrocoro/pg/PgTransaction.h"
#include <nitrocoro/utils/Debug.h>

namespace nitrocoro::pg
{

PooledConnection::PooledConnection(std::shared_ptr<PgConnection> conn,
                                   std::function<void(std::shared_ptr<PgConnection>)> returnFn)
    : conn_(std::move(conn)), returnFn_(std::move(returnFn))
{
}

PooledConnection::~PooledConnection() noexcept
{
    if (conn_)
    {
        try
        {
            returnFn_(std::move(conn_));
        }
        catch (...)
        {
        }
    }
}

Task<PooledConnection> PgPool::acquire()
{
    std::shared_ptr<PgConnection> conn;
    {
        [[maybe_unused]] auto lock = co_await mutex_.scoped_lock();
        if (!idle_.empty())
        {
            conn = std::move(idle_.front());
            idle_.pop();
        }
        else if (totalCount_ < maxSize_)
        {
            ++totalCount_;
        }
        else
        {
            Promise<std::shared_ptr<PgConnection>> promise(scheduler_);
            auto future = promise.get_future();
            waiters_.push(std::move(promise));
            lock.unlock();
            conn = co_await future.get();
        }
    }

    if (!conn)
    {
        // 锁外异步建连接
        std::exception_ptr err;
        try
        {
            conn = co_await factory_();
        }
        catch (...)
        {
            err = std::current_exception();
        }

        if (err)
        {
            [[maybe_unused]] auto lock = co_await mutex_.scoped_lock();
            --totalCount_;
            std::rethrow_exception(err);
        }
    }

    co_return PooledConnection(std::move(conn), [this](std::shared_ptr<PgConnection> c) {
        returnConnection(std::move(c));
    });
}

void PgPool::returnConnection(std::shared_ptr<PgConnection> conn) noexcept
{
    scheduler_->spawn([this, conn = std::move(conn)]() mutable -> Task<> {
        [[maybe_unused]] auto lock = co_await mutex_.scoped_lock();
        if (!conn->isAlive())
        {
            NITRO_ERROR("PgPool: connection dead, discarding\n");
            --totalCount_;
            if (!waiters_.empty())
            {
                waiters_.front().set_exception(
                    std::make_exception_ptr(std::runtime_error("PgPool: connection dead")));
                waiters_.pop();
            }
        }
        else if (!waiters_.empty())
        {
            waiters_.front().set_value(std::move(conn));
            waiters_.pop();
        }
        else
        {
            idle_.push(std::move(conn));
        }
    });
}

Task<PgTransaction> PgPool::newTransaction()
{
    auto conn = co_await acquire();
    co_await conn->begin();
    co_return PgTransaction(std::move(conn), scheduler_);
}

} // namespace nitrocoro::pg
