/**
 * @file HttpClient.cc
 * @brief HTTP client implementation
 */
#include <nitrocoro/http/HttpClient.h>

#include "Http1BodyReader.h"
#include "Http1RequestSink.h"
#include "HttpParser.h"
#include <nitrocoro/http/stream/HttpOutgoingMessage.h>

#include <nitrocoro/io/Stream.h>
#include <nitrocoro/net/Dns.h>
#include <nitrocoro/net/Url.h>
#include <nitrocoro/utils/StringBuffer.h>
#include <stdexcept>

namespace nitrocoro::http
{

static Task<HttpParseResult<HttpResponse>> parseResponse(io::StreamPtr stream, std::shared_ptr<utils::StringBuffer> buffer)
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

HttpClient::HttpClient(std::string baseUrl, HttpClientConfig config)
    : HttpClient(net::Url(std::move(baseUrl)), std::move(config))
{
}

HttpClient::HttpClient(net::Url url, HttpClientConfig config)
    : baseUrl_(std::move(url))
    , config_(std::move(config))
    , isHttps_(baseUrl_.scheme() == "https")
    , cookieStore_(config_.cookieStoreFactory ? config_.cookieStoreFactory() : nullptr)
{
    if (!baseUrl_.isValid())
        throw std::invalid_argument("Invalid base URL");
}

HttpClient::~HttpClient() = default;

Task<IncomingResponse> HttpClient::doRequest(ClientRequest req, io::StreamPtr stream)
{
    if (config_.add_host_header)
    {
        req.setHeader(HttpHeader::NameCode::Host, baseUrl_.host());
    }
    {
        [[maybe_unused]] auto lock = co_await cookieMutex_.scoped_lock();
        injectCookies(req, req.data_.path);
    }
    co_await req.flush(Http1RequestSink(stream));

    auto buffer = std::make_shared<utils::StringBuffer>();
    auto result = co_await parseResponse(stream, buffer);
    if (std::holds_alternative<std::monostate>(result))
        throw std::runtime_error("Connection closed");
    if (std::holds_alternative<HttpParseError>(result))
        throw std::runtime_error(std::get<HttpParseError>(result).message);

    auto & msg = std::get<HttpResponse>(result);
    {
        [[maybe_unused]] auto lock = co_await cookieMutex_.scoped_lock();
        collectCookies(req.data_.path, msg.cookies);
    }

    bool ignoreBody = req.data_.method == methods::Head;
    auto bodyReader = Http1BodyReader::create(stream, buffer, msg.transferMode, ignoreBody ? 0 : msg.contentLength);
    co_return IncomingResponse(std::move(msg), std::move(bodyReader));
}

Task<HttpCompleteResponse> HttpClient::get(std::string path)
{
    ClientRequest req;
    req.setMethod(methods::Get);
    req.setPath(std::move(path));

    auto stream = co_await acquireConnection();
    auto resp = co_await doRequest(std::move(req), stream);
    auto complete = co_await resp.toCompleteResponse();
    if (!complete.shouldClose())
        co_await releaseConnection(std::move(stream));
    co_return complete;
}

Task<HttpCompleteResponse> HttpClient::post(std::string path, std::string body)
{
    ClientRequest req;
    req.setMethod(methods::Post);
    req.setPath(std::move(path));
    req.setBody(std::move(body));

    auto stream = co_await acquireConnection();
    auto resp = co_await doRequest(std::move(req), stream);
    auto complete = co_await resp.toCompleteResponse();
    if (!complete.shouldClose())
        co_await releaseConnection(std::move(stream));
    co_return complete;
}

Task<IncomingResponse> HttpClient::request(ClientRequest req)
{
    auto stream = co_await acquireConnection();
    auto resp = co_await doRequest(std::move(req), stream);
    co_return resp;
}

Task<io::StreamPtr> HttpClient::acquireConnection()
{
    [[maybe_unused]] auto lock = co_await mutex_.scoped_lock();
    auto now = std::chrono::steady_clock::now();
    while (!idleConnections_.empty())
    {
        auto idle = std::move(idleConnections_.front());
        idleConnections_.pop_front();
        if (now - idle.idleAt < config_.idle_timeout && !idle.stream->peerClosed())
            co_return std::move(idle.stream);
    }
    co_return co_await connect();
}

Task<> HttpClient::releaseConnection(io::StreamPtr stream)
{
    [[maybe_unused]] auto lock = co_await mutex_.scoped_lock();
    if (idleConnections_.size() < config_.max_idle_connections)
        idleConnections_.push_back({ std::move(stream), std::chrono::steady_clock::now() });
}

void HttpClient::injectCookies(ClientRequest & req, const std::string & path)
{
    if (!cookieStore_)
        return;
    for (auto & c : cookieStore_->load(path))
    {
        if (c.secure && !isHttps_)
            continue;
        req.setCookie(c.name, c.value);
    }
}

void HttpClient::collectCookies(const std::string & path, const std::vector<Cookie> & cookies)
{
    if (!cookieStore_)
        return;
    cookieStore_->store(path, cookies);
}

Task<io::StreamPtr> HttpClient::connect()
{
    auto addresses = co_await net::resolve(baseUrl_.host());
    if (addresses.empty())
        throw std::runtime_error("DNS resolution returned no addresses");

    auto addr = addresses[0];
    auto conn = co_await net::TcpConnection::connect(net::InetAddress(addr.toIp(), baseUrl_.port(), addr.isIpV6()));

    if (baseUrl_.scheme() == "https")
    {
        if (!upgrader_)
            throw std::runtime_error("HTTPS requires a StreamUpgrader");
        auto stream = co_await upgrader_(conn);
        if (!stream)
            throw std::runtime_error("Stream upgrade failed");
        co_return stream;
    }
    co_return std::make_shared<io::Stream>(conn);
}

// ── Free functions ────────────────────────────────────────────────────────────

Task<HttpCompleteResponse> get(std::string url)
{
    net::Url parsed(url);
    auto fullPath = parsed.fullPath();
    HttpClient client(std::move(parsed));
    co_return co_await client.get(std::move(fullPath));
}

Task<HttpCompleteResponse> post(std::string url, std::string body)
{
    net::Url parsed(url);
    auto fullPath = parsed.fullPath();
    HttpClient client(std::move(parsed));
    co_return co_await client.post(std::move(fullPath), std::move(body));
}

Task<IncomingResponse> request(std::string url, ClientRequest req)
{
    net::Url parsed(url);
    req.setPath(parsed.fullPath());
    HttpClient client(std::move(parsed));
    co_return co_await client.request(std::move(req));
}

} // namespace nitrocoro::http
