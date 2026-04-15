/**
 * @file Http1ResponseSink.h
 * @brief HTTP/1.1 ResponseSink implementation
 */
#pragma once
#include <nitrocoro/http/ResponseSink.h>

#include <nitrocoro/io/Stream.h>

namespace nitrocoro::http
{

class Http1ResponseSink : public ResponseSink
{
public:
    // TODO: take HttpServerConfig as argument
    explicit Http1ResponseSink(io::StreamPtr stream, bool isHeadMethod = false, bool sendDateHeader = true);

    Task<> write(const HttpResponse & resp, std::string_view body) override;
    Task<> write(const HttpResponse & resp, const BodyWriterFn & bodyWriterFn) override;

private:
    void buildHeaderBuf(std::string & buf, const HttpResponse & resp, TransferMode mode) const;

    io::StreamPtr stream_;
    bool isHeadMethod_;
    bool sendDateHeader_;
};

} // namespace nitrocoro::http
