/**
 * @file Http2Session.h
 * @brief HTTP/2 connection session: reads frames, dispatches streams
 */
#pragma once

#include "Http2Stream.h"
#include "frame/FrameReader.h"
#include "hpack/Hpack.h"
#include <nitrocoro/core/Mutex.h>
#include <nitrocoro/core/Task.h>
#include <nitrocoro/http/HttpMessage.h>
#include <nitrocoro/http/HttpRouter.h>
#include <nitrocoro/io/Stream.h>

#include <memory>
#include <unordered_map>

namespace nitrocoro::http2
{

class Http2Session : public std::enable_shared_from_this<Http2Session>
{
public:
    Http2Session(io::StreamPtr stream, std::shared_ptr<http::HttpRouter> router,
                 Scheduler * scheduler);

    Task<> run();

    Task<> sendHeaders(uint32_t streamId, const http::HttpResponse & resp, bool endStream);
    Task<> sendData(uint32_t streamId, std::string_view data, bool endStream);
    Task<> sendRstStream(uint32_t streamId, uint32_t errorCode);

private:
    Task<> handleHeaders(const Frame & frame);
    Task<> handleContinuation(const Frame & frame);
    Task<> handleData(const Frame & frame);
    Task<> handleSettings(const Frame & frame);
    Task<> handlePing(const Frame & frame);
    Task<> finaliseHeaders();

    Task<> dispatchStream(std::shared_ptr<Http2Stream> h2stream);

    Task<> sendGoAway(uint32_t lastStreamId, uint32_t errorCode);
    Task<> sendSettingsAck();

    http::HttpRequest buildRequest(const hpack::DecodedHeaders & dh);

    FrameReader reader_;
    hpack::HpackDecoder decoder_;
    hpack::HpackEncoder encoder_;
    Mutex writeMutex_;

    std::shared_ptr<http::HttpRouter> router_;
    Scheduler * scheduler_;

    std::unordered_map<uint32_t, std::shared_ptr<Http2Stream>> streams_;

    uint32_t continuationStreamId_{ 0 };
    bool headersOnly_{ false };
    std::vector<uint8_t> headerBlockBuf_;

    uint32_t lastStreamId_{ 0 };
    uint32_t maxFrameSize_{ 16384 };
    uint32_t peerMaxFrameSize_{ 16384 };
    bool goAwaySent_{ false };
};

} // namespace nitrocoro::http2
