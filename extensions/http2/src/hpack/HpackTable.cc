/**
 * @file HpackTable.cc
 */
#include "HpackTable.h"

#include <stdexcept>

namespace nitrocoro::http2::hpack
{

// RFC 7541 Appendix A - Static Table
const HeaderEntry HpackTable::kStaticTable[kStaticTableSize] = {
    { ":authority", "" },                   // 1
    { ":method", "GET" },                   // 2
    { ":method", "POST" },                  // 3
    { ":path", "/" },                       // 4
    { ":path", "/index.html" },             // 5
    { ":scheme", "http" },                  // 6
    { ":scheme", "https" },                 // 7
    { ":status", "200" },                   // 8
    { ":status", "204" },                   // 9
    { ":status", "206" },                   // 10
    { ":status", "304" },                   // 11
    { ":status", "400" },                   // 12
    { ":status", "404" },                   // 13
    { ":status", "500" },                   // 14
    { "accept-charset", "" },               // 15
    { "accept-encoding", "gzip, deflate" }, // 16
    { "accept-language", "" },              // 17
    { "accept-ranges", "" },                // 18
    { "accept", "" },                       // 19
    { "access-control-allow-origin", "" },  // 20
    { "age", "" },                          // 21
    { "allow", "" },                        // 22
    { "authorization", "" },                // 23
    { "cache-control", "" },                // 24
    { "content-disposition", "" },          // 25
    { "content-encoding", "" },             // 26
    { "content-language", "" },             // 27
    { "content-length", "" },               // 28
    { "content-location", "" },             // 29
    { "content-range", "" },                // 30
    { "content-type", "" },                 // 31
    { "cookie", "" },                       // 32
    { "date", "" },                         // 33
    { "etag", "" },                         // 34
    { "expect", "" },                       // 35
    { "expires", "" },                      // 36
    { "from", "" },                         // 37
    { "host", "" },                         // 38
    { "if-match", "" },                     // 39
    { "if-modified-since", "" },            // 40
    { "if-none-match", "" },                // 41
    { "if-range", "" },                     // 42
    { "if-unmodified-since", "" },          // 43
    { "last-modified", "" },                // 44
    { "link", "" },                         // 45
    { "location", "" },                     // 46
    { "max-forwards", "" },                 // 47
    { "proxy-authenticate", "" },           // 48
    { "proxy-authorization", "" },          // 49
    { "range", "" },                        // 50
    { "referer", "" },                      // 51
    { "refresh", "" },                      // 52
    { "retry-after", "" },                  // 53
    { "server", "" },                       // 54
    { "set-cookie", "" },                   // 55
    { "strict-transport-security", "" },    // 56
    { "transfer-encoding", "" },            // 57
    { "user-agent", "" },                   // 58
    { "vary", "" },                         // 59
    { "via", "" },                          // 60
    { "www-authenticate", "" },             // 61
};

HpackTable::HpackTable(size_t maxSize)
    : maxSize_(maxSize)
{
}

const HeaderEntry & HpackTable::get(size_t index) const
{
    if (index == 0 || index > kStaticTableSize + dynamic_.size())
        throw std::out_of_range("HPACK table index out of range");
    if (index <= kStaticTableSize)
        return kStaticTable[index - 1];
    return dynamic_[index - kStaticTableSize - 1];
}

int HpackTable::find(std::string_view name, std::string_view value) const
{
    int nameOnlyIdx = 0;
    // Search static table
    for (size_t i = 0; i < kStaticTableSize; ++i)
    {
        if (kStaticTable[i].name == name)
        {
            if (kStaticTable[i].value == value)
                return static_cast<int>(i + 1);
            if (nameOnlyIdx == 0)
                nameOnlyIdx = -static_cast<int>(i + 1);
        }
    }
    // Search dynamic table
    for (size_t i = 0; i < dynamic_.size(); ++i)
    {
        if (dynamic_[i].name == name)
        {
            if (dynamic_[i].value == value)
                return static_cast<int>(kStaticTableSize + i + 1);
            if (nameOnlyIdx == 0)
                nameOnlyIdx = -static_cast<int>(kStaticTableSize + i + 1);
        }
    }
    return nameOnlyIdx;
}

void HpackTable::insert(std::string name, std::string value)
{
    size_t sz = name.size() + value.size() + 32;
    if (sz > maxSize_)
    {
        // Entry too large: evict everything
        dynamic_.clear();
        currentSize_ = 0;
        return;
    }
    dynamic_.push_front({ std::move(name), std::move(value) });
    currentSize_ += sz;
    evict();
}

void HpackTable::setMaxSize(size_t maxSize)
{
    maxSize_ = maxSize;
    evict();
}

void HpackTable::evict()
{
    while (currentSize_ > maxSize_ && !dynamic_.empty())
    {
        currentSize_ -= entrySize(dynamic_.back());
        dynamic_.pop_back();
    }
}

} // namespace nitrocoro::http2::hpack
