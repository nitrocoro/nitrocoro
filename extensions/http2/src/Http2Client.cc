/**
 * @file Http2Client.cc
 */
#include <nitrocoro/http2/Http2Client.h>

#include "Http2ClientSession.h"

#include <nitrocoro/core/Scheduler.h>
#include <nitrocoro/net/Dns.h>
#include <nitrocoro/net/TcpConnection.h>
#include <nitrocoro/tls/TlsContext.h>
#include <nitrocoro/tls/TlsStream.h>
#include <nitrocoro/utils/Debug.h>

namespace nitrocoro::http2
{

// ── Http2Client ───────────────────────────────────────────────────────────────

Http2Client::Http2Client(std::string baseUrl, Http2ClientConfig config)
    : Http2Client(net::Url(std::move(baseUrl)), std::move(config))
{
}

Http2Client::Http2Client(net::Url url, Http2ClientConfig config)
    : baseUrl_(std::move(url))
    , config_(std::move(config))
    , isHttps_(baseUrl_.scheme() == "https")
{
    if (baseUrl_.scheme() != "http" && baseUrl_.scheme() != "https")
        throw std::invalid_argument("Http2Client: unsupported scheme: " + baseUrl_.scheme());

    // Configure ALPN for HTTPS connections
    if (isHttps_)
    {
        config_.tls_policy.alpn = { "h2" };
        if (config_.allow_http1_fallback)
            config_.tls_policy.alpn.push_back("http/1.1");
    }
}

Http2Client::~Http2Client() = default;

Task<http::HttpCompleteResponse> Http2Client::get(std::string path)
{
    http::ClientRequest req;
    req.setMethod(http::methods::Get);
    req.setPath(std::move(path));

    auto response = co_await request(std::move(req));
    co_return co_await response.toCompleteResponse();
}

Task<http::HttpCompleteResponse> Http2Client::post(std::string path, std::string body)
{
    http::ClientRequest req;
    req.setMethod(http::methods::Post);
    req.setPath(std::move(path));
    req.setBody(std::move(body));

    auto response = co_await request(std::move(req));
    co_return co_await response.toCompleteResponse();
}

Task<http::IncomingResponse> Http2Client::request(http::ClientRequest req)
{
    if (config_.add_host_header)
        req.setHeader(http::HttpHeader::NameCode::Host, baseUrl_.host());

    auto session = co_await getSession();

    if (negotiatedProtocol_ == ProtocolVersion::Http11)
        throw std::runtime_error("HTTP/1.1 fallback not yet implemented");

    co_return co_await session->request(std::move(req));
}

Task<std::shared_ptr<Http2ClientSession>> Http2Client::getSession()
{
    if (http2Session_ && http2Session_->isAlive())
        co_return http2Session_;

    auto stream = co_await connect();
    negotiatedProtocol_ = co_await negotiateProtocol();

    if (negotiatedProtocol_ != ProtocolVersion::Http2)
        throw std::runtime_error("HTTP/1.1 fallback not yet implemented");

    http2Session_ = std::make_shared<Http2ClientSession>(std::move(stream), Scheduler::current(), baseUrl_.scheme());

    Scheduler::current()->spawn([session = http2Session_]() -> Task<> {
        try
        {
            co_await session->run();
        }
        catch (const std::exception & e)
        {
            NITRO_ERROR("Http2ClientSession error: %s", e.what());
        }
    });

    co_return http2Session_;
}

Task<io::StreamPtr> Http2Client::connect()
{
    auto addresses = co_await net::resolve(baseUrl_.host());
    if (addresses.empty())
        throw std::runtime_error("Http2Client: DNS resolution failed for " + baseUrl_.host());

    uint16_t port = baseUrl_.port() ? baseUrl_.port() : (isHttps_ ? 443 : 80);
    auto & addr = addresses[0];
    auto conn = co_await net::TcpConnection::connect(
        net::InetAddress(addr.toIp(), port, addr.isIpV6()));

    if (isHttps_)
    {
        auto tlsCtx = tls::TlsContext::createClient(config_.tls_policy);
        auto tlsStream = co_await tls::TlsStream::connect(conn, tlsCtx);
        negotiatedAlpn_ = tlsStream->negotiatedAlpn();
        co_return std::make_shared<io::Stream>(std::move(tlsStream));
    }

    co_return std::make_shared<io::Stream>(std::move(conn));
}

Task<ProtocolVersion> Http2Client::negotiateProtocol()
{
    if (isHttps_)
    {
        if (negotiatedAlpn_ == "h2")
        {
            NITRO_INFO("ALPN negotiated: HTTP/2");
            co_return ProtocolVersion::Http2;
        }
        if (negotiatedAlpn_ == "http/1.1" && config_.allow_http1_fallback)
        {
            NITRO_INFO("ALPN negotiated: HTTP/1.1 (fallback)");
            co_return ProtocolVersion::Http11;
        }
        throw std::runtime_error("Http2Client: No compatible protocol negotiated via ALPN");
    }
    // h2c: assume HTTP/2 for now (no upgrade negotiation)
    co_return ProtocolVersion::Http2;
}

// ── Free functions ────────────────────────────────────────────────────────────

Task<http::HttpCompleteResponse> get(std::string url)
{
    net::Url parsed(url);
    auto fullPath = parsed.fullPath();
    Http2Client client(std::move(parsed));
    co_return co_await client.get(std::move(fullPath));
}

Task<http::HttpCompleteResponse> post(std::string url, std::string body)
{
    net::Url parsed(url);
    auto fullPath = parsed.fullPath();
    Http2Client client(std::move(parsed));
    co_return co_await client.post(std::move(fullPath), std::move(body));
}

Task<http::IncomingResponse> request(std::string url, http::ClientRequest req)
{
    net::Url parsed(url);
    req.setPath(parsed.fullPath());
    Http2Client client(std::move(parsed));
    co_return co_await client.request(std::move(req));
}

} // namespace nitrocoro::http2
