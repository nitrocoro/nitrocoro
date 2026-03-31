/**
 * @file HttpClient.h
 * @brief HTTP client for making requests
 */
#pragma once
#include <nitrocoro/http/HttpCompleteMessage.h>
#include <nitrocoro/http/HttpMessage.h>
#include <nitrocoro/http/HttpStream.h>
#include <nitrocoro/http/HttpTypes.h>

#include <nitrocoro/core/Task.h>
#include <nitrocoro/io/Stream.h>
#include <nitrocoro/net/TcpConnection.h>
#include <nitrocoro/net/Url.h>

#include <string>

namespace nitrocoro::http
{

class HttpClient
{
public:
    using StreamUpgrader = std::function<Task<io::StreamPtr>(net::TcpConnectionPtr)>;

    HttpClient() = default;

    void setStreamUpgrader(StreamUpgrader upgrader);

    // Simple API
    Task<HttpCompleteResponse> get(const std::string & url);
    Task<HttpCompleteResponse> post(const std::string & url, const std::string & body);
    Task<HttpCompleteResponse> request(const HttpMethod & method, const std::string & url, const std::string & body = "");

    // Full control API
    Task<IncomingResponse> send(ClientRequest req);

private:
    Task<io::StreamPtr> connect(const net::Url & url);
    Task<IncomingResponse> readResponse(io::StreamPtr stream, bool ignoreContentLength = false);

    StreamUpgrader upgrader_;
};

} // namespace nitrocoro::http
