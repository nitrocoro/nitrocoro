/**
 * @file PgPool.h
 * @brief Coroutine-aware PostgreSQL connection pool
 */
#pragma once

#include <nitrocoro/core/CancelToken.h>
#include <nitrocoro/core/Scheduler.h>
#include <nitrocoro/core/Task.h>
#include <nitrocoro/pg/PgConfig.h>
#include <nitrocoro/pg/PgConnection.h>

#include <memory>

namespace nitrocoro::pg
{

struct PoolState;
class PgTransaction;

class PgPool
{
public:
    explicit PgPool(PgPoolConfig config, Scheduler * scheduler = Scheduler::current());
    ~PgPool();
    PgPool(const PgPool &) = delete;
    PgPool & operator=(const PgPool &) = delete;

    /**
     * Acquires a connection from the pool.
     *
     * If the pool has an idle connection, it is returned immediately.
     * If the pool is below capacity, a new connection is established.
     * Otherwise, the coroutine suspends until a connection becomes available.
     *
     * @param cancelToken  Optional cancellation token. When cancelled, the wait
     *                     is aborted and PgTimeoutError is thrown. If not provided,
     *                     defaults to a timeout derived from PgConnectConfig::connectTimeoutMs
     *                     (0 = wait indefinitely).
     * @throws PgTimeoutError      if the token is cancelled while waiting.
     * @throws PgConnectionError   if a new connection could not be established.
     */
    [[nodiscard]] Task<std::unique_ptr<PgConnection>> acquire(CancelToken cancelToken = {});
    [[nodiscard]] Task<std::unique_ptr<PgTransaction>> newTransaction();
    size_t idleCount() const;

private:
    std::shared_ptr<PoolState> state_;
    PgPoolConfig config_;
};

} // namespace nitrocoro::pg
