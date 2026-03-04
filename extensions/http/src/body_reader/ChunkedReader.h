/**
 * @file ChunkedReader.h
 * @brief Body reader for chunked transfer encoding
 */
#pragma once
#include <nitrocoro/http/BodyReader.h>
#include <nitrocoro/io/Stream.h>

namespace nitrocoro::http
{

class ChunkedReader : public BodyReader
{
public:
    ChunkedReader(io::StreamPtr stream, std::shared_ptr<utils::StringBuffer> buffer)
        : stream_(std::move(stream)), buffer_(std::move(buffer)) {}

    Task<size_t> readImpl(char * buf, size_t len) override;
    bool isComplete() const override { return complete_; }

private:
    enum class State
    {
        ReadSize,
        ReadData,
        Complete
    };

    io::StreamPtr stream_;
    std::shared_ptr<utils::StringBuffer> buffer_;
    State state_ = State::ReadSize;
    size_t currentChunkSize_ = 0;
    size_t currentChunkRead_ = 0;
    bool complete_ = false;

    Task<bool> parseChunkSize();
    Task<> skipCRLF();
};

} // namespace nitrocoro::http
