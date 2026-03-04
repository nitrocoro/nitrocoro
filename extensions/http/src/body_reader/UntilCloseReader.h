/**
 * @file UntilCloseReader.h
 * @brief Body reader that reads until connection closes
 */
#pragma once
#include <nitrocoro/http/BodyReader.h>
#include <nitrocoro/io/Stream.h>

namespace nitrocoro::http
{

class UntilCloseReader : public BodyReader
{
public:
    UntilCloseReader(io::StreamPtr stream, std::shared_ptr<utils::StringBuffer> buffer)
        : stream_(std::move(stream)), buffer_(std::move(buffer)) {}

    Task<size_t> readImpl(char * buf, size_t len) override;
    bool isComplete() const override { return complete_; }

private:
    io::StreamPtr stream_;
    std::shared_ptr<utils::StringBuffer> buffer_;
    bool complete_ = false;
};

} // namespace nitrocoro::http
