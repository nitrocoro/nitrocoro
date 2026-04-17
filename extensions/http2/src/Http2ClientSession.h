/**
 * @file Http2ClientSession.h
 * @brief HTTP/2 client session: manages connection and multiplexed streams
 */
#pragma once

#include "Http2ClientStream.h"
#include "frame/FrameReader.h"
#include "hpack/Hpack.h"
#include "nitrocoro/http/HttpStream.h"

#include <nitrocoro/core/Future.h>
#include <nitrocoro/core/Mutex.h>
#include <nitrocoro/core/Task.h>
#include <nitrocoro/http/HttpMessage.h>
#include <nitrocoro/io/Stream.h>

#include <memory>
#include <unordered_map>

namespace nitrocoro::http2
{

class Http2ClientSession : public std::enable_shared_from_this<Http2ClientSession>
{
public:
    Http2ClientSession(io::StreamPtr stream, Scheduler * scheduler, std::string scheme = "https");

    Task<> run();
    Task<http::IncomingResponse> request(http::ClientRequest req);

    bool isAlive() const { return running_; }

    Task<> sendRequest(uint32_t streamId, http::ClientRequest req);
    Task<> sendHeaders(uint32_t streamId, const http::HttpRequest & req, bool endStream);
    Task<> sendData(uint32_t streamId, std::string_view data, bool endStream);
    Task<> sendRstStream(uint32_t streamId, uint32_t errorCode);

private:
    Task<> handleHeaders(const Frame & frame);
    Task<> handleContinuation(const Frame & frame);
    Task<> handleData(const Frame & frame);
    Task<> handleSettings(const Frame & frame);
    Task<> handlePing(const Frame & frame);
    Task<> finaliseHeaders();

    Task<> sendClientPreface();
    Task<> sendSettingsAck();

    uint32_t allocateStreamId();
    http::HttpResponse buildResponse(const hpack::DecodedHeaders & dh);

    FrameReader reader_;
    hpack::HpackDecoder decoder_;
    hpack::HpackEncoder encoder_;
    Mutex writeMutex_;

    Scheduler * scheduler_;

    std::unordered_map<uint32_t, std::shared_ptr<Http2ClientStream>> streams_;

    uint32_t continuationStreamId_{ 0 };
    bool headersOnly_{ false };
    std::vector<uint8_t> headerBlockBuf_;

    std::string scheme_;
    uint32_t nextStreamId_{ 1 }; // Client uses odd stream IDs
    uint32_t maxFrameSize_{ 16384 };
    bool goAwaySent_{ false };
    bool settingsReceived_{ false };
    bool running_{ true };
    Promise<> readyPromise_;
    SharedFuture<> readyFuture_;
};

} // namespace nitrocoro::http2
