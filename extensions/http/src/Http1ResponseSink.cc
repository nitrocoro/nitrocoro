/**
 * @file Http1ResponseSink.cc
 * @brief HTTP/1.1 ResponseSink implementation
 */
#include "Http1ResponseSink.h"

#include "Http1BodyWriter.h"
#include <nitrocoro/http/HttpHeader.h>
#include <nitrocoro/http/HttpUtils.h>
#include <nitrocoro/http/stream/HttpOutgoingMessage.h>

#include <ctime>

namespace nitrocoro::http
{

Http1ResponseSink::Http1ResponseSink(io::StreamPtr stream, bool isHeadMethod, bool sendDateHeader)
    : stream_(std::move(stream))
    , isHeadMethod_(isHeadMethod)
    , sendDateHeader_(sendDateHeader)
{
}

Task<> Http1ResponseSink::write(const HttpResponse & resp, std::string_view body)
{
    std::string buf;
    buf.reserve(256 + resp.headers.size() * 64 + body.size());
    buildHeaderBuf(buf, resp, TransferMode::ContentLength);
    buf.append("\r\n");
    if (!isHeadMethod_)
        buf.append(body);
    co_await stream_->write(buf.data(), buf.size());
}

Task<> Http1ResponseSink::write(const HttpResponse & resp, const BodyWriterFn & bodyWriterFn)
{
    TransferMode mode;
    if (resp.version == Version::kHttp10)
        mode = TransferMode::UntilClose;
    else
        mode = TransferMode::Chunked;

    std::string buf;
    buf.reserve(256 + resp.headers.size() * 64);
    buildHeaderBuf(buf, resp, mode);
    buf.append("\r\n");
    co_await stream_->write(buf.data(), buf.size());

    if (isHeadMethod_)
        co_return;

    auto bodyWriter = Http1BodyWriter::create(mode, stream_);
    co_await bodyWriterFn(*bodyWriter);
    co_await bodyWriter->end();
}

void Http1ResponseSink::buildHeaderBuf(std::string & buf, const HttpResponse & resp, TransferMode mode) const
{
    buf.append(versionToString(resp.version))
        .append(" ")
        .append(std::to_string(resp.statusCode))
        .append(" ")
        .append(resp.statusReason.empty()
                    ? statusCodeToString(resp.statusCode)
                    : resp.statusReason)
        .append("\r\n");

    for (const auto & [name, header] : resp.headers)
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
            .append(std::to_string(resp.contentLength))
            .append("\r\n");

    if (mode == TransferMode::Chunked && !resp.headers.contains(HttpHeader::Name::TransferEncoding_L))
        buf.append(HttpHeader::Name::TransferEncoding_C).append(": chunked\r\n");

    for (const auto & cookie : resp.cookies)
        buf.append("Set-Cookie: ").append(cookie.toString()).append("\r\n");

    if (sendDateHeader_ && !resp.headers.contains(HttpHeader::Name::Date_L))
    {
        char dateBuf[32];
        std::time_t now = std::time(nullptr);
        std::tm tm{};
#ifdef _WIN32
        gmtime_s(&tm, &now);
#else
        gmtime_r(&now, &tm);
#endif
        std::strftime(dateBuf, sizeof(dateBuf), "%a, %d %b %Y %H:%M:%S GMT", &tm);
        buf.append("Date: ").append(dateBuf).append("\r\n");
    }

    bool needClose = (resp.shouldClose || mode == TransferMode::UntilClose) && resp.version != Version::kHttp10;
    bool needKeepAlive = !resp.shouldClose && mode != TransferMode::UntilClose && resp.version == Version::kHttp10;
    if ((needClose || needKeepAlive) && !resp.headers.contains(HttpHeader::Name::Connection_L))
        buf.append(needClose ? "Connection: close\r\n" : "Connection: keep-alive\r\n");
}

} // namespace nitrocoro::http
