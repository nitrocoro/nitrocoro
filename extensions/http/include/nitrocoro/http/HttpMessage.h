/**
 * @file HttpMessage.h
 * @brief HTTP message data structures, accessors, and complete message types
 */
#pragma once

#include <nitrocoro/http/Cookie.h>
#include <nitrocoro/http/HttpHeader.h>
#include <nitrocoro/http/HttpTypes.h>

#include <algorithm>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace nitrocoro::http
{

using HttpHeaderMap = std::map<std::string, HttpHeader, std::less<>>;
using HttpCookieMap = std::map<std::string, std::string, std::less<>>;
using HttpQueryMap = std::map<std::string, std::string, std::less<>>;
using HttpMultiQueryMap = std::map<std::string, std::vector<std::string>, std::less<>>;

struct HttpRequest
{
    Version version = Version::kHttp11;
    HttpMethod method = methods::_Invalid;
    std::string path;
    std::string rawPath;
    std::string query;
    HttpHeaderMap headers;
    HttpCookieMap cookies;
    HttpQueryMap queries;

    // Metadata parsed from headers
    TransferMode transferMode = TransferMode::ContentLength;
    size_t contentLength = 0;
    bool keepAlive = true;
};

struct HttpResponse
{
    Version version = Version::kHttp11;
    uint16_t statusCode{ static_cast<uint16_t>(StatusCode::k200OK) };
    std::string statusReason;
    HttpHeaderMap headers;
    std::vector<Cookie> cookies;

    // Metadata for sending
    TransferMode transferMode = TransferMode::ContentLength;
    size_t contentLength = 0;
    bool shouldClose = false;
};

// ============================================================================
// HttpMessageAccessor
// ============================================================================

class HttpRequestAccessor
{
public:
    explicit HttpRequestAccessor(HttpRequest message);

    Version version() const { return message_.version; }
    HttpMethod method() const { return message_.method; }
    const std::string & path() const { return message_.path; }
    const std::string & queryString() const { return message_.query; }
    const HttpQueryMap & queries() const { return message_.queries; }
    const std::string & getQuery(std::string_view name) const;

    // Returns all query parameters, with multiple values per key.
    // NOTE: will parse the raw query string on every call.
    HttpMultiQueryMap multiQueries() const;

    const HttpHeaderMap & headers() const { return message_.headers; }
    const std::string & getHeader(std::string_view name) const;
    const std::string & getHeader(HttpHeader::NameCode code) const;

    const HttpCookieMap & cookies() const { return message_.cookies; }
    const std::string & getCookie(std::string_view name) const;

protected:
    HttpRequest message_;
};

class HttpResponseAccessor
{
public:
    explicit HttpResponseAccessor(HttpResponse message);

    Version version() const { return message_.version; }
    uint16_t statusCode() const { return message_.statusCode; }
    const std::string & statusReason() const { return message_.statusReason; }
    bool shouldClose() const { return message_.shouldClose; }

    const HttpHeaderMap & headers() const { return message_.headers; }
    const std::string & getHeader(std::string_view name) const;
    const std::string & getHeader(HttpHeader::NameCode code) const;

    const std::vector<Cookie> & cookies() const { return message_.cookies; }

protected:
    HttpResponse message_;
};

// ============================================================================
// HttpCompleteMessage
// ============================================================================

class HttpCompleteRequest : public HttpRequestAccessor
{
public:
    HttpCompleteRequest(HttpRequest && req, std::string && body)
        : HttpRequestAccessor(std::move(req)), body_(std::move(body)) {}

    const std::string & body() const { return body_; }

private:
    std::string body_;
};

class HttpCompleteResponse : public HttpResponseAccessor
{
public:
    HttpCompleteResponse(HttpResponse && resp, std::string && body)
        : HttpResponseAccessor(std::move(resp)), body_(std::move(body)) {}

    const std::string & body() const { return body_; }

private:
    std::string body_;
};

} // namespace nitrocoro::http
