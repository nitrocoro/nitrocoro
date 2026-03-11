/**
 * @file FrameReader.h
 * @brief Reads HTTP/2 frames from an io::Stream
 */
#pragma once

#include "Frame.h"
#include <nitrocoro/core/Task.h>
#include <nitrocoro/io/Stream.h>

#include <optional>

namespace nitrocoro::http2
{

class FrameReader
{
public:
    explicit FrameReader(io::StreamPtr stream) : stream_(std::move(stream)) {}

    // Read one complete frame. Returns nullopt on clean EOF.
    Task<std::optional<Frame>> readFrame();

    // Write a frame to the stream (encodes header + payload).
    Task<> writeFrame(FrameType type, uint8_t flags, uint32_t streamId,
                      const void * payload, size_t payloadLen);

    io::StreamPtr stream() const { return stream_; }

private:
    Task<bool> readExact(void * buf, size_t len);

    io::StreamPtr stream_;
};

} // namespace nitrocoro::http2
