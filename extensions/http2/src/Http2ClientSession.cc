/**
 * @file Http2ClientSession.cc
 */
#include "Http2ClientSession.h"

#include "Http2BodyReader.h"
#include "Http2RequestSink.h"

#include <nitrocoro/http/HttpHeader.h>
#include <nitrocoro/http/Cookie.h>
#include <nitrocoro/http/stream/HttpIncomingStream.h>
#include <nitrocoro/utils/Debug.h>
#include <nitrocoro/utils/UrlEncode.h>

#include <algorithm>
#include <cstring>

namespace nitrocoro::http2
{

// ── Http2ClientSession ────────────────────────────────────────────────────────

Http2ClientSession::Http2ClientSession(io::StreamPtr stream, Scheduler * scheduler, std::string scheme)
    : reader_(std::move(stream))
    , scheduler_(scheduler)
    , scheme_(std::move(scheme))
    , readyPromise_(scheduler)
    , readyFuture_(readyPromise_.get_future().share())
{
}

Task<> Http2ClientSession::run()
{
    try
    {
        // 1. Send client connection preface
        co_await sendClientPreface();

        // 2. Send initial SETTINGS (empty = defaults)
        co_await reader_.writeFrame(FrameType::Settings, 0, 0, nullptr, 0);
    }
    catch (...)
    {
        readyPromise_.set_exception(std::current_exception());
        running_ = false;
        co_return;
    }

    readyPromise_.set_value();

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
            NITRO_ERROR("HTTP/2: expected CONTINUATION, got %d", static_cast<int>(frame.header.type));
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
            NITRO_ERROR("HTTP/2 client session error: %s", e.what());
            frameError = true;
        }
        if (frameError)
            break;
    }
    running_ = false;
}

Task<http::IncomingResponse> Http2ClientSession::request(http::ClientRequest req)
{
    co_await readyFuture_.get();

    uint32_t streamId = allocateStreamId();
    auto stream = std::make_shared<Http2ClientStream>(streamId, scheduler_);
    streams_[streamId] = stream;

    // Send request using HTTP/2 protocol
    co_await sendRequest(streamId, std::move(req));

    // Wait for response headers
    auto response = co_await stream->responseFuture.get();

    // Create body reader for response body
    auto bodyReader = std::make_shared<Http2BodyReader>(std::move(stream->bodyReceiver));

    co_return http::IncomingResponse(std::move(response), bodyReader);
}

Task<> Http2ClientSession::handleHeaders(const Frame & frame)
{
    uint32_t sid = frame.header.streamId;
    // Responses must arrive with odd stream IDs (client-initiated)
    if (sid == 0 || sid % 2 == 0)
    {
        NITRO_ERROR("HTTP/2: unexpected stream ID %u in HEADERS (server push not supported)", sid);
        co_return;
    }

    auto it = streams_.find(sid);
    if (it == streams_.end())
    {
        co_await sendRstStream(sid, ErrorCode::StreamClosed);
        co_return;
    }

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

Task<> Http2ClientSession::handleContinuation(const Frame & frame)
{
    if (frame.header.streamId != continuationStreamId_)
    {
        NITRO_ERROR("HTTP/2: CONTINUATION stream ID mismatch");
        co_return;
    }
    headerBlockBuf_.insert(headerBlockBuf_.end(),
                           frame.payload.begin(), frame.payload.end());
    if (frame.header.flags & FrameFlags::EndHeaders)
        co_await finaliseHeaders();
}

Task<> Http2ClientSession::finaliseHeaders()
{
    uint32_t sid = continuationStreamId_;
    continuationStreamId_ = 0;

    auto it = streams_.find(sid);
    if (it == streams_.end())
        co_return;

    auto & stream = it->second;

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
        stream->responsePromise.set_exception(std::make_exception_ptr(
            std::runtime_error("HPACK decode error")));
        streams_.erase(sid);
        co_return;
    }
    headerBlockBuf_.clear();

    stream->decodedHeaders = std::move(dh);
    stream->headersComplete = true;

    if (headersOnly_)
        stream->bodySender.reset(); // EOF immediately for headers-only responses

    // Build and resolve response
    auto response = buildResponse(stream->decodedHeaders);
    stream->responsePromise.set_value(std::move(response));
}

Task<> Http2ClientSession::handleData(const Frame & frame)
{
    uint32_t sid = frame.header.streamId;
    auto it = streams_.find(sid);
    if (it == streams_.end())
    {
        co_await sendRstStream(sid, ErrorCode::StreamClosed);
        co_return;
    }

    auto stream = it->second; // copy shared_ptr before any co_await
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

        // Send WINDOW_UPDATE (simple echo back)
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
    {
        stream->bodySender.reset();
        streams_.erase(sid);
    }
}

Task<> Http2ClientSession::handleSettings(const Frame & frame)
{
    if (frame.header.flags & FrameFlags::Ack)
    {
        settingsReceived_ = true;
        co_return;
    }
    // We accept all settings without applying them (flow control not implemented)
    co_await sendSettingsAck();
}

Task<> Http2ClientSession::handlePing(const Frame & frame)
{
    if (frame.header.flags & FrameFlags::Ack)
        co_return;
    if (frame.payload.size() != 8)
        co_return;
    auto lock = co_await writeMutex_.scoped_lock();
    co_await reader_.writeFrame(FrameType::Ping, FrameFlags::Ack, 0,
                                frame.payload.data(), 8);
}

Task<> Http2ClientSession::sendClientPreface()
{
    auto lock = co_await writeMutex_.scoped_lock();
    co_await reader_.stream()->write(kClientPreface.data(), kClientPreface.size());
}

Task<> Http2ClientSession::sendSettingsAck()
{
    auto lock = co_await writeMutex_.scoped_lock();
    co_await reader_.writeFrame(FrameType::Settings, FrameFlags::Ack, 0, nullptr, 0);
}

Task<> Http2ClientSession::sendHeaders(uint32_t streamId, const http::HttpRequest & req,
                                       bool endStream)
{
    std::vector<uint8_t> block;

    auto encodeLiteral = [&](std::string_view name, std::string_view value) {
        encoder_.encodeInt(block, 0, 4, 0x00);
        encoder_.encodeStr(block, name);
        encoder_.encodeStr(block, value);
    };

    encodeLiteral(":method", req.method.toString());
    encodeLiteral(":path", req.path.empty() ? "/" : req.path);
    encodeLiteral(":scheme", scheme_);
    for (const auto & [key, hdr] : req.headers)
    {
        if (hdr.nameCode() == http::HttpHeader::NameCode::Host)
        {
            encodeLiteral(":authority", hdr.value());
            continue;
        }
        // Skip HTTP/1.1-only headers
        if (hdr.nameCode() == http::HttpHeader::NameCode::Connection
            || hdr.nameCode() == http::HttpHeader::NameCode::TransferEncoding)
            continue;
        encodeLiteral(hdr.name(), hdr.value());
    }
    if (!req.cookies.empty())
    {
        std::string cookieHeader;
        for (const auto & [name, value] : req.cookies)
        {
            if (!cookieHeader.empty())
                cookieHeader += "; ";
            cookieHeader += name + "=" + value;
        }
        encodeLiteral("cookie", cookieHeader);
    }

    uint8_t flags = FrameFlags::EndHeaders | (endStream ? FrameFlags::EndStream : 0);
    auto lock = co_await writeMutex_.scoped_lock();
    co_await reader_.writeFrame(FrameType::Headers, flags, streamId,
                                block.data(), block.size());
}

Task<> Http2ClientSession::sendRequest(uint32_t streamId, http::ClientRequest req)
{
    Http2RequestSink sink(*this, streamId);
    co_await req.flush(sink);
}

Task<> Http2ClientSession::sendData(uint32_t streamId, std::string_view data, bool endStream)
{
    uint8_t flags = endStream ? FrameFlags::EndStream : 0;
    auto lock = co_await writeMutex_.scoped_lock();
    co_await reader_.writeFrame(FrameType::Data, flags, streamId,
                                data.data(), data.size());
}

Task<> Http2ClientSession::sendRstStream(uint32_t streamId, uint32_t errorCode)
{
    uint8_t payload[4];
    payload[0] = (errorCode >> 24) & 0xff;
    payload[1] = (errorCode >> 16) & 0xff;
    payload[2] = (errorCode >> 8) & 0xff;
    payload[3] = errorCode & 0xff;
    auto lock = co_await writeMutex_.scoped_lock();
    co_await reader_.writeFrame(FrameType::RstStream, 0, streamId, payload, 4);
}

uint32_t Http2ClientSession::allocateStreamId()
{
    uint32_t id = nextStreamId_;
    nextStreamId_ += 2; // Client uses odd stream IDs
    return id;
}

http::HttpResponse Http2ClientSession::buildResponse(const hpack::DecodedHeaders & dh)
{
    http::HttpResponse resp;
    resp.version = http::Version::kHttp11;

    if (!dh.status.empty())
    {
        int statusCode = std::stoi(dh.status);
        resp.statusCode = static_cast<uint16_t>(statusCode);
    }
    else
    {
        resp.statusCode = static_cast<uint16_t>(http::StatusCode::k200OK);
    }

    for (auto & [key, hdr] : dh.headers)
    {
        if (hdr.name() == "set-cookie")
            resp.cookies.push_back(http::Cookie::fromString(hdr.value()));
        else
            resp.headers.emplace(key, hdr);
    }

    return resp;
}

} // namespace nitrocoro::http2
