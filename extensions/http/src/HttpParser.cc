/**
 * @file HttpParser.cc
 * @brief HTTP parser implementations
 */
#include "HttpParser.h"

#include <nitrocoro/http/HttpHeader.h>
#include <nitrocoro/utils/UrlEncode.h>

namespace nitrocoro::http
{

void HttpParser<HttpRequest>::setError(HttpParseError code, std::string message)
{
    errorCode_ = code;
    errorMessage_ = std::move(message);
    state_ = HttpParserState::Error;
}

static Version parseHttpVersion(std::string_view versionStr)
{
    if (versionStr == "HTTP/1.0")
        return Version::kHttp10;
    if (versionStr == "HTTP/1.1")
        return Version::kHttp11;
    return Version::kUnknown;
}

struct HeaderLine
{
    std::string_view name;
    std::string_view value;
};

static HeaderLine parseHeaderLine(std::string_view line)
{
    size_t colonPos = line.find(':');
    if (colonPos == std::string_view::npos)
        return {};

    size_t nameStart = line.find_first_not_of(' ', 0);
    size_t nameEnd = line.find_last_not_of(' ', colonPos - 1);
    size_t valueStart = line.find_first_not_of(' ', colonPos + 1);
    size_t valueEnd = line.find_last_not_of(' ');

    if (nameStart == std::string_view::npos)
        return {};

    return {
        line.substr(nameStart, nameEnd - nameStart + 1),
        valueStart == std::string_view::npos
            ? ""
            : line.substr(valueStart, valueEnd - valueStart + 1)
    };
}

// ============================================================================
// HttpParser<HttpRequest> Implementation
// ============================================================================

void HttpParser<HttpResponse>::setError(HttpParseError code, std::string message)
{
    errorCode_ = code;
    errorMessage_ = std::move(message);
    state_ = HttpParserState::Error;
}

HttpParserState HttpParser<HttpRequest>::parseLine(std::string_view line)
{
    if (state_ == HttpParserState::ExpectStatusLine)
    {
        if (!parseRequestLine(line))
            return HttpParserState::Error;
        state_ = HttpParserState::ExpectHeader;
    }
    else if (!line.empty())
    {
        parseHeader(line);
    }
    else
    {
        if (!processHeaders())
            return HttpParserState::Error;
        state_ = HttpParserState::HeaderComplete;
    }
    return state_;
}

bool HttpParser<HttpRequest>::processHeaders()
{
    return processTransferMode() && processKeepAlive();
}

bool HttpParser<HttpRequest>::processTransferMode()
{
    // RFC 7230 Section 3.3.3: Message body length determination
    // 1. Check Transfer-Encoding first (takes precedence over Content-Length)
    auto it = data_.headers.find(HttpHeader::Name::TransferEncoding_L);
    if (it != data_.headers.end())
    {
        std::string_view value = it->second.value();

        if (value == "chunked")
        {
            data_.transferMode = TransferMode::Chunked;
            return true;
        }
        if (value != "identity")
        {
            setError(HttpParseError::UnsupportedTransferEncoding, "Unsupported Transfer-Encoding: " + std::string(value));
            return false;
        }
        // "identity" means no encoding, continue to check Content-Length
    }

    // 2. Check Content-Length
    it = data_.headers.find(HttpHeader::Name::ContentLength_L);
    if (it != data_.headers.end())
    {
        const std::string & clValue = it->second.value();
        if (clValue.empty() || clValue[0] == '-')
        {
            setError(HttpParseError::AmbiguousContentLength, "Invalid content length");
            return false;
        }

        try
        {
            data_.contentLength = std::stoul(clValue);
        }
        catch (const std::exception &)
        {
            setError(HttpParseError::AmbiguousContentLength, "Invalid content length");
            return false;
        }
        data_.transferMode = TransferMode::ContentLength;
    }
    else
    {
        // 3. For requests without Transfer-Encoding or Content-Length, body length is 0
        data_.contentLength = 0;
        data_.transferMode = TransferMode::ContentLength;
    }
    return true;
}

bool HttpParser<HttpRequest>::processKeepAlive()
{
    auto it = data_.headers.find(HttpHeader::Name::Connection_L);
    if (it != data_.headers.end())
    {
        std::string lowerValue = HttpHeader::toLower(it->second.value());
        data_.keepAlive = (lowerValue == "keep-alive");
    }
    else
    {
        // Default: HTTP/1.1 keep-alive, HTTP/1.0 close
        data_.keepAlive = (data_.version == Version::kHttp11);
    }
    return true;
}

bool HttpParser<HttpRequest>::parseRequestLine(std::string_view line)
{
    size_t pos1 = line.find(' ');
    if (pos1 == std::string_view::npos)
    {
        setError(HttpParseError::MalformedRequestLine, "Missing space in request line");
        return false;
    }
    size_t pos2 = line.find(' ', pos1 + 1);
    if (pos2 == std::string_view::npos)
    {
        setError(HttpParseError::MalformedRequestLine, "Missing second space in request line");
        return false;
    }

    data_.method = HttpMethod::fromString(line.substr(0, pos1));
    data_.version = parseHttpVersion(line.substr(pos2 + 1));
    if (data_.method == methods::_Invalid)
    {
        setError(HttpParseError::MalformedRequestLine, "Invalid http method");
        return false;
    }
    if (data_.version == Version::kUnknown)
    {
        setError(HttpParseError::MalformedRequestLine, "Invalid http version");
        return false;
    }

    std::string_view fullPath = line.substr(pos1 + 1, pos2 - pos1 - 1);
    size_t qpos = fullPath.find('?');
    std::string_view rawPath = fullPath.substr(0, qpos);

    data_.rawPath = rawPath;
    data_.path = utils::urlDecode(rawPath);

    if (qpos != std::string_view::npos)
    {
        data_.query = fullPath.substr(qpos + 1);
        parseQueryString(data_.query);
    }
    else
    {
        data_.query.clear();
    }
    return true;
}

void HttpParser<HttpRequest>::parseHeader(std::string_view line)
{
    auto [name, value] = parseHeaderLine(line);
    if (name.empty())
        return;

    HttpHeader header(name, std::string(value));
    auto nameCode = header.nameCode();
    if (nameCode == HttpHeader::NameCode::Cookie)
    {
        parseCookies(std::string(value));
    }
    else
    {
        auto [it, inserted] = data_.headers.emplace(header.name(), std::move(header));
        if (!inserted && nameCode == HttpHeader::NameCode::ContentLength && value != it->second.value())
        {
            setError(HttpParseError::AmbiguousContentLength, "Multiple Content-Length headers");
        }
    }
}

void HttpParser<HttpRequest>::parseQueryString(std::string_view queryStr)
{
    // TODO: multi-value
    size_t start = 0;
    while (start < queryStr.size())
    {
        size_t ampPos = queryStr.find('&', start);
        size_t end = (ampPos == std::string_view::npos) ? queryStr.size() : ampPos;

        std::string_view pair = queryStr.substr(start, end - start);
        size_t eqPos = pair.find('=');
        if (eqPos != std::string_view::npos)
        {
            auto key = utils::urlDecodeComponent(pair.substr(0, eqPos));
            auto value = utils::urlDecodeComponent(pair.substr(eqPos + 1));
            data_.queries.emplace(std::move(key), std::move(value));
        }

        if (ampPos == std::string_view::npos)
            break;
        start = ampPos + 1;
    }
}

void HttpParser<HttpRequest>::parseCookies(const std::string & cookieHeader)
{
    // TODO: parse cookies correctly
    std::string_view cookies(cookieHeader);
    size_t start = 0;
    while (start < cookies.size())
    {
        while (start < cookies.size() && cookies[start] == ' ')
            ++start;

        size_t semiPos = cookies.find(';', start);
        size_t end = (semiPos == std::string_view::npos) ? cookies.size() : semiPos;

        std::string_view pair = cookies.substr(start, end - start);
        size_t eqPos = pair.find('=');
        if (eqPos != std::string_view::npos)
        {
            data_.cookies[std::string(pair.substr(0, eqPos))] = std::string(pair.substr(eqPos + 1));
        }

        if (semiPos == std::string_view::npos)
            break;
        start = semiPos + 1;
    }
}

HttpParseResult<HttpRequest> HttpParser<HttpRequest>::extractResult()
{
    return { std::move(data_), errorCode_, errorMessage_ };
}

// ============================================================================
// HttpParser<HttpResponse> Implementation
// ============================================================================

HttpParserState HttpParser<HttpResponse>::parseLine(std::string_view line)
{
    if (state_ == HttpParserState::ExpectStatusLine)
    {
        if (!parseStatusLine(line))
            return HttpParserState::Error;
        state_ = HttpParserState::ExpectHeader;
    }
    else if (!line.empty())
    {
        parseHeader(line);
    }
    else
    {
        if (!processHeaders())
            return HttpParserState::Error;
        state_ = HttpParserState::HeaderComplete;
    }
    return state_;
}

bool HttpParser<HttpResponse>::processHeaders()
{
    return processTransferMode() && processConnectionClose();
}

bool HttpParser<HttpResponse>::processTransferMode()
{
    // RFC 7230 Section 3.3.3: Message body length determination
    // 1. Check Transfer-Encoding first (takes precedence over Content-Length)
    auto it = data_.headers.find(HttpHeader::Name::TransferEncoding_L);
    if (it != data_.headers.end())
    {
        std::string_view value = it->second.value();

        if (value == "chunked")
        {
            data_.transferMode = TransferMode::Chunked;
            return true;
        }
        if (value != "identity")
        {
            setError(HttpParseError::UnsupportedTransferEncoding, "Unsupported Transfer-Encoding: " + std::string(value));
            return false;
        }
        // "identity" means no encoding, continue to check Content-Length
    }

    // 2. Check Content-Length
    it = data_.headers.find(HttpHeader::Name::ContentLength_L);
    if (it != data_.headers.end())
    {
        const std::string & clValue = it->second.value();
        if (clValue.empty() || clValue[0] == '-')
        {
            setError(HttpParseError::AmbiguousContentLength, "Invalid content length");
            return false;
        }

        try
        {
            data_.contentLength = std::stoul(clValue);
        }
        catch (const std::exception &)
        {
            setError(HttpParseError::AmbiguousContentLength, "Invalid content length");
            return false;
        }
        data_.transferMode = TransferMode::ContentLength;
    }
    else
    {
        // 3. For responses without Transfer-Encoding or Content-Length, read until connection close
        data_.transferMode = TransferMode::UntilClose;
    }
    return true;
}

bool HttpParser<HttpResponse>::processConnectionClose()
{
    auto it = data_.headers.find(HttpHeader::Name::Connection_L);
    if (it != data_.headers.end())
    {
        std::string lowerValue = HttpHeader::toLower(it->second.value());
        data_.shouldClose = (lowerValue == "close");
    }
    else
    {
        // Default: HTTP/1.1 keep-alive (shouldClose=false), HTTP/1.0 close (shouldClose=true)
        data_.shouldClose = (data_.version == Version::kHttp10);
    }
    return true;
}

bool HttpParser<HttpResponse>::parseStatusLine(std::string_view line)
{
    size_t sp1 = line.find(' ');
    if (sp1 == std::string_view::npos)
    {
        setError(HttpParseError::MalformedRequestLine, "Invalid status line");
        return false;
    }
    data_.version = parseHttpVersion(line.substr(0, sp1));
    if (data_.version == Version::kUnknown)
    {
        setError(HttpParseError::MalformedRequestLine, "Invalid HTTP version");
        return false;
    }

    size_t sp2 = line.find(' ', sp1 + 1);
    std::string_view codeStr;
    std::string_view reason;
    if (sp2 == std::string_view::npos)
    {
        codeStr = line.substr(sp1 + 1);
        reason = {};
    }
    else
    {
        codeStr = line.substr(sp1 + 1, sp2 - sp1 - 1);
        reason = line.substr(sp2 + 1);
    }

    try
    {
        data_.statusCode = static_cast<uint16_t>(std::stoi(std::string(codeStr)));
    }
    catch (const std::exception &)
    {
        setError(HttpParseError::MalformedRequestLine, "Invalid status code");
        return false;
    }

    if (data_.statusCode < 100 || data_.statusCode > 999) // loose
    {
        setError(HttpParseError::MalformedRequestLine, "Invalid status code");
        return false;
    }
    data_.statusReason = std::string(reason);
    return true;
}

void HttpParser<HttpResponse>::parseHeader(std::string_view line)
{
    auto [name, value] = parseHeaderLine(line);
    if (name.empty())
        return;

    HttpHeader h(name, std::string(value));
    if (h.nameCode() == HttpHeader::NameCode::SetCookie)
    {
        parseCookies(std::string(value));
    }
    else
    {
        data_.headers.emplace(h.name(), std::move(h));
    }
}

void HttpParser<HttpResponse>::parseCookies(const std::string & cookieHeader)
{
    size_t eqPos = cookieHeader.find('=');
    if (eqPos != std::string::npos)
    {
        size_t endPos = cookieHeader.find(';', eqPos);
        std::string name = cookieHeader.substr(0, eqPos);
        std::string value = (endPos != std::string::npos)
                                ? cookieHeader.substr(eqPos + 1, endPos - eqPos - 1)
                                : cookieHeader.substr(eqPos + 1);
        data_.cookies.insert_or_assign(std::move(name), std::move(value));
    }
}

HttpParseResult<HttpResponse> HttpParser<HttpResponse>::extractResult()
{
    return { std::move(data_), errorCode_, errorMessage_ };
}

} // namespace nitrocoro::http
