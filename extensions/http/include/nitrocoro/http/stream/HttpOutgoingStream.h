/**
 * @file HttpOutgoingStream.h
 * @brief HTTP outgoing stream for writing requests and responses
 */
#pragma once
#include <nitrocoro/http/BodyWriter.h>
#include <nitrocoro/http/HttpHeader.h>
#include <nitrocoro/http/HttpMessage.h>
#include <nitrocoro/http/HttpTypes.h>

#include <nitrocoro/core/Future.h>
#include <nitrocoro/core/Task.h>
#include <nitrocoro/io/Stream.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace nitrocoro::http
{

namespace detail
{

template <typename DataType>
class HttpOutgoingStreamBase
{
public:
    explicit HttpOutgoingStreamBase(io::StreamPtr stream,
                                    Promise<> finishedPromise,
                                    std::optional<Future<>> prevFuture = std::nullopt,
                                    bool ignoreBody = false,
                                    bool send_date_header = true)
        : stream_(std::move(stream))
        , finishedPromise_(std::move(finishedPromise))
        , prevFuture_(std::move(prevFuture))
        , ignoreBody_(ignoreBody)
        , sendDateHeader_(send_date_header)
    {
    }

    void setHeader(std::string_view name, std::string value);
    void setHeader(HttpHeader::NameCode code, std::string value);
    void setHeader(HttpHeader header);
    void setCookie(const std::string & name, std::string value);
    Task<> write(const char * data, size_t len);
    Task<> write(std::string_view data);
    Task<> end();
    Task<> end(std::string_view data);

protected:
    static const char * getDefaultReason(uint16_t code);
    Task<> writeHeaders();
    void buildHeaders(std::string & buf);
    void decideTransferMode(std::optional<size_t> lengthHint = std::nullopt);

    DataType data_;
    io::StreamPtr stream_;
    bool headersSent_{ false };
    TransferMode transferMode_{ TransferMode::Chunked };
    std::unique_ptr<BodyWriter> bodyWriter_;
    Promise<> finishedPromise_;
    std::optional<Future<>> prevFuture_;
    bool ignoreBody_{ false };
    bool sendDateHeader_{ true };
    size_t bodyLength_{ 0 };
};

} // namespace detail

// Forward declaration
template <typename T>
class HttpOutgoingStream;

// ============================================================================
// HttpOutgoingStream<HttpRequest> - Write HTTP Request
// ============================================================================

template <>
class HttpOutgoingStream<HttpRequest>
    : public detail::HttpOutgoingStreamBase<HttpRequest>
{
public:
    explicit HttpOutgoingStream(io::StreamPtr stream,
                                Promise<> finishedPromise,
                                std::optional<Future<>> prevFuture = std::nullopt)
        : detail::HttpOutgoingStreamBase<HttpRequest>(std::move(stream),
                                                      std::move(finishedPromise),
                                                      std::move(prevFuture))
    {
    }
    explicit HttpOutgoingStream(io::StreamPtr stream)
        : detail::HttpOutgoingStreamBase<HttpRequest>(std::move(stream), Promise<>())
    {
    }

    void setMethod(HttpMethod method) { data_.method = method; }
    void setMethod(std::string_view method) { data_.method = HttpMethod::fromString(method); }
    void setPath(const std::string & path) { data_.path = path; }
    void setVersion(Version version) { data_.version = version; }
};

// ============================================================================
// HttpOutgoingStream<HttpResponse> - Write HTTP Response
// ============================================================================

template <>
class HttpOutgoingStream<HttpResponse>
    : public detail::HttpOutgoingStreamBase<HttpResponse>
{
public:
    explicit HttpOutgoingStream(io::StreamPtr stream,
                                Promise<> finishedPromise,
                                std::optional<Future<>> prevFuture = std::nullopt,
                                bool ignoreBody = false,
                                bool send_date_header = true)
        : detail::HttpOutgoingStreamBase<HttpResponse>(std::move(stream),
                                                       std::move(finishedPromise),
                                                       std::move(prevFuture),
                                                       ignoreBody,
                                                       send_date_header)
    {
    }

    void setStatus(int code, const std::string & reason = "");
    void setStatus(StatusCode code, const std::string & reason = "");
    void setVersion(Version version) { data_.version = version; }
    void setCloseConnection(bool shouldClose) { data_.shouldClose = shouldClose; }
};

} // namespace nitrocoro::http
