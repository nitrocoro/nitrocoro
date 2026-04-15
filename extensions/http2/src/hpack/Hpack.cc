/**
 * @file Hpack.cc
 * @brief HPACK encoder/decoder implementation (RFC 7541)
 */
#include "Hpack.h"

#include <nitrocoro/http/HttpHeader.h>
#include <nitrocoro/http/HttpTypes.h>

#include <algorithm>
#include <stdexcept>

namespace nitrocoro::http2::hpack
{

// ── Huffman decode table (RFC 7541 Appendix B) ────────────────────────────────
// Each entry: { code, code_len, symbol }
// We use a simple canonical Huffman decoder via a lookup approach.

struct HuffSymbol
{
    uint32_t code;
    uint8_t bits;
};

// RFC 7541 Appendix B - 257 symbols (0-255 + EOS=256)
static const HuffSymbol kHuffTable[257] = {
    { 0x1ff8, 13 }, { 0x7fffd8, 23 }, { 0xfffffe2, 28 }, { 0xfffffe3, 28 }, { 0xfffffe4, 28 }, { 0xfffffe5, 28 }, { 0xfffffe6, 28 }, { 0xfffffe7, 28 }, { 0xfffffe8, 28 }, { 0xffffea, 24 }, { 0x3ffffffc, 30 }, { 0xfffffe9, 28 }, { 0xfffffea, 28 }, { 0x3ffffffd, 30 }, { 0xfffffeb, 28 }, { 0xfffffec, 28 }, { 0xfffffed, 28 }, { 0xfffffee, 28 }, { 0xfffffef, 28 }, { 0xffffff0, 28 }, { 0xffffff1, 28 }, { 0xffffff2, 28 }, { 0x3ffffffe, 30 }, { 0xffffff3, 28 }, { 0xffffff4, 28 }, { 0xffffff5, 28 }, { 0xffffff6, 28 }, { 0xffffff7, 28 }, { 0xffffff8, 28 }, { 0xffffff9, 28 }, { 0xffffffa, 28 }, { 0xffffffb, 28 }, { 0x14, 6 }, { 0x3f8, 10 }, { 0x3f9, 10 }, { 0x7fa, 11 }, { 0x1ff9, 13 }, { 0x15, 6 }, { 0xf8, 8 }, { 0x7fb, 11 }, { 0x7fc, 11 }, { 0x7fd, 11 }, { 0x7fe, 11 }, { 0x7ff, 11 }, { 0x55, 7 }, { 0x56, 7 }, { 0x57, 7 }, { 0x58, 7 }, { 0x0, 5 }, { 0x1, 5 }, { 0x2, 5 }, { 0x19, 6 }, { 0x1a, 6 }, { 0x1b, 6 }, { 0x1c, 6 }, { 0x1d, 6 }, { 0x1e, 6 }, { 0x1f, 6 }, { 0x5c, 7 }, { 0xfb, 8 }, { 0x7ffd, 15 }, { 0x59, 7 }, { 0x7ffe, 15 }, { 0x3fa, 10 }, { 0x1ffa, 13 }, { 0x21, 6 }, { 0x5d, 7 }, { 0x5e, 7 }, { 0x5f, 7 }, { 0x60, 7 }, { 0x61, 7 }, { 0x62, 7 }, { 0x63, 7 }, { 0x64, 7 }, { 0x65, 7 }, { 0x66, 7 }, { 0x67, 7 }, { 0x68, 7 }, { 0x69, 7 }, { 0x6a, 7 }, { 0x6b, 7 }, { 0x6c, 7 }, { 0x6d, 7 }, { 0x6e, 7 }, { 0x6f, 7 }, { 0x70, 7 }, { 0x71, 7 }, { 0x72, 7 }, { 0xfc, 8 }, { 0x73, 7 }, { 0xfd, 8 }, { 0x1ffb, 13 }, { 0x7fff0, 19 }, { 0x1ffc, 13 }, { 0x3ffb, 14 }, { 0x22, 6 }, { 0x7ffc, 15 }, { 0x3, 5 }, { 0x23, 6 }, { 0x4, 5 }, { 0x24, 6 }, { 0x5, 5 }, { 0x25, 6 }, { 0x26, 6 }, { 0x27, 6 }, { 0x6, 5 }, { 0x74, 7 }, { 0x75, 7 }, { 0x28, 6 }, { 0x29, 6 }, { 0x2a, 6 }, { 0x7, 5 }, { 0x2b, 6 }, { 0x76, 7 }, { 0x2c, 6 }, { 0x8, 5 }, { 0x9, 5 }, { 0x2d, 6 }, { 0x77, 7 }, { 0x78, 7 }, { 0x79, 7 }, { 0x7a, 7 }, { 0x7b, 7 }, { 0x7fff1, 19 }, { 0xffffe6, 24 }, { 0x7fff2, 19 }, { 0xffffe7, 24 }, { 0xffffe8, 24 }, { 0xffffe9, 24 }, { 0xffffea, 24 }, { 0xffffeb, 24 }, { 0xffffec, 24 }, { 0xffffed, 24 }, { 0xffffee, 24 }, { 0xffffef, 24 }, { 0xfffff0, 24 }, { 0xfffff1, 24 }, { 0xfffff2, 24 }, { 0xfffff3, 24 }, { 0xfffff4, 24 }, { 0xfffff5, 24 }, { 0xfffff6, 24 }, { 0xfffff7, 24 }, { 0xfffff8, 24 }, { 0xfffff9, 24 }, { 0xfffffa, 24 }, { 0xfffffb, 24 }, { 0xfffffc, 24 }, { 0xfffffd, 24 }, { 0xfffffe, 24 }, { 0xffffff, 24 }, { 0x3ffffef, 26 }, { 0x3fffff0, 26 }, { 0x3fffff1, 26 }, { 0x3fffff2, 26 }, { 0x3fffff3, 26 }, { 0x3fffff4, 26 }, { 0x3fffff5, 26 }, { 0x3fffff6, 26 }, { 0x3fffff7, 26 }, { 0x3fffff8, 26 }, { 0x3fffff9, 26 }, { 0x3fffffa, 26 }, { 0x3fffffb, 26 }, { 0x3fffffc, 26 }, { 0x3fffffd, 26 }, { 0x3fffffe, 26 }, { 0x3ffffff, 26 }, { 0x3fffff, 22 }, { 0x3fffffe, 26 }, { 0x3ffffff0, 26 }, { 0x3ffffff1, 26 }, { 0x3ffffff2, 26 }, { 0x3ffffff3, 26 }, { 0x3ffffff4, 26 }, { 0x3ffffff5, 26 }, { 0x3ffffff6, 26 }, { 0x3ffffff7, 26 }, { 0x3ffffff8, 26 }, { 0x3ffffff9, 26 }, { 0x3ffffffa, 26 }, { 0x3ffffffb, 26 }, { 0x3ffffffc, 26 }, { 0x3ffffffd, 26 }, { 0x3ffffffe, 30 }, // EOS (256)
};

static std::string huffmanDecode(const uint8_t * data, size_t len)
{
    // Build a simple decode: accumulate bits, match against table
    uint64_t bits = 0;
    int bitsLeft = 0;
    std::string result;
    result.reserve(len);

    size_t pos = 0;
    while (pos < len || bitsLeft > 0)
    {
        // Fill up to 32 bits
        while (bitsLeft < 32 && pos < len)
        {
            bits = (bits << 8) | data[pos++];
            bitsLeft += 8;
        }

        // Try to match a symbol
        bool matched = false;
        for (int sym = 0; sym < 256; ++sym)
        {
            int nb = kHuffTable[sym].bits;
            if (nb > bitsLeft)
                continue;
            uint64_t mask = (uint64_t(1) << nb) - 1;
            uint64_t code = (bits >> (bitsLeft - nb)) & mask;
            if (code == kHuffTable[sym].code)
            {
                result += static_cast<char>(sym);
                bitsLeft -= nb;
                matched = true;
                break;
            }
        }
        if (!matched)
        {
            // Remaining bits must be EOS padding (all 1s)
            if (bitsLeft > 7)
                throw std::runtime_error("HPACK: Huffman decode error");
            break;
        }
    }
    return result;
}

// ── HpackDecoder ──────────────────────────────────────────────────────────────

HpackDecoder::HpackDecoder(size_t maxTableSize)
    : table_(maxTableSize)
{
}

uint64_t HpackDecoder::decodeInt(const uint8_t * data, size_t len, size_t & pos, uint8_t prefixBits)
{
    uint64_t mask = (1u << prefixBits) - 1;
    uint64_t value = data[pos++] & mask;
    if (value < mask)
        return value;
    uint64_t m = 0;
    while (pos < len)
    {
        uint8_t b = data[pos++];
        value += uint64_t(b & 0x7f) << m;
        m += 7;
        if (!(b & 0x80))
            break;
    }
    return value;
}

std::string HpackDecoder::decodeStr(const uint8_t * data, size_t len, size_t & pos)
{
    if (pos >= len)
        throw std::runtime_error("HPACK: unexpected end of data");
    bool huffman = (data[pos] & 0x80) != 0;
    uint64_t strLen = decodeInt(data, len, pos, 7);
    if (pos + strLen > len)
        throw std::runtime_error("HPACK: string length exceeds buffer");
    std::string result;
    if (huffman)
        result = huffmanDecode(data + pos, strLen);
    else
        result.assign(reinterpret_cast<const char *>(data + pos), strLen);
    pos += strLen;
    return result;
}

void HpackDecoder::applyHeader(DecodedHeaders & out, std::string name, std::string value)
{
    if (name == ":method")
        out.method = std::move(value);
    else if (name == ":path")
        out.path = std::move(value);
    else if (name == ":scheme")
        out.scheme = std::move(value);
    else if (name == ":authority")
        out.authority = std::move(value);
    else if (name == ":status")
        out.status = std::move(value);
    else
    {
        http::HttpHeader hdr(name, value);
        out.headers.insert_or_assign(hdr.name(), std::move(hdr));
    }
}

DecodedHeaders HpackDecoder::decode(const uint8_t * data, size_t len)
{
    DecodedHeaders out;
    size_t pos = 0;

    while (pos < len)
    {
        uint8_t first = data[pos];

        if (first & 0x80)
        {
            // Indexed Header Field (RFC 7541 §6.1)
            uint64_t idx = decodeInt(data, len, pos, 7);
            if (idx == 0)
                throw std::runtime_error("HPACK: index 0 is invalid");
            const auto & entry = table_.get(idx);
            applyHeader(out, entry.name, entry.value);
        }
        else if ((first & 0xc0) == 0x40)
        {
            // Literal with Incremental Indexing (RFC 7541 §6.2.1)
            uint64_t idx = decodeInt(data, len, pos, 6);
            std::string name, value;
            if (idx == 0)
                name = decodeStr(data, len, pos);
            else
                name = table_.get(idx).name;
            value = decodeStr(data, len, pos);
            table_.insert(name, value);
            applyHeader(out, std::move(name), std::move(value));
        }
        else if ((first & 0xe0) == 0x20)
        {
            // Dynamic Table Size Update (RFC 7541 §6.3)
            uint64_t newSize = decodeInt(data, len, pos, 5);
            table_.setMaxSize(newSize);
        }
        else
        {
            // Literal without indexing / never indexed (RFC 7541 §6.2.2, §6.2.3)
            uint8_t prefixBits = ((first & 0xf0) == 0x10) ? 4 : 4;
            uint64_t idx = decodeInt(data, len, pos, prefixBits);
            std::string name, value;
            if (idx == 0)
                name = decodeStr(data, len, pos);
            else
                name = table_.get(idx).name;
            value = decodeStr(data, len, pos);
            applyHeader(out, std::move(name), std::move(value));
        }
    }
    return out;
}

// ── HpackEncoder ──────────────────────────────────────────────────────────────

HpackEncoder::HpackEncoder(size_t maxTableSize)
    : table_(maxTableSize)
{
}

void HpackEncoder::encodeInt(std::vector<uint8_t> & out, uint64_t value,
                             uint8_t prefixBits, uint8_t firstByte)
{
    uint64_t maxVal = (1u << prefixBits) - 1;
    if (value < maxVal)
    {
        out.push_back(firstByte | static_cast<uint8_t>(value));
        return;
    }
    out.push_back(firstByte | static_cast<uint8_t>(maxVal));
    value -= maxVal;
    while (value >= 0x80)
    {
        out.push_back(static_cast<uint8_t>((value & 0x7f) | 0x80));
        value >>= 7;
    }
    out.push_back(static_cast<uint8_t>(value));
}

void HpackEncoder::encodeStr(std::vector<uint8_t> & out, std::string_view s)
{
    // Literal, no Huffman encoding
    encodeInt(out, s.size(), 7, 0x00);
    out.insert(out.end(), s.begin(), s.end());
}

std::vector<uint8_t> HpackEncoder::encodeResponse(const http::HttpResponse & resp)
{
    std::vector<uint8_t> out;
    out.reserve(128);

    std::string statusStr = std::to_string(resp.statusCode);
    int idx = table_.find(":status", statusStr);
    if (idx > 0)
    {
        encodeInt(out, idx, 7, 0x80);
    }
    else
    {
        int nameIdx = (idx < 0) ? -idx : 0;
        encodeInt(out, nameIdx, 6, 0x40);
        if (nameIdx == 0)
            encodeStr(out, ":status");
        encodeStr(out, statusStr);
        table_.insert(":status", statusStr);
    }

    for (const auto & [key, hdr] : resp.headers)
    {
        if (hdr.nameCode() == http::HttpHeader::NameCode::ContentLength
            || hdr.nameCode() == http::HttpHeader::NameCode::TransferEncoding
            || hdr.nameCode() == http::HttpHeader::NameCode::Connection)
            continue;

        std::string_view lname = hdr.name(); // already lowercase in our impl
        std::string_view val = hdr.value();

        int hidx = table_.find(lname, val);
        if (hidx > 0)
        {
            encodeInt(out, hidx, 7, 0x80);
        }
        else
        {
            int nidx = (hidx < 0) ? -hidx : 0;
            encodeInt(out, nidx, 6, 0x40);
            if (nidx == 0)
                encodeStr(out, lname);
            encodeStr(out, val);
            table_.insert(std::string(lname), std::string(val));
        }
    }

    if (resp.contentLength > 0)
    {
        std::string val = std::to_string(resp.contentLength);
        encodeInt(out, 0, 6, 0x40);
        encodeStr(out, "content-length");
        encodeStr(out, val);
    }

    for (const auto & cookie : resp.cookies)
    {
        std::string val = cookie.toString();
        encodeInt(out, 0, 6, 0x40);
        encodeStr(out, "set-cookie");
        encodeStr(out, val);
    }

    return out;
}

} // namespace nitrocoro::http2::hpack
