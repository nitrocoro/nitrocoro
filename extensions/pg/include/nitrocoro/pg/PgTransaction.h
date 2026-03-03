/**
 * @file PgTransaction.h
 * @brief RAII PostgreSQL transaction with automatic rollback on destruction
 */
#pragma once
#include "nitrocoro/pg/PgConnection.h"
#include "nitrocoro/pg/PgPool.h"
#include "nitrocoro/pg/PgResult.h"
#include <nitrocoro/core/Scheduler.h>
#include <nitrocoro/core/Task.h>

#include <memory>
#include <string_view>
#include <vector>

namespace nitrocoro::pg
{

using nitrocoro::Scheduler;
using nitrocoro::Task;

class PgTransaction
{
public:
    // Static factory methods
    static Task<PgTransaction> begin(PooledConnection && conn);
    static Task<PgTransaction> begin(PgConnection && conn);

    ~PgTransaction();

    PgTransaction(const PgTransaction &) = delete;
    PgTransaction & operator=(const PgTransaction &) = delete;
    PgTransaction(PgTransaction && other) noexcept;
    PgTransaction & operator=(PgTransaction && other) noexcept;

    Task<PgResult> query(std::string_view sql, std::vector<PgValue> params = {});
    Task<> execute(std::string_view sql, std::vector<PgValue> params = {});
    Task<> commit();
    Task<> rollback();

private:
    PgTransaction(PooledConnection conn, Scheduler * scheduler);
    PgTransaction(PgConnection conn, Scheduler * scheduler);

    PgConnection * conn_{ nullptr };
    PooledConnection pooledConn_;
    std::unique_ptr<PgConnection> ownedConn_;
    Scheduler * scheduler_{ nullptr };
    bool done_{ false };
};

} // namespace nitrocoro::pg
