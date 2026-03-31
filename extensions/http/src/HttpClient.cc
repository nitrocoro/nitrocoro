/**
 * @file HttpClient.cc
 * @brief HTTP client implementation
 */
#include <nitrocoro/http/HttpClient.h>

#include <nitrocoro/http/stream/HttpOutgoingMessage.h>
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
                co_return std::monostate{};
            buffer->commitWrite(n);
            continue;
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

void HttpClient::setStreamUpgrader(StreamUpgrader upgrader)
{
    upgrader_ = std::move(upgrader);
}

Task<HttpCompleteResponse> HttpClient::get(const std::string & url)
{
    ClientRequest req;
    req.setUrl(url);
    req.setMethod(methods::Get);
    auto resp = co_await send(std::move(req));
    co_return co_await resp.toCompleteResponse();
}

Task<HttpCompleteResponse> HttpClient::post(const std::string & url, const std::string & body)
{
    ClientRequest req;
    req.setUrl(url);
    req.setMethod(methods::Post);
    req.setBody(body);
    auto resp = co_await send(std::move(req));
    co_return co_await resp.toCompleteResponse();
}

Task<HttpCompleteResponse> HttpClient::request(const HttpMethod & method, const std::string & url, const std::string & body)
{
    ClientRequest req;
    req.setUrl(url);
    req.setMethod(method);
    if (!body.empty())
        req.setBody(body);
    auto resp = co_await send(std::move(req));
    co_return co_await resp.toCompleteResponse();
}

Task<IncomingResponse> HttpClient::send(ClientRequest req)
{
    net::Url parsedUrl(req.url_);
    if (!parsedUrl.isValid())
        throw std::invalid_argument("Invalid URL");

    auto stream = co_await connect(parsedUrl);

    // Inject stream and fill in defaults
    req.setStream(stream);
    std::string requestTarget = parsedUrl.path();
    if (!parsedUrl.query().empty())
        requestTarget.append("?").append(parsedUrl.query());
    req.setPath(requestTarget);
    if (!req.data_.headers.contains(HttpHeader::Name::Host_L))
        req.setHeader(HttpHeader::NameCode::Host, parsedUrl.host());
    if (!req.data_.headers.contains(HttpHeader::Name::Connection_L))
        req.setHeader(HttpHeader::NameCode::Connection, "close");
    co_await req.flush();

    co_return co_await readResponse(stream, req.data_.method == methods::Head);
}

Task<io::StreamPtr> HttpClient::connect(const net::Url & url)
{
    // Resolve hostname
    auto addresses = co_await net::resolve(url.host());
    if (addresses.empty())
        throw std::runtime_error("DNS resolution returned no addresses");

    // Try to connect to first address
    auto addr = addresses[0];
    auto conn = co_await net::TcpConnection::connect(net::InetAddress(addr.toIp(), url.port(), addr.isIpV6()));

    if (upgrader_)
    {
        auto stream = co_await upgrader_(conn);
        if (!stream)
            throw std::runtime_error("Stream upgrade failed");
        co_return stream;
    }
    co_return std::make_shared<io::Stream>(conn);
}

Task<IncomingResponse> HttpClient::readResponse(io::StreamPtr stream, bool ignoreContentLength)
{
    auto buffer = std::make_shared<utils::StringBuffer>();
    auto result = co_await parseNext(stream, buffer);
    if (std::holds_alternative<std::monostate>(result))
        throw std::runtime_error("Connection closed");
    if (std::holds_alternative<HttpParseError>(result))
        throw std::runtime_error(std::get<HttpParseError>(result).message);

    auto & msg = std::get<HttpResponse>(result);
    auto transferMode = msg.transferMode;
    auto contentLength = msg.contentLength;
    auto bodyReader = BodyReader::create(stream, buffer, transferMode, ignoreContentLength ? 0 : contentLength);
    co_return IncomingResponse(std::move(msg), std::move(bodyReader));
}

} // namespace nitrocoro::http
