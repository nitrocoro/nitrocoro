/**
 * @file WsTypes.h
 * @brief WebSocket shared types — message type and close codes (RFC 6455 §7.4.1)
 */
#pragma once

#include <cstdint>

namespace nitrocoro::websocket
{

static constexpr std::string_view kWebSocketGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

enum class WsMessageType
{
    Text,
    Binary,
};

enum class CloseCode : uint16_t
{
    // 1000 indicates a normal closure, meaning that the purpose for which the
    // connection was established has been fulfilled.
    NormalClosure = 1000,
    // 1001 indicates that an endpoint is "going away", such as a server going
    // down or a browser having navigated away from a page.
    EndpointGone = 1001,
    // 1002 indicates that an endpoint is terminating the connection due to a
    // protocol error.
    ProtocolError = 1002,
    // 1003 indicates that an endpoint is terminating the connection because it
    // has received a type of data it cannot accept.
    InvalidMessage = 1003,
    // 1005 is a reserved value and MUST NOT be set as a status code in a Close
    // control frame. Designated for use in applications expecting a status code
    // to indicate that no status code was actually present.
    None = 1005,
    // 1006 is a reserved value and MUST NOT be set as a status code in a Close
    // control frame. Designated for use in applications expecting a status code
    // to indicate that the connection was closed abnormally, e.g., without
    // sending or receiving a Close control frame.
    Abnormally = 1006,
    // 1007 indicates that an endpoint is terminating the connection because it
    // has received data within a message that was not consistent with the type
    // of the message (e.g., non-UTF-8 data within a text message).
    WrongMessageContent = 1007,
    // 1008 indicates that an endpoint is terminating the connection because it
    // has received a message that violates its policy.
    Violation = 1008,
    // 1009 indicates that an endpoint is terminating the connection because it
    // has received a message that is too big for it to process.
    MessageTooBig = 1009,
    // 1010 indicates that an endpoint (client) is terminating the connection
    // because it has expected the server to negotiate one or more extension,
    // but the server didn't return them in the response message of the
    // WebSocket handshake.
    NeedMoreExtensions = 1010,
    // 1011 indicates that a server is terminating the connection because it
    // encountered an unexpected condition that prevented it from fulfilling
    // the request.
    UnexpectedCondition = 1011,
    // 1015 is a reserved value and MUST NOT be set as a status code in a Close
    // control frame. Designated for use in applications expecting a status code
    // to indicate that the connection was closed due to a failure to perform a
    // TLS handshake.
    TLSFailed = 1015,
};

} // namespace nitrocoro::websocket
