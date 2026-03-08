/**
 * @file PgConnectionImpl.h
 * @brief Internal libpq implementation of PgConnection
 */
#pragma once

#include <nitrocoro/core/CancelToken.h>
#include <nitrocoro/core/Future.h>
#include <nitrocoro/io/Channel.h>
#include <nitrocoro/pg/PgConnection.h>

#include <functional>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace nitrocoro::pg
{

class PgConnectionImpl : public PgConnection
{
    struct PgConnWrapper;
    struct ConnectionContext;

public:
    static Task<std::unique_ptr<PgConnectionImpl>> connect(const PgConnectConfig & config,
                                                           Scheduler * scheduler);
    static Task<std::unique_ptr<PgConnectionImpl>> connect(std::string connStr,
                                                           CancelToken cancelToken,
                                                           Scheduler * scheduler);

    explicit PgConnectionImpl(std::shared_ptr<ConnectionContext> ctx);
    ~PgConnectionImpl() override;

    PgConnectionImpl(const PgConnectionImpl &) = delete;
    PgConnectionImpl & operator=(const PgConnectionImpl &) = delete;
    PgConnectionImpl(PgConnectionImpl &&) = delete;
    PgConnectionImpl & operator=(PgConnectionImpl &&) = delete;

    Scheduler * scheduler() const override;
    bool isAlive() const override;

    Task<PgResult> query(std::string_view sql, std::vector<PgValue> params, CancelToken cancelToken) override;
    Task<> execute(std::string_view sql, std::vector<PgValue> params, CancelToken cancelToken) override;

    // void setNotifyHandler(std::function<void(std::string, std::string, int)> h) { ctx_->notifyHandler = std::move(h); }
    // void setDisconnectHandler(std::function<void()> h) { ctx_->disconnectHandler = std::move(h); }

private:
    Task<PgResult> sendAndReceive(std::string_view sql, std::vector<PgValue> params, CancelToken cancelToken);
    static Task<> readLoop(std::shared_ptr<ConnectionContext> ctx);

    std::shared_ptr<ConnectionContext> ctx_;
};

} // namespace nitrocoro::pg
