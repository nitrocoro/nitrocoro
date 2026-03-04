#pragma once

#include <type_traits>

namespace nitrocoro
{

namespace detail
{

// priority tag
template <int N>
struct Tag : Tag<N - 1>
{
};
template <>
struct Tag<0>
{
};

template <typename T>
auto getAwaiterImpl(T && value, Tag<2>) noexcept(
    noexcept(static_cast<T &&>(value).operator co_await()))
    -> decltype(static_cast<T &&>(value).operator co_await())
{
    return static_cast<T &&>(value).operator co_await();
}

template <typename T>
auto getAwaiterImpl(T && value, Tag<1>) noexcept(
    noexcept(operator co_await(static_cast<T &&>(value))))
    -> decltype(operator co_await(static_cast<T &&>(value)))
{
    return operator co_await(static_cast<T &&>(value));
}

template <typename T>
auto getAwaiterImpl(T && value, Tag<0>) noexcept
    -> decltype(static_cast<T &&>(value).await_ready(), static_cast<T &&>(value))
{
    return static_cast<T &&>(value);
}

template <typename T>
auto getAwaiter(T && value) noexcept(
    noexcept(getAwaiterImpl(static_cast<T &&>(value), Tag<2>{})))
    -> decltype(getAwaiterImpl(static_cast<T &&>(value), Tag<2>{}))
{
    return getAwaiterImpl(static_cast<T &&>(value), Tag<2>{});
}

} // namespace detail

template <typename T>
using void_to_false_t = std::conditional_t<std::is_same_v<T, void>, std::false_type, T>;

template <typename T>
struct await_result
{
    using awaiter_t = decltype(detail::getAwaiter(std::declval<T>()));
    using type = decltype(std::declval<awaiter_t>().await_resume());
};

template <typename T>
using await_result_t = typename await_result<T>::type;

template <typename T, typename = std::void_t<>>
struct is_awaitable : std::false_type
{
};

template <typename T>
struct is_awaitable<
    T,
    std::void_t<decltype(detail::getAwaiter(std::declval<T>()))>>
    : std::true_type
{
};

template <typename T>
constexpr bool is_awaitable_v = is_awaitable<T>::value;

} // namespace nitrocoro
