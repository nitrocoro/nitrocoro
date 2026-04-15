/**
 * @file Http1RequestSink.h
 * @brief HTTP/1.1 RequestSink implementation
 */
#pragma once
#include <nitrocoro/http/BodyWriter.h>
#include <nitrocoro/http/RequestSink.h>

#include <nitrocoro/io/Stream.h>

namespace nitrocoro::http
{

class Http1RequestSink : public RequestSink
{
public:
    explicit Http1RequestSink(io::StreamPtr stream);

    Task<> write(const HttpRequest & req, std::string_view body) override;
    Task<> write(const HttpRequest & req, const BodyWriterFn & bodyWriterFn) override;

private:
    void buildHeaderBuf(std::string & buf, const HttpRequest & req, TransferMode mode) const;

    io::StreamPtr stream_;
};

} // namespace nitrocoro::http
