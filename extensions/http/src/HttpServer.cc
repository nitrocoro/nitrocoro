/**
 * @file HttpServer.cc
 * @brief HTTP server implementation
 */
#include <nitrocoro/http/HttpServer.h>

#include "HttpContext.h"
#include <nitrocoro/core/Future.h>
#include <nitrocoro/http/HttpHeader.h>
#include <nitrocoro/http/HttpMessage.h>
#include <nitrocoro/http/HttpRouter.h>
#include <nitrocoro/io/Stream.h>
#include <nitrocoro/utils/Debug.h>

namespace nitrocoro::http
{

HttpServer::HttpServer(uint16_t port, Scheduler * scheduler)
    : HttpServer(port, std::make_shared<HttpRouter>(), scheduler)
{
}

HttpServer::HttpServer(uint16_t port, std::shared_ptr<HttpRouter> router, Scheduler * scheduler)
    : port_(port), scheduler_(scheduler), router_(std::move(router))
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

void HttpServer::setRequestUpgrader(RequestUpgrader upgrader)
{
    requestUpgrader_ = std::move(upgrader);
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

        if (requestUpgrader_ && !request.getHeader(HttpHeader::Name::Upgrade_L).empty())
        {
            bool taken = co_await requestUpgrader_(request, stream);
            if (taken)
                co_return;
        }

        auto method = request.method();
        if (method == methods::_Invalid)
        {
            response.setStatus(StatusCode::k400BadRequest);
            co_await response.end("Bad Request");
            if (!bodyReader->isComplete())
                co_await bodyReader->drain();
            if (!keepAlive)
            {
                co_await stream->shutdown();
                co_return;
            }
            continue;
        }

        auto result = router_->route(method, request.path());
        if (result.reason != HttpRouter::RouteResult::Reason::Ok || !result.handler)
        {
            // TODO: custom handler
            if (result.reason == HttpRouter::RouteResult::Reason::MethodNotAllowed)
            {
                response.setStatus(StatusCode::k405MethodNotAllowed);
                co_await response.end("Method Not Allowed");
            }
            else
            {
                response.setStatus(StatusCode::k404NotFound);
                co_await response.end("Not Found");
            }
        }
        else
        {
            std::exception_ptr exPtr;
            try
            {
                co_await result.handler->invoke(std::move(request), std::move(response), std::move(result.params));
            }
            catch (const std::exception & ex)
            {
                NITRO_ERROR("Unhandled exception in handler: %s", ex.what());
                exPtr = std::current_exception();
            }
            catch (...)
            {
                NITRO_ERROR("Unhandled exception in handler");
                exPtr = std::current_exception();
            }
            // TODO: custom exception handler
            if (exPtr)
            {
                // TODO: should we send 500 and continue the connection?
                co_await stream->shutdown();
                break;
            }
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

SharedFuture<> HttpServer::started() const
{
    return server_->started();
}

SharedFuture<> HttpServer::wait() const
{
    return server_->wait();
}

} // namespace nitrocoro::http
