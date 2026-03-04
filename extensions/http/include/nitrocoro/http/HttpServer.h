/**
 * @file HttpServer.h
 * @brief HTTP server based on TcpServer
 */
#pragma once

#include <nitrocoro/core/Task.h>
#include <nitrocoro/http/HttpStream.h>
#include <nitrocoro/io/Stream.h>
#include <nitrocoro/net/TcpServer.h>

#include <functional>
#include <map>
#include <memory>
#include <string>

namespace nitrocoro::http
{

class HttpServer
{
public:
    using Handler = std::function<Task<>(HttpIncomingStream<HttpRequest> &, HttpOutgoingStream<HttpResponse> &)>;
    using StreamUpgrader = std::function<Task<io::StreamPtr>(net::TcpConnectionPtr)>;

    explicit HttpServer(uint16_t port, Scheduler * scheduler = Scheduler::current());

    uint16_t listeningPort() const { return port_; }

    void setStreamUpgrader(StreamUpgrader upgrader);
    void route(const std::string & method, const std::string & path, Handler handler);
    Task<> start();
    Task<> stop();

private:
    Task<> handleConnection(net::TcpConnectionPtr conn);

    uint16_t port_;
    Scheduler * scheduler_;
    StreamUpgrader upgrader_;
    std::map<std::pair<std::string, std::string>, Handler> routes_;
    std::unique_ptr<net::TcpServer> server_;
};

} // namespace nitrocoro::http
