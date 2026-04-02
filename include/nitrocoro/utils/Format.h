/**
 * @file Format.h
 * @brief fmt::format-style string formatting with compile-time format string parsing.
 *
 * Usage:
 *   format("Hello, {}! You are {} years old.", name, age)
 *
 * The format string is parsed at compile time. Placeholder count mismatches
 * are caught at compile time.
 */
#pragma once

#include <charconv>
#include <string>
#include <string_view>
#include <type_traits>

namespace nitrocoro::utils
{

namespace detail
{

consteval std::size_t formatCountArgs(std::string_view fmt)
{
    std::size_t n = 0;
    for (std::size_t i = 0; i + 1 < fmt.size(); ++i)
        if (fmt[i] == '{' && fmt[i + 1] == '}')
        {
            ++n;
            ++i;
        }
    return n;
}

template <typename T>
void formatAppendArg(std::string & out, T && val)
{
    using D = std::decay_t<T>;
    if constexpr (std::is_same_v<D, std::string> || std::is_same_v<D, std::string_view>)
        out += val;
    else if constexpr (std::is_same_v<D, const char *> || std::is_same_v<D, char *>)
        out += val ? val : "(null)";
    else if constexpr (std::is_same_v<D, char>)
        out += val;
    else if constexpr (std::is_same_v<D, bool>)
        out += val ? "true" : "false";
    else if constexpr (std::is_integral_v<D> || std::is_floating_point_v<D>)
    {
        char buf[64];
        auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), val);
        if (ec == std::errc{}) out.append(buf, ptr);
    }
    else
        out += std::string(std::forward<T>(val));
}

} // namespace detail

// Carries the compile-time parsed format string.
// The consteval constructor enables implicit conversion from string literals.
template <typename... Args>
struct FormatString
{
    static constexpr std::size_t argCount = sizeof...(Args);

    template <std::size_t M>
    consteval FormatString(const char (&fmt)[M])
        : sv_(fmt, M - 1)
    {
        static_assert(detail::formatCountArgs(std::string_view(fmt, M - 1)) == sizeof...(Args),
            "nitrocoro::format: argument count does not match placeholder count");
    }

    std::string_view sv_;
};

template <typename... Args>
std::string format(FormatString<std::type_identity_t<Args>...> fmt, Args &&... args)
{
    std::string result;
    result.reserve(fmt.sv_.size() + 16);

    std::size_t argIdx = 0;
    std::size_t i = 0;
    const auto & sv = fmt.sv_;

    auto appendNth = [&](std::size_t n)
    {
        [&]<std::size_t... Is>(std::index_sequence<Is...>)
        {
            auto tup = std::forward_as_tuple(std::forward<Args>(args)...);
            ((Is == n ? (detail::formatAppendArg(result, std::get<Is>(tup)), true) : false) || ...);
        }(std::index_sequence_for<Args...>{});
    };

    std::size_t litStart = 0;
    while (i < sv.size())
    {
        if (sv[i] == '{' && i + 1 < sv.size() && sv[i + 1] == '}')
        {
            result.append(sv.data() + litStart, i - litStart);
            appendNth(argIdx++);
            i += 2;
            litStart = i;
        }
        else
            ++i;
    }
    result.append(sv.data() + litStart, sv.size() - litStart);
    return result;
}

} // namespace nitrocoro
