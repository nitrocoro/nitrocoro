/**
 * @file HttpHeader.cc
 * @brief Implementation of HttpHeader
 */
#include <nitrocoro/http/HttpHeader.h>

#include <cctype>
#include <unordered_map>

namespace nitrocoro::http
{

HttpHeader::HttpHeader(std::string_view name, std::string value)
    : name_(toLower(name))
    , value_(std::move(value))
    , nameCode_(nameToCode(name_))
{
}

HttpHeader::HttpHeader(NameCode name, std::string value)
{
    auto & [lower, canonical] = codeToNames(name);
    name_ = lower;
    value_ = std::move(value);
    nameCode_ = name;
}

bool HttpHeader::nameEquals(std::string_view name) const
{
    if (name.size() != name_.size())
        return false;

    for (size_t i = 0; i < name.size(); ++i)
    {
        if (std::tolower(static_cast<unsigned char>(name[i])) != name_[i])
            return false;
    }
    return true;
}

std::string HttpHeader::toLower(std::string_view str)
{
    std::string result{ str };
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return result;
}

std::string HttpHeader::toCanonical(std::string_view str)
{
    std::string result{ str };
    bool capitalizeNext = true;

    for (char & c : result)
    {
        if (capitalizeNext && std::isalpha(static_cast<unsigned char>(c)))
        {
            c = std::toupper(static_cast<unsigned char>(c));
            capitalizeNext = false;
        }
        else if (c == '-')
        {
            capitalizeNext = true;
        }
        else
        {
            c = std::tolower(static_cast<unsigned char>(c));
        }
    }
    return result;
}

const std::pair<std::string_view, std::string_view> & HttpHeader::codeToNames(NameCode code)
{
    static constexpr std::pair<std::string_view, std::string_view> pairs[] = {
        { Name::CacheControl_L, Name::CacheControl_C },
        { Name::Connection_L, Name::Connection_C },
        { Name::Date_L, Name::Date_C },
        { Name::TransferEncoding_L, Name::TransferEncoding_C },
        { Name::Upgrade_L, Name::Upgrade_C },
        { Name::Accept_L, Name::Accept_C },
        { Name::AcceptEncoding_L, Name::AcceptEncoding_C },
        { Name::AcceptLanguage_L, Name::AcceptLanguage_C },
        { Name::Authorization_L, Name::Authorization_C },
        { Name::Host_L, Name::Host_C },
        { Name::IfModifiedSince_L, Name::IfModifiedSince_C },
        { Name::IfNoneMatch_L, Name::IfNoneMatch_C },
        { Name::Referer_L, Name::Referer_C },
        { Name::UserAgent_L, Name::UserAgent_C },
        { Name::Expect_L, Name::Expect_C },
        { Name::AcceptRanges_L, Name::AcceptRanges_C },
        { Name::Age_L, Name::Age_C },
        { Name::ETag_L, Name::ETag_C },
        { Name::Location_L, Name::Location_C },
        { Name::RetryAfter_L, Name::RetryAfter_C },
        { Name::Server_L, Name::Server_C },
        { Name::Vary_L, Name::Vary_C },
        { Name::WwwAuthenticate_L, Name::WwwAuthenticate_C },
        { Name::Allow_L, Name::Allow_C },
        { Name::ContentEncoding_L, Name::ContentEncoding_C },
        { Name::ContentLanguage_L, Name::ContentLanguage_C },
        { Name::ContentLength_L, Name::ContentLength_C },
        { Name::ContentRange_L, Name::ContentRange_C },
        { Name::ContentType_L, Name::ContentType_C },
        { Name::Expires_L, Name::Expires_C },
        { Name::LastModified_L, Name::LastModified_C },
        { Name::Cookie_L, Name::Cookie_C },
        { Name::SetCookie_L, Name::SetCookie_C },
        { Name::AccessControlAllowOrigin_L, Name::AccessControlAllowOrigin_C },
        { Name::AccessControlAllowMethods_L, Name::AccessControlAllowMethods_C },
        { Name::AccessControlAllowHeaders_L, Name::AccessControlAllowHeaders_C },
        { Name::AccessControlAllowCredentials_L, Name::AccessControlAllowCredentials_C },
        { Name::Origin_L, Name::Origin_C },
        { Name::SecWebSocketKey_L, Name::SecWebSocketKey_C },
        { Name::SecWebSocketAccept_L, Name::SecWebSocketAccept_C },
        { Name::SecWebSocketVersion_L, Name::SecWebSocketVersion_C },
        { Name::SecWebSocketProtocol_L, Name::SecWebSocketProtocol_C },
        { Name::SecWebSocketExtensions_L, Name::SecWebSocketExtensions_C },
        { Name::XForwardedFor_L, Name::XForwardedFor_C },
        { Name::XForwardedProto_L, Name::XForwardedProto_C },
        { Name::XRealIp_L, Name::XRealIp_C },
        { "", "" },
    };

    static_assert(std::size(pairs) == static_cast<size_t>(NameCode::Unknown) + 1);

#define nitrocoro_HTTP_HEADER_CHECK_PAIR(name)                                         \
    static_assert(pairs[static_cast<size_t>(NameCode::name)].first == Name::name##_L); \
    static_assert(pairs[static_cast<size_t>(NameCode::name)].second == Name::name##_C)

    nitrocoro_HTTP_HEADER_CHECK_PAIR(CacheControl);
    nitrocoro_HTTP_HEADER_CHECK_PAIR(Connection);
    nitrocoro_HTTP_HEADER_CHECK_PAIR(Date);
    nitrocoro_HTTP_HEADER_CHECK_PAIR(TransferEncoding);
    nitrocoro_HTTP_HEADER_CHECK_PAIR(Upgrade);
    nitrocoro_HTTP_HEADER_CHECK_PAIR(Accept);
    nitrocoro_HTTP_HEADER_CHECK_PAIR(AcceptEncoding);
    nitrocoro_HTTP_HEADER_CHECK_PAIR(AcceptLanguage);
    nitrocoro_HTTP_HEADER_CHECK_PAIR(Authorization);
    nitrocoro_HTTP_HEADER_CHECK_PAIR(Host);
    nitrocoro_HTTP_HEADER_CHECK_PAIR(IfModifiedSince);
    nitrocoro_HTTP_HEADER_CHECK_PAIR(IfNoneMatch);
    nitrocoro_HTTP_HEADER_CHECK_PAIR(Referer);
    nitrocoro_HTTP_HEADER_CHECK_PAIR(UserAgent);
    nitrocoro_HTTP_HEADER_CHECK_PAIR(Expect);
    nitrocoro_HTTP_HEADER_CHECK_PAIR(AcceptRanges);
    nitrocoro_HTTP_HEADER_CHECK_PAIR(Age);
    nitrocoro_HTTP_HEADER_CHECK_PAIR(ETag);
    nitrocoro_HTTP_HEADER_CHECK_PAIR(Location);
    nitrocoro_HTTP_HEADER_CHECK_PAIR(RetryAfter);
    nitrocoro_HTTP_HEADER_CHECK_PAIR(Server);
    nitrocoro_HTTP_HEADER_CHECK_PAIR(Vary);
    nitrocoro_HTTP_HEADER_CHECK_PAIR(WwwAuthenticate);
    nitrocoro_HTTP_HEADER_CHECK_PAIR(Allow);
    nitrocoro_HTTP_HEADER_CHECK_PAIR(ContentEncoding);
    nitrocoro_HTTP_HEADER_CHECK_PAIR(ContentLanguage);
    nitrocoro_HTTP_HEADER_CHECK_PAIR(ContentLength);
    nitrocoro_HTTP_HEADER_CHECK_PAIR(ContentRange);
    nitrocoro_HTTP_HEADER_CHECK_PAIR(ContentType);
    nitrocoro_HTTP_HEADER_CHECK_PAIR(Expires);
    nitrocoro_HTTP_HEADER_CHECK_PAIR(LastModified);
    nitrocoro_HTTP_HEADER_CHECK_PAIR(Cookie);
    nitrocoro_HTTP_HEADER_CHECK_PAIR(SetCookie);
    nitrocoro_HTTP_HEADER_CHECK_PAIR(AccessControlAllowOrigin);
    nitrocoro_HTTP_HEADER_CHECK_PAIR(AccessControlAllowMethods);
    nitrocoro_HTTP_HEADER_CHECK_PAIR(AccessControlAllowHeaders);
    nitrocoro_HTTP_HEADER_CHECK_PAIR(AccessControlAllowCredentials);
    nitrocoro_HTTP_HEADER_CHECK_PAIR(Origin);
    nitrocoro_HTTP_HEADER_CHECK_PAIR(SecWebSocketKey);
    nitrocoro_HTTP_HEADER_CHECK_PAIR(SecWebSocketAccept);
    nitrocoro_HTTP_HEADER_CHECK_PAIR(SecWebSocketVersion);
    nitrocoro_HTTP_HEADER_CHECK_PAIR(SecWebSocketProtocol);
    nitrocoro_HTTP_HEADER_CHECK_PAIR(SecWebSocketExtensions);
    nitrocoro_HTTP_HEADER_CHECK_PAIR(XForwardedFor);
    nitrocoro_HTTP_HEADER_CHECK_PAIR(XForwardedProto);
    nitrocoro_HTTP_HEADER_CHECK_PAIR(XRealIp);

#undef nitrocoro_HTTP_HEADER_CHECK_PAIR

    return pairs[static_cast<size_t>(code)];
}

std::string_view HttpHeader::codeToName(NameCode code)
{
    return codeToNames(code).first;
}

std::string_view HttpHeader::codeToCanonicalName(NameCode code)
{
    return codeToNames(code).second;
}

HttpHeader::NameCode HttpHeader::nameToCode(const std::string & lowerName)
{
    return nameToCode(std::string_view(lowerName));
}

HttpHeader::NameCode HttpHeader::nameToCode(std::string_view lowerName)
{
    static const std::unordered_map<std::string_view, NameCode> nameMap = {
        { Name::CacheControl_L, NameCode::CacheControl },
        { Name::Connection_L, NameCode::Connection },
        { Name::Date_L, NameCode::Date },
        { Name::TransferEncoding_L, NameCode::TransferEncoding },
        { Name::Upgrade_L, NameCode::Upgrade },
        { Name::Accept_L, NameCode::Accept },
        { Name::AcceptEncoding_L, NameCode::AcceptEncoding },
        { Name::AcceptLanguage_L, NameCode::AcceptLanguage },
        { Name::Authorization_L, NameCode::Authorization },
        { Name::Host_L, NameCode::Host },
        { Name::IfModifiedSince_L, NameCode::IfModifiedSince },
        { Name::IfNoneMatch_L, NameCode::IfNoneMatch },
        { Name::Referer_L, NameCode::Referer },
        { Name::UserAgent_L, NameCode::UserAgent },
        { Name::Expect_L, NameCode::Expect },
        { Name::AcceptRanges_L, NameCode::AcceptRanges },
        { Name::Age_L, NameCode::Age },
        { Name::ETag_L, NameCode::ETag },
        { Name::Location_L, NameCode::Location },
        { Name::RetryAfter_L, NameCode::RetryAfter },
        { Name::Server_L, NameCode::Server },
        { Name::Vary_L, NameCode::Vary },
        { Name::WwwAuthenticate_L, NameCode::WwwAuthenticate },
        { Name::Allow_L, NameCode::Allow },
        { Name::ContentEncoding_L, NameCode::ContentEncoding },
        { Name::ContentLanguage_L, NameCode::ContentLanguage },
        { Name::ContentLength_L, NameCode::ContentLength },
        { Name::ContentRange_L, NameCode::ContentRange },
        { Name::ContentType_L, NameCode::ContentType },
        { Name::Expires_L, NameCode::Expires },
        { Name::LastModified_L, NameCode::LastModified },
        { Name::Cookie_L, NameCode::Cookie },
        { Name::SetCookie_L, NameCode::SetCookie },
        { Name::AccessControlAllowOrigin_L, NameCode::AccessControlAllowOrigin },
        { Name::AccessControlAllowMethods_L, NameCode::AccessControlAllowMethods },
        { Name::AccessControlAllowHeaders_L, NameCode::AccessControlAllowHeaders },
        { Name::AccessControlAllowCredentials_L, NameCode::AccessControlAllowCredentials },
        { Name::Origin_L, NameCode::Origin },
        { Name::SecWebSocketKey_L, NameCode::SecWebSocketKey },
        { Name::SecWebSocketAccept_L, NameCode::SecWebSocketAccept },
        { Name::SecWebSocketVersion_L, NameCode::SecWebSocketVersion },
        { Name::SecWebSocketProtocol_L, NameCode::SecWebSocketProtocol },
        { Name::SecWebSocketExtensions_L, NameCode::SecWebSocketExtensions },
        { Name::XForwardedFor_L, NameCode::XForwardedFor },
        { Name::XForwardedProto_L, NameCode::XForwardedProto },
        { Name::XRealIp_L, NameCode::XRealIp },
    };

    auto it = nameMap.find(lowerName);
    return it != nameMap.end() ? it->second : NameCode::Unknown;
}

} // namespace nitrocoro::http
