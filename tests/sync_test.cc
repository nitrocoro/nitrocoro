/**
 * @file sync_test.cc
 * @brief Tests for Future/Promise/SharedFuture and Mutex.
 */
#include <nitrocoro/core/Future.h>
#include <nitrocoro/core/Mutex.h>
#include <nitrocoro/core/Scheduler.h>
#include <nitrocoro/core/Task.h>
#include <nitrocoro/testing/Test.h>

using namespace nitrocoro;

// ── Promise / Future — normal paths ──────────────────────────────────────────

/** A value set by Promise is received by the awaiting Future. */
NITRO_TEST(future_set_value)
{
    Promise<int> p;
    auto f = p.get_future();
    Scheduler::current()->spawn([TEST_CTX, p = std::move(p)]() mutable -> Task<> {
        co_await Scheduler::current()->sleep_for(0.02);
        p.set_value(99);
    });
    NITRO_CHECK_EQ(co_await f.get(), 99);
    co_return;
}

/** An exception set on a Promise is rethrown when the Future is awaited. */
NITRO_TEST(future_set_exception)
{
    Promise<int> p;
    auto f = p.get_future();
    Scheduler::current()->spawn([TEST_CTX, p = std::move(p)]() mutable -> Task<> {
        p.set_exception(std::make_exception_ptr(std::runtime_error("err")));
        co_return;
    });
    NITRO_CHECK_THROWS_AS(co_await f.get(), std::runtime_error);
}

/** SharedFuture resumes all waiters when the Promise is fulfilled. */
NITRO_TEST(shared_future_multiple_waiters)
{
    Promise<int> p;
    auto sf = p.get_future().share();
    int sum = 0;

    Scheduler::current()->spawn([TEST_CTX, sf, &sum]() mutable -> Task<> {
        sum += co_await sf.get();
    });
    Scheduler::current()->spawn([TEST_CTX, sf, &sum]() mutable -> Task<> {
        sum += co_await sf.get();
    });

    co_await Scheduler::current()->sleep_for(0.01);
    p.set_value(10);
    co_await Scheduler::current()->sleep_for(0.01);
    NITRO_CHECK_EQ(sum, 20);
}

// ── Promise — error states ────────────────────────────────────────────────────

/** Promise destroyed without set_value delivers broken_promise to the waiter. */
NITRO_TEST(promise_broken_promise)
{
    auto f = []() -> Future<> {
        Promise<> p;
        return p.get_future();
        // p destroyed here without set_value
    }();
    NITRO_CHECK_THROWS_AS(co_await f.get(), std::future_error);
}

/** Calling set_value twice throws promise_already_satisfied. */
NITRO_TEST(promise_already_satisfied)
{
    {
        Promise<> p;
        p.set_value();
        NITRO_CHECK_THROWS_AS(p.set_value(), std::future_error);
    }
    {
        Promise<> p;
        p.set_value();
        NITRO_CHECK_THROWS_AS(
            p.set_exception(std::make_exception_ptr(std::runtime_error(""))),
            std::future_error);
    }
    {
        Promise<> p;
        p.set_exception(std::make_exception_ptr(std::runtime_error("")));
        NITRO_CHECK_THROWS_AS(
            p.set_exception(std::make_exception_ptr(std::runtime_error(""))),
            std::future_error);
    }
    {
        Promise<> p;
        p.set_exception(std::make_exception_ptr(std::runtime_error("")));
        NITRO_CHECK_THROWS_AS(
            p.set_value(),
            std::future_error);
    }
    co_return;
}

/** Calling get_future twice throws future_already_retrieved. */
NITRO_TEST(promise_future_already_retrieved)
{
    Promise<> p;
    p.get_future();
    NITRO_CHECK_THROWS_AS(p.get_future(), std::future_error);
    co_return;
}

// ── Promise — move semantics ──────────────────────────────────────────────────

/** Moving a Promise transfers ownership; the moved-from destructor does not fire broken_promise. */
NITRO_TEST(promise_move_safe)
{
    Promise<int> p;
    auto f = p.get_future();
    Promise<int> p2 = std::move(p);
    // p is now empty; its destructor must NOT signal broken_promise
    // p2 fulfills normally
    Scheduler::current()->spawn([TEST_CTX, p2 = std::move(p2)]() mutable -> Task<> {
        p2.set_value(666);
        co_return;
    });
    NITRO_CHECK_EQ(co_await f.get(), 666); // must not throw
    co_return;
}

/** Calling set_value on a moved-from Promise throws no_state. */
NITRO_TEST(promise_set_after_move)
{
    Promise<> p;
    auto p1 = std::move(p);
    NITRO_CHECK_THROWS_AS(p.set_value(), std::future_error);
    co_return;
}

/** Calling get_future on a moved-from Promise throws no_state. */
NITRO_TEST(promise_get_future_after_move)
{
    Promise<> p;
    auto p2 = std::move(p);
    NITRO_CHECK_THROWS_AS(p.get_future(), std::future_error);
    co_return;
}

// ── Future / SharedFuture — consumed / invalid state ─────────────────────────

/** Calling Future::get() after it has been consumed throws no_state. */
NITRO_TEST(future_get_after_consumed)
{
    Promise<> p;
    auto f = p.get_future();
    p.set_value();
    co_await f.get();
    NITRO_CHECK_THROWS_AS(f.get(), std::future_error);
    co_return;
}

/** Calling SharedFuture::get() on a moved-from instance throws no_state. */
NITRO_TEST(shared_future_get_no_state)
{
    Promise<> p;
    auto sf = p.get_future().share();
    auto sf2 = std::move(sf); // sf is now invalid
    NITRO_CHECK_THROWS_AS(sf.get(), std::future_error);
    co_return;
}

// ── Mutex ─────────────────────────────────────────────────────────────────────

/** try_lock succeeds when unlocked and fails when already held. */
NITRO_TEST(mutex_try_lock)
{
    Mutex m;
    NITRO_CHECK(m.try_lock());
    NITRO_CHECK(!m.try_lock());
    m.unlock();
    NITRO_CHECK(m.try_lock());
    m.unlock();
    co_return;
}

/** scoped_lock provides exclusive access; concurrent coroutines serialize. */
NITRO_TEST(mutex_scoped_lock_exclusive)
{
    Mutex m;
    int counter = 0;
    Promise<> done;
    auto f = done.get_future();

    Scheduler::current()->spawn([TEST_CTX, &m, &counter, done = std::move(done)]() mutable -> Task<> {
        for (int i = 0; i < 5; ++i)
        {
            [[maybe_unused]] auto lock = co_await m.scoped_lock();
            ++counter;
        }
        done.set_value();
    });

    for (int i = 0; i < 5; ++i)
    {
        [[maybe_unused]] auto lock = co_await m.scoped_lock();
        ++counter;
    }
    co_await f.get();
    NITRO_CHECK_EQ(counter, 10);
}

int main(int argc, char ** argv)
{
    return nitrocoro::test::run_all(argc, argv);
}
