/**
 * @file PgPool.h
 * @brief Coroutine-aware PostgreSQL connection pool
 */
#pragma once

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

    [[nodiscard]] Task<std::unique_ptr<PgConnection>> acquire();
    [[nodiscard]] Task<std::unique_ptr<PgTransaction>> newTransaction();
    size_t idleCount() const;

private:
    std::shared_ptr<PoolState> state_;
    PgPoolConfig config_;
    std::string connStr_;
};

} // namespace nitrocoro::pg
