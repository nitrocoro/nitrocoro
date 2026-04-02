/**
 * @file HttpsClient.h
 * @brief HTTPS client — combines nitrocoro-http and nitrocoro-tls
 */
#pragma once
#include <nitrocoro/http/HttpClient.h>
#include <nitrocoro/tls/TlsPolicy.h>

namespace nitrocoro::https
{

struct HttpsClientConfig
{
    http::HttpClientConfig http;
    tls::TlsPolicy tlsPolicy = tls::TlsPolicy::defaultClient();
};

class HttpsClient : public http::HttpClient
{
public:
    explicit HttpsClient(std::string baseUrl, HttpsClientConfig config = {});
};

Task<http::HttpCompleteResponse> get(std::string url);
Task<http::HttpCompleteResponse> post(std::string url, std::string body);
Task<http::IncomingResponse> request(std::string url, http::ClientRequest req);

} // namespace nitrocoro::https
