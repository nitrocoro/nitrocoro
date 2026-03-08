/**
 * @file core_test.cc
 * @brief Tests for Task, Scheduler, and Generator.
 */
#include <nitrocoro/core/Future.h>
#include <nitrocoro/core/Generator.h>
#include <nitrocoro/core/Scheduler.h>
#include <nitrocoro/core/Task.h>
#include <nitrocoro/testing/Test.h>

using namespace nitrocoro;

// ── Task ──────────────────────────────────────────────────────────────────────

/** Task<T> correctly returns a value via co_return. */
NITRO_TEST(task_return_value)
{
    auto make = []() -> Task<int> { co_return 42; };
    NITRO_CHECK_EQ(co_await make(), 42);
}

/** Exceptions thrown inside a Task propagate to the awaiting coroutine. */
NITRO_TEST(task_exception_propagates)
{
    auto thrower = []() -> Task<> { throw std::runtime_error("boom"); co_return; };
    NITRO_CHECK_THROWS_AS(co_await thrower(), std::runtime_error);
}

/** Nested co_await chains resolve in the correct order. */
NITRO_TEST(task_chain)
{
    auto add = [](int a, int b) -> Task<int> { co_return a + b; };
    int r = co_await add(co_await add(1, 2), co_await add(3, 4));
    NITRO_CHECK_EQ(r, 10);
}

// ── Scheduler ─────────────────────────────────────────────────────────────────

/** sleep_for suspends for at least the requested duration. */
NITRO_TEST(scheduler_sleep)
{
    auto t0 = std::chrono::steady_clock::now();
    co_await Scheduler::current()->sleep_for(0.05);
    auto elapsed = std::chrono::steady_clock::now() - t0;
    NITRO_CHECK(elapsed >= std::chrono::milliseconds(40));
}

/** spawn() enqueues work after the current coroutine yields. */
NITRO_TEST(scheduler_spawn_order)
{
    std::vector<int> order;
    Promise<> done(Scheduler::current());
    auto f = done.get_future();

    Scheduler::current()->spawn([TEST_CTX, &order, done = std::move(done)]() mutable -> Task<> {
        order.push_back(2);
        done.set_value();
        co_return;
    });
    order.push_back(1);
    co_await f.get();
    order.push_back(3);

    NITRO_REQUIRE_EQ(order.size(), 3u);
    NITRO_CHECK_EQ(order[0], 1);
    NITRO_CHECK_EQ(order[1], 2);
    NITRO_CHECK_EQ(order[2], 3);

    co_return;
}

// ── Generator ─────────────────────────────────────────────────────────────────

/** Generator yields values lazily and stops at the end. */
NITRO_TEST(generator_range)
{
    auto range = [](int n) -> Generator<int> {
        for (int i = 0; i < n; ++i)
            co_yield i;
    };
    int sum = 0;
    for (int v : range(5))
        sum += v;
    NITRO_CHECK_EQ(sum, 10);
    co_return;
}

/** A generator that co_returns immediately produces no values. */
NITRO_TEST(generator_empty)
{
    auto empty = []() -> Generator<int> { co_return; };
    int count = 0;
    for ([[maybe_unused]] int v : empty())
        ++count;
    NITRO_CHECK_EQ(count, 0);
    co_return;
}

/** Exceptions thrown inside a generator propagate to the range-for caller. */
NITRO_TEST(generator_exception)
{
    auto thrower = []() -> Generator<int> {
        co_yield 1;
        throw std::runtime_error("gen error");
    };
    NITRO_CHECK_THROWS_AS([&] { for (int _ : thrower()) {} }(), std::runtime_error);
    co_return;
}

int main(int argc, char ** argv)
{
    return nitrocoro::test::run_all(argc, argv);
}
