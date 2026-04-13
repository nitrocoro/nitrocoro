/**
 * @file HttpIncomingStream.h
 * @brief HTTP incoming stream for reading requests and responses
 */
#pragma once
#include <nitrocoro/http/BodyReader.h>
#include <nitrocoro/http/HttpMessage.h>

#include <nitrocoro/core/Task.h>
#include <nitrocoro/utils/WriteBuffer.h>

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

    Task<size_t> readToEnd(utils::WriteBuffer & buf);

protected:
    std::shared_ptr<BodyReader> bodyReader_;
};

} // namespace detail

template <typename T>
class HttpIncomingStream;

using PathParams = std::unordered_map<std::string, std::string>;

template <>
class HttpIncomingStream<HttpRequest>
    : public HttpRequestAccessor,
      public detail::HttpIncomingStreamBase
{
public:
    HttpIncomingStream(HttpRequest message, std::shared_ptr<BodyReader> bodyReader)
        : HttpRequestAccessor(std::move(message)), detail::HttpIncomingStreamBase(std::move(bodyReader)) {}

    Task<HttpCompleteRequest> toCompleteRequest();

    PathParams & pathParams() { return pathParams_; }
    const PathParams & pathParams() const { return pathParams_; }

protected:
    PathParams pathParams_;
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
