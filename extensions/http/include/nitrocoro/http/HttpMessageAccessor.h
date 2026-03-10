/**
 * @file HttpMessageAccessor.h
 * @brief Base classes for read-only HTTP message access
 */
#pragma once
#include <nitrocoro/http/HttpHeader.h>
#include <nitrocoro/http/HttpMessage.h>
#include <nitrocoro/http/HttpTypes.h>

#include <algorithm>
#include <map>
#include <string>
#include <string_view>

namespace nitrocoro::http
{

template <typename Message>
class HttpMessageAccessor
{
public:
    explicit HttpMessageAccessor(Message message);

    const HttpHeaderMap & headers() const;
    const HttpCookieMap & cookies() const;
    const std::string & getHeader(std::string_view name) const;
    const std::string & getHeader(HttpHeader::NameCode code) const;
    const std::string & getCookie(std::string_view name) const;

protected:
    Message message_;
};

class HttpRequestAccessor : public HttpMessageAccessor<HttpRequest>
{
public:
    using HttpMessageAccessor::HttpMessageAccessor;

    Version version() const { return message_.version; }
    HttpMethod method() const { return message_.method; }
    const std::string & path() const { return message_.path; }
    const HttpQueryMap & queries() const { return message_.queries; }

    const std::string & getQuery(std::string_view name) const
    {
        static const std::string emptyValue{};
        auto it = message_.queries.find(name);
        return it != message_.queries.end() ? it->second : emptyValue;
    }
};

class HttpResponseAccessor : public HttpMessageAccessor<HttpResponse>
{
public:
    using HttpMessageAccessor::HttpMessageAccessor;

    Version version() const { return message_.version; }
    StatusCode statusCode() const { return message_.statusCode; }
    const std::string & statusReason() const { return message_.statusReason; }
};

template <typename Message>
HttpMessageAccessor<Message>::HttpMessageAccessor(Message message)
    : message_(std::move(message))
{
}

template <typename Message>
const HttpHeaderMap & HttpMessageAccessor<Message>::headers() const
{
    return message_.headers;
}

template <typename Message>
const HttpCookieMap & HttpMessageAccessor<Message>::cookies() const
{
    return message_.cookies;
}

template <typename Message>
const std::string & HttpMessageAccessor<Message>::getHeader(std::string_view name) const
{
    static const std::string emptyValue{};
    std::string lowerName{ name };
    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    auto it = message_.headers.find(lowerName);
    return it != message_.headers.end() ? it->second.value() : emptyValue;
}

template <typename Message>
const std::string & HttpMessageAccessor<Message>::getHeader(HttpHeader::NameCode code) const
{
    static const std::string emptyValue{};
    auto it = message_.headers.find(HttpHeader::codeToName(code));
    return it != message_.headers.end() ? it->second.value() : emptyValue;
}

template <typename Message>
const std::string & HttpMessageAccessor<Message>::getCookie(std::string_view name) const
{
    static const std::string emptyValue{};
    auto it = message_.cookies.find(name);
    return it != message_.cookies.end() ? it->second : emptyValue;
}

} // namespace nitrocoro::http
