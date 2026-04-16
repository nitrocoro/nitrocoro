/**
 * @file Http2Client.h
 * @brief HTTP/2 client for making requests
 */
#pragma once

#include <nitrocoro/http/HttpClient.h>
#include <nitrocoro/http/HttpStream.h>

#include <nitrocoro/core/Mutex.h>
#include <nitrocoro/core/Task.h>
#include <nitrocoro/http/CookieStore.h>
#include <nitrocoro/http/HttpMessage.h>
#include <nitrocoro/io/Stream.h>
#include <nitrocoro/net/Url.h>
#include <nitrocoro/tls/TlsPolicy.h>

#include <chrono>
#include <memory>
#include <string>

namespace nitrocoro::http2
{

class Http2ClientSession;

enum class ProtocolVersion
{
    Http11,
    Http2
};

struct Http2ClientConfig
{
    std::chrono::seconds connection_timeout = std::chrono::seconds(30);
    bool add_host_header = true;
    tls::TlsPolicy tls_policy;        // TLS configuration for HTTPS
    bool allow_http1_fallback = true; // Allow fallback to HTTP/1.1 if HTTP/2 not supported
    http::CookieStoreFactory cookieStoreFactory = nullptr;
};

class Http2Client
{
public:
    explicit Http2Client(std::string baseUrl, Http2ClientConfig config = {});
    explicit Http2Client(net::Url url, Http2ClientConfig config = {});

    virtual ~Http2Client();

    Http2Client(const Http2Client &) = delete;
    Http2Client & operator=(const Http2Client &) = delete;
    Http2Client(Http2Client &&) = delete;
    Http2Client & operator=(Http2Client &&) = delete;

    Task<http::HttpCompleteResponse> get(std::string path);
    Task<http::HttpCompleteResponse> post(std::string path, std::string body);
    Task<http::IncomingResponse> request(http::ClientRequest req);

private:
    Task<std::shared_ptr<Http2ClientSession>> getSession();
    Task<io::StreamPtr> connect();
    Task<ProtocolVersion> negotiateProtocol();
    void injectCookies(http::ClientRequest & req, const std::string & path);
    void collectCookies(const std::string & path, const std::vector<http::Cookie> & cookies);

    net::Url baseUrl_;
    Http2ClientConfig config_;
    bool isHttps_;
    ProtocolVersion negotiatedProtocol_ = ProtocolVersion::Http2;

    std::shared_ptr<Http2ClientSession> http2Session_;
    std::unique_ptr<http::HttpClient> http1Fallback_;
    std::string negotiatedAlpn_;
    Mutex cookieMutex_;
    std::unique_ptr<http::CookieStore> cookieStore_;
};

Task<http::HttpCompleteResponse> get(std::string url);
Task<http::HttpCompleteResponse> post(std::string url, std::string body);
Task<http::IncomingResponse> request(std::string url, http::ClientRequest req);

} // namespace nitrocoro::http2
