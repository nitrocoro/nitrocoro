/**
 * @file http_method_test.cc
 * @brief Unit tests for HttpMethod: standard methods, fromString, toString, custom methods.
 */
#include <nitrocoro/http/HttpTypes.h>
#include <nitrocoro/testing/Test.h>

using namespace nitrocoro;
using namespace nitrocoro::http;

// ── Standard methods: fromString / toString round-trip ───────────────────────

NITRO_TEST(http_method_standard_round_trip)
{
    struct
    {
        HttpMethod method;
        std::string_view name;
    } cases[] = {
        { methods::Get, "GET" },
        { methods::Head, "HEAD" },
        { methods::Post, "POST" },
        { methods::Put, "PUT" },
        { methods::Delete, "DELETE" },
        { methods::Options, "OPTIONS" },
        { methods::Patch, "PATCH" },
        { methods::Trace, "TRACE" },
        { methods::Connect, "CONNECT" },
    };
    for (auto & [method, name] : cases)
    {
        NITRO_CHECK_EQ(method.toString(), name);
        NITRO_CHECK(HttpMethod::fromString(name) == method);
    }
    co_return;
}

// ── Unknown string → _Invalid ─────────────────────────────────────────────────

NITRO_TEST(http_method_unknown_string_returns_invalid)
{
    NITRO_CHECK(HttpMethod::fromString("WRONG") == methods::_Invalid);
    NITRO_CHECK(HttpMethod::fromString("") == methods::_Invalid);
    co_return;
}

// ── Custom method registration ────────────────────────────────────────────────

NITRO_TEST(http_method_custom_register)
{
    HttpMethod purge = HttpMethod::registerMethod("PURGE");
    NITRO_CHECK(purge != methods::_Invalid);
    NITRO_CHECK_EQ(purge.toString(), "PURGE");
    NITRO_CHECK(HttpMethod::fromString("PURGE") == purge);
    co_return;
}

// registering the same custom method twice returns the same id
NITRO_TEST(http_method_custom_register_idempotent)
{
    HttpMethod a = HttpMethod::registerMethod("SEARCH");
    HttpMethod b = HttpMethod::registerMethod("SEARCH");
    NITRO_CHECK(a == b);
    co_return;
}

// custom method is distinct from all standard methods
NITRO_TEST(http_method_custom_distinct_from_standard)
{
    HttpMethod custom = HttpMethod::registerMethod("NOTIFY");
    NITRO_CHECK(custom != methods::Get);
    NITRO_CHECK(custom != methods::Post);
    NITRO_CHECK(custom != methods::_Invalid);
    co_return;
}

int main(int argc, char ** argv)
{
    return nitrocoro::test::run_all(argc, argv);
}
