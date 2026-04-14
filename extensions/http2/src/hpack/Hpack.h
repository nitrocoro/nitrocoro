/**
 * @file Hpack.h
 * @brief HPACK encoder and decoder (RFC 7541)
 */
#pragma once

#include "HpackTable.h"
#include <nitrocoro/http/HttpMessage.h>

#include <cstdint>
#include <string>
#include <vector>

namespace nitrocoro::http2::hpack
{

// Decoded pseudo-headers + regular headers from a HEADERS block
struct DecodedHeaders
{
    std::string method;
    std::string path;
    std::string scheme;
    std::string authority;
    std::string status; // for responses
    http::HttpHeaderMap headers;
};

class HpackDecoder
{
public:
    explicit HpackDecoder(size_t maxTableSize = HpackTable::kDefaultMaxSize);

    // Decode a complete header block (may span multiple HEADERS+CONTINUATION frames).
    // Throws on protocol error.
    DecodedHeaders decode(const uint8_t * data, size_t len);

private:
    uint64_t decodeInt(const uint8_t * data, size_t len, size_t & pos, uint8_t prefixBits);
    std::string decodeStr(const uint8_t * data, size_t len, size_t & pos);
    void applyHeader(DecodedHeaders & out, std::string name, std::string value);

    HpackTable table_;
};

class HpackEncoder
{
public:
    explicit HpackEncoder(size_t maxTableSize = HpackTable::kDefaultMaxSize);

    // Encode response headers. Uses literal without indexing for simplicity.
    std::vector<uint8_t> encodeResponse(uint16_t statusCode,
                                        const http::HttpHeaderMap & headers);

private:
    void encodeInt(std::vector<uint8_t> & out, uint64_t value, uint8_t prefixBits, uint8_t firstByte);
    void encodeStr(std::vector<uint8_t> & out, std::string_view s);

    HpackTable table_;
};

} // namespace nitrocoro::http2::hpack
