/**
 * @file Http2Stream.h
 * @brief Per-stream state for an HTTP/2 connection (RFC 7540 §5)
 */
#pragma once

#include "hpack/Hpack.h"
#include <nitrocoro/core/Pipe.h>
#include <nitrocoro/core/Task.h>
#include <nitrocoro/http/HttpMessage.h>

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

    // Data frames are pushed here; closed on END_STREAM
    std::shared_ptr<PipeSender<std::string>> bodySender;
    std::unique_ptr<PipeReceiver<std::string>> bodyReceiver;

    explicit Http2Stream(uint32_t id, Scheduler * sched)
        : streamId(id)
    {
        auto [tx, rx] = makePipe<std::string>(sched);
        bodySender = std::move(tx);
        bodyReceiver = std::move(rx);
    }
};

} // namespace nitrocoro::http2
