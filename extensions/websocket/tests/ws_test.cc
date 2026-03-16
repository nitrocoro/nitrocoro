/**
 * @file ws_test.cc
 * @brief Tests for WsServer and WsConnection
 */
#include <nitrocoro/http/HttpServer.h>
#include <nitrocoro/net/TcpConnection.h>
#include <nitrocoro/testing/Test.h>
#include <nitrocoro/utils/Base64.h>
#include <nitrocoro/utils/Sha1.h>
#include <nitrocoro/websocket/WsServer.h>

#include <nitrocoro/http/HttpClient.h>

#include <string>

using namespace nitrocoro;
using namespace nitrocoro::websocket;
using namespace std::chrono_literals;

// ── Minimal WS client helpers ─────────────────────────────────────────────────

static std::string computeAccept(const std::string & key)
{
    auto digest = nitrocoro::utils::sha1(key + std::string{ kWebSocketGuid });
    return nitrocoro::utils::base64Encode(std::string_view(reinterpret_cast<const char *>(digest.data()), digest.size()));
}

// Read until \r\n\r\n, return response headers as string
static Task<std::string> readHttpResponse(net::TcpConnection & conn)
{
    std::string buf;
    char c;
    while (true)
    {
        co_await conn.read(&c, 1);
        buf += c;
        if (buf.size() >= 4 && buf.substr(buf.size() - 4) == "\r\n\r\n")
            co_return buf;
    }
}

// Send a masked text frame (clients must mask per RFC 6455)
static Task<> sendMaskedText(net::TcpConnection & conn, std::string_view text)
{
    std::vector<uint8_t> frame;
    frame.push_back(0x81); // FIN + Text
    uint8_t maskBit = 0x80;
    size_t len = text.size();
    if (len < 126)
        frame.push_back(maskBit | static_cast<uint8_t>(len));
    else if (len < 65536)
    {
        frame.push_back(maskBit | 126);
        frame.push_back(len >> 8);
        frame.push_back(len & 0xFF);
    }
    uint8_t mask[4] = { 0x12, 0x34, 0x56, 0x78 };
    frame.insert(frame.end(), mask, mask + 4);
    for (size_t i = 0; i < len; ++i)
        frame.push_back(static_cast<uint8_t>(text[i]) ^ mask[i % 4]);
    co_await conn.write(frame.data(), frame.size());
}

// Read one unmasked text frame from server
static Task<std::string> recvTextFrame(net::TcpConnection & conn)
{
    uint8_t header[2];
    size_t got = 0;
    while (got < 2)
        got += co_await conn.read(header + got, 2 - got);
    uint64_t payloadLen = header[1] & 0x7F;
    if (payloadLen == 126)
    {
        uint8_t ext[2];
        got = 0;
        while (got < 2)
            got += co_await conn.read(ext + got, 2 - got);
        payloadLen = (static_cast<uint64_t>(ext[0]) << 8) | ext[1];
    }
    std::string payload(payloadLen, '\0');
    got = 0;
    while (got < payloadLen)
        got += co_await conn.read(payload.data() + got, payloadLen - got);
    co_return payload;
}

// ── Test helpers ──────────────────────────────────────────────────────────────

static Task<http::HttpServer *> startServer(http::HttpServer & server)
{
    Scheduler::current()->spawn([&server]() -> Task<> { co_await server.start(); });
    co_await sleep(50ms);
    co_return &server;
}

// ── Tests ─────────────────────────────────────────────────────────────────────

/** Server echoes text messages back to client. */
NITRO_TEST(ws_echo)
{
    http::HttpServer server(0);
    WsServer ws;
    ws.route("/ws", [](WsConnection & conn) -> Task<> {
        while (auto msg = co_await conn.receive())
            co_await conn.send(msg->payload);
    });
    ws.attachTo(server);
    co_await startServer(server);

    auto conn = co_await net::TcpConnection::connect({ "127.0.0.1", server.listeningPort() });
    std::string key = "dGhlIHNhbXBsZSBub25jZQ=="; // fixed test key

    std::string req = "GET /ws HTTP/1.1\r\n"
                      "Host: localhost\r\n"
                      "Upgrade: websocket\r\n"
                      "Connection: Upgrade\r\n"
                      "Sec-WebSocket-Key: "
                      + key + "\r\n"
                              "Sec-WebSocket-Version: 13\r\n"
                              "\r\n";
    co_await conn->write(req.data(), req.size());

    auto resp = co_await readHttpResponse(*conn);
    NITRO_CHECK(resp.find("101") != std::string::npos);
    NITRO_CHECK(resp.find(computeAccept(key)) != std::string::npos);

    co_await sendMaskedText(*conn, "hello");
    auto reply = co_await recvTextFrame(*conn);
    NITRO_CHECK_EQ(reply, "hello");

    co_await sendMaskedText(*conn, "world");
    reply = co_await recvTextFrame(*conn);
    NITRO_CHECK_EQ(reply, "world");

    co_await server.stop();
}

/** Non-WebSocket requests on the same server still work normally. */
NITRO_TEST(ws_http_coexist)
{
    http::HttpServer server(0);
    server.route("/ping", { "GET" }, [](auto, auto resp) -> Task<> {
        co_await resp->end("pong");
    });
    WsServer ws;
    ws.route("/ws", [](WsConnection & conn) -> Task<> {
        co_await conn.shutdown();
    });
    ws.attachTo(server);
    co_await startServer(server);

    http::HttpClient client;
    auto resp = co_await client.get("http://127.0.0.1:" + std::to_string(server.listeningPort()) + "/ping");
    NITRO_CHECK_EQ(resp.statusCode(), http::StatusCode::k200OK);
    NITRO_CHECK_EQ(resp.body(), "pong");

    co_await server.stop();
}

/** Upgrade request to an unregistered path is ignored (falls through to 404). */
NITRO_TEST(ws_unknown_path)
{
    http::HttpServer server(0);
    WsServer ws;
    ws.attachTo(server);
    co_await startServer(server);

    auto conn = co_await net::TcpConnection::connect({ "127.0.0.1", server.listeningPort() });
    std::string req = "GET /no-such-path HTTP/1.1\r\n"
                      "Host: localhost\r\n"
                      "Upgrade: websocket\r\n"
                      "Connection: Upgrade\r\n"
                      "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                      "Sec-WebSocket-Version: 13\r\n"
                      "\r\n";
    co_await conn->write(req.data(), req.size());

    auto resp = co_await readHttpResponse(*conn);
    NITRO_CHECK(resp.find("404") != std::string::npos);

    co_await server.stop();
}

int main(int argc, char ** argv)
{
    return nitrocoro::test::run_all(argc, argv);
}
