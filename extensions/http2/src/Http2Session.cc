/**
 * @file Http2Session.cc
 */
#include "Http2Session.h"

#include <nitrocoro/http/HttpHeader.h>
#include <nitrocoro/http/stream/HttpIncomingStream.h>
#include <nitrocoro/http/stream/HttpOutgoingMessage.h>
#include <nitrocoro/utils/Debug.h>
#include <nitrocoro/utils/UrlEncode.h>

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace nitrocoro::http2
{

// ── StringBodyReader: BodyReader backed by a pre-buffered string ──────────────

class StringBodyReader : public http::BodyReader
{
public:
    explicit StringBodyReader(std::string body) : body_(std::move(body)) {}
    bool isComplete() const override { return pos_ >= body_.size(); }

protected:
    Task<size_t> readImpl(char * buf, size_t len) override
    {
        size_t n = std::min(len, body_.size() - pos_);
        std::memcpy(buf, body_.data() + pos_, n);
        pos_ += n;
        co_return n;
    }

private:
    std::string body_;
    size_t pos_{ 0 };
};

// ── Http2WriteProxy: io::Stream that buffers writes, then sends H2 frames ─────
//
// HttpOutgoingStream<HttpResponse> serialises HTTP/1.1 text into this stream.
// After the handler finishes, we parse the buffered bytes and send proper
// HEADERS + DATA frames.  This lets us reuse the existing HttpOutgoingStream
// without modification.

class Http2WriteProxy
{
public:
    Http2WriteProxy(std::weak_ptr<Http2Session> session, uint32_t streamId)
        : session_(std::move(session)), streamId_(streamId) {}

    Task<size_t> read(void *, size_t) { co_return 0; }

    Task<size_t> write(const void * buf, size_t len)
    {
        buf_.append(static_cast<const char *>(buf), len);
        co_return len;
    }

    Task<> shutdown() { co_return; }

    // Parse the buffered HTTP/1.1 response and send H2 frames.
    Task<> flush()
    {
        auto s = session_.lock();
        if (!s || buf_.empty())
            co_return;

        // Find end of headers (\r\n\r\n)
        size_t headerEnd = buf_.find("\r\n\r\n");
        if (headerEnd == std::string::npos)
        {
            co_await s->sendRstStream(streamId_, ErrorCode::InternalError);
            co_return;
        }

        std::string_view headerSection(buf_.data(), headerEnd);
        std::string_view body(buf_.data() + headerEnd + 4,
                              buf_.size() - headerEnd - 4);

        // Parse status line
        size_t sp1 = headerSection.find(' ');
        size_t sp2 = headerSection.find(' ', sp1 + 1);
        size_t nl  = headerSection.find("\r\n");
        int statusInt = std::stoi(std::string(headerSection.substr(sp1 + 1, sp2 - sp1 - 1)));
        auto statusCode = static_cast<http::StatusCode>(statusInt);

        // Parse headers
        http::HttpHeaderMap headers;
        size_t pos = nl + 2;
        while (pos < headerSection.size())
        {
            size_t end = headerSection.find("\r\n", pos);
            if (end == std::string_view::npos)
                end = headerSection.size();
            std::string_view line = headerSection.substr(pos, end - pos);
            size_t colon = line.find(':');
            if (colon != std::string_view::npos)
            {
                std::string name(line.substr(0, colon));
                size_t vs = line.find_first_not_of(' ', colon + 1);
                std::string value = vs != std::string_view::npos
                                        ? std::string(line.substr(vs))
                                        : "";
                // Skip HTTP/1.1-specific headers that have no meaning in H2
                std::string lname = http::HttpHeader::toLower(name);
                if (lname == "transfer-encoding" || lname == "connection" ||
                    lname == "keep-alive" || lname == "upgrade")
                {
                    pos = end + 2;
                    continue;
                }
                http::HttpHeader hdr(name, std::move(value));
                headers.insert_or_assign(hdr.name(), std::move(hdr));
            }
            if (end == headerSection.size())
                break;
            pos = end + 2;
        }

        co_await s->sendHeaders(streamId_, statusCode, headers, body.empty());
        if (!body.empty())
            co_await s->sendData(streamId_, body, true);
    }

private:
    std::weak_ptr<Http2Session> session_;
    uint32_t streamId_;
    std::string buf_;
};

// ── Http2Session ──────────────────────────────────────────────────────────────

Http2Session::Http2Session(io::StreamPtr stream,
                            std::shared_ptr<http::HttpRouter> router,
                            Scheduler * scheduler)
    : reader_(std::move(stream))
    , router_(std::move(router))
    , scheduler_(scheduler)
{
}

Task<> Http2Session::run()
{
    // 1. Read and validate client connection preface (24 bytes)
    {
        char preface[24];
        size_t got = 0;
        while (got < 24)
        {
            size_t n = co_await reader_.stream()->read(preface + got, 24 - got);
            if (n == 0)
                co_return;
            got += n;
        }
        if (std::string_view(preface, 24) != kClientPreface)
        {
            NITRO_ERROR("HTTP/2: invalid client preface");
            co_return;
        }
    }

    // 2. Send server SETTINGS (empty = defaults)
    co_await reader_.writeFrame(FrameType::Settings, 0, 0, nullptr, 0);

    // 3. Frame read loop
    while (!goAwaySent_)
    {
        auto maybeFrame = co_await reader_.readFrame();
        if (!maybeFrame)
            break;

        auto & frame = *maybeFrame;

        // While collecting CONTINUATION, only CONTINUATION is allowed
        if (continuationStreamId_ != 0 && frame.header.type != FrameType::Continuation)
        {
            co_await sendGoAway(lastStreamId_, ErrorCode::ProtocolError);
            break;
        }

        bool frameError = false;
        try
        {
            switch (frame.header.type)
            {
                case FrameType::Headers:      co_await handleHeaders(frame);      break;
                case FrameType::Continuation: co_await handleContinuation(frame); break;
                case FrameType::Data:         co_await handleData(frame);         break;
                case FrameType::Settings:     co_await handleSettings(frame);     break;
                case FrameType::WindowUpdate: break; // flow control not implemented
                case FrameType::Ping:         co_await handlePing(frame);         break;
                case FrameType::RstStream:    streams_.erase(frame.header.streamId); break;
                case FrameType::GoAway:       goAwaySent_ = true;                break;
                case FrameType::Priority:     break; // ignored
                default:                      break;
            }
        }
        catch (const std::exception & e)
        {
            NITRO_ERROR("HTTP/2 session error: %s", e.what());
            frameError = true;
        }
        if (frameError)
        {
            co_await sendGoAway(lastStreamId_, ErrorCode::InternalError);
            break;
        }
    }
}

Task<> Http2Session::handleHeaders(const Frame & frame)
{
    uint32_t sid = frame.header.streamId;
    if (sid == 0)
    {
        co_await sendGoAway(lastStreamId_, ErrorCode::ProtocolError);
        co_return;
    }
    lastStreamId_ = std::max(lastStreamId_, sid);

    const auto * payload = frame.payload.data();
    size_t       payLen  = frame.payload.size();
    size_t       offset  = 0;

    uint8_t padLen = 0;
    if (frame.header.flags & FrameFlags::Padded)
        padLen = payload[offset++];
    if (frame.header.flags & FrameFlags::Priority)
        offset += 5;

    size_t blockLen = payLen - offset - padLen;
    headerBlockBuf_.assign(payload + offset, payload + offset + blockLen);
    continuationStreamId_ = sid;
    pendingEndStream_ = (frame.header.flags & FrameFlags::EndStream) != 0;

    if (frame.header.flags & FrameFlags::EndHeaders)
        co_await finaliseHeaders();
}

Task<> Http2Session::handleContinuation(const Frame & frame)
{
    if (frame.header.streamId != continuationStreamId_)
    {
        co_await sendGoAway(lastStreamId_, ErrorCode::ProtocolError);
        co_return;
    }
    headerBlockBuf_.insert(headerBlockBuf_.end(),
                           frame.payload.begin(), frame.payload.end());
    if (frame.header.flags & FrameFlags::EndHeaders)
        co_await finaliseHeaders();
}

Task<> Http2Session::finaliseHeaders()
{
    uint32_t sid = continuationStreamId_;
    continuationStreamId_ = 0;

    hpack::DecodedHeaders dh;
    bool decodeError = false;
    try
    {
        dh = decoder_.decode(headerBlockBuf_.data(), headerBlockBuf_.size());
    }
    catch (const std::exception & e)
    {
        NITRO_ERROR("HPACK decode error: %s", e.what());
        decodeError = true;
    }
    if (decodeError)
    {
        co_await sendGoAway(lastStreamId_, ErrorCode::CompressionError);
        co_return;
    }
    headerBlockBuf_.clear();

    auto stream = std::make_shared<Http2Stream>(sid, scheduler_);
    stream->decodedHeaders = std::move(dh);
    stream->headersComplete = true;
    stream->endStream = pendingEndStream_;
    streams_[sid] = stream;

    if (pendingEndStream_)
    {
        stream->requestReady.set_value();
        dispatchStream(stream);
    }
}

Task<> Http2Session::handleData(const Frame & frame)
{
    uint32_t sid = frame.header.streamId;
    auto it = streams_.find(sid);
    if (it == streams_.end())
    {
        co_await sendRstStream(sid, ErrorCode::StreamClosed);
        co_return;
    }

    auto & stream = it->second;
    const auto * payload = frame.payload.data();
    size_t       payLen  = frame.payload.size();
    size_t       offset  = 0;
    uint8_t      padLen  = 0;

    if (frame.header.flags & FrameFlags::Padded)
        padLen = payload[offset++];

    size_t dataLen = payLen - offset - padLen;
    stream->body.append(reinterpret_cast<const char *>(payload + offset), dataLen);

    // Send WINDOW_UPDATE to keep windows open
    if (dataLen > 0)
    {
        uint8_t wu[4];
        uint32_t inc = static_cast<uint32_t>(dataLen);
        wu[0] = (inc >> 24) & 0x7f;
        wu[1] = (inc >> 16) & 0xff;
        wu[2] = (inc >> 8)  & 0xff;
        wu[3] =  inc        & 0xff;
        auto lock = co_await writeMutex_.scoped_lock();
        co_await reader_.writeFrame(FrameType::WindowUpdate, 0, 0,   wu, 4);
        co_await reader_.writeFrame(FrameType::WindowUpdate, 0, sid, wu, 4);
    }

    if (frame.header.flags & FrameFlags::EndStream)
    {
        stream->endStream = true;
        stream->requestReady.set_value();
        dispatchStream(stream);
    }
}

Task<> Http2Session::handleSettings(const Frame & frame)
{
    if (frame.header.flags & FrameFlags::Ack)
        co_return;
    // We accept all settings without applying them (flow control not implemented)
    co_await sendSettingsAck();
}

Task<> Http2Session::handlePing(const Frame & frame)
{
    if (frame.header.flags & FrameFlags::Ack)
        co_return;
    if (frame.payload.size() != 8)
        co_return;
    auto lock = co_await writeMutex_.scoped_lock();
    co_await reader_.writeFrame(FrameType::Ping, FrameFlags::Ack, 0,
                                frame.payload.data(), 8);
}

void Http2Session::dispatchStream(std::shared_ptr<Http2Stream> h2stream)
{
    auto self = shared_from_this();
    scheduler_->spawn([self, h2stream]() -> Task<> {
        uint32_t sid = h2stream->streamId;

        http::HttpRequest req = self->buildRequest(h2stream->decodedHeaders, h2stream->body);
        auto result = self->router_->route(req.method, req.path);

        auto bodyReader = std::make_shared<StringBodyReader>(h2stream->body);
        auto request = std::make_shared<http::IncomingRequest>(std::move(req), bodyReader);
        request->pathParams() = std::move(result.params);

        // Create a proxy stream that captures HTTP/1.1 serialised output
        auto proxy = std::make_shared<Http2WriteProxy>(self, sid);
        auto proxyStream = std::make_shared<io::Stream>(proxy);
        auto response = std::make_shared<http::ServerResponse>();

        if (!result.handler)
        {
            if (result.reason == http::HttpRouter::RouteResult::Reason::MethodNotAllowed)
                response->setStatus(http::StatusCode::k405MethodNotAllowed);
            else
                response->setStatus(http::StatusCode::k404NotFound);
            co_await response->flush(proxyStream);
            co_await proxy->flush();
            self->streams_.erase(sid);
            co_return;
        }

        bool handlerError = false;
        try
        {
            co_await result.handler->invoke(request, response);
            co_await response->flush(proxyStream);
            co_await proxy->flush();
        }
        catch (const std::exception & e)
        {
            NITRO_ERROR("HTTP/2 handler error: %s", e.what());
            handlerError = true;
        }

        if (handlerError)
            co_await self->sendRstStream(sid, ErrorCode::InternalError);

        self->streams_.erase(sid);
    });
}

http::HttpRequest Http2Session::buildRequest(const hpack::DecodedHeaders & dh,
                                              const std::string & body)
{
    http::HttpRequest req;
    req.version = http::Version::kHttp11;
    req.method  = http::HttpMethod::fromString(dh.method);

    size_t qpos = dh.path.find('?');
    std::string rawPath = dh.path.substr(0, qpos);
    req.rawPath = rawPath;
    req.path    = utils::urlDecode(rawPath);
    if (qpos != std::string::npos)
    {
        req.query = dh.path.substr(qpos + 1);
        // Parse query params
        std::string_view qs(req.query);
        size_t start = 0;
        while (start < qs.size())
        {
            size_t amp = qs.find('&', start);
            size_t end = (amp == std::string_view::npos) ? qs.size() : amp;
            std::string_view pair = qs.substr(start, end - start);
            size_t eq = pair.find('=');
            if (eq != std::string_view::npos)
                req.queries.emplace(utils::urlDecodeComponent(pair.substr(0, eq)),
                                    utils::urlDecodeComponent(pair.substr(eq + 1)));
            if (amp == std::string_view::npos) break;
            start = amp + 1;
        }
    }

    if (!dh.authority.empty())
    {
        http::HttpHeader h(http::HttpHeader::NameCode::Host, dh.authority);
        req.headers.insert_or_assign(h.name(), std::move(h));
    }
    req.headers.insert(dh.headers.begin(), dh.headers.end());

    req.transferMode  = http::TransferMode::ContentLength;
    req.contentLength = body.size();
    req.keepAlive     = false;
    return req;
}

Task<> Http2Session::sendHeaders(uint32_t streamId, http::StatusCode status,
                                  const http::HttpHeaderMap & headers, bool endStream)
{
    auto block = encoder_.encodeResponse(status, headers);
    uint8_t flags = FrameFlags::EndHeaders | (endStream ? FrameFlags::EndStream : 0);
    auto lock = co_await writeMutex_.scoped_lock();
    co_await reader_.writeFrame(FrameType::Headers, flags, streamId,
                                block.data(), block.size());
}

Task<> Http2Session::sendData(uint32_t streamId, std::string_view data, bool endStream)
{
    uint8_t flags = endStream ? FrameFlags::EndStream : 0;
    auto lock = co_await writeMutex_.scoped_lock();
    co_await reader_.writeFrame(FrameType::Data, flags, streamId,
                                data.data(), data.size());
}

Task<> Http2Session::sendGoAway(uint32_t lastStreamId, uint32_t errorCode)
{
    goAwaySent_ = true;
    uint8_t payload[8];
    payload[0] = (lastStreamId >> 24) & 0x7f;
    payload[1] = (lastStreamId >> 16) & 0xff;
    payload[2] = (lastStreamId >> 8)  & 0xff;
    payload[3] =  lastStreamId        & 0xff;
    payload[4] = (errorCode >> 24) & 0xff;
    payload[5] = (errorCode >> 16) & 0xff;
    payload[6] = (errorCode >> 8)  & 0xff;
    payload[7] =  errorCode        & 0xff;
    auto lock = co_await writeMutex_.scoped_lock();
    co_await reader_.writeFrame(FrameType::GoAway, 0, 0, payload, 8);
}

Task<> Http2Session::sendSettingsAck()
{
    auto lock = co_await writeMutex_.scoped_lock();
    co_await reader_.writeFrame(FrameType::Settings, FrameFlags::Ack, 0, nullptr, 0);
}

Task<> Http2Session::sendRstStream(uint32_t streamId, uint32_t errorCode)
{
    uint8_t payload[4];
    payload[0] = (errorCode >> 24) & 0xff;
    payload[1] = (errorCode >> 16) & 0xff;
    payload[2] = (errorCode >> 8)  & 0xff;
    payload[3] =  errorCode        & 0xff;
    auto lock = co_await writeMutex_.scoped_lock();
    co_await reader_.writeFrame(FrameType::RstStream, 0, streamId, payload, 4);
}

} // namespace nitrocoro::http2
