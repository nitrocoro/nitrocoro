/**
 * @file HttpClient.h
 * @brief HTTP client for making requests
 */
#pragma once
#include <nitrocoro/http/HttpCompleteMessage.h>
#include <nitrocoro/http/HttpMessage.h>
#include <nitrocoro/http/HttpStream.h>

#include <nitrocoro/core/Future.h>
#include <nitrocoro/core/Task.h>
#include <nitrocoro/io/Stream.h>
#include <nitrocoro/net/TcpConnection.h>
#include <nitrocoro/net/Url.h>

#include <string>

namespace nitrocoro::http
{

struct HttpClientSession
{
    HttpOutgoingStream<HttpRequest> request;
    Future<HttpIncomingStream<HttpResponse>> response;
};

class HttpClient
{
public:
    using StreamUpgrader = std::function<Task<io::StreamPtr>(net::TcpConnectionPtr)>;

    HttpClient() = default;

    void setStreamUpgrader(StreamUpgrader upgrader);

    // Simple API
    Task<HttpCompleteResponse> get(const std::string & url);
    Task<HttpCompleteResponse> post(const std::string & url, const std::string & body);
    Task<HttpCompleteResponse> request(const std::string & method, const std::string & url, const std::string & body = "");

    // Stream API
    Task<HttpClientSession> stream(const std::string & method, const std::string & url);

private:
    Task<HttpCompleteResponse> sendRequest(const std::string & method, const net::Url & url, const std::string & body);
    Task<HttpCompleteResponse> readResponse(io::StreamPtr stream);

    StreamUpgrader upgrader_;
};

} // namespace nitrocoro::http
