/**
 * @file HttpTypes.h
 * @brief HTTP type definitions
 */
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace nitrocoro::http
{

enum class StatusCode
{
    kUnknown = 0,
    k100Continue = 100,
    k101SwitchingProtocols = 101,
    k102Processing = 102,
    k103EarlyHints = 103,
    k200OK = 200,
    k201Created = 201,
    k202Accepted = 202,
    k203NonAuthoritativeInformation = 203,
    k204NoContent = 204,
    k205ResetContent = 205,
    k206PartialContent = 206,
    k207MultiStatus = 207,
    k208AlreadyReported = 208,
    k226IMUsed = 226,
    k300MultipleChoices = 300,
    k301MovedPermanently = 301,
    k302Found = 302,
    k303SeeOther = 303,
    k304NotModified = 304,
    k305UseProxy = 305,
    k306Unused = 306,
    k307TemporaryRedirect = 307,
    k308PermanentRedirect = 308,
    k400BadRequest = 400,
    k401Unauthorized = 401,
    k402PaymentRequired = 402,
    k403Forbidden = 403,
    k404NotFound = 404,
    k405MethodNotAllowed = 405,
    k406NotAcceptable = 406,
    k407ProxyAuthenticationRequired = 407,
    k408RequestTimeout = 408,
    k409Conflict = 409,
    k410Gone = 410,
    k411LengthRequired = 411,
    k412PreconditionFailed = 412,
    k413RequestEntityTooLarge = 413,
    k414RequestURITooLarge = 414,
    k415UnsupportedMediaType = 415,
    k416RequestedRangeNotSatisfiable = 416,
    k417ExpectationFailed = 417,
    k418ImATeapot = 418,
    k421MisdirectedRequest = 421,
    k422UnprocessableEntity = 422,
    k423Locked = 423,
    k424FailedDependency = 424,
    k425TooEarly = 425,
    k426UpgradeRequired = 426,
    k428PreconditionRequired = 428,
    k429TooManyRequests = 429,
    k431RequestHeaderFieldsTooLarge = 431,
    k451UnavailableForLegalReasons = 451,
    k500InternalServerError = 500,
    k501NotImplemented = 501,
    k502BadGateway = 502,
    k503ServiceUnavailable = 503,
    k504GatewayTimeout = 504,
    k505HTTPVersionNotSupported = 505,
    k506VariantAlsoNegotiates = 506,
    k507InsufficientStorage = 507,
    k508LoopDetected = 508,
    k510NotExtended = 510,
    k511NetworkAuthenticationRequired = 511
};

enum class Version
{
    kUnknown = 0,
    kHttp10,
    kHttp11
};

enum class TransferMode
{
    ContentLength,
    Chunked,
    UntilClose
};

/**
 * @brief Strongly-typed HTTP method identifier.
 *
 * Standard methods are predefined as static constexpr members.
 * Custom methods can be registered via HttpMethod::registerMethod() before
 * any router is constructed. fromString() returns methods::_Invalid for
 * unknown methods.
 */
struct HttpMethod
{
    uint16_t id;

    enum Standard : uint16_t
    {
        Get = 0,
        Head,
        Post,
        Put,
        Delete,
        Options,
        Patch,
        Trace,
        Connect,
        _Invalid
    };

    bool operator==(const HttpMethod &) const = default;

    std::string_view toString() const;

    /// Convert a method string to HttpMethod. Returns methods::_Invalid if unknown.
    static HttpMethod fromString(std::string_view s);

    /// Register a custom method. Returns the existing HttpMethod if already registered.
    static HttpMethod registerMethod(std::string name);
};

namespace methods
{
inline constexpr HttpMethod Get{ HttpMethod::Get };
inline constexpr HttpMethod Head{ HttpMethod::Head };
inline constexpr HttpMethod Post{ HttpMethod::Post };
inline constexpr HttpMethod Put{ HttpMethod::Put };
inline constexpr HttpMethod Delete{ HttpMethod::Delete };
inline constexpr HttpMethod Options{ HttpMethod::Options };
inline constexpr HttpMethod Patch{ HttpMethod::Patch };
inline constexpr HttpMethod Trace{ HttpMethod::Trace };
inline constexpr HttpMethod Connect{ HttpMethod::Connect };
inline constexpr HttpMethod _Invalid{ HttpMethod::_Invalid };
} // namespace methods

} // namespace nitrocoro::http

template <>
struct std::hash<nitrocoro::http::HttpMethod>
{
    size_t operator()(nitrocoro::http::HttpMethod m) const noexcept
    {
        return std::hash<uint16_t>{}(m.id);
    }
};
