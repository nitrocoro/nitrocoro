/**
 * @file WsContext.h
 * @brief WebSocket request context — carries the upgrade request and response,
 *        and provides the accept() entry point to complete the handshake.
 */
#pragma once
#include <nitrocoro/websocket/WsConnection.h>

#include <nitrocoro/http/HttpStream.h>

namespace nitrocoro::websocket
{

class WsContext;
using WsContextPtr = std::shared_ptr<WsContext>;

class WsContext
{
public:
    virtual ~WsContext() = default;

    /** Completes the WebSocket handshake and returns the established connection. */
    virtual const http::IncomingRequestPtr & req() const = 0;
    virtual const http::ServerResponsePtr & resp() const = 0;
    virtual Task<WsConnection> accept() = 0;
};

} // namespace nitrocoro::websocket
