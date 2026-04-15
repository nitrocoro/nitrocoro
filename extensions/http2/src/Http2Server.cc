/**
 * @file Http2Server.cc
 */
#include <nitrocoro/http2/Http2Server.h>

#include "Http2Session.h"
#include <nitrocoro/io/Stream.h>
#include <nitrocoro/tls/TlsStream.h>

namespace nitrocoro::http2
{

void enableHttp2(http::HttpServer & server, const tls::TlsContextPtr & ctx)
{
    auto router = server.router();
    server.setStreamUpgrader([ctx, router](net::TcpConnectionPtr conn) -> Task<io::StreamPtr> {
        if (!ctx)
        {
            auto stream = std::make_shared<io::Stream>(conn);
            auto session = std::make_shared<Http2Session>(stream, router, Scheduler::current());
            co_await session->run();
            co_return nullptr;
        }

        auto tlsStream = co_await tls::TlsStream::accept(conn, ctx);
        if (tlsStream->negotiatedAlpn() == "h2")
        {
            auto stream = std::make_shared<io::Stream>(std::move(tlsStream));
            auto session = std::make_shared<Http2Session>(std::move(stream), router, Scheduler::current());
            co_await session->run();
            co_return nullptr;
        }

        co_return std::make_shared<io::Stream>(std::move(tlsStream));
    });
}

} // namespace nitrocoro::http2
