/**
 * @file ContentLengthWriter.h
 * @brief Body writer for Content-Length based transfer
 */
#pragma once
#include "../Http1BodyWriter.h"
#include <nitrocoro/io/Stream.h>

namespace nitrocoro::http
{

class ContentLengthWriter : public Http1BodyWriter
{
public:
    ContentLengthWriter(io::StreamPtr stream, size_t contentLength)
        : stream_(std::move(stream)), contentLength_(contentLength) {}

    Task<> write(std::string_view data) override;
    Task<> end() override;

private:
    io::StreamPtr stream_;
    const size_t contentLength_;
    size_t bytesWritten_ = 0;
};

} // namespace nitrocoro::http
