/**
 * @file HttpServer.cc
 * @brief HTTP server implementation
 */
#include <nitrocoro/http/HttpServer.h>

#include "HttpContext.h"
#include <nitrocoro/core/Future.h>
#include <nitrocoro/http/HttpMessage.h>
#include <nitrocoro/io/Stream.h>
#include <nitrocoro/utils/Debug.h>

namespace nitrocoro::http
{

HttpServer::HttpServer(uint16_t port, Scheduler * scheduler)
    : port_(port), scheduler_(scheduler)
{
    server_ = std::make_unique<net::TcpServer>(port_, scheduler_);
    if (port_ == 0)
    {
        port_ = server_->port();
    }
}

void HttpServer::setStreamUpgrader(StreamUpgrader upgrader)
{
    upgrader_ = std::move(upgrader);
}

void HttpServer::route(const std::string & method, const std::string & path, Handler handler)
{
    routes_[{ method, path }] = std::move(handler);
}

Task<> HttpServer::start()
{
    NITRO_INFO("HTTP server listening on port %hu", port_);

    co_await server_->start([this](net::TcpConnectionPtr conn) -> Task<> {
        try
        {
            co_await handleConnection(conn);
        }
        catch (const std::exception & e)
        {
            NITRO_ERROR("Error handling connection: %s", e.what());
        }
        catch (...)
        {
            NITRO_ERROR("Unknown error handling connection");
        }
        // TODO: force close?
    });
}

Task<> HttpServer::stop()
{
    if (server_)
    {
        co_await server_->stop();
    }
}

Task<> HttpServer::handleConnection(net::TcpConnectionPtr conn)
{
    io::StreamPtr stream;

    if (upgrader_)
    {
        // Use upgrader to upgrade connection (e.g., TLS handshake)
        stream = co_await upgrader_(conn);
        if (!stream)
        {
            // Upgrade failed, close connection
            co_await conn->shutdown();
            co_return;
        }
    }
    else
    {
        // No upgrader, use TcpConnection directly
        stream = std::make_shared<io::Stream>(conn);
    }

    auto buffer = std::make_shared<utils::StringBuffer>();
    HttpContext<HttpRequest> context(stream, buffer);
    std::optional<Future<>> prevFuture;
    while (true)
    {
        auto message = co_await context.receiveMessage();
        if (!message)
            co_return; // shutdown?
        bool keepAlive = message->keepAlive;

        // TODO: BodyReader should read regardless of the request border or user desired length!!!
        auto bodyReader = BodyReader::create(stream, buffer, message->transferMode, message->contentLength);

        auto request = HttpIncomingStream<HttpRequest>(std::move(*message), bodyReader);
        Promise<> finishedPromise(scheduler_);
        auto finishedFuture = finishedPromise.get_future();
        HttpOutgoingStream<HttpResponse> response(stream, std::move(finishedPromise), std::move(prevFuture));
        prevFuture = std::move(finishedFuture);
        response.setCloseConnection(!keepAlive);

        auto key = std::make_pair(std::string{ request.method() }, std::string{ request.path() });
        auto it = routes_.find(key);
        if (it != routes_.end())
        {
            co_await it->second(request, response);
        }
        else
        {
            response.setStatus(StatusCode::k404NotFound);
            co_await response.end("Not Found");
        }

        if (!bodyReader->isComplete())
            co_await bodyReader->drain();
        if (!keepAlive)
        {
            co_await stream->shutdown();
            break;
        }
    }
}

} // namespace nitrocoro::http
