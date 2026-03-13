/**
 * @file HttpHeader.h
 * @brief HTTP header representation
 */
#pragma once

#include <algorithm>
#include <string>
#include <string_view>

namespace nitrocoro::http
{

class HttpHeader
{
public:
    struct Name
    {
        // General
        static constexpr std::string_view CacheControl_L = "cache-control";
        static constexpr std::string_view CacheControl_C = "Cache-Control";
        static constexpr std::string_view Connection_L = "connection";
        static constexpr std::string_view Connection_C = "Connection";
        static constexpr std::string_view Date_L = "date";
        static constexpr std::string_view Date_C = "Date";
        static constexpr std::string_view TransferEncoding_L = "transfer-encoding";
        static constexpr std::string_view TransferEncoding_C = "Transfer-Encoding";
        static constexpr std::string_view Upgrade_L = "upgrade";
        static constexpr std::string_view Upgrade_C = "Upgrade";

        // Request
        static constexpr std::string_view Accept_L = "accept";
        static constexpr std::string_view Accept_C = "Accept";
        static constexpr std::string_view AcceptEncoding_L = "accept-encoding";
        static constexpr std::string_view AcceptEncoding_C = "Accept-Encoding";
        static constexpr std::string_view AcceptLanguage_L = "accept-language";
        static constexpr std::string_view AcceptLanguage_C = "Accept-Language";
        static constexpr std::string_view Authorization_L = "authorization";
        static constexpr std::string_view Authorization_C = "Authorization";
        static constexpr std::string_view Host_L = "host";
        static constexpr std::string_view Host_C = "Host";
        static constexpr std::string_view IfModifiedSince_L = "if-modified-since";
        static constexpr std::string_view IfModifiedSince_C = "If-Modified-Since";
        static constexpr std::string_view IfNoneMatch_L = "if-none-match";
        static constexpr std::string_view IfNoneMatch_C = "If-None-Match";
        static constexpr std::string_view Referer_L = "referer";
        static constexpr std::string_view Referer_C = "Referer";
        static constexpr std::string_view UserAgent_L = "user-agent";
        static constexpr std::string_view UserAgent_C = "User-Agent";
        static constexpr std::string_view Expect_L = "expect";
        static constexpr std::string_view Expect_C = "Expect";

        // Response
        static constexpr std::string_view AcceptRanges_L = "accept-ranges";
        static constexpr std::string_view AcceptRanges_C = "Accept-Ranges";
        static constexpr std::string_view Age_L = "age";
        static constexpr std::string_view Age_C = "Age";
        static constexpr std::string_view ETag_L = "etag";
        static constexpr std::string_view ETag_C = "ETag";
        static constexpr std::string_view Location_L = "location";
        static constexpr std::string_view Location_C = "Location";
        static constexpr std::string_view RetryAfter_L = "retry-after";
        static constexpr std::string_view RetryAfter_C = "Retry-After";
        static constexpr std::string_view Server_L = "server";
        static constexpr std::string_view Server_C = "Server";
        static constexpr std::string_view Vary_L = "vary";
        static constexpr std::string_view Vary_C = "Vary";
        static constexpr std::string_view WwwAuthenticate_L = "www-authenticate";
        static constexpr std::string_view WwwAuthenticate_C = "WWW-Authenticate";

        // Entity
        static constexpr std::string_view Allow_L = "allow";
        static constexpr std::string_view Allow_C = "Allow";
        static constexpr std::string_view ContentEncoding_L = "content-encoding";
        static constexpr std::string_view ContentEncoding_C = "Content-Encoding";
        static constexpr std::string_view ContentLanguage_L = "content-language";
        static constexpr std::string_view ContentLanguage_C = "Content-Language";
        static constexpr std::string_view ContentLength_L = "content-length";
        static constexpr std::string_view ContentLength_C = "Content-Length";
        static constexpr std::string_view ContentRange_L = "content-range";
        static constexpr std::string_view ContentRange_C = "Content-Range";
        static constexpr std::string_view ContentType_L = "content-type";
        static constexpr std::string_view ContentType_C = "Content-Type";
        static constexpr std::string_view Expires_L = "expires";
        static constexpr std::string_view Expires_C = "Expires";
        static constexpr std::string_view LastModified_L = "last-modified";
        static constexpr std::string_view LastModified_C = "Last-Modified";

        // Cookie
        static constexpr std::string_view Cookie_L = "cookie";
        static constexpr std::string_view Cookie_C = "Cookie";
        static constexpr std::string_view SetCookie_L = "set-cookie";
        static constexpr std::string_view SetCookie_C = "Set-Cookie";

        // CORS
        static constexpr std::string_view AccessControlAllowOrigin_L = "access-control-allow-origin";
        static constexpr std::string_view AccessControlAllowOrigin_C = "Access-Control-Allow-Origin";
        static constexpr std::string_view AccessControlAllowMethods_L = "access-control-allow-methods";
        static constexpr std::string_view AccessControlAllowMethods_C = "Access-Control-Allow-Methods";
        static constexpr std::string_view AccessControlAllowHeaders_L = "access-control-allow-headers";
        static constexpr std::string_view AccessControlAllowHeaders_C = "Access-Control-Allow-Headers";
        static constexpr std::string_view AccessControlAllowCredentials_L = "access-control-allow-credentials";
        static constexpr std::string_view AccessControlAllowCredentials_C = "Access-Control-Allow-Credentials";
        static constexpr std::string_view Origin_L = "origin";
        static constexpr std::string_view Origin_C = "Origin";

        // WebSocket
        static constexpr std::string_view SecWebSocketKey_L = "sec-websocket-key";
        static constexpr std::string_view SecWebSocketKey_C = "Sec-WebSocket-Key";
        static constexpr std::string_view SecWebSocketAccept_L = "sec-websocket-accept";
        static constexpr std::string_view SecWebSocketAccept_C = "Sec-WebSocket-Accept";
        static constexpr std::string_view SecWebSocketVersion_L = "sec-websocket-version";
        static constexpr std::string_view SecWebSocketVersion_C = "Sec-WebSocket-Version";
        static constexpr std::string_view SecWebSocketProtocol_L = "sec-websocket-protocol";
        static constexpr std::string_view SecWebSocketProtocol_C = "Sec-WebSocket-Protocol";
        static constexpr std::string_view SecWebSocketExtensions_L = "sec-websocket-extensions";
        static constexpr std::string_view SecWebSocketExtensions_C = "Sec-WebSocket-Extensions";

        // Custom
        static constexpr std::string_view XForwardedFor_L = "x-forwarded-for";
        static constexpr std::string_view XForwardedFor_C = "X-Forwarded-For";
        static constexpr std::string_view XForwardedProto_L = "x-forwarded-proto";
        static constexpr std::string_view XForwardedProto_C = "X-Forwarded-Proto";
        static constexpr std::string_view XRealIp_L = "x-real-ip";
        static constexpr std::string_view XRealIp_C = "X-Real-IP";
    };

    enum class NameCode
    {
        // General
        CacheControl,
        Connection,
        Date,
        TransferEncoding,
        Upgrade,

        // Request
        Accept,
        AcceptEncoding,
        AcceptLanguage,
        Authorization,
        Host,
        IfModifiedSince,
        IfNoneMatch,
        Referer,
        UserAgent,
        Expect,

        // Response
        AcceptRanges,
        Age,
        ETag,
        Location,
        RetryAfter,
        Server,
        Vary,
        WwwAuthenticate,

        // Entity
        Allow,
        ContentEncoding,
        ContentLanguage,
        ContentLength,
        ContentRange,
        ContentType,
        Expires,
        LastModified,

        // Cookie
        Cookie,
        SetCookie,

        // CORS
        AccessControlAllowOrigin,
        AccessControlAllowMethods,
        AccessControlAllowHeaders,
        AccessControlAllowCredentials,
        Origin,

        // WebSocket
        SecWebSocketKey,
        SecWebSocketAccept,
        SecWebSocketVersion,
        SecWebSocketProtocol,
        SecWebSocketExtensions,

        // Custom
        XForwardedFor,
        XForwardedProto,
        XRealIp,

        Unknown,
    };

    HttpHeader(std::string_view name, std::string value);
    HttpHeader(NameCode name, std::string value);

    const std::string & name() const { return name_; }
    std::string canonicalName() const
    {
        if (nameCode_ != NameCode::Unknown)
            return std::string(codeToCanonicalName(nameCode_));
        return toCanonical(name_);
    }
    const std::string & value() const { return value_; }
    NameCode nameCode() const { return nameCode_; }

    bool nameEquals(std::string_view name) const;
    bool nameEqualsLower(std::string_view lowerName) const { return name_ == lowerName; }

    static const std::pair<std::string_view, std::string_view> & codeToNames(NameCode code);
    static std::string_view codeToName(NameCode code);
    static std::string_view codeToCanonicalName(NameCode code);
    static NameCode nameToCode(const std::string & lowerName);
    static NameCode nameToCode(std::string_view lowerName);

    static std::string toLower(std::string_view str);
    static std::string toCanonical(std::string_view str);

private:
    std::string name_;
    std::string value_;
    NameCode nameCode_;
};

} // namespace nitrocoro::http
