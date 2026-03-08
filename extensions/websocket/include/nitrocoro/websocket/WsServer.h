/**
 * @file WsServer.h
 * @brief WebSocket server — attaches to HttpServer via RequestUpgrader
 */
#pragma once

#include <nitrocoro/http/HttpServer.h>
#include <nitrocoro/websocket/WsConnection.h>

#include <functional>
#include <map>
#include <string>

namespace nitrocoro::websocket
{

class WsServer
{
public:
    using Handler = std::function<Task<>(WsConnection &)>;

    void route(const std::string & path, Handler handler);

    /** Registers this WsServer as the RequestUpgrader on the given HttpServer. */
    void attachTo(http::HttpServer & server);

private:
    Task<bool> handleUpgrade(http::HttpIncomingStream<http::HttpRequest> & req, io::StreamPtr stream);

    std::map<std::string, Handler> routes_;
};

} // namespace nitrocoro::websocket
