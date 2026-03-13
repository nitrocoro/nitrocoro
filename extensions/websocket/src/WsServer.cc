/**
 * @file WsServer.cc
 * @brief WebSocket upgrade handshake + routing
 */
#include <nitrocoro/websocket/WsServer.h>

#include <nitrocoro/http/HttpHeader.h>
#include <nitrocoro/utils/Base64.h>
#include <nitrocoro/utils/Sha1.h>
#include <nitrocoro/websocket/WsTypes.h>

static std::string computeAccept(const std::string & key)
{
    auto digest = nitrocoro::utils::sha1(key + std::string{ nitrocoro::websocket::kWebSocketGuid });
    return nitrocoro::utils::base64Encode(std::string_view(reinterpret_cast<const char *>(digest.data()), digest.size()));
}

namespace nitrocoro::websocket
{

void WsServer::route(const std::string & path, Handler handler)
{
    routes_[path] = std::move(handler);
}

void WsServer::attachTo(http::HttpServer & server)
{
    server.setRequestUpgrader([this](http::HttpIncomingStream<http::HttpRequest> & req,
                                     http::HttpOutgoingStream<http::HttpResponse> & resp,
                                     io::StreamPtr stream) -> Task<bool> {
        co_return co_await handleUpgrade(req, resp, std::move(stream));
    });
}

Task<bool> WsServer::handleUpgrade(http::HttpIncomingStream<http::HttpRequest> & req,
                                   http::HttpOutgoingStream<http::HttpResponse> & resp,
                                   io::StreamPtr stream)
{
    using http::HttpHeader;

    // Only handle WebSocket upgrades
    auto & upgrade = req.getHeader(HttpHeader::NameCode::Upgrade);
    if (HttpHeader::toLower(upgrade) != "websocket")
        co_return false;

    auto it = routes_.find(std::string(req.path()));
    if (it == routes_.end())
        co_return false;

    auto & key = req.getHeader(HttpHeader::NameCode::SecWebSocketKey);
    if (key.empty())
        co_return false;

    std::string accept = computeAccept(key);

    resp.setStatus(http::StatusCode::k101SwitchingProtocols);
    resp.setHeader(HttpHeader::NameCode::Upgrade, "websocket");
    resp.setHeader(HttpHeader::NameCode::Connection, "Upgrade");
    resp.setHeader(HttpHeader::NameCode::SecWebSocketAccept, accept);
    co_await resp.end();

    WsConnection conn(std::move(stream));
    co_await it->second(conn);
    co_return true;
}

} // namespace nitrocoro::websocket
