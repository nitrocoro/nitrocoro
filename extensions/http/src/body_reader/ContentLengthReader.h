/**
 * @file ContentLengthReader.h
 * @brief Body reader for Content-Length based transfer
 */
#pragma once
#include "../Http1BodyReader.h"
#include <nitrocoro/io/Stream.h>

namespace nitrocoro::http
{

class ContentLengthReader : public Http1BodyReader
{
public:
    ContentLengthReader(io::StreamPtr stream, std::shared_ptr<utils::StringBuffer> buffer, size_t contentLength)
        : stream_(std::move(stream)), buffer_(std::move(buffer)), contentLength_(contentLength) {}

    Task<size_t> readImpl(char * buf, size_t len) override;
    bool isComplete() const override { return bytesRead_ >= contentLength_; }

private:
    io::StreamPtr stream_;
    std::shared_ptr<utils::StringBuffer> buffer_;
    const size_t contentLength_;
    size_t bytesRead_ = 0;
};

} // namespace nitrocoro::http
