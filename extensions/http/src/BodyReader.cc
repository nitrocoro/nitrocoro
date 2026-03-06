/**
 * @file BodyReader.cc
 * @brief Implementation for BodyReader
 */
#include <nitrocoro/http/BodyReader.h>

#include "body_reader/ChunkedReader.h"
#include "body_reader/ContentLengthReader.h"
#include "body_reader/UntilCloseReader.h"

namespace nitrocoro::http
{

class NoopReader : public BodyReader
{
public:
    bool isComplete() const override { return true; }

protected:
    Task<size_t> readImpl(char *, size_t) override { co_return 0; }
};

std::shared_ptr<BodyReader> BodyReader::create(
    io::StreamPtr stream,
    std::shared_ptr<utils::StringBuffer> buffer,
    TransferMode mode,
    size_t contentLength)
{
    switch (mode)
    {
        case TransferMode::ContentLength:
            if (contentLength == 0)
            {
                static auto noop = std::make_shared<NoopReader>();
                return noop;
            }
            return std::make_shared<ContentLengthReader>(std::move(stream), std::move(buffer), contentLength);
        case TransferMode::Chunked:
            return std::make_shared<ChunkedReader>(std::move(stream), std::move(buffer));
        case TransferMode::UntilClose:
            return std::make_shared<UntilCloseReader>(std::move(stream), std::move(buffer));
    }
    return std::make_shared<UntilCloseReader>(std::move(stream), std::move(buffer));
}

Task<size_t> BodyReader::read(char * buf, size_t len)
{
    [[maybe_unused]] auto lock = co_await mutex_.scoped_lock();
    if (draining_ || isComplete())
        co_return 0;
    co_return co_await readImpl(buf, len);
}

Task<> BodyReader::drain()
{
    [[maybe_unused]] auto lock = co_await mutex_.scoped_lock();
    draining_ = true;
    char buf[4096];
    while (!isComplete())
    {
        size_t n = co_await readImpl(buf, sizeof(buf));
        if (n == 0)
            break;
    }
}

} // namespace nitrocoro::http
