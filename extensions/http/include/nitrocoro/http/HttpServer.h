/**
 * @file HttpServer.h
 * @brief HTTP server based on TcpServer
 */
#pragma once
#include <nitrocoro/http/HttpRouter.h>

#include <nitrocoro/core/Task.h>
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
    template <typename F>
    void route(const std::string & path, HttpMethods methods, F && handler)
    {
        router_->addRoute(path, std::move(methods), std::forward<F>(handler));
    }

    template <typename F>
    void routeRegex(const std::string & pattern, HttpMethods methods, F && handler)
    {
        router_->addRouteRegex(pattern, std::move(methods), std::forward<F>(handler));
    }

    std::shared_ptr<HttpRouter> router() const { return router_; }
    Task<> start();
    Task<> stop();

    SharedFuture<> started() const;
    SharedFuture<> wait() const;

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
