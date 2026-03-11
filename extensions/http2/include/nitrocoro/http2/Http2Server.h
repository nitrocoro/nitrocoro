/**
 * @file Http2Server.h
 * @brief HTTP/2 server supporting h2 (TLS+ALPN) and h2c (plaintext)
 */
#pragma once

#include <nitrocoro/core/Future.h>
#include <nitrocoro/core/Task.h>
#include <nitrocoro/http/HttpRouter.h>
#include <nitrocoro/io/Stream.h>
#include <nitrocoro/net/TcpServer.h>

#include <functional>
#include <memory>

namespace nitrocoro::http2
{

class Http2Server
{
public:
    // Optional: wrap the raw TcpConnection into a different stream (e.g. TLS).
    // For h2 over TLS, set this to a TLS upgrader that negotiates ALPN "h2".
    using StreamUpgrader = std::function<Task<io::StreamPtr>(net::TcpConnectionPtr)>;

    explicit Http2Server(uint16_t port, Scheduler * scheduler = Scheduler::current());
    explicit Http2Server(uint16_t port, std::shared_ptr<http::HttpRouter> router,
                         Scheduler * scheduler = Scheduler::current());

    uint16_t listeningPort() const { return port_; }

    void setStreamUpgrader(StreamUpgrader upgrader) { upgrader_ = std::move(upgrader); }

    template <typename F>
    void route(const std::string & path, http::detail::MethodList methods, F && handler)
    {
        router_->addRoute(path, std::move(methods), std::forward<F>(handler));
    }

    std::shared_ptr<http::HttpRouter> router() const { return router_; }

    Task<> start();
    Task<> stop();
    SharedFuture<> started() const;

private:
    Task<> handleConnection(net::TcpConnectionPtr conn);

    uint16_t port_;
    Scheduler * scheduler_;
    StreamUpgrader upgrader_;
    std::shared_ptr<http::HttpRouter> router_;
    std::unique_ptr<net::TcpServer> server_;
};

} // namespace nitrocoro::http2
