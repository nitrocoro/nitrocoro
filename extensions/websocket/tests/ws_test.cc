/**
 * @file ws_test.cc
 * @brief Tests for WsServer and WsConnection
 */
#include <nitrocoro/http/HttpServer.h>
#include <nitrocoro/net/TcpConnection.h>
#include <nitrocoro/testing/Test.h>
#include <nitrocoro/websocket/WsServer.h>

#include "nitrocoro/http/HttpClient.h"
#include <array>
#include <cstring>
#include <string>

using namespace nitrocoro;
using namespace nitrocoro::websocket;
using namespace std::chrono_literals;

// ── Minimal WS client helpers ─────────────────────────────────────────────────

// Compute Sec-WebSocket-Accept (same logic as WsServer.cc)
static void sha1(const uint8_t * data, size_t len, uint8_t out[20])
{
    uint32_t h[5] = { 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0 };
    auto rol = [](uint32_t v, int n) { return (v << n) | (v >> (32 - n)); };
    uint64_t bitLen = uint64_t(len) * 8;
    size_t padLen = (len % 64 < 56) ? (56 - len % 64) : (120 - len % 64);
    size_t total = len + padLen + 8;
    std::vector<uint8_t> msg(total, 0);
    memcpy(msg.data(), data, len);
    msg[len] = 0x80;
    for (int i = 0; i < 8; ++i)
        msg[total - 8 + i] = uint8_t(bitLen >> (56 - i * 8));
    for (size_t i = 0; i < total; i += 64)
    {
        uint32_t w[80];
        for (int j = 0; j < 16; ++j)
            w[j] = (uint32_t(msg[i + j * 4]) << 24) | (uint32_t(msg[i + j * 4 + 1]) << 16) | (uint32_t(msg[i + j * 4 + 2]) << 8) | msg[i + j * 4 + 3];
        for (int j = 16; j < 80; ++j)
            w[j] = rol(w[j - 3] ^ w[j - 8] ^ w[j - 14] ^ w[j - 16], 1);
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int j = 0; j < 80; ++j)
        {
            uint32_t f, k;
            if (j < 20)
            {
                f = (b & c) | (~b & d);
                k = 0x5A827999;
            }
            else if (j < 40)
            {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1;
            }
            else if (j < 60)
            {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDC;
            }
            else
            {
                f = b ^ c ^ d;
                k = 0xCA62C1D6;
            }
            uint32_t t = rol(a, 5) + f + e + k + w[j];
            e = d;
            d = c;
            c = rol(b, 30);
            b = a;
            a = t;
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
    }
    for (int i = 0; i < 5; ++i)
        for (int j = 0; j < 4; ++j)
            out[i * 4 + j] = uint8_t(h[i] >> (24 - j * 8));
}

static std::string base64Encode(const uint8_t * data, size_t len)
{
    static constexpr char kT[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (size_t i = 0; i < len; i += 3)
    {
        uint32_t v = uint32_t(data[i]) << 16;
        if (i + 1 < len)
            v |= uint32_t(data[i + 1]) << 8;
        if (i + 2 < len)
            v |= uint32_t(data[i + 2]);
        out += kT[(v >> 18) & 0x3F];
        out += kT[(v >> 12) & 0x3F];
        out += (i + 1 < len) ? kT[(v >> 6) & 0x3F] : '=';
        out += (i + 2 < len) ? kT[v & 0x3F] : '=';
    }
    return out;
}

static std::string computeAccept(const std::string & key)
{
    std::string input = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    uint8_t digest[20];
    sha1(reinterpret_cast<const uint8_t *>(input.data()), input.size(), digest);
    return base64Encode(digest, 20);
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
        frame.push_back(maskBit | uint8_t(len));
    else if (len < 65536)
    {
        frame.push_back(maskBit | 126);
        frame.push_back(len >> 8);
        frame.push_back(len & 0xFF);
    }
    uint8_t mask[4] = { 0x12, 0x34, 0x56, 0x78 };
    frame.insert(frame.end(), mask, mask + 4);
    for (size_t i = 0; i < len; ++i)
        frame.push_back(uint8_t(text[i]) ^ mask[i % 4]);
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
        payloadLen = (uint64_t(ext[0]) << 8) | ext[1];
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
            co_await conn.sendText(msg->text());
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
    server.route("GET", "/ping", [](auto & req, auto & resp) -> Task<> {
        co_await resp.end("pong");
    });
    WsServer ws;
    ws.route("/ws", [](WsConnection & conn) -> Task<> {
        co_await conn.close();
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
