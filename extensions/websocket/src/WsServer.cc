/**
 * @file WsServer.cc
 * @brief WebSocket upgrade handshake + routing
 */
#include <nitrocoro/websocket/WsServer.h>

#include <nitrocoro/http/HttpHeader.h>

#include <array>
#include <cstring>
#include <string>
#include <vector>

static void sha1(const uint8_t * data, size_t len, uint8_t out[20])
{
    uint32_t h[5] = { 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0 };
    auto rol = [](uint32_t v, int n) { return (v << n) | (v >> (32 - n)); };

    // Pre-processing: build padded message in chunks
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
    static constexpr char kTable[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    for (size_t i = 0; i < len; i += 3)
    {
        uint32_t v = uint32_t(data[i]) << 16;
        if (i + 1 < len)
            v |= uint32_t(data[i + 1]) << 8;
        if (i + 2 < len)
            v |= uint32_t(data[i + 2]);
        out += kTable[(v >> 18) & 0x3F];
        out += kTable[(v >> 12) & 0x3F];
        out += (i + 1 < len) ? kTable[(v >> 6) & 0x3F] : '=';
        out += (i + 2 < len) ? kTable[v & 0x3F] : '=';
    }
    return out;
}

static std::string computeAccept(const std::string & key)
{
    static constexpr std::string_view kGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    std::string input = key + std::string(kGuid);
    uint8_t digest[20];
    sha1(reinterpret_cast<const uint8_t *>(input.data()), input.size(), digest);
    return base64Encode(digest, 20);
}

namespace nitrocoro::websocket
{

void WsServer::route(const std::string & path, Handler handler)
{
    routes_[path] = std::move(handler);
}

void WsServer::attachTo(http::HttpServer & server)
{
    server.setRequestUpgrader([this](http::HttpIncomingStream<http::HttpRequest> & req, io::StreamPtr stream) -> Task<bool> {
        co_return co_await handleUpgrade(req, std::move(stream));
    });
}

Task<bool> WsServer::handleUpgrade(http::HttpIncomingStream<http::HttpRequest> & req, io::StreamPtr stream)
{
    using http::HttpHeader;

    // Only handle WebSocket upgrades
    auto & upgrade = req.getHeader(HttpHeader::Name::Upgrade_L);
    if (upgrade != "websocket")
        co_return false;

    auto it = routes_.find(std::string(req.path()));
    if (it == routes_.end())
        co_return false;

    auto & key = req.getHeader("sec-websocket-key");
    if (key.empty())
        co_return false;

    std::string accept = computeAccept(key);

    // Send 101 Switching Protocols directly on the stream
    std::string response = "HTTP/1.1 101 Switching Protocols\r\n"
                           "Upgrade: websocket\r\n"
                           "Connection: Upgrade\r\n"
                           "Sec-WebSocket-Accept: "
                           + accept + "\r\n"
                                      "\r\n";
    co_await stream->write(response.data(), response.size());

    WsConnection conn(std::move(stream));
    co_await it->second(conn);
    co_return true;
}

} // namespace nitrocoro::websocket
