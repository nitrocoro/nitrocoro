#include <nitrocoro/utils/UrlEncode.h>

namespace nitrocoro::utils
{

static constexpr char kHex[] = "0123456789ABCDEF";

static bool isUnreserved(char c)
{
    return (c >= 'A' && c <= 'Z')
           || (c >= 'a' && c <= 'z')
           || (c >= '0' && c <= '9')
           || c == '-' || c == '_' || c == '.' || c == '~';
}

std::string formEncode(std::string_view input)
{
    std::string result;
    result.reserve(input.size());
    for (char c : input)
    {
        if (isUnreserved(c))
            result += c;
        else if (c == ' ')
            result += '+';
        else
        {
            result += '%';
            result += kHex[c >> 4];
            result += kHex[c & 0xf];
        }
    }
    return result;
}

std::string formDecode(std::string_view input)
{
    std::string result;
    result.reserve(input.size());
    for (size_t i = 0; i < input.size(); ++i)
    {
        if (input[i] == '+')
            result += ' ';
        else if (input[i] == '%' && i + 2 < input.size())
        {
            auto hexVal = [](char c) -> int {
                if (c >= '0' && c <= '9')
                    return c - '0';
                if (c >= 'A' && c <= 'F')
                    return c - 'A' + 10;
                if (c >= 'a' && c <= 'f')
                    return c - 'a' + 10;
                return -1;
            };
            int hi = hexVal(input[i + 1]);
            int lo = hexVal(input[i + 2]);
            if (hi >= 0 && lo >= 0)
            {
                result += (char)((hi << 4) | lo);
                i += 2;
            }
            else
                result += input[i];
        }
        else
            result += input[i];
    }
    return result;
}


bool needsUrlDecoding(std::string_view input)
{
    return input.find('%') != std::string_view::npos;
}

std::string urlEncodeComponent(std::string_view input)
{
    std::string result;
    result.reserve(input.size());
    for (char c : input)
    {
        if (isUnreserved(c))
            result += c;
        else
        {
            result += '%';
            result += kHex[c >> 4];
            result += kHex[c & 0xf];
        }
    }
    return result;
}

std::string urlDecodeComponent(std::string_view input)
{
    std::string result;
    result.reserve(input.size());
    for (size_t i = 0; i < input.size(); ++i)
    {
        if (input[i] == '%' && i + 2 < input.size())
        {
            auto hexVal = [](char c) -> int {
                if (c >= '0' && c <= '9')
                    return c - '0';
                if (c >= 'A' && c <= 'F')
                    return c - 'A' + 10;
                if (c >= 'a' && c <= 'f')
                    return c - 'a' + 10;
                return -1;
            };
            int hi = hexVal(input[i + 1]);
            int lo = hexVal(input[i + 2]);
            if (hi >= 0 && lo >= 0)
            {
                result += static_cast<char>((hi << 4) | lo);
                i += 2;
            }
            else
                result += input[i];
        }
        else
            result += input[i];
    }
    return result;
}

} // namespace nitrocoro::utils
