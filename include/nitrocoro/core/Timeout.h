/**
 * @file Timeout.h
 * @brief with_timeout — wraps any co_await-able with a deadline.
 */
#pragma once

#include <nitrocoro/core/CoroTraits.h>
#include <nitrocoro/core/Scheduler.h>
#include <nitrocoro/core/Task.h>

#include <atomic>
#include <chrono>
#include <coroutine>
#include <memory>
#include <optional>
#include <stdexcept>

namespace nitrocoro
{

struct TimeoutException : std::runtime_error
{
    TimeoutException()
        : std::runtime_error("operation timed out") {}
};

namespace detail
{

// Shared state between the inner task and the timer.
template <typename T>
struct TimeoutState
{
    std::atomic<bool> done_{ false }; // first to win sets this
    std::coroutine_handle<> caller_;
    std::optional<T> value_;
    std::exception_ptr exception_;
    bool timed_out_{ false };
};

template <>
struct TimeoutState<void>
{
    std::atomic<bool> done_{ false };
    std::coroutine_handle<> caller_;
    std::exception_ptr exception_;
    bool timed_out_{ false };
};

// Timer coroutine: fires after deadline, resumes caller if it wins the race.
template <typename T>
Task<> timeoutTimer(std::shared_ptr<TimeoutState<T>> state, TimePoint deadline)
{
    co_await Scheduler::current()->sleep_until(deadline);
    bool expected = false;
    if (state->done_.compare_exchange_strong(expected, true))
    {
        state->timed_out_ = true;
        Scheduler::current()->schedule(state->caller_);
    }
}

// Inner task: co_awaits the original awaitable, resumes caller if it wins.
template <typename Awaitable, typename T>
Task<> timeoutInner(std::shared_ptr<TimeoutState<T>> state, Awaitable awaitable)
{
    try
    {
        if constexpr (std::is_void_v<T>)
        {
            co_await std::move(awaitable);
        }
        else
        {
            state->value_ = co_await std::move(awaitable);
        }
    }
    catch (...)
    {
        state->exception_ = std::current_exception();
    }

    bool expected = false;
    if (state->done_.compare_exchange_strong(expected, true))
    {
        Scheduler::current()->schedule(state->caller_);
    }
}

template <typename Awaitable>
struct [[nodiscard]] TimeoutAwaiter
{
    using T = await_result_t<Awaitable>;

    Awaitable awaitable_;
    TimePoint deadline_;
    std::shared_ptr<TimeoutState<T>> state_;

    TimeoutAwaiter(Awaitable && a, TimePoint deadline)
        : awaitable_(std::forward<Awaitable>(a))
        , deadline_(deadline)
        , state_(std::make_shared<TimeoutState<T>>())
    {
    }

    bool await_ready() const noexcept { return false; }

    void await_suspend(std::coroutine_handle<> h)
    {
        state_->caller_ = h;
        auto * sched = Scheduler::current();
        sched->spawn([state = state_, awaitable = std::move(awaitable_)]() mutable -> Task<> {
            co_await timeoutInner<Awaitable, T>(state, std::move(awaitable));
        });
        sched->spawn([state = state_, deadline = deadline_]() mutable -> Task<> {
            co_await timeoutTimer<T>(state, deadline);
        });
    }

    auto await_resume()
    {
        if (state_->timed_out_)
            throw TimeoutException{};
        if (state_->exception_)
            std::rethrow_exception(state_->exception_);
        if constexpr (!std::is_void_v<T>)
            return std::move(*state_->value_);
    }
};

} // namespace detail

template <typename Awaitable>
auto withTimeout(Awaitable && a, double seconds)
{
    using TimeoutAwaiter = detail::TimeoutAwaiter<std::decay_t<Awaitable>>;

    auto deadline = std::chrono::steady_clock::now()
                    + std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::duration<double>(seconds));
    return TimeoutAwaiter(std::forward<Awaitable>(a), deadline);
}

template <typename Awaitable>
auto withTimeout(Awaitable && a, std::chrono::steady_clock::duration dur)
{
    using TimeoutAwaiter = detail::TimeoutAwaiter<std::decay_t<Awaitable>>;

    return TimeoutAwaiter(std::forward<Awaitable>(a), std::chrono::steady_clock::now() + dur);
}

} // namespace nitrocoro
