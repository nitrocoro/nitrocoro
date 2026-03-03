/**
 * @file PgConnection.h
 * @brief PostgreSQL async connection using libpq
 */
#pragma once

#include "nitrocoro/pg/PgResult.h"
#include <nitrocoro/core/Scheduler.h>
#include <nitrocoro/core/Task.h>
#include <nitrocoro/io/IoChannel.h>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

struct pg_conn;
typedef struct pg_conn PGconn;

namespace nitrocoro::pg
{

using nitrocoro::Scheduler;
using nitrocoro::Task;
using nitrocoro::io::IoChannel;

class PgConnection
{
public:
    static Task<std::unique_ptr<PgConnection>> connect(std::string connStr,
                                                       Scheduler * scheduler = Scheduler::current());

    PgConnection(const PgConnection &) = delete;
    PgConnection & operator=(const PgConnection &) = delete;
    PgConnection(PgConnection &&) noexcept = default;
    PgConnection & operator=(PgConnection &&) noexcept = default;
    ~PgConnection();

    Scheduler * scheduler() const { return channel_->scheduler(); }
    bool isAlive() const;

    Task<PgResult> query(std::string_view sql, std::vector<PgValue> params = {});
    Task<> execute(std::string_view sql, std::vector<PgValue> params = {});

private:
    PgConnection(std::shared_ptr<PGconn> conn, std::unique_ptr<IoChannel> channel);

    Task<PgResult> sendAndReceive(std::string_view sql, std::vector<PgValue> params);

    std::shared_ptr<PGconn> pgConn_;
    std::unique_ptr<IoChannel> channel_;
};

} // namespace nitrocoro::pg
