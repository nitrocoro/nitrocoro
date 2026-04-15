/**
 * @file Http1RequestSink.cc
 * @brief HTTP/1.1 RequestSink implementation
 */
#include "Http1RequestSink.h"

#include "Http1BodyWriter.h"
#include <nitrocoro/http/HttpHeader.h>
#include <nitrocoro/http/HttpUtils.h>

namespace nitrocoro::http
{

Http1RequestSink::Http1RequestSink(io::StreamPtr stream)
    : stream_(std::move(stream))
{
}

Task<> Http1RequestSink::write(const HttpRequest & req, std::string_view body)
{
    std::string buf;
    buf.reserve(256 + req.headers.size() * 64 + body.size());
    buildHeaderBuf(buf, req, TransferMode::ContentLength);
    buf.append("\r\n");
    buf.append(body);
    co_await stream_->write(buf.data(), buf.size());
}

Task<> Http1RequestSink::write(const HttpRequest & req, const BodyWriterFn & bodyWriterFn)
{
    TransferMode mode = (req.version == Version::kHttp10) ? TransferMode::UntilClose : TransferMode::Chunked;

    std::string buf;
    buf.reserve(256 + req.headers.size() * 64);
    buildHeaderBuf(buf, req, mode);
    buf.append("\r\n");
    co_await stream_->write(buf.data(), buf.size());

    auto bodyWriter = Http1BodyWriter::create(mode, stream_);
    co_await bodyWriterFn(*bodyWriter);
    co_await bodyWriter->end();
}

void Http1RequestSink::buildHeaderBuf(std::string & buf, const HttpRequest & req, TransferMode mode) const
{
    buf.append(req.method.toString())
        .append(" ")
        .append(req.path)
        .append(" ")
        .append(versionToString(req.version))
        .append("\r\n");

    for (const auto & [name, header] : req.headers)
    {
        if (header.nameCode() == HttpHeader::NameCode::ContentLength
            || header.nameCode() == HttpHeader::NameCode::TransferEncoding)
            continue;

        if (header.nameCode() != HttpHeader::NameCode::Unknown)
            buf.append(HttpHeader::codeToCanonicalName(header.nameCode()));
        else
            buf.append(HttpHeader::toCanonical(header.name()));
        buf.append(": ").append(header.value()).append("\r\n");
    }

    if (mode == TransferMode::ContentLength)
        buf.append(HttpHeader::Name::ContentLength_C)
            .append(": ")
            .append(std::to_string(req.contentLength))
            .append("\r\n");

    if (mode == TransferMode::Chunked && !req.headers.contains(HttpHeader::Name::TransferEncoding_L))
        buf.append(HttpHeader::Name::TransferEncoding_C).append(": chunked\r\n");

    if (!req.cookies.empty())
    {
        buf.append("Cookie: ");
        bool first = true;
        for (const auto & [name, value] : req.cookies)
        {
            if (!first)
                buf.append("; ");
            buf.append(name).append("=").append(value);
            first = false;
        }
        buf.append("\r\n");
    }

    bool needClose = !req.keepAlive && req.version != Version::kHttp10;
    bool needKeepAlive = req.keepAlive && req.version == Version::kHttp10;
    if ((needClose || needKeepAlive) && !req.headers.contains(HttpHeader::Name::Connection_L))
        buf.append(needClose ? "Connection: close\r\n" : "Connection: keep-alive\r\n");
}

} // namespace nitrocoro::http
