/**
 * @file PgConnection.h
 * @brief PostgreSQL async connection using libpq
 */
#pragma once

#include <nitrocoro/core/Scheduler.h>
#include <nitrocoro/core/Task.h>
#include <nitrocoro/io/IoChannel.h>
#include <nitrocoro/pg/PgResult.h>

#include <libpq-fe.h>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

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
    ~PgConnection();

    Task<std::unique_ptr<PgResult>> query(std::string_view sql, std::vector<PgValue> params = {});
    Task<> execute(std::string_view sql, std::vector<PgValue> params = {});
    Task<> begin();
    Task<> commit();
    Task<> rollback();
    bool isAlive() const;

private:
    PgConnection(std::shared_ptr<PGconn> conn, std::unique_ptr<IoChannel> channel);

    Task<std::unique_ptr<PgResult>> sendAndReceive(std::string_view sql, std::vector<PgValue> params);

    std::shared_ptr<PGconn> pgConn_;
    std::unique_ptr<IoChannel> channel_;
};

} // namespace nitrocoro::pg
