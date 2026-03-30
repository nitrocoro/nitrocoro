/**
 * @file HttpOutgoingMessage.cc
 * @brief HTTP outgoing stream implementations
 */
#include <nitrocoro/http/BodyWriter.h>
#include <nitrocoro/http/Cookie.h>
#include <nitrocoro/http/stream/HttpOutgoingMessage.h>

#include <nitrocoro/utils/Debug.h>

#include <ctime>

namespace nitrocoro::http
{

static const char * toVersionString(Version version)
{
    switch (version)
    {
        case Version::kHttp10:
            return "HTTP/1.0";
        case Version::kHttp11:
            return "HTTP/1.1";
        default:
            return "UNKNOWN";
    }
}

} // namespace nitrocoro::http

namespace nitrocoro::http::detail
{

// ============================================================================
// HttpOutgoingMessageBase Implementation
// ============================================================================

template <typename DataType>
void HttpOutgoingMessageBase<DataType>::setHeader(std::string_view name, std::string value)
{
    HttpHeader header(name, std::move(value));
    data_.headers.insert_or_assign(header.name(), std::move(header));
}

template <typename DataType>
void HttpOutgoingMessageBase<DataType>::setHeader(HttpHeader::NameCode code, std::string value)
{
    HttpHeader header(code, std::move(value));
    data_.headers.insert_or_assign(header.name(), std::move(header));
}

template <typename DataType>
void HttpOutgoingMessageBase<DataType>::setHeader(HttpHeader header)
{
    data_.headers.insert_or_assign(header.name(), std::move(header));
}

template <typename DataType>
void HttpOutgoingMessageBase<DataType>::setBody(std::string body)
{
    body_ = std::move(body);
    bodyWriterFn_ = nullptr;
}

template <typename DataType>
void HttpOutgoingMessageBase<DataType>::setBody(const char * data, size_t len)
{
    body_ = std::string(data, len);
    bodyWriterFn_ = nullptr;
}

template <typename DataType>
void HttpOutgoingMessageBase<DataType>::setBody(BodyWriterFn bodyWriterFn)
{
    bodyWriterFn_ = std::move(bodyWriterFn);
}

template <typename DataType>
Task<> HttpOutgoingMessageBase<DataType>::flush()
{
    if (startSending_)
        co_return;
    startSending_ = true;

    std::unique_ptr<BodyWriter> bodyWriter;
    if (bodyWriterFn_)
    {
        if (data_.version == Version::kHttp10)
        {
            // HTTP/1.0 does not support chunked; fall back to close-delimited
            transferMode_ = TransferMode::UntilClose;
            bodyWriter = BodyWriter::create(TransferMode::UntilClose, stream_);
            if constexpr (std::is_same_v<DataType, HttpResponse>)
                data_.shouldClose = true;
        }
        else
        {
            transferMode_ = TransferMode::Chunked;
            auto it = data_.headers.find(HttpHeader::Name::ContentLength_L);
            if (it != data_.headers.end())
                data_.headers.erase(it);
            setHeader(HttpHeader::NameCode::TransferEncoding, "chunked");
            bodyWriter = BodyWriter::create(TransferMode::Chunked, stream_);
        }
    }
    else
    {
        transferMode_ = TransferMode::ContentLength;
        setHeader(HttpHeader::NameCode::ContentLength, std::to_string(body_.size()));
    }

    std::string buf;
    buf.reserve(128 + data_.headers.size() * 64 + body_.size());
    buildHeaders(buf);
    buf.append("\r\n");

    if (!bodyWriterFn_)
    {
        // TODO: writev
        buf.append(body_);
        co_await stream_->write(buf.data(), buf.size());
    }
    else
    {
        // assert(bodyWriter);
        // send headers
        co_await stream_->write(buf.data(), buf.size());
        // send body
        BodyStream bodyStream(bodyWriter.get());
        co_await bodyWriterFn_(bodyStream);
        co_await bodyWriter->end();
    }
}

template <typename DataType>
void HttpOutgoingMessageBase<DataType>::buildHeaders(std::string & buf)
{
    if constexpr (std::is_same_v<DataType, HttpRequest>)
    {
        buf.append(data_.method.toString())
            .append(" ")
            .append(data_.path)
            .append(" ")
            .append(toVersionString(data_.version))
            .append("\r\n");

        for (const auto & [name, header] : data_.headers)
        {
            buf.append(header.name()).append(": ").append(header.value()).append("\r\n");
        }

        if (!data_.cookies.empty())
        {
            buf.append("Cookie: ");
            bool first = true;
            for (const auto & [name, value] : data_.cookies)
            {
                if (!first)
                    buf.append("; ");
                buf.append(name).append("=").append(value);
                first = false;
            }
            buf.append("\r\n");
        }
    }
    else // HttpResponse
    {
        buf.append(toVersionString(data_.version))
            .append(" ")
            .append(std::to_string(data_.statusCode))
            .append(" ")
            .append(data_.statusReason.empty() ? getDefaultReason(data_.statusCode) : data_.statusReason)
            .append("\r\n");

        for (const auto & [name, header] : data_.headers)
        {
            buf.append(header.name()).append(": ").append(header.value()).append("\r\n");
        }

        for (const auto & cookie : data_.cookies)
        {
            buf.append("Set-Cookie: ").append(cookie.toString()).append("\r\n");
        }

        if (sendDateHeader_ && data_.headers.find(HttpHeader::Name::Date_L) == data_.headers.end())
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

        if (data_.headers.find(HttpHeader::Name::Connection_L) == data_.headers.end())
        {
            if (data_.shouldClose)
            {
                buf.append("Connection: close\r\n");
            }
            else if (data_.version == Version::kHttp10)
            {
                buf.append("Connection: keep-alive\r\n");
            }
        }
    }
}

// TODO: move to http helpers
template <typename DataType>
const char * HttpOutgoingMessageBase<DataType>::getDefaultReason(uint16_t code)
{
    switch (static_cast<StatusCode>(code))
    {
        case StatusCode::k100Continue:
            return "Continue";
        case StatusCode::k101SwitchingProtocols:
            return "Switching Protocols";
        case StatusCode::k102Processing:
            return "Processing";
        case StatusCode::k103EarlyHints:
            return "Early Hints";
        case StatusCode::k200OK:
            return "OK";
        case StatusCode::k201Created:
            return "Created";
        case StatusCode::k202Accepted:
            return "Accepted";
        case StatusCode::k203NonAuthoritativeInformation:
            return "Non-Authoritative Information";
        case StatusCode::k204NoContent:
            return "No Content";
        case StatusCode::k205ResetContent:
            return "Reset Content";
        case StatusCode::k206PartialContent:
            return "Partial Content";
        case StatusCode::k207MultiStatus:
            return "Multi-Status";
        case StatusCode::k208AlreadyReported:
            return "Already Reported";
        case StatusCode::k226IMUsed:
            return "IM Used";
        case StatusCode::k300MultipleChoices:
            return "Multiple Choices";
        case StatusCode::k301MovedPermanently:
            return "Moved Permanently";
        case StatusCode::k302Found:
            return "Found";
        case StatusCode::k303SeeOther:
            return "See Other";
        case StatusCode::k304NotModified:
            return "Not Modified";
        case StatusCode::k305UseProxy:
            return "Use Proxy";
        case StatusCode::k307TemporaryRedirect:
            return "Temporary Redirect";
        case StatusCode::k308PermanentRedirect:
            return "Permanent Redirect";
        case StatusCode::k400BadRequest:
            return "Bad Request";
        case StatusCode::k401Unauthorized:
            return "Unauthorized";
        case StatusCode::k402PaymentRequired:
            return "Payment Required";
        case StatusCode::k403Forbidden:
            return "Forbidden";
        case StatusCode::k404NotFound:
            return "Not Found";
        case StatusCode::k405MethodNotAllowed:
            return "Method Not Allowed";
        case StatusCode::k406NotAcceptable:
            return "Not Acceptable";
        case StatusCode::k407ProxyAuthenticationRequired:
            return "Proxy Authentication Required";
        case StatusCode::k408RequestTimeout:
            return "Request Timeout";
        case StatusCode::k409Conflict:
            return "Conflict";
        case StatusCode::k410Gone:
            return "Gone";
        case StatusCode::k411LengthRequired:
            return "Length Required";
        case StatusCode::k412PreconditionFailed:
            return "Precondition Failed";
        case StatusCode::k413RequestEntityTooLarge:
            return "Request Entity Too Large";
        case StatusCode::k414RequestURITooLarge:
            return "Request-URI Too Large";
        case StatusCode::k415UnsupportedMediaType:
            return "Unsupported Media Type";
        case StatusCode::k416RequestedRangeNotSatisfiable:
            return "Requested Range Not Satisfiable";
        case StatusCode::k417ExpectationFailed:
            return "Expectation Failed";
        case StatusCode::k418ImATeapot:
            return "I'm a teapot";
        case StatusCode::k421MisdirectedRequest:
            return "Misdirected Request";
        case StatusCode::k422UnprocessableEntity:
            return "Unprocessable Entity";
        case StatusCode::k423Locked:
            return "Locked";
        case StatusCode::k424FailedDependency:
            return "Failed Dependency";
        case StatusCode::k425TooEarly:
            return "Too Early";
        case StatusCode::k426UpgradeRequired:
            return "Upgrade Required";
        case StatusCode::k428PreconditionRequired:
            return "Precondition Required";
        case StatusCode::k429TooManyRequests:
            return "Too Many Requests";
        case StatusCode::k431RequestHeaderFieldsTooLarge:
            return "Request Header Fields Too Large";
        case StatusCode::k451UnavailableForLegalReasons:
            return "Unavailable For Legal Reasons";
        case StatusCode::k500InternalServerError:
            return "Internal Server Error";
        case StatusCode::k501NotImplemented:
            return "Not Implemented";
        case StatusCode::k502BadGateway:
            return "Bad Gateway";
        case StatusCode::k503ServiceUnavailable:
            return "Service Unavailable";
        case StatusCode::k504GatewayTimeout:
            return "Gateway Timeout";
        case StatusCode::k505HTTPVersionNotSupported:
            return "HTTP Version Not Supported";
        case StatusCode::k506VariantAlsoNegotiates:
            return "Variant Also Negotiates";
        case StatusCode::k507InsufficientStorage:
            return "Insufficient Storage";
        case StatusCode::k508LoopDetected:
            return "Loop Detected";
        case StatusCode::k510NotExtended:
            return "Not Extended";
        case StatusCode::k511NetworkAuthenticationRequired:
            return "Network Authentication Required";
        default:
            return "";
    }
}

// Explicit instantiations
template class HttpOutgoingMessageBase<HttpRequest>;
template class HttpOutgoingMessageBase<HttpResponse>;

} // namespace nitrocoro::http::detail

namespace nitrocoro::http
{

// ============================================================================
// HttpOutgoingMessage<HttpRequest> Implementation
// ============================================================================

// ============================================================================
// HttpOutgoingMessage<HttpResponse> Implementation
// ============================================================================

void HttpOutgoingMessage<HttpResponse>::setStatus(int code, const std::string & reason)
{
    data_.statusCode = code;
    data_.statusReason = reason.empty() ? detail::HttpOutgoingMessageBase<HttpResponse>::getDefaultReason(code) : reason;
}

void HttpOutgoingMessage<HttpResponse>::setStatus(StatusCode code, const std::string & reason)
{
    setStatus(static_cast<uint16_t>(code), reason);
}

} // namespace nitrocoro::http
