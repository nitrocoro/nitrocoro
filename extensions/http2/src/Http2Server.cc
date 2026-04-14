/**
 * @file Http2Server.cc
 */
#include <nitrocoro/http2/Http2Server.h>

#include "Http2Session.h"
#include <nitrocoro/utils/Debug.h>

namespace nitrocoro::http2
{

Http2Server::Http2Server(uint16_t port, Scheduler * scheduler)
    : Http2Server(port, std::make_shared<http::HttpRouter>(), scheduler)
{
}

Http2Server::Http2Server(uint16_t port, std::shared_ptr<http::HttpRouter> router,
                         Scheduler * scheduler)
    : port_(port), scheduler_(scheduler), router_(std::move(router))
{
    server_ = std::make_unique<net::TcpServer>(port_, scheduler_);
    if (port_ == 0)
        port_ = server_->port();
}

Task<> Http2Server::start()
{
    NITRO_INFO("HTTP/2 server listening on port %hu", port_);
    co_await server_->start([this](net::TcpConnectionPtr conn) -> Task<> {
        try
        {
            co_await handleConnection(std::move(conn));
        }
        catch (const std::exception & e)
        {
            NITRO_ERROR("HTTP/2 connection error: %s", e.what());
        }
        catch (...)
        {
            NITRO_ERROR("HTTP/2 connection: unknown error");
        }
    });
}

Task<> Http2Server::stop()
{
    if (server_)
        co_await server_->stop();
}

SharedFuture<> Http2Server::started() const
{
    return server_->started();
}

Task<> Http2Server::handleConnection(net::TcpConnectionPtr conn)
{
    io::StreamPtr stream;
    if (upgrader_)
    {
        stream = co_await upgrader_(conn);
        if (!stream)
        {
            co_await conn->shutdown();
            co_return;
        }
    }
    else
    {
        stream = std::make_shared<io::Stream>(conn);
    }

    auto session = std::make_shared<Http2Session>(std::move(stream), router_, scheduler_);
    co_await session->run();
}

} // namespace nitrocoro::http2
