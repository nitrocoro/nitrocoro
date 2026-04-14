/**
 * @file Http2ResponseSink.h
 * @brief HTTP/2 ResponseSink implementation
 */
#pragma once
#include <nitrocoro/http/BodyWriter.h>
#include <nitrocoro/http/ResponseSink.h>

#include <cstdint>
#include <memory>

namespace nitrocoro::http2
{

class Http2Session;

class Http2ResponseSink : public http::ResponseSink
{
public:
    Http2ResponseSink(std::weak_ptr<Http2Session> session, uint32_t streamId);

    Task<> send(const http::HttpResponse & resp, std::string_view body,
                bool ignoreBody = false) override;
    Task<> sendStream(const http::HttpResponse & resp,
                      const http::BodyWriterFn & bodyWriterFn) override;

private:
    std::weak_ptr<Http2Session> session_;
    uint32_t streamId_;
};

} // namespace nitrocoro::http2
