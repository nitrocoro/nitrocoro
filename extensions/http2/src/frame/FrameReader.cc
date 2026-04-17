/**
 * @file FrameReader.cc
 */
#include "FrameReader.h"

#include <cstring>
#include <stdexcept>

namespace nitrocoro::http2
{

Task<bool> FrameReader::readExact(void * buf, size_t len)
{
    size_t received = 0;
    auto * ptr = static_cast<uint8_t *>(buf);
    while (received < len)
    {
        size_t n = co_await stream_->read(ptr + received, len - received);
        if (n == 0)
            co_return false;
        received += n;
    }
    co_return true;
}

Task<std::optional<Frame>> FrameReader::readFrame(uint32_t maxFrameSize)
{
    // 9-byte frame header
    uint8_t hdr[9];
    if (!co_await readExact(hdr, 9))
        co_return std::nullopt;

    Frame frame;
    frame.header.length = (uint32_t(hdr[0]) << 16) | (uint32_t(hdr[1]) << 8) | hdr[2];
    frame.header.type = static_cast<FrameType>(hdr[3]);
    frame.header.flags = hdr[4];
    frame.header.streamId = ((uint32_t(hdr[5]) & 0x7f) << 24) | (uint32_t(hdr[6]) << 16) | (uint32_t(hdr[7]) << 8) | uint32_t(hdr[8]);

    if (frame.header.length > maxFrameSize)
        co_return frame; // payload empty, caller checks header.length > maxFrameSize

    if (frame.header.length > 0)
    {
        frame.payload.resize(frame.header.length);
        if (!co_await readExact(frame.payload.data(), frame.header.length))
            co_return std::nullopt;
    }

    co_return frame;
}

Task<> FrameReader::writeFrame(FrameType type, uint8_t flags, uint32_t streamId,
                               const void * payload, size_t payloadLen)
{
    uint8_t hdr[9];
    hdr[0] = (payloadLen >> 16) & 0xff;
    hdr[1] = (payloadLen >> 8) & 0xff;
    hdr[2] = payloadLen & 0xff;
    hdr[3] = static_cast<uint8_t>(type);
    hdr[4] = flags;
    hdr[5] = (streamId >> 24) & 0x7f;
    hdr[6] = (streamId >> 16) & 0xff;
    hdr[7] = (streamId >> 8) & 0xff;
    hdr[8] = streamId & 0xff;

    co_await stream_->write(hdr, 9);
    if (payloadLen > 0)
        co_await stream_->write(payload, payloadLen);
}

} // namespace nitrocoro::http2
