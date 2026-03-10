#pragma once

#include <string>
#include <string_view>

namespace nitrocoro::utils
{

// RFC 3986 component encoding: space -> %20, encodes all except unreserved chars
// Use for URL path segments and query parameter values
std::string urlEncodeComponent(std::string_view input);
std::string urlDecodeComponent(std::string_view input);
bool needsUrlDecoding(std::string_view input);

// HTML application/x-www-form-urlencoded: space -> '+'
// Use for form body encoding/decoding
std::string formEncode(std::string_view input);
std::string formDecode(std::string_view input);

} // namespace nitrocoro::utils
