/**
 * @file Frame.h
 * @brief HTTP/2 frame type definitions (RFC 7540 §6)
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace nitrocoro::http2
{

enum class FrameType : uint8_t
{
    Data         = 0x0,
    Headers      = 0x1,
    Priority     = 0x2,
    RstStream    = 0x3,
    Settings     = 0x4,
    PushPromise  = 0x5,
    Ping         = 0x6,
    GoAway       = 0x7,
    WindowUpdate = 0x8,
    Continuation = 0x9,
};

namespace FrameFlags
{
inline constexpr uint8_t EndStream  = 0x1;
inline constexpr uint8_t EndHeaders = 0x4;
inline constexpr uint8_t Padded     = 0x8;
inline constexpr uint8_t Priority   = 0x20;
inline constexpr uint8_t Ack        = 0x1;
} // namespace FrameFlags

namespace ErrorCode
{
inline constexpr uint32_t NoError            = 0x0;
inline constexpr uint32_t ProtocolError      = 0x1;
inline constexpr uint32_t InternalError      = 0x2;
inline constexpr uint32_t FlowControlError   = 0x3;
inline constexpr uint32_t SettingsTimeout    = 0x4;
inline constexpr uint32_t StreamClosed       = 0x5;
inline constexpr uint32_t FrameSizeError     = 0x6;
inline constexpr uint32_t RefusedStream      = 0x7;
inline constexpr uint32_t Cancel             = 0x8;
inline constexpr uint32_t CompressionError   = 0x9;
inline constexpr uint32_t ConnectError       = 0xa;
inline constexpr uint32_t EnhanceYourCalm    = 0xb;
inline constexpr uint32_t InadequateSecurity = 0xc;
inline constexpr uint32_t Http11Required     = 0xd;
} // namespace ErrorCode

struct FrameHeader
{
    uint32_t length;   // 24-bit payload length
    FrameType type;
    uint8_t  flags;
    uint32_t streamId; // 31-bit (R bit masked out)
};

struct Frame
{
    FrameHeader     header;
    std::vector<uint8_t> payload;
};

// Settings parameter identifiers (RFC 7540 §6.5.2)
namespace SettingsParam
{
inline constexpr uint16_t HeaderTableSize      = 0x1;
inline constexpr uint16_t EnablePush           = 0x2;
inline constexpr uint16_t MaxConcurrentStreams = 0x3;
inline constexpr uint16_t InitialWindowSize    = 0x4;
inline constexpr uint16_t MaxFrameSize         = 0x5;
inline constexpr uint16_t MaxHeaderListSize    = 0x6;
} // namespace SettingsParam

// Client connection preface magic (RFC 7540 §3.5)
inline constexpr std::string_view kClientPreface =
    "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";

} // namespace nitrocoro::http2
