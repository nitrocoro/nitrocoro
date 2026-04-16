/**
 * @file HttpOutgoingMessage.h
 * @brief HTTP outgoing stream for writing requests and responses
 */
#pragma once
#include <nitrocoro/http/BodyWriter.h>
#include <nitrocoro/http/Cookie.h>
#include <nitrocoro/http/HttpHeader.h>
#include <nitrocoro/http/HttpMessage.h>
#include <nitrocoro/http/HttpTypes.h>
#include <nitrocoro/http/RequestSink.h>
#include <nitrocoro/http/ResponseSink.h>

#include <nitrocoro/core/Task.h>

#include <string>
#include <string_view>

namespace nitrocoro::http
{

namespace detail
{

template <typename DataType>
class HttpOutgoingMessageBase
{
public:
    HttpOutgoingMessageBase() = default;

    const DataType & data() const { return data_; }

    void setHeader(std::string_view name, std::string value);
    void setHeader(HttpHeader::NameCode code, std::string value);
    void setHeader(HttpHeader header);

    void setBody(std::string body);
    void setBody(const char * data, size_t len);
    void setBody(BodyWriterFn bodyWriterFn);

protected:
    DataType data_;
    std::string body_;
    BodyWriterFn bodyWriterFn_;
};

} // namespace detail

// Forward declaration
template <typename T>
class HttpOutgoingMessage;

// ============================================================================
// HttpOutgoingMessage<HttpRequest> - Write HTTP Request
// ============================================================================

template <>
class HttpOutgoingMessage<HttpRequest>
    : public detail::HttpOutgoingMessageBase<HttpRequest>
{
public:
    HttpOutgoingMessage() = default;

    void setMethod(HttpMethod method) { data_.method = method; }
    void setMethod(std::string_view method) { data_.method = HttpMethod::fromString(method); }
    void setPath(std::string path) { data_.path = std::move(path); }
    void setVersion(Version version) { data_.version = version; }
    void setKeepAlive(bool keepAlive) { data_.keepAlive = keepAlive; }
    void setCookie(const std::string & name, std::string value) { data_.cookies[name] = std::move(value); }

    Task<> flush(RequestSink & sink) const;
    Task<> flush(RequestSink && sink) const;
};

// ============================================================================
// HttpOutgoingMessage<HttpResponse> - Write HTTP Response
// ============================================================================

template <>
class HttpOutgoingMessage<HttpResponse>
    : public detail::HttpOutgoingMessageBase<HttpResponse>
{
public:
    HttpOutgoingMessage() = default;

    void setStatus(int code, const std::string & reason = "");
    void setStatus(StatusCode code, const std::string & reason = "");
    void setVersion(Version version) { data_.version = version; }
    void setCloseConnection(bool shouldClose) { data_.shouldClose = shouldClose; }
    void addCookie(Cookie cookie) { data_.cookies.push_back(std::move(cookie)); }

    void clear();

    Task<> flush(ResponseSink & sink) const;
    Task<> flush(ResponseSink && sink) const;
};

} // namespace nitrocoro::http
