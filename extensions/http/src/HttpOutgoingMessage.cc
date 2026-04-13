/**
 * @file HttpOutgoingMessage.cc
 * @brief HTTP outgoing stream implementations
 */
#include <nitrocoro/http/stream/HttpOutgoingMessage.h>

#include "Http1BodyWriter.h"
#include <nitrocoro/http/HttpUtils.h>
#include <nitrocoro/http/RequestSink.h>
#include <nitrocoro/http/ResponseSink.h>

namespace nitrocoro::http::detail
{

// ============================================================================
// HttpOutgoingMessageBase Implementation
// ============================================================================

template <typename DataType>
void HttpOutgoingMessageBase<DataType>::setHeader(std::string_view name, std::string value)
{
    HttpHeader header(name, std::move(value));
    data_.headers.insert_or_assign(header.name(), std::move(header));
}

template <typename DataType>
void HttpOutgoingMessageBase<DataType>::setHeader(HttpHeader::NameCode code, std::string value)
{
    HttpHeader header(code, std::move(value));
    data_.headers.insert_or_assign(header.name(), std::move(header));
}

template <typename DataType>
void HttpOutgoingMessageBase<DataType>::setHeader(HttpHeader header)
{
    data_.headers.insert_or_assign(header.name(), std::move(header));
}

template <typename DataType>
void HttpOutgoingMessageBase<DataType>::setBody(std::string body)
{
    body_ = std::move(body);
    bodyWriterFn_ = nullptr;
}

template <typename DataType>
void HttpOutgoingMessageBase<DataType>::setBody(const char * data, size_t len)
{
    body_ = std::string(data, len);
    bodyWriterFn_ = nullptr;
}

template <typename DataType>
void HttpOutgoingMessageBase<DataType>::setBody(BodyWriterFn bodyWriterFn)
{
    bodyWriterFn_ = std::move(bodyWriterFn);
}

// Explicit instantiations
template class HttpOutgoingMessageBase<HttpRequest>;
template class HttpOutgoingMessageBase<HttpResponse>;

} // namespace nitrocoro::http::detail

namespace nitrocoro::http
{

// ============================================================================
// HttpOutgoingMessage<HttpRequest> Implementation
// ============================================================================

Task<> HttpOutgoingMessage<HttpRequest>::flush(RequestSink & sink) const
{
    if (bodyWriterFn_)
        co_await sink.sendStream(data_, bodyWriterFn_);
    else
        co_await sink.send(data_, std::string_view(body_));
}

Task<> HttpOutgoingMessage<HttpRequest>::flush(RequestSink && sink) const
{
    co_await flush(sink);
}

// ============================================================================
// HttpOutgoingMessage<HttpResponse> Implementation
// ============================================================================

void HttpOutgoingMessage<HttpResponse>::setStatus(int code, const std::string & reason)
{
    data_.statusCode = code;
    data_.statusReason = reason.empty() ? statusCodeToString(code) : reason;
}

void HttpOutgoingMessage<HttpResponse>::setStatus(StatusCode code, const std::string & reason)
{
    setStatus(static_cast<uint16_t>(code), reason);
}

void HttpOutgoingMessage<HttpResponse>::clear()
{
    data_.statusCode = static_cast<uint16_t>(StatusCode::k200OK);
    data_.statusReason.clear();
    data_.headers.clear();
    data_.cookies.clear();

    body_.clear();
    bodyWriterFn_ = nullptr;
}

Task<> HttpOutgoingMessage<HttpResponse>::flush(ResponseSink & sink) const
{

    if (bodyWriterFn_)
    {
        co_await sink.sendStream(data_, bodyWriterFn_);
    }
    else
    {
        co_await sink.send(data_, std::string_view(body_), ignoreBody_);
    }
}

Task<> HttpOutgoingMessage<HttpResponse>::flush(ResponseSink && sink) const
{
    co_await flush(sink);
}

} // namespace nitrocoro::http
