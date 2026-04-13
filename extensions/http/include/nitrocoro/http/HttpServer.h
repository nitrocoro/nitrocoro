/**
 * @file HttpServer.h
 * @brief HTTP server based on TcpServer
 */
#pragma once
#include <nitrocoro/http/HttpRouter.h>
#include <nitrocoro/http/Middleware.h>

#include <nitrocoro/core/Task.h>
#include <nitrocoro/io/Stream.h>
#include <nitrocoro/net/TcpServer.h>
#include <nitrocoro/utils/StringBuffer.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace nitrocoro::http
{

struct HttpServerConfig
{
    uint16_t port{ 0 };
    std::shared_ptr<HttpRouter> router;
    bool send_date_header{ true };
};

class HttpServer
{
public:
    using StreamUpgrader = std::function<Task<io::StreamPtr>(net::TcpConnectionPtr)>;
    using StreamHandler = std::function<Task<>(io::StreamPtr)>;
    // RequestUpgrader: called when request has "Connection: Upgrade".
    // Returns a StreamHandler to take over the connection, or nullptr to decline.
    // HttpServer flushes the response before invoking the StreamHandler.
    using RequestUpgrader = std::function<Task<StreamHandler>(IncomingRequestPtr, ServerResponsePtr)>;

    explicit HttpServer(uint16_t port, Scheduler * scheduler = Scheduler::current());
    explicit HttpServer(HttpServerConfig config, Scheduler * scheduler = Scheduler::current());

    uint16_t listeningPort() const { return port_; }

    void setStreamUpgrader(StreamUpgrader upgrader);
    void setRequestUpgrader(RequestUpgrader upgrader);
    void use(Middleware middleware);

    // Convenience: forwards to the internal router.
    template <typename F>
    void route(const std::string & path, detail::MethodList methods, F && handler)
    {
        router_->addRoute(path, std::move(methods), std::forward<F>(handler));
    }

    template <typename F>
    void routeRegex(const std::string & pattern, detail::MethodList methods, F && handler)
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

    struct HandleResult
    {
        enum class Action
        {
            Disconnected,
            Close,
            Shutdown,
            Continue
        };

        Action action{ Action::Continue };
        ServerResponsePtr resp;
        std::shared_ptr<BodyReader> bodyReader;
    };
    Task<HandleResult> handleNextRequest(
        io::StreamPtr, std::shared_ptr<utils::StringBuffer>);

    HttpServerConfig config_;
    Scheduler * scheduler_;
    uint16_t port_;
    StreamUpgrader upgrader_;
    RequestUpgrader requestUpgrader_;
    std::vector<Middleware> middlewares_;
    std::shared_ptr<HttpRouter> router_;
    std::unique_ptr<net::TcpServer> server_;
};

} // namespace nitrocoro::http
