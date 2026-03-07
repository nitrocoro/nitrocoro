/**
 * @file testing/Test.h
 * @brief Minimal coroutine-aware test framework for NitroCoro
 *
 * Assertion levels:
 *   NITRO_CHECK(expr)   — soft: log failure, continue
 *   NITRO_REQUIRE(expr) — hard: log failure, co_return (abort current test)
 *   NITRO_MANDATE(expr) — fatal: log failure, exit(1)
 *
 * Usage:
 *   NITRO_TEST(my_test)
 *   {
 *       // TEST_CTX is a shared_ptr<TestCtx>; capture it in spawned coroutines
 *       // to keep the test alive until all spawned work finishes.
 *       Scheduler::current()->spawn([TEST_CTX]() -> Task<> {
 *           co_await something();
 *           NITRO_CHECK(a == b);
 *       });
 *   }
 *   int main() { return nitrocoro::test::run_all(); }
 */
#pragma once

#include <nitrocoro/core/Future.h>
#include <nitrocoro/core/Scheduler.h>
#include <nitrocoro/core/Task.h>
#include <nitrocoro/utils/Debug.h>

#include <atomic>
#include <cstdlib>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <typeinfo>
#include <vector>

namespace nitrocoro::test
{

#if defined(__GNUG__) || defined(__clang__)
#include <cxxabi.h>
inline std::string demangle(const char * name)
{
    int status;
    char * buf = abi::__cxa_demangle(name, nullptr, nullptr, &status);
    std::string result = (status == 0 && buf) ? buf : name;
    std::free(buf);
    return result;
}
#else
inline std::string demangle(const char * name)
{
    return name;
}
#endif

struct TestCtx
{
    const char * name;
    std::atomic<int> checks{ 0 };
    std::atomic<int> failures{ 0 };

    // Called when the last shared_ptr<TestCtx> is destroyed.
    std::function<void(TestCtx &)> onDone_;

    ~TestCtx()
    {
        if (onDone_)
            onDone_(*this);
    }
};

using TestCtxPtr = std::shared_ptr<TestCtx>;

struct TestCase
{
    const char * name;
    std::function<Task<>(TestCtxPtr)> fn;
    bool expectFail{ false };
};

inline std::vector<TestCase> & registry()
{
    static std::vector<TestCase> cases;
    return cases;
}

inline void record_check(TestCtxPtr ctx)
{
    if (ctx)
        ++ctx->checks;
}

inline void record_failure(const char * file, int line, const char * expr, TestCtxPtr ctx)
{
    printf("\x1B[0;31m[FAIL]\x1B[0m %s:%d: %s\n", file, line, expr);
    if (ctx)
        ++ctx->failures;
}

template <typename T>
std::string attemptPrint(const T & v)
{
    if constexpr (std::is_null_pointer_v<T>)
    {
        return "nullptr";
    }
    else if constexpr (std::is_same_v<T, bool>)
    {
        return v ? "true" : "false";
    }
    else if constexpr (std::is_same_v<T, char>)
    {
        return '\'' + std::string(1, v) + '\'';
    }
    else if constexpr (std::is_convertible_v<T, std::string_view>)
    {
        return '"' + std::string(std::string_view(v)) + '"';
    }
    else if constexpr (std::is_enum_v<T>)
    {
        return std::to_string(static_cast<std::underlying_type_t<T>>(v));
    }
    else if constexpr (requires(std::ostream & os) { os << v; })
    {
        std::ostringstream ss;
        ss << v;
        return ss.str();
    }
    return "{un-printable}";
}

template <typename A, typename B>
inline void record_failure_eq(
    const char * file, int line, const char * expr, const A & a, const B & b, TestCtxPtr ctx)
{
    printf("\x1B[0;31m[FAIL]\x1B[0m %s:%d: %s\n  With expansion\n    \x1B[0;33m%s\x1B[0m\n",
           file, line, expr,
           (attemptPrint(a) + " == " + attemptPrint(b)).c_str());
    if (ctx)
        ++ctx->failures;
}

template <typename A, typename B>
inline void record_failure_ne(
    const char * file, int line, const char * expr, const A & a, const B & b, TestCtxPtr ctx)
{
    printf("\x1B[0;31m[FAIL]\x1B[0m %s:%d: %s\n  With expansion\n    \x1B[0;33m%s\x1B[0m\n",
           file, line, expr,
           (attemptPrint(a) + " != " + attemptPrint(b)).c_str());
    if (ctx)
        ++ctx->failures;
}

inline int run_all()
{
    int passed = 0, failed = 0;

    auto run_tests = [&]() -> Task<> {
        for (auto & tc : registry())
        {
            printf("\n\x1B[1;37m--- %s ---\x1B[0m\n", tc.name);

            auto done = std::make_shared<Promise<>>(Scheduler::current());
            auto future = done->get_future();

            auto ctx = std::make_shared<TestCtx>();
            ctx->name = tc.name;
            ctx->onDone_ = [done](TestCtx &) {
                done->set_value();
            };

            try
            {
                co_await tc.fn(ctx);
            }
            catch (const std::exception & e)
            {
                printf("\x1B[0;31m[FAIL]\x1B[0m %s threw: %s\n", tc.name, e.what());
                ++ctx->failures;
            }

            // Release our own reference; spawned coroutines may still hold theirs.
            int checks = ctx->checks.load();
            int failures = ctx->failures.load();
            ctx.reset();

            co_await future.get();

            bool testFailed = failures > 0;
            if (tc.expectFail)
            {
                if (testFailed)
                {
                    printf("\x1B[0;33m[XFAIL]\x1B[0m %s \x1B[0;33m(expected failure)\x1B[0m\n", tc.name);
                    ++passed;
                }
                else
                {
                    printf("\x1B[0;31m[FAIL]\x1B[0m %s (expected to fail but passed)\n", tc.name);
                    ++failed;
                }
            }
            else if (testFailed)
            {
                printf("\x1B[0;31m[FAIL]\x1B[0m %s  \x1B[0;37m%d/%d checks passed\x1B[0m\n", tc.name, checks - failures, checks);
                ++failed;
            }
            else
            {
                printf("\x1B[0;32m[PASS]\x1B[0m %s  \x1B[0;37m%d checks\x1B[0m\n", tc.name, checks);
                ++passed;
            }
        }
        printf("\n\x1B[1;37m=== Results: \x1B[0;32m%d passed\x1B[0m, \x1B[0;31m%d failed\x1B[1;37m ===\x1B[0m\n", passed, failed);
        Scheduler::current()->stop();
    };

    Scheduler scheduler;
    scheduler.spawn(run_tests);
    scheduler.run();
    return failed > 0 ? 1 : 0;
}

struct Registrar
{
    Registrar(const char * name, std::function<Task<>(TestCtxPtr)> fn, bool expectFail = false)
    {
        registry().push_back({ name, std::move(fn), expectFail });
    }
};

} // namespace nitrocoro::test

// ── Internal helpers ──────────────────────────────────────────────────────────

#define NITRO_TEST_RECORD_FAILURE__(expr, ctx) \
    nitrocoro::test::record_failure(__FILE__, __LINE__, expr, ctx)

// ── Test registration ─────────────────────────────────────────────────────────

#define NITRO_TEST(name)                                                                  \
    static nitrocoro::Task<> _nitro_test_fn_##name(nitrocoro::test::TestCtxPtr TEST_CTX); \
    static nitrocoro::test::Registrar _nitro_reg_##name{ #name, _nitro_test_fn_##name };  \
    static nitrocoro::Task<> _nitro_test_fn_##name(nitrocoro::test::TestCtxPtr TEST_CTX)

#define NITRO_TEST_EXPECT_FAIL(name)                                                           \
    static nitrocoro::Task<> _nitro_test_fn_##name(nitrocoro::test::TestCtxPtr TEST_CTX);      \
    static nitrocoro::test::Registrar _nitro_reg_##name{ #name, _nitro_test_fn_##name, true }; \
    static nitrocoro::Task<> _nitro_test_fn_##name(nitrocoro::test::TestCtxPtr TEST_CTX)

// ── CHECK — soft assertion: log failure, continue ─────────────────────────────

#define NITRO_CHECK(expr)                                 \
    do                                                    \
    {                                                     \
        nitrocoro::test::record_check(TEST_CTX);          \
        if (!(expr))                                      \
            NITRO_TEST_RECORD_FAILURE__(#expr, TEST_CTX); \
    } while (0)

#define NITRO_CHECK_EQ(a, b)                                                                      \
    do                                                                                            \
    {                                                                                             \
        nitrocoro::test::record_check(TEST_CTX);                                                  \
        if (!((a) == (b)))                                                                        \
            nitrocoro::test::record_failure_eq(__FILE__, __LINE__, #a " == " #b, a, b, TEST_CTX); \
    } while (0)

#define NITRO_CHECK_NE(a, b)                                                                      \
    do                                                                                            \
    {                                                                                             \
        nitrocoro::test::record_check(TEST_CTX);                                                  \
        if (!((a) != (b)))                                                                        \
            nitrocoro::test::record_failure_ne(__FILE__, __LINE__, #a " != " #b, a, b, TEST_CTX); \
    } while (0)

// ── REQUIRE — hard assertion: log failure, abort current test (co_return) ─────

#define NITRO_REQUIRE(expr)                               \
    do                                                    \
    {                                                     \
        nitrocoro::test::record_check(TEST_CTX);          \
        if (!(expr))                                      \
        {                                                 \
            NITRO_TEST_RECORD_FAILURE__(#expr, TEST_CTX); \
            co_return;                                    \
        }                                                 \
    } while (0)

#define NITRO_REQUIRE_EQ(a, b)                                                                    \
    do                                                                                            \
    {                                                                                             \
        nitrocoro::test::record_check(TEST_CTX);                                                  \
        if (!((a) == (b)))                                                                        \
        {                                                                                         \
            nitrocoro::test::record_failure_eq(__FILE__, __LINE__, #a " == " #b, a, b, TEST_CTX); \
            co_return;                                                                            \
        }                                                                                         \
    } while (0)

#define NITRO_REQUIRE_NE(a, b)                                                                    \
    do                                                                                            \
    {                                                                                             \
        nitrocoro::test::record_check(TEST_CTX);                                                  \
        if (!((a) != (b)))                                                                        \
        {                                                                                         \
            nitrocoro::test::record_failure_ne(__FILE__, __LINE__, #a " != " #b, a, b, TEST_CTX); \
            co_return;                                                                            \
        }                                                                                         \
    } while (0)

// ── MANDATE — fatal assertion: log failure, exit(1) ───────────────────────────

#define NITRO_MANDATE(expr)                               \
    do                                                    \
    {                                                     \
        nitrocoro::test::record_check(TEST_CTX);          \
        if (!(expr))                                      \
        {                                                 \
            NITRO_TEST_RECORD_FAILURE__(#expr, TEST_CTX); \
            std::exit(1);                                 \
        }                                                 \
    } while (0)

#define NITRO_MANDATE_EQ(a, b)                                                                    \
    do                                                                                            \
    {                                                                                             \
        nitrocoro::test::record_check(TEST_CTX);                                                  \
        if (!((a) == (b)))                                                                        \
        {                                                                                         \
            nitrocoro::test::record_failure_eq(__FILE__, __LINE__, #a " == " #b, a, b, TEST_CTX); \
            std::exit(1);                                                                         \
        }                                                                                         \
    } while (0)

#define NITRO_MANDATE_NE(a, b)                                                                    \
    do                                                                                            \
    {                                                                                             \
        nitrocoro::test::record_check(TEST_CTX);                                                  \
        if (!((a) != (b)))                                                                        \
        {                                                                                         \
            nitrocoro::test::record_failure_ne(__FILE__, __LINE__, #a " != " #b, a, b, TEST_CTX); \
            std::exit(1);                                                                         \
        }                                                                                         \
    } while (0)

// ── THROWS — assert that an expression throws ─────────────────────────────────

#define NITRO_THROWS__(expr, msg, on_fail)              \
    do                                                  \
    {                                                   \
        nitrocoro::test::record_check(TEST_CTX);        \
        bool _threw_ = false;                           \
        try                                             \
        {                                               \
            (void)(expr);                               \
        }                                               \
        catch (...)                                     \
        {                                               \
            _threw_ = true;                             \
        }                                               \
        if (!_threw_)                                   \
        {                                               \
            NITRO_TEST_RECORD_FAILURE__(msg, TEST_CTX); \
            on_fail;                                    \
        }                                               \
    } while (0)

#define NITRO_THROWS_AS__(expr, ExType, msg, on_fail)                     \
    do                                                                    \
    {                                                                     \
        nitrocoro::test::record_check(TEST_CTX);                          \
        bool _threw_ = false;                                             \
        std::string _wrong_type_;                                         \
        std::string _wrong_msg_;                                          \
        try                                                               \
        {                                                                 \
            (void)(expr);                                                 \
        }                                                                 \
        catch (const ExType &)                                            \
        {                                                                 \
            _threw_ = true;                                               \
        }                                                                 \
        catch (const std::exception & _e_)                                \
        {                                                                 \
            _wrong_type_ = nitrocoro::test::demangle(typeid(_e_).name()); \
            _wrong_msg_ = _e_.what();                                     \
        }                                                                 \
        catch (...)                                                       \
        {                                                                 \
            _wrong_type_ = "<unknown non-std exception>";                 \
        }                                                                 \
        if (!_threw_)                                                     \
        {                                                                 \
            if (!_wrong_type_.empty())                                    \
                printf("\x1B[0;31m[FAIL]\x1B[0m %s:%d: " msg              \
                       "\n  Got: %s: %s\n",                               \
                       __FILE__, __LINE__,                                \
                       _wrong_type_.c_str(), _wrong_msg_.c_str());        \
            else                                                          \
                NITRO_TEST_RECORD_FAILURE__(msg, TEST_CTX);               \
            if (!_wrong_type_.empty())                                    \
                ++TEST_CTX->failures;                                     \
            on_fail;                                                      \
        }                                                                 \
    } while (0)

#define NITRO_CHECK_THROWS(expr)              NITRO_THROWS__(expr, #expr " did not throw", (void)0)
#define NITRO_CHECK_THROWS_AS(expr, ExType)   NITRO_THROWS_AS__(expr, ExType, #expr " did not throw " #ExType, (void)0)
#define NITRO_REQUIRE_THROWS(expr)            NITRO_THROWS__(expr, #expr " did not throw", co_return)
#define NITRO_REQUIRE_THROWS_AS(expr, ExType) NITRO_THROWS_AS__(expr, ExType, #expr " did not throw " #ExType, co_return)
#define NITRO_MANDATE_THROWS(expr)            NITRO_THROWS__(expr, #expr " did not throw", std::exit(1))
#define NITRO_MANDATE_THROWS_AS(expr, ExType) NITRO_THROWS_AS__(expr, ExType, #expr " did not throw " #ExType, std::exit(1))
