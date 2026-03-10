/**
 * @file HttpParser.cc
 * @brief HTTP parser implementations
 */
#include "HttpParser.h"

#include <nitrocoro/http/HttpHeader.h>

#include <optional>
#include <stdexcept>

namespace nitrocoro::http
{

static Version parseHttpVersion(std::string_view versionStr)
{
    if (versionStr == "HTTP/1.0")
        return Version::kHttp10;
    if (versionStr == "HTTP/1.1")
        return Version::kHttp11;
    return Version::kUnknown;
}

static StatusCode parseStatusCode(int code)
{
    return static_cast<StatusCode>(code);
}

static std::optional<HttpHeader> parseHeaderLine(std::string_view line)
{
    size_t colonPos = line.find(':');
    if (colonPos == std::string_view::npos)
        return std::nullopt;

    size_t nameStart = line.find_first_not_of(' ', 0);
    size_t nameEnd = line.find_last_not_of(' ', colonPos - 1);
    size_t valueStart = line.find_first_not_of(' ', colonPos + 1);
    size_t valueEnd = line.find_last_not_of(' ');

    if (nameStart == std::string_view::npos)
        return std::nullopt;

    std::string name(line.substr(nameStart, nameEnd - nameStart + 1));
    std::string value(valueStart == std::string_view::npos ? "" : line.substr(valueStart, valueEnd - valueStart + 1));
    return HttpHeader(std::move(name), std::move(value));
}

// ============================================================================
// HttpParser<HttpRequest> Implementation
// ============================================================================

bool HttpParser<HttpRequest>::parseLine(std::string_view line)
{
    if (state_ == State::ExpectStatusLine)
    {
        parseRequestLine(line);
        state_ = State::ExpectHeader;
    }
    else if (!line.empty())
    {
        parseHeader(line);
    }
    else
    {
        processHeaders();
        state_ = State::Complete;
    }
    return state_ == State::Complete;
}

void HttpParser<HttpRequest>::processHeaders()
{
    processTransferMode();
    processKeepAlive();
}

void HttpParser<HttpRequest>::processTransferMode()
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
            return;
        }
        if (value != "identity")
        {
            throw std::runtime_error("Unsupported Transfer-Encoding: " + std::string(value));
        }
        // "identity" means no encoding, continue to check Content-Length
    }

    // 2. Check Content-Length
    it = data_.headers.find(HttpHeader::Name::ContentLength_L);
    if (it != data_.headers.end())
    {
        data_.contentLength = std::stoul(it->second.value());
        data_.transferMode = TransferMode::ContentLength;
    }
    else
    {
        // 3. For requests without Transfer-Encoding or Content-Length, body length is 0
        data_.contentLength = 0;
        data_.transferMode = TransferMode::ContentLength;
    }
}

void HttpParser<HttpRequest>::processKeepAlive()
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
}

void HttpParser<HttpRequest>::parseRequestLine(std::string_view line)
{
    size_t pos1 = line.find(' ');
    size_t pos2 = line.find(' ', pos1 + 1);

    data_.method = HttpMethod::fromString(line.substr(0, pos1));
    data_.fullPath = line.substr(pos1 + 1, pos2 - pos1 - 1);
    data_.version = parseHttpVersion(line.substr(pos2 + 1));

    size_t qpos = data_.fullPath.find('?');
    if (qpos != std::string::npos)
    {
        data_.path = data_.fullPath.substr(0, qpos);
        data_.query = data_.fullPath.substr(qpos + 1);
        parseQueryString(data_.query);
    }
    else
    {
        data_.path = data_.fullPath;
        data_.query.clear();
    }
}

void HttpParser<HttpRequest>::parseHeader(std::string_view line)
{
    auto header = parseHeaderLine(line);
    if (!header)
        return;

    if (header->nameCode() == HttpHeader::NameCode::Cookie)
    {
        parseCookies(header->value());
    }
    else
    {
        data_.headers.emplace(header->name(), std::move(*header));
    }
}

void HttpParser<HttpRequest>::parseQueryString(std::string_view queryStr)
{
    // TODO: url decode
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
            std::string key = std::string(pair.substr(0, eqPos));
            std::string value = std::string(pair.substr(eqPos + 1));
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

// ============================================================================
// HttpParser<HttpResponse> Implementation
// ============================================================================

bool HttpParser<HttpResponse>::parseLine(std::string_view line)
{
    if (state_ == State::ExpectStatusLine)
    {
        parseStatusLine(line);
        state_ = State::ExpectHeader;
    }
    else if (!line.empty())
    {
        parseHeader(line);
    }
    else
    {
        processHeaders();
        state_ = State::Complete;
    }
    return state_ == State::Complete;
}

void HttpParser<HttpResponse>::processHeaders()
{
    processTransferMode();
    processConnectionClose();
}

void HttpParser<HttpResponse>::processTransferMode()
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
            return;
        }
        if (value != "identity")
        {
            throw std::runtime_error("Unsupported Transfer-Encoding: " + std::string(value));
        }
        // "identity" means no encoding, continue to check Content-Length
    }

    // 2. Check Content-Length
    it = data_.headers.find(HttpHeader::Name::ContentLength_L);
    if (it != data_.headers.end())
    {
        data_.contentLength = std::stoul(it->second.value());
        data_.transferMode = TransferMode::ContentLength;
    }
    else
    {
        // 3. For responses without Transfer-Encoding or Content-Length, read until connection close
        data_.transferMode = TransferMode::UntilClose;
    }
}

void HttpParser<HttpResponse>::processConnectionClose()
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
}

void HttpParser<HttpResponse>::parseStatusLine(std::string_view line)
{
    size_t sp1 = line.find(' ');
    size_t sp2 = line.find(' ', sp1 + 1);

    data_.version = parseHttpVersion(line.substr(0, sp1));
    data_.statusCode = parseStatusCode(std::stoi(std::string(line.substr(sp1 + 1, sp2 - sp1 - 1))));
    data_.statusReason = line.substr(sp2 + 1);
}

void HttpParser<HttpResponse>::parseHeader(std::string_view line)
{
    auto header = parseHeaderLine(line);
    if (!header)
        return;

    if (header->nameCode() == HttpHeader::NameCode::SetCookie)
    {
        parseCookies(header->value());
    }
    else
    {
        data_.headers.emplace(header->name(), std::move(*header));
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

} // namespace nitrocoro::http
