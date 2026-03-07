/**
 * @file PgConnection.h
 * @brief PostgreSQL async connection interface
 */
#pragma once

#include <nitrocoro/core/CancelToken.h>
#include <nitrocoro/core/Scheduler.h>
#include <nitrocoro/core/Task.h>
#include <nitrocoro/pg/PgConfig.h>
#include <nitrocoro/pg/PgResult.h>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace nitrocoro::pg
{

class PgConnection
{
public:
    static Task<std::unique_ptr<PgConnection>> connect(const PgConnectConfig & config,
                                                       Scheduler * scheduler = Scheduler::current());
    static Task<std::unique_ptr<PgConnection>> connect(std::string connStr,
                                                       CancelToken cancelToken = {},
                                                       Scheduler * scheduler = Scheduler::current());

    PgConnection(const PgConnection &) = delete;
    PgConnection & operator=(const PgConnection &) = delete;
    PgConnection(PgConnection &&) = delete;
    PgConnection & operator=(PgConnection &&) = delete;
    virtual ~PgConnection() = default;

    virtual Scheduler * scheduler() const = 0;
    virtual bool isAlive() const = 0;

    // Cancellation note: if cancelToken fires after the query has been sent to the server,
    // the server may still execute it. The connection state is undefined after any exception;
    // do not reuse the connection — discard it and acquire a new one (preferably via a pool).
    virtual Task<PgResult> query(std::string_view sql, std::vector<PgValue> params, CancelToken cancelToken) = 0;
    virtual Task<> execute(std::string_view sql, std::vector<PgValue> params, CancelToken cancelToken) = 0;

    Task<PgResult> query(std::string_view sql, std::vector<PgValue> params);
    Task<PgResult> query(std::string_view sql, CancelToken cancelToken = {});
    Task<> execute(std::string_view sql, std::vector<PgValue> params);
    Task<> execute(std::string_view sql, CancelToken cancelToken = {});

protected:
    PgConnection() = default;
};

} // namespace nitrocoro::pg
