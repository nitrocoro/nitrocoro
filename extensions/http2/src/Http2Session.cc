/**
 * @file Http2Session.cc
 */
#include "Http2Session.h"

#include "Http2BodyReader.h"
#include "Http2ResponseSink.h"

#include <nitrocoro/http/HttpHeader.h>
#include <nitrocoro/http/stream/HttpIncomingStream.h>
#include <nitrocoro/http/stream/HttpOutgoingMessage.h>
#include <nitrocoro/utils/Debug.h>
#include <nitrocoro/utils/UrlEncode.h>

#include <algorithm>
#include <cstring>

namespace nitrocoro::http2
{

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
                case FrameType::Headers:
                    co_await handleHeaders(frame);
                    break;
                case FrameType::Continuation:
                    co_await handleContinuation(frame);
                    break;
                case FrameType::Data:
                    co_await handleData(frame);
                    break;
                case FrameType::Settings:
                    co_await handleSettings(frame);
                    break;
                case FrameType::WindowUpdate:
                    break; // flow control not implemented
                case FrameType::Ping:
                    co_await handlePing(frame);
                    break;
                case FrameType::RstStream:
                    streams_.erase(frame.header.streamId);
                    break;
                case FrameType::GoAway:
                    goAwaySent_ = true;
                    break;
                case FrameType::Priority:
                    break; // ignored
                default:
                    break;
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
    size_t payLen = frame.payload.size();
    size_t offset = 0;

    uint8_t padLen = 0;
    if (frame.header.flags & FrameFlags::Padded)
        padLen = payload[offset++];
    if (frame.header.flags & FrameFlags::Priority)
        offset += 5;

    size_t blockLen = payLen - offset - padLen;
    headerBlockBuf_.assign(payload + offset, payload + offset + blockLen);
    continuationStreamId_ = sid;
    headersOnly_ = (frame.header.flags & FrameFlags::EndStream) != 0;

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
    streams_[sid] = stream;

    if (headersOnly_)
        stream->bodySender.reset(); // EOF immediately for headers-only requests

    scheduler_->spawn([weakThis = weak_from_this(), stream = std::move(stream)]() -> Task<> {
        auto thisPtr = weakThis.lock();
        if (!thisPtr)
            co_return;

        try
        {
            co_await thisPtr->dispatchStream(stream);
        }
        catch (const std::exception & ex)
        {
            NITRO_ERROR("Http2Session::dispatchStream error: %s", ex.what());
        }
        thisPtr->streams_.erase(stream->streamId);
    });
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
    size_t payLen = frame.payload.size();
    size_t offset = 0;
    uint8_t padLen = 0;

    if (frame.header.flags & FrameFlags::Padded)
        padLen = payload[offset++];

    size_t dataLen = payLen - offset - padLen;
    if (dataLen > 0)
    {
        stream->bodySender->send(
            std::string(reinterpret_cast<const char *>(payload + offset), dataLen));

        uint8_t wu[4];
        uint32_t inc = static_cast<uint32_t>(dataLen);
        wu[0] = (inc >> 24) & 0x7f;
        wu[1] = (inc >> 16) & 0xff;
        wu[2] = (inc >> 8) & 0xff;
        wu[3] = inc & 0xff;
        auto lock = co_await writeMutex_.scoped_lock();
        co_await reader_.writeFrame(FrameType::WindowUpdate, 0, 0, wu, 4);
        co_await reader_.writeFrame(FrameType::WindowUpdate, 0, sid, wu, 4);
    }

    if (frame.header.flags & FrameFlags::EndStream)
        stream->bodySender.reset(); // RAII close
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

Task<> Http2Session::dispatchStream(std::shared_ptr<Http2Stream> h2stream)
{
    uint32_t sid = h2stream->streamId;

    http::HttpRequest req = buildRequest(h2stream->decodedHeaders);
    auto result = router_->route(req.method, req.path);

    auto bodyReader = std::make_shared<Http2BodyReader>(std::move(h2stream->bodyReceiver));
    auto request = std::make_shared<http::IncomingRequest>(std::move(req), bodyReader);
    request->pathParams() = std::move(result.params);

    Http2ResponseSink sink(weak_from_this(), sid, req.method == http::methods::Head);
    auto response = std::make_shared<http::ServerResponse>();

    if (!result.handler)
    {
        if (result.reason == http::HttpRouter::RouteResult::Reason::MethodNotAllowed)
            response->setStatus(http::StatusCode::k405MethodNotAllowed);
        else
            response->setStatus(http::StatusCode::k404NotFound);
        co_await response->flush(sink);
        co_return;
    }

    bool handlerError = false;
    try
    {
        co_await result.handler->invoke(request, response);
        co_await response->flush(sink);
    }
    catch (const std::exception & e)
    {
        NITRO_ERROR("HTTP/2 handler error: %s", e.what());
        handlerError = true;
    }

    if (handlerError)
        co_await sendRstStream(sid, ErrorCode::InternalError);
}

http::HttpRequest Http2Session::buildRequest(const hpack::DecodedHeaders & dh)
{
    http::HttpRequest req;
    req.version = http::Version::kHttp11;
    req.method = http::HttpMethod::fromString(dh.method);

    size_t qPos = dh.path.find('?');
    std::string rawPath = dh.path.substr(0, qPos);
    req.rawPath = rawPath;
    req.path = utils::urlDecode(rawPath);
    if (qPos != std::string::npos)
    {
        req.query = dh.path.substr(qPos + 1);
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
            if (amp == std::string_view::npos)
                break;
            start = amp + 1;
        }
    }

    if (!dh.authority.empty())
    {
        http::HttpHeader h(http::HttpHeader::NameCode::Host, dh.authority);
        req.headers.insert_or_assign(h.name(), std::move(h));
    }
    req.headers.insert(dh.headers.begin(), dh.headers.end());
    return req;
}

Task<> Http2Session::sendHeaders(uint32_t streamId, const http::HttpResponse & resp,
                                 bool endStream)
{
    auto block = encoder_.encodeResponse(resp);
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
    payload[2] = (lastStreamId >> 8) & 0xff;
    payload[3] = lastStreamId & 0xff;
    payload[4] = (errorCode >> 24) & 0xff;
    payload[5] = (errorCode >> 16) & 0xff;
    payload[6] = (errorCode >> 8) & 0xff;
    payload[7] = errorCode & 0xff;
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
    payload[2] = (errorCode >> 8) & 0xff;
    payload[3] = errorCode & 0xff;
    auto lock = co_await writeMutex_.scoped_lock();
    co_await reader_.writeFrame(FrameType::RstStream, 0, streamId, payload, 4);
}

} // namespace nitrocoro::http2
