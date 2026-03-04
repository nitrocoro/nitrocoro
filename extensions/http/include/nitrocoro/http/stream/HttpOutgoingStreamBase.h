/**
 * @file HttpOutgoingStreamBase.h
 * @brief Base class for HTTP outgoing streams
 */
#pragma once
#include <nitrocoro/core/Future.h>
#include <nitrocoro/core/Task.h>
#include <nitrocoro/http/BodyWriter.h>
#include <nitrocoro/io/Stream.h>

#include <memory>
#include <string>
#include <string_view>

namespace nitrocoro::http
{

template <typename DataType>
class HttpOutgoingStreamBase
{
public:
    explicit HttpOutgoingStreamBase(io::StreamPtr stream, Promise<> finishedPromise)
        : stream_(std::move(stream)), finishedPromise_(std::move(finishedPromise)) {}

    void setHeader(std::string_view name, std::string value);
    void setHeader(HttpHeader::NameCode code, std::string value);
    void setHeader(HttpHeader header);
    void setCookie(const std::string & name, std::string value);
    Task<> write(const char * data, size_t len);
    Task<> write(std::string_view data);
    Task<> end();
    Task<> end(std::string_view data);

protected:
    static const char * getDefaultReason(StatusCode code);
    Task<> writeHeaders();
    void buildHeaders(std::string & buf);
    void decideTransferMode(std::optional<size_t> lengthHint = std::nullopt);

    DataType data_;
    io::StreamPtr stream_;
    bool headersSent_ = false;
    TransferMode transferMode_ = TransferMode::Chunked;
    std::unique_ptr<BodyWriter> bodyWriter_;
    Promise<> finishedPromise_;
};

} // namespace nitrocoro::http
