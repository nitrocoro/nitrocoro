/**
 * @file HttpIncomingStream.cc
 * @brief HTTP incoming stream implementations
 */
#include <nitrocoro/http/stream/HttpIncomingStream.h>

#include "Http1BodyReader.h"
#include <nitrocoro/http/HttpMessage.h>

namespace nitrocoro::http::detail
{

// ============================================================================
// HttpIncomingStreamBase Implementation
// ============================================================================

Task<size_t> HttpIncomingStreamBase::read(char * buf, size_t len)
{
    co_return co_await bodyReader_->read(buf, len);
}

Task<std::string> HttpIncomingStreamBase::read(size_t maxLen)
{
    std::string result(maxLen, '\0');
    size_t n = co_await read(result.data(), maxLen);
    result.resize(n);
    co_return result;
}

Task<size_t> HttpIncomingStreamBase::readToEnd(utils::WriteBuffer & buf)
{
    constexpr size_t kExtendThreshold = 4096;
    constexpr size_t kExtendSize = 8192;

    size_t total = 0;
    while (!bodyReader_->isComplete())
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

        size_t n = co_await bodyReader_->read(ptr, available);
        if (n == 0)
            break;

        buf.commitWrite(n);
        total += n;
    }
    co_return total;
}

} // namespace nitrocoro::http::detail

namespace nitrocoro::http
{

Task<HttpCompleteRequest> HttpIncomingStream<HttpRequest>::toCompleteRequest()
{
    utils::StringBuffer bodyBuf;
    co_await readToEnd(bodyBuf);
    co_return HttpCompleteRequest(std::move(message_), bodyBuf.extract());
}

Task<HttpCompleteResponse> HttpIncomingStream<HttpResponse>::toCompleteResponse()
{
    utils::StringBuffer bodyBuf;
    co_await readToEnd(bodyBuf);
    co_return HttpCompleteResponse(std::move(message_), bodyBuf.extract());
}

} // namespace nitrocoro::http
