/**
 * @file StringBuffer.h
 * @brief High-performance string buffer with zero-copy read operations
 */
#pragma once

#include <nitrocoro/utils/WriteBuffer.h>

#include <cstring>
#include <string>
#include <string_view>

namespace nitrocoro::utils
{

/**
 * @brief String buffer with read/write offsets.
 */
class StringBuffer : public WriteBuffer
{
public:
    explicit StringBuffer() = default;

    // Get view of unconsumed data
    std::string_view view() const { return { buffer_.data() + readOffset_, writeOffset_ - readOffset_ }; }

    // Find pattern in unconsumed data
    size_t find(std::string_view pattern, size_t pos = 0) const
    {
        std::string_view unconsumed = view();
        size_t result = unconsumed.find(pattern, pos);
        return result;
    }

    // Mark n bytes as consumed
    void consume(size_t n)
    {
        // TODO: check n
        readOffset_ += n;
    }

    // Prepare space for writing, returns pointer to write position
    char * prepareWrite(size_t len) override
    {
        if (buffer_.size() - writeOffset_ < len)
        {
            if (readOffset_ > 0 && buffer_.size() - (writeOffset_ - readOffset_) >= len)
            {
                // Compact: move unconsumed data to front
                size_t remaining = writeOffset_ - readOffset_;
                std::memmove(buffer_.data(), buffer_.data() + readOffset_, remaining);
                writeOffset_ = remaining;
                readOffset_ = 0;
            }
            else
            {
                buffer_.resize(writeOffset_ + len);
            }
        }
        return buffer_.data() + writeOffset_;
    }

    // Get write begin position (without growing)
    char * beginWrite() override { return buffer_.data() + writeOffset_; }

    // Get writable size (without growing)
    size_t writableSize() const override { return buffer_.size() - writeOffset_; }

    // Commit actual written bytes
    void commitWrite(size_t len) override { writeOffset_ += len; }

    // Get size of unconsumed data
    size_t remainSize() const { return writeOffset_ - readOffset_; }
    bool hasRemaining() const { return readOffset_ < writeOffset_; }

    // Reset offsets (keep buffer capacity)
    void reset()
    {
        readOffset_ = 0;
        writeOffset_ = 0;
    }

    // Extract the unconsumed data as string
    std::string extract()
    {
        if (readOffset_ == 0)
        {
            // Data starts from beginning, resize and move
            std::string result = std::move(buffer_);
            result.resize(writeOffset_);
            readOffset_ = 0;
            writeOffset_ = 0;
            return result;
        }
        else
        {
            // Extract unconsumed portion (need to copy)
            std::string result(buffer_.data() + readOffset_, writeOffset_ - readOffset_);
            buffer_.clear();
            readOffset_ = 0;
            writeOffset_ = 0;
            return result;
        }
    }

private:
    std::string buffer_;
    size_t readOffset_ = 0;  // Start of unconsumed data
    size_t writeOffset_ = 0; // End of written data
};

} // namespace nitrocoro::utils
