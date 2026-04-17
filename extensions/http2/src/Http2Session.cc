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
        auto maybeFrame = co_await reader_.readFrame(maxFrameSize_);
        if (!maybeFrame)
            break;

        auto & frame = *maybeFrame;

        // Frame size check
        if (frame.header.length > maxFrameSize_)
        {
            co_await sendGoAway(lastStreamId_, ErrorCode::FrameSizeError);
            break;
        }

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
                    if (frame.payload.size() != 4)
                    {
                        co_await sendGoAway(lastStreamId_, ErrorCode::FrameSizeError);
                        frameError = true;
                    }
                    else if (frame.header.streamId != 0
                             && !streams_.contains(frame.header.streamId))
                    {
                        co_await sendGoAway(lastStreamId_, ErrorCode::ProtocolError);
                        frameError = true;
                    }
                    else if (frame.payload.size() == 4)
                    {
                        uint32_t inc = ((frame.payload[0] & 0x7f) << 24)
                                       | (frame.payload[1] << 16)
                                       | (frame.payload[2] << 8)
                                       | frame.payload[3];
                        if (inc == 0)
                        {
                            co_await sendGoAway(lastStreamId_, ErrorCode::ProtocolError);
                            frameError = true;
                        }
                    }
                    break;
                case FrameType::Ping:
                    co_await handlePing(frame);
                    break;
                case FrameType::RstStream:
                    if (frame.header.streamId == 0)
                    {
                        co_await sendGoAway(lastStreamId_, ErrorCode::ProtocolError);
                        frameError = true;
                    }
                    else if (frame.payload.size() != 4)
                    {
                        co_await sendGoAway(lastStreamId_, ErrorCode::FrameSizeError);
                        frameError = true;
                    }
                    else if (!streams_.contains(frame.header.streamId))
                    {
                        co_await sendGoAway(lastStreamId_, ErrorCode::ProtocolError);
                        frameError = true;
                    }
                    else
                    {
                        streams_.erase(frame.header.streamId);
                    }
                    break;
                case FrameType::GoAway:
                    goAwaySent_ = true;
                    break;
                case FrameType::Priority:
                    if (frame.header.streamId == 0)
                    {
                        co_await sendGoAway(lastStreamId_, ErrorCode::ProtocolError);
                        frameError = true;
                    }
                    else if (frame.payload.size() != 5)
                    {
                        co_await sendRstStream(frame.header.streamId, ErrorCode::FrameSizeError);
                    }
                    else
                    {
                        uint32_t dep = ((frame.payload[0] & 0x7f) << 24)
                                       | (frame.payload[1] << 16)
                                       | (frame.payload[2] << 8)
                                       | frame.payload[3];
                        if (dep == frame.header.streamId)
                        {
                            co_await sendRstStream(frame.header.streamId, ErrorCode::ProtocolError);
                        }
                    }
                    break;
                case FrameType::PushPromise:
                    co_await sendGoAway(lastStreamId_, ErrorCode::ProtocolError);
                    frameError = true;
                    break;
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
    if (sid % 2 == 0)
    {
        co_await sendGoAway(lastStreamId_, ErrorCode::ProtocolError);
        co_return;
    }
    if (sid <= lastStreamId_ && !streams_.contains(sid))
    {
        co_await sendGoAway(lastStreamId_, ErrorCode::StreamClosed);
        co_return;
    }
    if (streams_.contains(sid))
    {
        co_await sendRstStream(sid, ErrorCode::StreamClosed);
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
    {
        uint32_t dep = ((payload[offset] & 0x7f) << 24) | (payload[offset + 1] << 16)
                       | (payload[offset + 2] << 8) | payload[offset + 3];
        if (dep == sid)
        {
            co_await sendRstStream(sid, ErrorCode::ProtocolError);
            co_return;
        }
        offset += 5;
    }

    if (offset + padLen > payLen)
    {
        co_await sendGoAway(lastStreamId_, ErrorCode::ProtocolError);
        co_return;
    }
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

    // Check for connection-specific headers (RFC 7540 §8.1.2.2)
    for (const auto & [key, hdr] : dh.headers)
    {
        if (hdr.nameCode() == http::HttpHeader::NameCode::Connection
            || hdr.nameCode() == http::HttpHeader::NameCode::TransferEncoding
            || hdr.name() == "keep-alive" || hdr.name() == "proxy-connection"
            || hdr.name() == "upgrade")
        {
            co_await sendRstStream(sid, ErrorCode::ProtocolError);
            co_return;
        }
        if (hdr.name() == "te" && hdr.value() != "trailers")
        {
            co_await sendRstStream(sid, ErrorCode::ProtocolError);
            co_return;
        }
        // Uppercase header name check (RFC 7540 §8.1.2)
        for (char c : hdr.name())
        {
            if (c >= 'A' && c <= 'Z')
            {
                co_await sendRstStream(sid, ErrorCode::ProtocolError);
                co_return;
            }
        }
    }

    // Required pseudo-headers for requests (RFC 7540 §8.1.2.3)
    if (dh.method.empty() || dh.path.empty() || dh.scheme.empty())
    {
        co_await sendRstStream(sid, ErrorCode::ProtocolError);
        co_return;
    }

    NITRO_TRACE("creating Http2Stream sid=%u", sid);
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
    if (sid == 0)
    {
        co_await sendGoAway(lastStreamId_, ErrorCode::ProtocolError);
        co_return;
    }
    auto it = streams_.find(sid);
    if (it == streams_.end())
    {
        if (sid <= lastStreamId_)
            co_await sendRstStream(sid, ErrorCode::StreamClosed);
        else
            co_await sendGoAway(lastStreamId_, ErrorCode::ProtocolError);
        co_return;
    }

    auto & stream = it->second;
    const auto * payload = frame.payload.data();
    size_t payLen = frame.payload.size();
    size_t offset = 0;
    uint8_t padLen = 0;

    if (frame.header.flags & FrameFlags::Padded)
        padLen = payload[offset++];

    if (offset + padLen > payLen)
    {
        co_await sendGoAway(lastStreamId_, ErrorCode::ProtocolError);
        co_return;
    }
    size_t dataLen = payLen - offset - padLen;
    if (dataLen > 0)
    {
        if (!stream->bodySender)
        {
            co_await sendRstStream(sid, ErrorCode::StreamClosed);
            co_return;
        }
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
    if (frame.header.streamId != 0)
    {
        co_await sendGoAway(lastStreamId_, ErrorCode::ProtocolError);
        co_return;
    }
    if (frame.header.flags & FrameFlags::Ack)
    {
        if (!frame.payload.empty())
        {
            co_await sendGoAway(lastStreamId_, ErrorCode::FrameSizeError);
            co_return;
        }
        co_return;
    }
    if (frame.payload.size() % 6 != 0)
    {
        co_await sendGoAway(lastStreamId_, ErrorCode::FrameSizeError);
        co_return;
    }
    // Parse and validate settings
    for (size_t i = 0; i < frame.payload.size(); i += 6)
    {
        uint16_t id = (frame.payload[i] << 8) | frame.payload[i + 1];
        uint32_t val = (frame.payload[i + 2] << 24) | (frame.payload[i + 3] << 16)
                       | (frame.payload[i + 4] << 8) | frame.payload[i + 5];
        if (id == 0x2 && val > 1)
        {
            co_await sendGoAway(lastStreamId_, ErrorCode::ProtocolError);
            co_return;
        }
        if (id == 0x4 && val > 0x7fffffff)
        {
            co_await sendGoAway(lastStreamId_, ErrorCode::FlowControlError);
            co_return;
        }
        if (id == 0x5 && (val < 16384 || val > 16777215))
        {
            co_await sendGoAway(lastStreamId_, ErrorCode::ProtocolError);
            co_return;
        }
        if (id == SettingsParam::MaxFrameSize)
            peerMaxFrameSize_ = val;
    }
    co_await sendSettingsAck();
}

Task<> Http2Session::handlePing(const Frame & frame)
{
    if (frame.header.streamId != 0)
    {
        co_await sendGoAway(lastStreamId_, ErrorCode::ProtocolError);
        co_return;
    }
    if (frame.payload.size() != 8)
    {
        co_await sendGoAway(lastStreamId_, ErrorCode::FrameSizeError);
        co_return;
    }
    if (frame.header.flags & FrameFlags::Ack)
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
    for (auto & [key, hdr] : dh.headers)
    {
        if (hdr.nameCode() == http::HttpHeader::NameCode::Cookie)
        {
            std::string_view cookieStr = hdr.value();
            size_t start = 0;
            while (start < cookieStr.size())
            {
                size_t semi = cookieStr.find(';', start);
                size_t end = (semi == std::string_view::npos) ? cookieStr.size() : semi;
                std::string_view pair = cookieStr.substr(start, end - start);
                size_t eq = pair.find('=');
                if (eq != std::string_view::npos)
                {
                    auto trim = [](std::string_view s) {
                        size_t l = s.find_first_not_of(' ');
                        size_t r = s.find_last_not_of(' ');
                        return (l == std::string_view::npos) ? std::string_view{} : s.substr(l, r - l + 1);
                    };
                    auto name = trim(pair.substr(0, eq));
                    auto value = trim(pair.substr(eq + 1));
                    if (!name.empty())
                        req.cookies[std::string(name)] = std::string(value);
                }
                if (semi == std::string_view::npos)
                    break;
                start = semi + 1;
            }
        }
        else
        {
            req.headers.insert_or_assign(key, hdr);
        }
    }
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
