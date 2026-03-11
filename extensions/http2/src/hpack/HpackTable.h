/**
 * @file HpackTable.h
 * @brief HPACK header table (RFC 7541 §2.3)
 */
#pragma once

#include <deque>
#include <string>
#include <string_view>

namespace nitrocoro::http2::hpack
{

struct HeaderEntry
{
    std::string name;
    std::string value;
};

// RFC 7541 §4.1: entry size = name.size() + value.size() + 32
inline size_t entrySize(const HeaderEntry & e)
{
    return e.name.size() + e.value.size() + 32;
}

class HpackTable
{
public:
    static constexpr size_t kDefaultMaxSize = 4096;
    static constexpr size_t kStaticTableSize = 61;

    explicit HpackTable(size_t maxSize = kDefaultMaxSize);

    // Look up by 1-based index (1..61 = static, 62+ = dynamic)
    const HeaderEntry & get(size_t index) const;

    // Find index for name+value (returns 0 if not found, negative if name-only match)
    // Returns: >0 full match, <0 name-only match (abs value = index), 0 = not found
    int find(std::string_view name, std::string_view value) const;

    void insert(std::string name, std::string value);
    void setMaxSize(size_t maxSize);
    size_t maxSize() const { return maxSize_; }
    size_t currentSize() const { return currentSize_; }
    size_t dynamicCount() const { return dynamic_.size(); }

private:
    static const HeaderEntry kStaticTable[kStaticTableSize];

    void evict();

    size_t maxSize_;
    size_t currentSize_{ 0 };
    std::deque<HeaderEntry> dynamic_; // front = most recently added (index 62)
};

} // namespace nitrocoro::http2::hpack
