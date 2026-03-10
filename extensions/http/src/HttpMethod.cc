/**
 * @file HttpMethod.cc
 * @brief HttpMethod registry implementation
 */
#include <nitrocoro/http/HttpTypes.h>

#include <cstdint>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace nitrocoro::http
{

namespace
{

static constexpr std::string_view standardMethodName(uint16_t id)
{
    switch (id)
    {
        case HttpMethod::Get:
            return "GET";
        case HttpMethod::Head:
            return "HEAD";
        case HttpMethod::Post:
            return "POST";
        case HttpMethod::Put:
            return "PUT";
        case HttpMethod::Delete:
            return "DELETE";
        case HttpMethod::Options:
            return "OPTIONS";
        case HttpMethod::Patch:
            return "PATCH";
        case HttpMethod::Trace:
            return "TRACE";
        case HttpMethod::Connect:
            return "CONNECT";
        default:
            return {};
    }
}

struct MethodRegistry
{
    std::vector<std::string> names;
    std::unordered_map<std::string, uint16_t> ids;

    MethodRegistry()
    {
        auto count = static_cast<uint16_t>(HttpMethod::_Invalid);
        for (uint16_t i = 0; i < count; ++i)
            add(std::string(standardMethodName(i)));
        add(""); // slot for _Invalid (id = count)
    }

    HttpMethod add(std::string name)
    {
        auto [it, inserted] = ids.emplace(name, static_cast<uint16_t>(names.size()));
        if (inserted)
            names.push_back(std::move(name));
        return { it->second };
    }

    static MethodRegistry & instance()
    {
        static MethodRegistry reg;
        return reg;
    }
};

} // namespace

std::string_view HttpMethod::toString() const
{
    auto & reg = MethodRegistry::instance();
    if (id < reg.names.size())
        return reg.names[id];
    return {};
}

HttpMethod HttpMethod::fromString(std::string_view s)
{
    auto & reg = MethodRegistry::instance();
    auto it = reg.ids.find(std::string(s));
    if (it == reg.ids.end())
        return methods::_Invalid;
    return { it->second };
}

HttpMethod HttpMethod::registerMethod(std::string name)
{
    return MethodRegistry::instance().add(std::move(name));
}

} // namespace nitrocoro::http
