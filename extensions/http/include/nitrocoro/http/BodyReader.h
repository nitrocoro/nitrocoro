/**
 * @file BodyReader.h
 * @brief Body reader interface and factory
 */
#pragma once
#include <nitrocoro/core/Mutex.h>
#include <nitrocoro/core/Task.h>
#include <nitrocoro/http/HttpParser.h>
#include <nitrocoro/io/Stream.h>
#include <nitrocoro/utils/ExtendableBuffer.h>
#include <nitrocoro/utils/StringBuffer.h>

namespace nitrocoro::http
{

class BodyReader
{
public:
    static std::shared_ptr<BodyReader> create(
        io::StreamPtr stream,
        std::shared_ptr<utils::StringBuffer> buffer,
        TransferMode mode,
        size_t contentLength);

    virtual ~BodyReader() = default;
    virtual bool isComplete() const = 0;

    // Returns 0 if body is complete or drain() has been called.
    Task<size_t> read(char * buf, size_t len);

    // Consumes remaining body from TCP stream, discarding data.
    // After this, read() returns 0 immediately.
    Task<> drain();

    template <utils::ExtendableBuffer T>
    Task<size_t> readToEnd(T & buf);

protected:
    virtual Task<size_t> readImpl(char * buf, size_t len) = 0;

private:
    Mutex mutex_;
    bool draining_{ false };
};

template <utils::ExtendableBuffer T>
Task<size_t> BodyReader::readToEnd(T & buf)
{
    constexpr size_t kExtendThreshold = 4096;
    constexpr size_t kExtendSize = 8192;

    size_t total = 0;
    while (!isComplete())
    {
        size_t available = buf.writableSize();
        char * ptr;

        if (available < kExtendThreshold)
        {
            ptr = buf.prepareWrite(kExtendSize);
            available = kExtendSize;
        }
        else
        {
            ptr = buf.beginWrite();
        }

        size_t n = co_await read(ptr, available);
        if (n == 0)
            break;

        buf.commitWrite(n);
        total += n;
    }
    co_return total;
}

} // namespace nitrocoro::http
