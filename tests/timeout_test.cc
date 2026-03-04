/**
 * @file timeout_test.cc
 * @brief Tests for with_timeout.
 */
#include <nitrocoro/core/Future.h>
#include <nitrocoro/core/Scheduler.h>
#include <nitrocoro/core/Task.h>
#include <nitrocoro/core/Timeout.h>
#include <nitrocoro/testing/Test.h>

using namespace nitrocoro;

/** Operation completes before timeout: result is returned normally. */
NITRO_TEST(timeout_completes_in_time)
{
    co_await withTimeout(Scheduler::current()->sleep_for(0.01), 1.0);
    co_return;
}

/** Operation exceeds timeout: TimeoutException is thrown. */
NITRO_TEST(timeout_fires)
{
    NITRO_CHECK_THROWS_AS(
        co_await withTimeout(Scheduler::current()->sleep_for(1.0), 0.02),
        TimeoutException);
}

/** with_timeout propagates the return value of the awaitable. */
NITRO_TEST(timeout_returns_value)
{
    Promise<int> p;
    auto f = p.get_future();
    Scheduler::current()->spawn([TEST_CTX, p = std::move(p)]() mutable -> Task<> {
        co_await Scheduler::current()->sleep_for(0.01);
        p.set_value(42);
    });
    NITRO_CHECK_EQ(co_await withTimeout(f.get(), 1.0), 42);
}

/** with_timeout propagates exceptions from the inner awaitable. */
NITRO_TEST(timeout_propagates_exception)
{
    Promise<int> p;
    auto f = p.get_future();
    Scheduler::current()->spawn([TEST_CTX, p = std::move(p)]() mutable -> Task<> {
        p.set_exception(std::make_exception_ptr(std::runtime_error("inner")));
        co_return;
    });
    NITRO_CHECK_THROWS_AS(co_await withTimeout(f.get(), 1.0), std::runtime_error);
}

/** After timeout fires, the inner coroutine keeps running and eventually completes. */
NITRO_TEST(timeout_inner_continues_after_timeout)
{
    bool innerCompleted = false;

    // inner: sleeps 0.06s then sets the flag
    auto inner = [&]() -> Task<> {
        co_await Scheduler::current()->sleep_for(0.06);
        innerCompleted = true;
    };

    // timeout fires at 0.02s, well before inner finishes
    NITRO_CHECK_THROWS_AS(co_await withTimeout(inner(), 0.02), TimeoutException);
    NITRO_CHECK(!innerCompleted); // not yet done at timeout

    // wait long enough for inner to finish on its own
    co_await Scheduler::current()->sleep_for(0.08);
    NITRO_CHECK(innerCompleted); // inner ran to completion despite timeout
}

int main()
{
    return nitrocoro::test::run_all();
}
