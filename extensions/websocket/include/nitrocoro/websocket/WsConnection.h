/**
 * @file WsConnection.h
 * @brief WebSocket connection — framed message read/write over an io::Stream
 */
#pragma once

#include <nitrocoro/core/Task.h>
#include <nitrocoro/io/Stream.h>

#include <cstdint>
#include <string>
#include <vector>

namespace nitrocoro::websocket
{

enum class WsOpcode : uint8_t
{
    Continuation = 0x0,
    Text = 0x1,
    Binary = 0x2,
    Close = 0x8,
    Ping = 0x9,
    Pong = 0xA,
};

struct WsMessage
{
    WsOpcode opcode;
    std::vector<uint8_t> payload;

    std::string_view text() const { return { reinterpret_cast<const char *>(payload.data()), payload.size() }; }
};

class WsConnection
{
public:
    explicit WsConnection(io::StreamPtr stream) : stream_(std::move(stream)) {}

    /** Read one complete (possibly fragmented) message. Returns nullopt on close. */
    Task<std::optional<WsMessage>> receive();

    Task<> sendText(std::string_view text);
    Task<> sendBinary(std::vector<uint8_t> data);
    Task<> close(uint16_t code = 1000);

private:
    Task<> sendFrame(WsOpcode opcode, const void * data, size_t len, bool mask = false);

    io::StreamPtr stream_;
};

} // namespace nitrocoro::websocket
