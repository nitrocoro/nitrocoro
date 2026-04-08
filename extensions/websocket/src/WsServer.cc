/**
 * @file WsServer.cc
 * @brief WebSocket upgrade handshake + routing
 */
#include <nitrocoro/websocket/WsServer.h>

#include "WsContextImpl.h"

#include <nitrocoro/http/HttpHeader.h>
#include <nitrocoro/utils/Base64.h>
#include <nitrocoro/utils/Debug.h>
#include <nitrocoro/utils/Sha1.h>
#include <nitrocoro/websocket/WsTypes.h>

static std::string computeAccept(const std::string & key)
{
    auto digest = nitrocoro::utils::sha1(key + std::string{ nitrocoro::websocket::kWebSocketGuid });
    return nitrocoro::utils::base64Encode(std::string_view(reinterpret_cast<const char *>(digest.data()), digest.size()));
}

namespace nitrocoro::websocket
{

void WsServer::attachTo(http::HttpServer & server)
{
    server.setRequestUpgrader([this](http::IncomingRequestPtr req,
                                     http::ServerResponsePtr resp) -> Task<http::HttpServer::StreamHandler> {
        co_return co_await handleUpgrade(req, resp);
    });
}

Task<http::HttpServer::StreamHandler> WsServer::handleUpgrade(http::IncomingRequestPtr req,
                                                              http::ServerResponsePtr resp)
{
    using http::HttpHeader;

    // Only handle WebSocket upgrades
    auto & upgrade = req->getHeader(HttpHeader::NameCode::Upgrade);
    if (HttpHeader::toLower(upgrade) != "websocket")
        co_return nullptr;

    // Use WsRouter to find handler
    auto result = router_.route(req->path());
    if (!result)
        co_return nullptr;

    auto & key = req->getHeader(HttpHeader::NameCode::SecWebSocketKey);
    if (key.empty())
        co_return nullptr;

    std::string accept = computeAccept(key);

    resp->setStatus(http::StatusCode::k101SwitchingProtocols);
    resp->setHeader(HttpHeader::NameCode::Upgrade, "websocket");
    resp->setHeader(HttpHeader::NameCode::Connection, "Upgrade");
    resp->setHeader(HttpHeader::NameCode::SecWebSocketAccept, accept);

    req->pathParams() = std::move(std::move(result.params));
    auto connPromisePtr = std::make_shared<Promise<WsConnection>>();
    auto ctx = std::make_shared<WsContextImpl>(req, resp, connPromisePtr->get_future());

    Scheduler::current()->spawn([ctx, handler = std::move(result.handler)]() mutable -> Task<> {
        try
        {
            co_await handler->invoke(std::move(ctx));
        }
        catch (const std::exception & ex)
        {
            NITRO_ERROR("WsServer handler unhandled exception: %s", ex.what());
        }
        catch (...)
        {
            NITRO_ERROR("WsServer handler unknown exception");
        }
    });
    auto acceptFuture = ctx->acceptFuture();
    ctx.reset(); // allow ctx to destruct

    bool accepted = co_await acceptFuture.get();
    if (!accepted)
    {
        co_return [](io::StreamPtr) -> Task<> {
            co_return;
        };
    }

    co_return [connPromisePtr](io::StreamPtr stream) -> Task<> {
        WsConnection conn(std::move(stream));
        connPromisePtr->set_value(std::move(conn));
        co_return;
    };
}

} // namespace nitrocoro::websocket
