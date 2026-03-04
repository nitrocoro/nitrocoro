#include "nitrocoro/net/Url.h"
#include <algorithm>

namespace nitrocoro::net
{

Url::Url(std::string_view url)
{
    parse(url);
}

void Url::parse(std::string_view url)
{
    if (url.empty())
    {
        return;
    }

    size_t pos = 0;

    // Parse scheme: "http://" or "https://"
    size_t schemeEnd = url.find("://", pos);
    if (schemeEnd == std::string_view::npos)
    {
        return;
    }
    scheme_ = url.substr(pos, schemeEnd);
    std::transform(scheme_.begin(), scheme_.end(), scheme_.begin(), ::tolower);
    pos = schemeEnd + 3;

    // Parse host and optional port
    size_t pathStart = url.find('/', pos);
    size_t queryStart = url.find('?', pos);
    size_t hostEnd = std::min(pathStart, queryStart);
    if (hostEnd == std::string_view::npos)
    {
        hostEnd = url.size();
    }

    std::string_view hostPart = url.substr(pos, hostEnd - pos);
    size_t portPos = hostPart.find(':');
    if (portPos != std::string_view::npos)
    {
        host_ = hostPart.substr(0, portPos);
        std::string_view portStr = hostPart.substr(portPos + 1);
        port_ = 0;
        for (char c : portStr)
        {
            if (c < '0' || c > '9')
            {
                return;
            }
            port_ = port_ * 10 + (c - '0');
        }
    }
    else
    {
        host_ = hostPart;
        port_ = getDefaultPort();
    }

    pos = hostEnd;

    // Parse path
    if (pos < url.size() && url[pos] == '/')
    {
        size_t pathEnd = url.find('?', pos);
        if (pathEnd == std::string_view::npos)
        {
            path_ = url.substr(pos);
        }
        else
        {
            path_ = url.substr(pos, pathEnd - pos);
            pos = pathEnd;
        }
    }
    else
    {
        path_ = "/";
    }

    // Parse query
    if (pos < url.size() && url[pos] == '?')
    {
        query_ = url.substr(pos + 1);
    }

    valid_ = !scheme_.empty() && !host_.empty() && port_ > 0;
}

uint16_t Url::getDefaultPort() const
{
    if (scheme_ == "http" || scheme_ == "ws")
    {
        return 80;
    }
    else if (scheme_ == "https" || scheme_ == "wss")
    {
        return 443;
    }
    return 0;
}

} // namespace nitrocoro::net
