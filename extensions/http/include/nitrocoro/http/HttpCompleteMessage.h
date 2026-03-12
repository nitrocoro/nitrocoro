/**
 * @file HttpCompleteMessage.h
 * @brief HTTP message data structures
 */
#pragma once
#include <nitrocoro/http/HttpMessage.h>
#include <nitrocoro/http/HttpMessageAccessor.h>

#include <string>

namespace nitrocoro::http
{

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
