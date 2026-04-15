/**
 * @file Http2Server.h
 * @brief HTTP/2 support — attaches to HttpServer via StreamUpgrader
 */
#pragma once

#include <nitrocoro/http/HttpServer.h>
#include <nitrocoro/tls/TlsContext.h>

namespace nitrocoro::http2
{

/** Attach HTTP/2 support to an existing HttpServer.
 *  ctx != nullptr: TLS+ALPN, "h2" → Http2Session, otherwise fall back to HTTP/1.1.
 *  ctx == nullptr: h2c plaintext, all connections handled as HTTP/2. */
void enableHttp2(http::HttpServer & server, const tls::TlsContextPtr & ctx = nullptr);

} // namespace nitrocoro::http2
