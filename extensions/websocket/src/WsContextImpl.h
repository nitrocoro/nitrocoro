/**
 * @file WsContextImpl.h
 * @brief WebSocket request context — carries the upgrade request and response,
 *        and provides the accept() entry point to complete the handshake.
 */
#pragma once
#include <nitrocoro/websocket/WsContext.h>

#include <nitrocoro/core/Future.h>
#include <nitrocoro/http/HttpStream.h>

#include <atomic>

namespace nitrocoro::websocket
{

class WsContextImpl : public WsContext
{
public:
    WsContextImpl(http::IncomingRequestPtr req,
                  http::ServerResponsePtr resp,
                  Future<WsConnection> connFuture);
    ~WsContextImpl() override;

    const http::IncomingRequestPtr & req() const override
    {
        return req_;
    }
    const http::ServerResponsePtr & resp() const override
    {
        return resp_;
    }

    /** Completes the WebSocket handshake and returns the established connection. */
    Task<WsConnection> accept() override;

    SharedFuture<bool> acceptFuture() const
    {
        return acceptFuture_;
    }

private:
    http::IncomingRequestPtr req_;
    http::ServerResponsePtr resp_;

    std::atomic_flag accepted_{};
    Promise<bool> acceptPromise_;
    SharedFuture<bool> acceptFuture_;
    Future<WsConnection> connFuture_;
};

} // namespace nitrocoro::websocket
