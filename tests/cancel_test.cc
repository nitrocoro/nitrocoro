/**
 * @file cancel_test.cc
 * @brief Tests for CancelSource / CancelToken / CancelRegistration.
 */
#include <nitrocoro/core/CancelToken.h>
#include <nitrocoro/core/Scheduler.h>
#include <nitrocoro/core/Task.h>
#include <nitrocoro/testing/Test.h>

using namespace nitrocoro;

/** Default-constructed token is never cancelled. */
NITRO_TEST(cancel_token_none)
{
    CancelToken none;
    NITRO_CHECK(!none.isCancelled());
    co_return;
}

/** isCancelled reflects cancel() on both source and token. */
NITRO_TEST(cancel_basic)
{
    CancelSource src;
    auto token = src.token();
    NITRO_CHECK(!src.isCancelled());
    NITRO_CHECK(!token.isCancelled());
    src.cancel();
    NITRO_CHECK(src.isCancelled());
    NITRO_CHECK(token.isCancelled());
    co_return;
}

/** cancel() is idempotent. */
NITRO_TEST(cancel_idempotent)
{
    CancelSource src;
    src.cancel();
    src.cancel(); // must not crash or double-fire
    NITRO_CHECK(src.isCancelled());
    co_return;
}

/** onCancel callback is invoked when cancel() is called. */
NITRO_TEST(cancel_on_cancel_callback)
{
    CancelSource src;
    int count = 0;
    [[maybe_unused]] auto reg = src.token().onCancel([&] { ++count; });
    NITRO_CHECK_EQ(count, 0);
    src.cancel();
    NITRO_CHECK_EQ(count, 1);
    co_return;
}

/** onCancel callback is NOT invoked after CancelRegistration is destroyed. */
NITRO_TEST(cancel_registration_unregisters)
{
    CancelSource src;
    int count = 0;
    {
        [[maybe_unused]] auto reg = src.token().onCancel([&] { ++count; });
    } // reg destroyed here
    src.cancel();
    NITRO_CHECK_EQ(count, 0);
    co_return;
}

/** onCancel on an already-cancelled token fires the callback immediately. */
NITRO_TEST(cancel_on_cancel_after_cancel)
{
    CancelSource src;
    src.cancel();
    int count = 0;
    [[maybe_unused]] auto reg = src.token().onCancel([&] { ++count; });
    NITRO_CHECK_EQ(count, 1);
    co_return;
}

/** Multiple tokens from the same source all reflect cancellation. */
NITRO_TEST(cancel_multiple_tokens)
{
    CancelSource src;
    auto t1 = src.token();
    auto t2 = src.token();
    src.cancel();
    NITRO_CHECK(t1.isCancelled());
    NITRO_CHECK(t2.isCancelled());
    co_return;
}

/** co_await token.cancelled() resumes after cancel() is called. */
NITRO_TEST(cancel_cancelled_awaitable)
{
    CancelSource src;
    bool resumed = false;

    Scheduler::current()->spawn([TEST_CTX, token = src.token(), &resumed]() mutable -> Task<> {
        co_await token.cancelled();
        resumed = true;
    });

    co_await Scheduler::current()->sleep_for(0.01);
    NITRO_CHECK(!resumed);
    src.cancel();
    co_await Scheduler::current()->sleep_for(0.01);
    NITRO_CHECK(resumed);
}

/** co_await token.cancelled() returns immediately if already cancelled. */
NITRO_TEST(cancel_cancelled_already_cancelled)
{
    CancelSource src;
    src.cancel();
    co_await src.token().cancelled(); // must not suspend
    co_return;
}

/** Multiple coroutines awaiting cancelled() are all resumed. */
NITRO_TEST(cancel_cancelled_multiple_waiters)
{
    CancelSource src;
    int count = 0;

    for (int i = 0; i < 3; ++i)
    {
        Scheduler::current()->spawn([TEST_CTX, token = src.token(), &count]() mutable -> Task<> {
            co_await token.cancelled();
            ++count;
        });
    }

    co_await Scheduler::current()->sleep_for(0.01);
    NITRO_CHECK_EQ(count, 0);
    src.cancel();
    co_await Scheduler::current()->sleep_for(0.01);
    NITRO_CHECK_EQ(count, 3);
}

/** CancelSource destructor does NOT cancel — token remains live. */
NITRO_TEST(cancel_source_destructor_no_cancel)
{
    CancelToken token;
    {
        CancelSource src;
        token = src.token();
    } // src destroyed — must NOT cancel
    NITRO_CHECK(!token.isCancelled());
    co_return;
}

/** CancelSource destructor does NOT fire onCancel callbacks. */
NITRO_TEST(cancel_source_destructor_no_callbacks)
{
    int count = 0;
    CancelRegistration reg;
    {
        CancelSource src;
        reg = src.token().onCancel([&] { ++count; });
    }
    NITRO_CHECK_EQ(count, 0);
    co_return;
}

/** CancelSource(duration) cancels the token after the given timeout. */
NITRO_TEST(cancel_source_duration_ctor)
{
    using namespace std::chrono_literals;
    CancelSource src(20ms);
    auto token = src.token();
    NITRO_CHECK(!token.isCancelled());
    co_await Scheduler::current()->sleep_for(40ms);
    NITRO_CHECK(token.isCancelled());
}

int main()
{
    return nitrocoro::test::run_all();
}
