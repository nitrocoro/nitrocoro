/**
 * @file HuffmanTable.h
 * @brief HPACK Huffman encode/decode tables (RFC 7541 Appendix B)
 */
#pragma once

#include <cstddef>
#include <cstdint>

namespace nitrocoro::http2::hpack
{

enum class HuffDecodeFlag : uint8_t
{
    Accepted = 1,
    Sym = 1 << 1,
};

struct HuffDecode
{
    uint16_t fstate;
    uint8_t flags;
    uint8_t sym;
};

struct HuffSym
{
    uint32_t nbits;
    /* Huffman code aligned to LSB */
    uint32_t code;
};

extern const HuffSym kHuffSymTable[];
extern const HuffDecode kHuffDecodeTable[][16];

} // namespace nitrocoro::http2::hpack
