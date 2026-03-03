/**
 * @file PgPool.cc
 * @brief PgPool implementation
 */
#include "nitrocoro/pg/PgPool.h"
#include "nitrocoro/pg/PgConnection.h"
#include "nitrocoro/pg/PgTransaction.h"
#include <nitrocoro/utils/Debug.h>

namespace nitrocoro::pg
{

struct PgPool::PoolState
{
    Scheduler * scheduler;
    size_t maxSize;
    size_t totalCount = 0;
    Mutex mutex;
    std::queue<std::unique_ptr<PgConnection>> idle;
    std::queue<Promise<std::unique_ptr<PgConnection>>> waiters;
};

static void returnConnection(const auto & weakState, PgConnection * conn) noexcept
{
    auto state = weakState.lock();
    if (!state)
    {
        delete conn;
        return;
    }

    std::unique_ptr<PgConnection> connPtr(conn);
    state->scheduler->spawn([state, connPtr = std::move(connPtr)]() mutable -> Task<> {
        [[maybe_unused]] auto lock = co_await state->mutex.scoped_lock();

        if (!connPtr->isAlive())
        {
            NITRO_ERROR("PgPool: connection dead, discarding\n");
            --state->totalCount;
            if (!state->waiters.empty())
            {
                state->waiters.front().set_exception(
                    std::make_exception_ptr(std::runtime_error("PgPool: connection dead")));
                state->waiters.pop();
            }
        }
        else if (!state->waiters.empty())
        {
            state->waiters.front().set_value(std::move(connPtr));
            state->waiters.pop();
        }
        else
        {
            state->idle.push(std::move(connPtr));
        }
    });
}

static void detachConnection(const auto & weakState) noexcept
{
    auto state = weakState.lock();
    if (!state)
    {
        return;
    }

    state->scheduler->spawn([state]() -> Task<> {
        [[maybe_unused]] auto lock = co_await state->mutex.scoped_lock();
        --state->totalCount;
    });
}

PgPool::PgPool(size_t maxSize, Factory factory, Scheduler * scheduler)
    : state_(std::make_shared<PoolState>(scheduler, maxSize))
    , factory_(std::move(factory))
{
}

PgPool::~PgPool() = default;

size_t PgPool::idleCount() const
{
    return state_->idle.size();
}

Task<PooledConnection> PgPool::acquire()
{
    std::unique_ptr<PgConnection> conn;
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
            Promise<std::unique_ptr<PgConnection>> promise(state_->scheduler);
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
            conn = co_await factory_();
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

    co_return PooledConnection{ std::move(conn), std::weak_ptr<PoolState>(state_) };
}

Task<PgTransaction> PgPool::newTransaction()
{
    auto conn = co_await acquire();
    co_await conn->begin();
    co_return PgTransaction(std::move(conn), state_->scheduler);
}

// PooledConnection implementation
PooledConnection::PooledConnection(std::unique_ptr<PgConnection> conn, std::weak_ptr<PgPool::PoolState> state)
    : conn_(std::move(conn)), state_(std::move(state))
{
}

PooledConnection::~PooledConnection()
{
    reset();
}

PooledConnection::PooledConnection(PooledConnection && other) noexcept
    : conn_(std::move(other.conn_)), state_(std::move(other.state_))
{
}

PooledConnection & PooledConnection::operator=(PooledConnection && other) noexcept
{
    if (this != &other)
    {
        reset();
        conn_ = std::move(other.conn_);
        state_ = std::move(other.state_);
    }
    return *this;
}

void PooledConnection::reset() noexcept
{
    if (conn_)
    {
        returnConnection(state_, conn_.release());
        state_.reset();
    }
}

std::unique_ptr<PgConnection> PooledConnection::detach()
{
    if (conn_)
    {
        detachConnection(state_);
        state_.reset();
        return std::move(conn_);
    }
    return nullptr;
}

} // namespace nitrocoro::pg
