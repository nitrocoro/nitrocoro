/**
 * @file HttpIncomingStream.h
 * @brief HTTP incoming stream for reading requests and responses
 */
#pragma once
#include <nitrocoro/http/BodyReader.h>
#include <nitrocoro/http/HttpCompleteMessage.h>
#include <nitrocoro/http/HttpMessage.h>
#include <nitrocoro/http/HttpMessageAccessor.h>

#include <nitrocoro/core/Task.h>

#include <memory>
#include <string>

namespace nitrocoro::http
{

namespace detail
{

class HttpIncomingStreamBase
{
public:
    explicit HttpIncomingStreamBase(std::shared_ptr<BodyReader> bodyReader)
        : bodyReader_(std::move(bodyReader)) {}

    Task<size_t> read(char * buf, size_t maxLen);
    Task<std::string> read(size_t maxLen);

    template <utils::ExtendableBuffer T>
    Task<size_t> readToEnd(T & buf)
    {
        co_return co_await bodyReader_->readToEnd(buf);
    }

protected:
    std::shared_ptr<BodyReader> bodyReader_;
};

} // namespace detail

template <typename T>
class HttpIncomingStream;

template <>
class HttpIncomingStream<HttpRequest>
    : public HttpRequestAccessor,
      public detail::HttpIncomingStreamBase
{
public:
    HttpIncomingStream(HttpRequest message, std::shared_ptr<BodyReader> bodyReader)
        : HttpRequestAccessor(std::move(message)), detail::HttpIncomingStreamBase(std::move(bodyReader)) {}

    Task<HttpCompleteRequest> toCompleteRequest();
};

template <>
class HttpIncomingStream<HttpResponse>
    : public HttpResponseAccessor,
      public detail::HttpIncomingStreamBase
{
public:
    HttpIncomingStream(HttpResponse message, std::shared_ptr<BodyReader> bodyReader)
        : HttpResponseAccessor(std::move(message)), detail::HttpIncomingStreamBase(std::move(bodyReader)) {}

    Task<HttpCompleteResponse> toCompleteResponse();
};

} // namespace nitrocoro::http
