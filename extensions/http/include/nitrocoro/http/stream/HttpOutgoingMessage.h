/**
 * @file HttpOutgoingMessage.h
 * @brief HTTP outgoing stream for writing requests and responses
 */
#pragma once
#include <nitrocoro/http/BodyWriter.h>
#include <nitrocoro/http/Cookie.h>
#include <nitrocoro/http/HttpHeader.h>
#include <nitrocoro/http/HttpMessage.h>
#include <nitrocoro/http/HttpTypes.h>

#include <nitrocoro/core/Task.h>
#include <nitrocoro/io/Stream.h>

#include <memory>
#include <string>
#include <string_view>

namespace nitrocoro::http
{

namespace detail
{

template <typename DataType>
class HttpOutgoingMessageBase
{
public:
    class BodyStream
    {
    public:
        explicit BodyStream(BodyWriter * writer)
            : writer_(writer) {}

        BodyStream(const BodyStream &) = delete;
        BodyStream(BodyStream &&) = delete;

        Task<> write(std::string_view data) { return writer_->write(data); }

    private:
        BodyWriter * writer_;
    };

    using BodyWriterFn = std::function<Task<>(BodyStream &)>;

    explicit HttpOutgoingMessageBase(io::StreamPtr stream,
                                     bool ignoreBody = false,
                                     bool send_date_header = true)
        : stream_(std::move(stream))
        , ignoreBody_(ignoreBody)
        , sendDateHeader_(send_date_header)
    {
    }

    void setHeader(std::string_view name, std::string value);
    void setHeader(HttpHeader::NameCode code, std::string value);
    void setHeader(HttpHeader header);

    void setBody(std::string body);
    void setBody(const char * data, size_t len);
    void setBody(BodyWriterFn bodyWriterFn);
    bool sendStarted() const { return startSending_; }

protected:
    Task<> flush();
    static const char * getDefaultReason(uint16_t code);
    void buildHeaders(std::string & buf);

    DataType data_;
    std::string body_;
    BodyWriterFn bodyWriterFn_;

    io::StreamPtr stream_;
    bool startSending_{ false };
    TransferMode transferMode_{ TransferMode::Chunked };
    bool ignoreBody_{ false };
    bool sendDateHeader_{ true };
    size_t bodyLength_{ 0 };
};

} // namespace detail

// Forward declaration
template <typename T>
class HttpOutgoingMessage;

// ============================================================================
// HttpOutgoingMessage<HttpRequest> - Write HTTP Request
// ============================================================================

template <>
class HttpOutgoingMessage<HttpRequest>
    : public detail::HttpOutgoingMessageBase<HttpRequest>
{
public:
    explicit HttpOutgoingMessage(io::StreamPtr stream)
        : detail::HttpOutgoingMessageBase<HttpRequest>(std::move(stream))
    {
    }

    Task<> flush() { return detail::HttpOutgoingMessageBase<HttpRequest>::flush(); }

    void setMethod(HttpMethod method) { data_.method = method; }
    void setMethod(std::string_view method) { data_.method = HttpMethod::fromString(method); }
    void setPath(const std::string & path) { data_.path = path; }
    void setVersion(Version version) { data_.version = version; }
    void setCookie(const std::string & name, std::string value) { data_.cookies[name] = std::move(value); }
};

// ============================================================================
// HttpOutgoingMessage<HttpResponse> - Write HTTP Response
// ============================================================================

template <>
class HttpOutgoingMessage<HttpResponse>
    : public detail::HttpOutgoingMessageBase<HttpResponse>
{
    friend class HttpServer;

public:
    explicit HttpOutgoingMessage(io::StreamPtr stream,
                                 bool ignoreBody = false,
                                 bool send_date_header = true)
        : detail::HttpOutgoingMessageBase<HttpResponse>(std::move(stream),
                                                        ignoreBody,
                                                        send_date_header)
    {
    }

    void setStatus(int code, const std::string & reason = "");
    void setStatus(StatusCode code, const std::string & reason = "");
    void setVersion(Version version) { data_.version = version; }
    void setCloseConnection(bool shouldClose) { data_.shouldClose = shouldClose; }
    void addCookie(Cookie cookie) { data_.cookies.push_back(std::move(cookie)); }
};

} // namespace nitrocoro::http
