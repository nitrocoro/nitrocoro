/**
 * @file HttpsClient.cc
 * @brief HTTPS client implementation
 */
#include <nitrocoro/https/HttpsClient.h>
#include <nitrocoro/tls/TlsContext.h>
#include <nitrocoro/tls/TlsStream.h>

namespace nitrocoro::https
{

HttpsClient::HttpsClient(std::string baseUrl, HttpsClientConfig config)
    : http::HttpClient(std::move(baseUrl), std::move(config.http))
{
    tls::TlsPolicy policy = config.tlsPolicy;
    if (policy.hostname.empty())
        policy.hostname = baseUrl_.host();
    auto tlsContext = tls::TlsContext::createClient(policy);
    setStreamUpgrader([tlsContext](net::TcpConnectionPtr conn) -> Task<io::StreamPtr> {
        auto tlsStream = co_await tls::TlsStream::connect(std::move(conn), tlsContext);
        co_return std::make_shared<io::Stream>(std::move(tlsStream));
    });
}

// ── Free functions ────────────────────────────────────────────────────────────

Task<http::HttpCompleteResponse> get(std::string url)
{
    net::Url parsed(url);
    HttpsClient client(parsed.baseUrl());
    co_return co_await client.get(parsed.fullPath());
}

Task<http::HttpCompleteResponse> post(std::string url, std::string body)
{
    net::Url parsed(url);
    HttpsClient client(parsed.baseUrl());
    co_return co_await client.post(parsed.fullPath(), std::move(body));
}

Task<http::IncomingResponse> request(std::string url, http::ClientRequest req)
{
    net::Url parsed(url);
    HttpsClient client(parsed.baseUrl());
    req.setPath(parsed.fullPath());
    co_return co_await client.request(std::move(req));
}

} // namespace nitrocoro::https
