/**
 * @file HttpClient.cc
 * @brief HTTP client implementation
 */
#include <nitrocoro/http/HttpClient.h>

#include <nitrocoro/core/Future.h>
#include <nitrocoro/core/Scheduler.h>
#include <nitrocoro/http/HttpMessage.h>
#include <nitrocoro/io/Stream.h>
#include <nitrocoro/net/Dns.h>
#include <nitrocoro/net/Url.h>

#include "HttpParser.h"
#include <stdexcept>

namespace nitrocoro::http
{

static Task<HttpParseResult<HttpResponse>> parseNext(io::StreamPtr stream, std::shared_ptr<utils::StringBuffer> buffer)
{
    HttpParser<HttpResponse> parser;

    while (true)
    {
        size_t pos = buffer->find("\r\n");
        if (pos == std::string::npos)
        {
            char * writePtr = buffer->prepareWrite(4096);
            size_t n = co_await stream->read(writePtr, 4096);
            if (n == 0)
                co_return { {}, HttpParseError::ConnectionClosed, "Connection closed before headers complete" };
            buffer->commitWrite(n);
            continue;
        }

        std::string_view line = buffer->view().substr(0, pos);
        auto state = parser.parseLine(line);
        buffer->consume(pos + 2);

        if (state == HttpParserState::Error)
            co_return parser.extractResult();
        if (state == HttpParserState::HeaderComplete)
            break;
    }

    co_return parser.extractResult();
}

void HttpClient::setStreamUpgrader(StreamUpgrader upgrader)
{
    upgrader_ = std::move(upgrader);
}

Task<HttpCompleteResponse> HttpClient::get(const std::string & url)
{
    co_return co_await request("GET", url);
}

Task<HttpCompleteResponse> HttpClient::post(const std::string & url, const std::string & body)
{
    co_return co_await request("POST", url, body);
}

Task<HttpCompleteResponse> HttpClient::request(const std::string & method, const std::string & url, const std::string & body)
{
    net::Url parsedUrl(url);
    if (!parsedUrl.isValid())
        throw std::invalid_argument("Invalid URL");
    co_return co_await sendRequest(method, parsedUrl, body);
}

Task<HttpCompleteResponse> HttpClient::sendRequest(const std::string & method, const net::Url & url, const std::string & body)
{
    // Resolve hostname
    auto addresses = co_await net::resolve(url.host());
    if (addresses.empty())
        throw std::runtime_error("DNS resolution returned no addresses");

    // Try to connect to first address
    auto addr = addresses[0];
    auto conn = co_await net::TcpConnection::connect(net::InetAddress(addr.toIp(), url.port(), addr.isIpV6()));

    // Upgrade stream if upgrader is set
    io::StreamPtr stream;
    if (upgrader_)
    {
        stream = co_await upgrader_(conn);
        if (!stream)
            throw std::runtime_error("Stream upgrade failed");
    }
    else
    {
        stream = std::make_shared<io::Stream>(conn);
    }

    // Build request
    std::string request;
    request.reserve(method.size() + url.path().size() + url.host().size() + body.size() + 64);
    std::string requestTarget = url.path();
    if (!url.query().empty())
        requestTarget.append("?").append(url.query());
    request.append(method).append(" ").append(requestTarget).append(" HTTP/1.1\r\n");
    request.append("Host: ").append(url.host()).append("\r\n");
    request.append("Connection: close\r\n");

    if (!body.empty())
    {
        request.append("Content-Length: ").append(std::to_string(body.size())).append("\r\n");
    }

    request.append("\r\n");

    if (!body.empty())
    {
        request.append(body);
    }
    co_await stream->write(request.c_str(), request.size());

    co_return co_await readResponse(stream, method == "HEAD");
}

Task<HttpCompleteResponse> HttpClient::readResponse(io::StreamPtr stream, bool ignoreContentLength)
{
    auto buffer = std::make_shared<utils::StringBuffer>();
    auto result = co_await parseNext(stream, buffer);
    if (result.error())
        throw std::runtime_error(result.errorMessage);

    auto transferMode = result.message.transferMode;
    auto contentLength = result.message.contentLength;
    auto bodyReader = BodyReader::create(stream, buffer, transferMode, ignoreContentLength ? 0 : contentLength);
    auto incomingStream = HttpIncomingStream<HttpResponse>(std::move(result.message), std::move(bodyReader));
    co_return co_await incomingStream.toCompleteResponse();
}

Task<HttpClientSession> HttpClient::stream(const std::string & method, const std::string & url)
{
    net::Url parsedUrl(url);
    if (!parsedUrl.isValid())
        throw std::invalid_argument("Invalid URL");

    // Resolve and connect
    auto addresses = co_await net::resolve(parsedUrl.host());
    if (addresses.empty())
        throw std::runtime_error("DNS resolution failed");

    auto conn = co_await net::TcpConnection::connect(net::InetAddress(addresses[0].toIp(), parsedUrl.port(), addresses[0].isIpV6()));

    // Upgrade stream if upgrader is set
    io::StreamPtr anyStream;
    if (upgrader_)
    {
        anyStream = co_await upgrader_(conn);
        if (!anyStream)
            throw std::runtime_error("Stream upgrade failed");
    }
    else
    {
        anyStream = std::make_shared<io::Stream>(conn);
    }

    // Create outgoing stream for request body
    HttpOutgoingStream<HttpRequest> requestStream(anyStream);
    requestStream.setMethod(method);
    requestStream.setPath(parsedUrl.path());
    requestStream.setHeader(HttpHeader::NameCode::Host, parsedUrl.host());

    // Create promise/future for response
    Promise<HttpIncomingStream<HttpResponse>> promise(Scheduler::current());
    auto responseFuture = promise.get_future();

    // Spawn background task to receive response
    Scheduler::current()->spawn([anyStream, promise = std::move(promise)]() mutable -> Task<> {
        try
        {
            auto buffer = std::make_shared<utils::StringBuffer>();
            auto result = co_await parseNext(anyStream, buffer);
            if (result.error())
            {
                promise.set_exception(std::make_exception_ptr(std::runtime_error(result.errorMessage)));
                co_return;
            }

            auto transferMode = result.message.transferMode;
            auto contentLength = result.message.contentLength;
            auto response = HttpIncomingStream<HttpResponse>(
                std::move(result.message),
                BodyReader::create(anyStream, buffer, transferMode, contentLength));
            promise.set_value(std::move(response));
        }
        catch (...)
        {
            promise.set_exception(std::current_exception());
        }
    });

    co_return HttpClientSession{ std::move(requestStream), std::move(responseFuture) };
}

} // namespace nitrocoro::http
