/**
 * @file Http2BodyReader.h
 * @brief BodyReader backed by a Pipe, for streaming HTTP/2 request bodies
 */
#pragma once

#include <nitrocoro/core/Pipe.h>
#include <nitrocoro/http/BodyReader.h>

#include <cstring>
#include <string>

namespace nitrocoro::http2
{

class Http2BodyReader : public http::BodyReader
{
public:
    explicit Http2BodyReader(std::unique_ptr<PipeReceiver<std::string>> rx)
        : rx_(std::move(rx))
    {
    }

    bool isComplete() const override { return done_; }

    Task<size_t> read(char * buf, size_t len) override
    {
        if (buf_.empty())
        {
            auto chunk = co_await rx_->recv();
            if (!chunk)
            {
                done_ = true;
                co_return 0;
            }
            if (chunk->size() <= len)
            {
                size_t n = chunk->size();
                std::memcpy(buf, chunk->data(), n);
                co_return n;
            }
            buf_ = std::move(*chunk);
            pos_ = 0;
        }
        size_t n = std::min(len, buf_.size() - pos_);
        std::memcpy(buf, buf_.data() + pos_, n);
        pos_ += n;
        if (pos_ == buf_.size())
            buf_.clear();
        co_return n;
    }

    Task<> drain() override
    {
        while (!done_)
        {
            auto chunk = co_await rx_->recv();
            if (!chunk)
                done_ = true;
        }
        co_return;
    }

private:
    std::unique_ptr<PipeReceiver<std::string>> rx_;
    std::string buf_;
    size_t pos_{ 0 };
    bool done_{ false };
};

} // namespace nitrocoro::http2
