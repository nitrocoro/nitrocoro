/**
 * @file PgPool.h
 * @brief Coroutine-aware PostgreSQL connection pool
 */
#pragma once

#include <nitrocoro/core/Future.h>
#include <nitrocoro/core/Mutex.h>
#include <nitrocoro/core/Scheduler.h>
#include <nitrocoro/core/Task.h>

#include <functional>
#include <memory>
#include <queue>

namespace nitrocoro::pg
{

using nitrocoro::Mutex;
using nitrocoro::Promise;
using nitrocoro::Scheduler;
using nitrocoro::Task;

class PgConnection;
class PgTransaction;
class PooledConnection;

class PgPool
{
public:
    struct PoolState;
    using Factory = std::function<Task<std::unique_ptr<PgConnection>>()>;

    PgPool(size_t maxSize, Factory factory, Scheduler * scheduler = Scheduler::current());
    ~PgPool();
    PgPool(const PgPool &) = delete;
    PgPool & operator=(const PgPool &) = delete;

    [[nodiscard]] Task<PooledConnection> acquire();
    [[nodiscard]] Task<PgTransaction> newTransaction();
    size_t idleCount() const;

private:
    std::shared_ptr<PoolState> state_;
    Factory factory_;
};

class PooledConnection
{
public:
    PooledConnection() = default;
    ~PooledConnection();

    PooledConnection(const PooledConnection &) = delete;
    PooledConnection & operator=(const PooledConnection &) = delete;
    PooledConnection(PooledConnection && other) noexcept;
    PooledConnection & operator=(PooledConnection && other) noexcept;

    PgConnection * operator->() const noexcept { return conn_.get(); }
    PgConnection & operator*() const noexcept { return *conn_; }
    explicit operator bool() const noexcept { return static_cast<bool>(conn_); }

    void reset() noexcept;
    std::unique_ptr<PgConnection> detach();

private:
    friend class PgPool;
    PooledConnection(std::unique_ptr<PgConnection> conn, std::weak_ptr<PgPool::PoolState> state);

    std::unique_ptr<PgConnection> conn_;
    std::weak_ptr<PgPool::PoolState> state_;
};

} // namespace nitrocoro::pg
