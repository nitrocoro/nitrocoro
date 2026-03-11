/**
 * @file Http2Stream.h
 * @brief Per-stream state for an HTTP/2 connection (RFC 7540 §5)
 */
#pragma once

#include "hpack/Hpack.h"
#include <nitrocoro/core/Future.h>
#include <nitrocoro/core/Task.h>
#include <nitrocoro/http/HttpMessage.h>

#include <string>

namespace nitrocoro::http2
{

// Lifecycle of a single HTTP/2 stream from the server's perspective.
// Created when the first HEADERS frame for a stream_id arrives.
struct Http2Stream
{
    uint32_t streamId;

    // Filled by HEADERS/CONTINUATION frames
    hpack::DecodedHeaders decodedHeaders;
    bool headersComplete{ false };

    // Filled by DATA frames
    std::string body;
    bool endStream{ false };

    // Signalled when END_STREAM is received (headers-only or after DATA)
    Promise<> requestReady;

    explicit Http2Stream(uint32_t id, Scheduler * sched)
        : streamId(id), requestReady(sched)
    {
    }
};

} // namespace nitrocoro::http2
