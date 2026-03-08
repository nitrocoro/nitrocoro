/**
 * @file WsConnection.cc
 * @brief WebSocket framing (RFC 6455)
 */
#include <nitrocoro/websocket/WsConnection.h>

#include <array>
#include <cstring>
#include <stdexcept>

namespace nitrocoro::websocket
{

// ── helpers ──────────────────────────────────────────────────────────────────

static Task<size_t> readExact(io::Stream & s, void * buf, size_t len)
{
    size_t total = 0;
    while (total < len)
    {
        size_t n = co_await s.read(static_cast<char *>(buf) + total, len - total);
        if (n == 0)
            throw std::runtime_error("WsConnection: connection closed");
        total += n;
    }
    co_return total;
}

// ── receive ──────────────────────────────────────────────────────────────────

Task<std::optional<WsMessage>> WsConnection::receive()
{
    std::vector<uint8_t> payload;
    WsOpcode finalOpcode = WsOpcode::Continuation;

    while (true)
    {
        // Read 2-byte frame header
        uint8_t header[2];
        co_await readExact(*stream_, header, 2);

        bool fin    = (header[0] & 0x80) != 0;
        auto opcode = static_cast<WsOpcode>(header[0] & 0x0F);
        bool masked = (header[1] & 0x80) != 0;
        uint64_t payloadLen = header[1] & 0x7F;

        if (payloadLen == 126)
        {
            uint8_t ext[2];
            co_await readExact(*stream_, ext, 2);
            payloadLen = (uint64_t(ext[0]) << 8) | ext[1];
        }
        else if (payloadLen == 127)
        {
            uint8_t ext[8];
            co_await readExact(*stream_, ext, 8);
            payloadLen = 0;
            for (int i = 0; i < 8; ++i)
                payloadLen = (payloadLen << 8) | ext[i];
        }

        uint8_t maskKey[4] = {};
        if (masked)
            co_await readExact(*stream_, maskKey, 4);

        size_t offset = payload.size();
        payload.resize(offset + payloadLen);
        co_await readExact(*stream_, payload.data() + offset, payloadLen);

        if (masked)
            for (size_t i = 0; i < payloadLen; ++i)
                payload[offset + i] ^= maskKey[i % 4];

        // Control frames (ping/pong/close) are never fragmented
        if (opcode == WsOpcode::Ping)
        {
            co_await sendFrame(WsOpcode::Pong, payload.data() + offset, payloadLen);
            payload.resize(offset);
            continue;
        }
        if (opcode == WsOpcode::Close)
            co_return std::nullopt;

        if (opcode != WsOpcode::Continuation)
            finalOpcode = opcode;

        if (fin)
            co_return WsMessage{ finalOpcode, std::move(payload) };
    }
}

// ── send ─────────────────────────────────────────────────────────────────────

Task<> WsConnection::sendFrame(WsOpcode opcode, const void * data, size_t len, bool mask)
{
    std::vector<uint8_t> frame;
    frame.reserve(10 + len);

    frame.push_back(0x80 | static_cast<uint8_t>(opcode)); // FIN + opcode

    uint8_t maskBit = mask ? 0x80 : 0x00;
    if (len < 126)
    {
        frame.push_back(maskBit | static_cast<uint8_t>(len));
    }
    else if (len < 65536)
    {
        frame.push_back(maskBit | 126);
        frame.push_back(static_cast<uint8_t>(len >> 8));
        frame.push_back(static_cast<uint8_t>(len));
    }
    else
    {
        frame.push_back(maskBit | 127);
        for (int i = 7; i >= 0; --i)
            frame.push_back(static_cast<uint8_t>(len >> (i * 8)));
    }

    const auto * bytes = static_cast<const uint8_t *>(data);
    frame.insert(frame.end(), bytes, bytes + len);

    co_await stream_->write(frame.data(), frame.size());
}

Task<> WsConnection::sendText(std::string_view text)
{
    co_await sendFrame(WsOpcode::Text, text.data(), text.size());
}

Task<> WsConnection::sendBinary(std::vector<uint8_t> data)
{
    co_await sendFrame(WsOpcode::Binary, data.data(), data.size());
}

Task<> WsConnection::close(uint16_t code)
{
    uint8_t payload[2] = { static_cast<uint8_t>(code >> 8), static_cast<uint8_t>(code) };
    co_await sendFrame(WsOpcode::Close, payload, 2);
}

} // namespace nitrocoro::websocket
