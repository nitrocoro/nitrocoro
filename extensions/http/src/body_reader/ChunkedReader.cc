/**
 * @file ChunkedReader.cc
 * @brief Implementation of ChunkedReader
 */
#include "ChunkedReader.h"
#include <cstring>

namespace nitrocoro::http
{

Task<bool> ChunkedReader::parseChunkSize()
{
    constexpr size_t MAX_CHUNK_SIZE_LINE = 64;

    while (true)
    {
        size_t pos = buffer_->find("\r\n");
        if (pos == std::string::npos)
        {
            if (buffer_->remainSize() >= MAX_CHUNK_SIZE_LINE)
                throw std::runtime_error("Invalid chunked encoding: chunk size line too long");

            char * writePtr = buffer_->prepareWrite(128);
            size_t n = co_await stream_->read(writePtr, 128);
            if (n == 0)
                throw std::runtime_error("Connection closed before chunk size complete");
            buffer_->commitWrite(n);
            continue;
        }

        if (pos > MAX_CHUNK_SIZE_LINE)
            throw std::runtime_error("Invalid chunked encoding: chunk size line too long");

        std::string_view line = buffer_->view().substr(0, pos);
        currentChunkSize_ = std::stoul(std::string(line), nullptr, 16);
        buffer_->consume(pos + 2);
        currentChunkRead_ = 0;

        if (currentChunkSize_ == 0)
        {
            co_await skipCRLF();
            complete_ = true;
            state_ = State::Complete;
        }
        else
        {
            state_ = State::ReadData;
        }

        co_return true;
    }
}

Task<> ChunkedReader::skipCRLF()
{
    while (buffer_->remainSize() < 2)
    {
        char * writePtr = buffer_->prepareWrite(128);
        size_t n = co_await stream_->read(writePtr, 128);
        if (n == 0)
            throw std::runtime_error("Connection closed before chunk CRLF complete");
        buffer_->commitWrite(n);
    }
    buffer_->consume(2);
}

Task<size_t> ChunkedReader::readImpl(char * buf, size_t len)
{
    if (complete_)
        co_return 0;

    if (state_ == State::ReadSize)
    {
        if (!co_await parseChunkSize())
            throw std::runtime_error("Invalid chunked encoding");
        if (complete_)
            co_return 0;
    }

    size_t available = buffer_->remainSize();
    size_t remaining = currentChunkSize_ - currentChunkRead_;

    if (available > 0)
    {
        size_t toRead = std::min({ available, remaining, len });
        std::memcpy(buf, buffer_->view().data(), toRead);
        buffer_->consume(toRead);
        currentChunkRead_ += toRead;

        if (currentChunkRead_ >= currentChunkSize_)
        {
            co_await skipCRLF();
            state_ = State::ReadSize;
        }

        co_return toRead;
    }

    size_t toRead = std::min(len, remaining);
    size_t n = co_await stream_->read(buf, toRead);
    if (n == 0)
        throw std::runtime_error("Connection closed before chunk data complete");
    currentChunkRead_ += n;

    if (currentChunkRead_ >= currentChunkSize_)
    {
        co_await skipCRLF();
        state_ = State::ReadSize;
    }

    co_return n;
}

} // namespace nitrocoro::http
