/**
 * @file Hpack.cc
 * @brief HPACK encoder/decoder implementation (RFC 7541)
 */
#include "Hpack.h"
#include "HuffmanTable.h"

#include <nitrocoro/http/HttpHeader.h>
#include <nitrocoro/http/HttpTypes.h>

#include <algorithm>
#include <nitrocoro/utils/Debug.h>
#include <stdexcept>

namespace nitrocoro::http2::hpack
{

static std::string huffmanDecode(const uint8_t * data, size_t len)
{
    std::string result;
    result.reserve(len * 8 / 5);
    HuffDecode t = { 0, static_cast<uint8_t>(HuffDecodeFlag::Accepted), 0 };
    for (size_t i = 0; i < len; ++i)
    {
        uint8_t c = data[i];
        t = kHuffDecodeTable[t.fstate][c >> 4];
        if (t.fstate == 0x100)
            throw std::runtime_error("HPACK: Huffman decode error");
        if (t.flags & static_cast<uint8_t>(HuffDecodeFlag::Sym))
            result += static_cast<char>(t.sym);
        t = kHuffDecodeTable[t.fstate][c & 0xf];
        if (t.fstate == 0x100)
            throw std::runtime_error("HPACK: Huffman decode error");
        if (t.flags & static_cast<uint8_t>(HuffDecodeFlag::Sym))
            result += static_cast<char>(t.sym);
    }
    if (!(t.flags & static_cast<uint8_t>(HuffDecodeFlag::Accepted)))
        throw std::runtime_error("HPACK: Huffman decode error");
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
    uint64_t code = 0;
    size_t nbits = 0;
    size_t startSize = out.size();
    out.push_back(0); // placeholder for length prefix
    for (unsigned char c : s)
    {
        const HuffSym & sym = kHuffSymTable[c];
        code |= (uint64_t)sym.code << (32 - nbits);
        nbits += sym.nbits;
        while (nbits >= 8)
        {
            out.push_back((uint8_t)(code >> 56));
            code <<= 8;
            nbits -= 8;
        }
    }
    if (nbits > 0)
        out.push_back((uint8_t)(code >> 56) | ((1 << (8 - nbits)) - 1));
    size_t huffLen = out.size() - startSize - 1;
    if (huffLen < s.size())
    {
        // Huffman is shorter: write length with H-bit set
        uint8_t tmp = out[startSize];
        (void)tmp;
        out.erase(out.begin() + startSize);
        std::vector<uint8_t> lenBytes;
        encodeInt(lenBytes, huffLen, 7, 0x80);
        out.insert(out.begin() + startSize, lenBytes.begin(), lenBytes.end());
    }
    else
    {
        // Huffman is not shorter: revert to literal
        out.resize(startSize);
        encodeInt(out, s.size(), 7, 0x00);
        out.insert(out.end(), s.begin(), s.end());
    }
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
