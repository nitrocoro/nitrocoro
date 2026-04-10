/**
 * @file HttpServer.cc
 * @brief HTTP server implementation
 */
#include <nitrocoro/http/HttpServer.h>

#include "HttpParser.h"

#include <nitrocoro/core/Future.h>
#include <nitrocoro/http/HttpHeader.h>
#include <nitrocoro/http/HttpMessage.h>
#include <nitrocoro/http/HttpRouter.h>
#include <nitrocoro/io/Stream.h>
#include <nitrocoro/utils/Debug.h>

namespace nitrocoro::http
{

static constexpr size_t kMaxHeaderCount = 128;
static constexpr size_t kMaxHeaderLineSize = 8192;

static bool isUpgradeRequest(const HttpIncomingStream<HttpRequest> & request)
{
    const auto & connection = request.getHeader(HttpHeader::NameCode::Connection);
    if (connection.empty())
    {
        return false;
    }
    auto lower = HttpHeader::toLower(connection);
    if (lower.find("upgrade") == std::string_view::npos)
    {
        return false;
    }
    if (request.getHeader(HttpHeader::NameCode::Upgrade).empty())
    {
        return false;
    }
    return true;
}

static Task<HttpParseResult<HttpRequest>> parseNext(io::StreamPtr stream, std::shared_ptr<utils::StringBuffer> buffer)
{
    HttpParser<HttpRequest> parser;
    int lines = 0;

    while (true)
    {
        size_t pos = buffer->find("\r\n");
        if (pos == std::string::npos)
        {
            if (buffer->remainSize() > kMaxHeaderLineSize)
            {
                co_return HttpParseError{ HttpParseError::MalformedRequestLine, "Header line too long" };
            }

            char * writePtr = buffer->prepareWrite(4096);
            size_t n = co_await stream->read(writePtr, 4096);
            if (n == 0)
            {
                co_return std::monostate{};
            }
            buffer->commitWrite(n);
            continue;
        }

        if (++lines > kMaxHeaderCount)
        {
            co_return HttpParseError{ HttpParseError::MalformedRequestLine, "Too many headers" };
        }
        std::string_view line = buffer->view().substr(0, pos);
        auto state = parser.feedLine(line);
        buffer->consume(pos + 2);

        if (state == HttpParserState::Error)
            co_return parser.extractResult();
        if (state == HttpParserState::HeaderComplete)
            break;
    }

    co_return parser.extractResult();
}

HttpServer::HttpServer(uint16_t port, Scheduler * scheduler)
    : HttpServer(HttpServerConfig(port), scheduler)
{
}

HttpServer::HttpServer(HttpServerConfig config, Scheduler * scheduler)
    : config_(std::move(config))
    , scheduler_(scheduler)
    , port_(config_.port)
    , router_(config_.router)
    , server_(std::make_unique<net::TcpServer>(port_, scheduler_))
{
    if (!config_.router)
    {
        router_ = std::make_shared<HttpRouter>();
    }

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

void HttpServer::use(Middleware middleware)
{
    middlewares_.push_back(std::move(middleware));
}

Task<> HttpServer::start()
{
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
    });
}

Task<> HttpServer::stop()
{
    if (server_)
    {
        co_await server_->stop();
    }
}

Task<HttpServer::HandleResult> HttpServer::handleNextRequest(
    io::StreamPtr stream,
    std::shared_ptr<utils::StringBuffer> buffer)
{
    using Action = HandleResult::Action;

    auto parsed = co_await parseNext(stream, buffer);
    if (std::holds_alternative<std::monostate>(parsed))
    {
        co_return { Action::Disconnected };
    }
    if (std::holds_alternative<HttpParseError>(parsed))
    {
        NITRO_DEBUG("Bad request: %s", std::get<HttpParseError>(parsed).message.c_str());
        auto response = std::make_shared<ServerResponse>(false, config_.send_date_header);
        response->setStatus(StatusCode::k400BadRequest);
        response->setCloseConnection(true);
        response->setBody("Bad Request");
        co_return { Action::Close, std::move(response) };
    }

    auto & parsedMsg = std::get<HttpRequest>(parsed);
    bool keepAlive = parsedMsg.keepAlive;
    auto transferMode = parsedMsg.transferMode;
    auto contentLength = parsedMsg.contentLength;

    auto bodyReader = BodyReader::create(stream, buffer, transferMode, contentLength);
    auto request = std::make_shared<IncomingRequest>(std::move(parsedMsg), bodyReader);

    auto method = request->method();
    bool ignoreBody = (method == methods::Head);
    auto response = std::make_shared<ServerResponse>(ignoreBody, config_.send_date_header);
    response->setVersion(request->version());
    response->setCloseConnection(!keepAlive);

    if (method == methods::_Invalid)
    {
        response->setStatus(StatusCode::k400BadRequest);
        response->setBody("Bad Request");
        co_return { keepAlive ? Action::Continue : Action::Shutdown, std::move(response), std::move(bodyReader) };
    }

    if (requestUpgrader_ && isUpgradeRequest(*request))
    {
        StreamHandler handler;
        std::exception_ptr exPtr;
        auto & middlewares = middlewares_;
        auto invokeChain = [&](auto & self, size_t index,
                               IncomingRequestPtr req, ServerResponsePtr resp) -> Task<> {
            if (index < middlewares.size())
            {
                co_await middlewares[index](req, resp, [&]() -> Task<> {
                    co_await self(self, index + 1, req, resp);
                });
            }
            else
            {
                handler = co_await requestUpgrader_(request, response);
            }
        };

        try
        {
            co_await invokeChain(invokeChain, 0, request, response);
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
            response->clear();
            response->setVersion(request->version());
            response->setCloseConnection(true);
            response->setStatus(StatusCode::k500InternalServerError);
            response->setBody("Internal Server Error");
            co_return { Action::Shutdown, std::move(response), std::move(bodyReader) };
        }
        if (handler)
        {
            co_await response->flush(stream);
            co_await handler(stream);
            co_return { Action::Close };
        }
        co_return { keepAlive ? Action::Continue : Action::Shutdown, std::move(response), std::move(bodyReader) };
    }

    auto routeRes = router_->route(method, request->path());
    request->pathParams() = std::move(routeRes.params);
    if (routeRes.reason != HttpRouter::RouteResult::Reason::Ok || !routeRes.handler)
    {
        // TODO: custom handler? 404 could use * route?
        if (routeRes.reason == HttpRouter::RouteResult::Reason::MethodNotAllowed)
        {
            if (method == methods::Options)
            {
                response->setStatus(StatusCode::k200OK);
                response->setHeader(HttpHeader::NameCode::Allow, routeRes.allowedMethods);
            }
            else
            {
                response->setStatus(StatusCode::k405MethodNotAllowed);
                response->setHeader(HttpHeader::NameCode::Allow, routeRes.allowedMethods);
                response->setBody("Method Not Allowed");
            }
        }
        else
        {
            response->setStatus(StatusCode::k404NotFound);
            response->setBody("Not Found");
        }

        co_return { keepAlive ? Action::Continue : Action::Shutdown, std::move(response), std::move(bodyReader) };
    }

    auto expect = request->getHeader(HttpHeader::NameCode::Expect);
    if (!expect.empty())
    {
        if (expect != "100-continue")
        {
            response->setStatus(StatusCode::k417ExpectationFailed);
            response->setBody("Expectation Failed");
            co_return { keepAlive ? Action::Continue : Action::Shutdown, std::move(response), std::move(bodyReader) };
        }
        // TODO: custom handler
        co_await stream->write("HTTP/1.1 100 Continue\r\n\r\n", 25);
    }

    std::exception_ptr exPtr;
    auto & middlewares = middlewares_;
    auto invokeChain = [&](auto & self, size_t index,
                           IncomingRequestPtr req, ServerResponsePtr resp) -> Task<> {
        if (index < middlewares.size())
        {
            co_await middlewares[index](req, resp, [&]() -> Task<> {
                co_await self(self, index + 1, req, resp);
            });
        }
        else
        {
            co_await routeRes.handler->invoke(req, resp);
        }
    };
    try
    {
        co_await invokeChain(invokeChain, 0, request, response);
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
        keepAlive = false;
        response->clear();
        response->setVersion(request->version());
        response->setCloseConnection(true);
        response->setStatus(StatusCode::k500InternalServerError);
        response->setBody("Internal Server Error");
    }
    co_return { keepAlive ? Action::Continue : Action::Shutdown, std::move(response), std::move(bodyReader) };
}

Task<> HttpServer::handleConnection(net::TcpConnectionPtr conn)
{
    using Action = HandleResult::Action;

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
    while (true)
    {
        auto result = co_await handleNextRequest(stream, buffer);
        if (result.action == Action::Disconnected || !result.resp)
            break;

        // flush resp
        co_await result.resp->flush(stream);

        if (result.action == Action::Continue)
        {
            if (result.bodyReader && !result.bodyReader->isComplete())
                co_await result.bodyReader->drain();
            continue;
        }

        if (result.action == Action::Shutdown)
        {
            co_await stream->shutdown();
        }
        // Close
        break;
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
