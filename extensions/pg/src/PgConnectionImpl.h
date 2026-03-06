/**
 * @file PgConnectionImpl.h
 * @brief Internal libpq implementation of PgConnection
 */
#pragma once

#include <nitrocoro/core/CancelToken.h>
#include <nitrocoro/io/Channel.h>
#include <nitrocoro/pg/PgConnection.h>

#include <memory>
#include <string_view>
#include <vector>

namespace nitrocoro::pg
{

class PgConnectionImpl : public PgConnection
{
    struct PgConnWrapper;

public:
    static Task<std::unique_ptr<PgConnectionImpl>> connect(const PgConnectConfig & config, Scheduler * scheduler);
    static Task<std::unique_ptr<PgConnectionImpl>> connect(std::string connStr,
                                                           Scheduler * scheduler,
                                                           CancelToken cancelToken = {});

    PgConnectionImpl(std::shared_ptr<PgConnWrapper> conn, std::unique_ptr<io::Channel> channel);
    ~PgConnectionImpl() override = default;

    PgConnectionImpl(const PgConnectionImpl &) = delete;
    PgConnectionImpl & operator=(const PgConnectionImpl &) = delete;
    PgConnectionImpl(PgConnectionImpl &&) = delete;
    PgConnectionImpl & operator=(PgConnectionImpl &&) = delete;

    Scheduler * scheduler() const override;
    bool isAlive() const override;

    Task<PgResult> query(std::string_view sql, std::vector<PgValue> params) override;
    Task<> execute(std::string_view sql, std::vector<PgValue> params) override;

private:
    Task<PgResult> sendAndReceive(std::string_view sql, std::vector<PgValue> params);

    std::shared_ptr<PgConnWrapper> pgConn_;
    std::unique_ptr<io::Channel> channel_;
};

} // namespace nitrocoro::pg
