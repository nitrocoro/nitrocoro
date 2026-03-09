/**
 * @file HttpServer.h
 * @brief HTTP server based on TcpServer
 */
#pragma once

#include <nitrocoro/core/Task.h>
#include <nitrocoro/http/HttpRouter.h>
#include <nitrocoro/io/Stream.h>
#include <nitrocoro/net/TcpServer.h>

#include <functional>
#include <memory>
#include <string>

namespace nitrocoro::http
{

class HttpServer
{
public:
    using Handler = HttpRouter::Handler;
    using StreamUpgrader = std::function<Task<io::StreamPtr>(net::TcpConnectionPtr)>;
    // RequestUpgrader: called when request has "Connection: Upgrade".
    // Receives the request and the raw stream; returns true if the connection was taken over.
    using RequestUpgrader = std::function<Task<bool>(HttpIncomingStream<HttpRequest> &, io::StreamPtr)>;

    explicit HttpServer(uint16_t port, Scheduler * scheduler = Scheduler::current());
    explicit HttpServer(uint16_t port, std::shared_ptr<HttpRouter> router, Scheduler * scheduler = Scheduler::current());

    uint16_t listeningPort() const { return port_; }

    void setStreamUpgrader(StreamUpgrader upgrader);
    void setRequestUpgrader(RequestUpgrader upgrader);
    // Convenience: forwards to the internal router.
    void route(const std::string & method, const std::string & path, Handler handler);

    std::shared_ptr<HttpRouter> router() const { return router_; }
    Task<> start();
    Task<> stop();

private:
    Task<> handleConnection(net::TcpConnectionPtr conn);

    uint16_t port_;
    Scheduler * scheduler_;
    StreamUpgrader upgrader_;
    RequestUpgrader requestUpgrader_;
    std::shared_ptr<HttpRouter> router_;
    std::unique_ptr<net::TcpServer> server_;
};

} // namespace nitrocoro::http
