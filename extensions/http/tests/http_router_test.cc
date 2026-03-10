/**
 * @file http_router_test.cc
 * @brief Unit tests for HttpRouter route matching logic.
 */
#include <nitrocoro/http/HttpRouter.h>
#include <nitrocoro/testing/Test.h>

using namespace nitrocoro;
using namespace nitrocoro::http;

// ── Helpers ───────────────────────────────────────────────────────────────────

static auto dummyHandler()
{
    return [](auto req, auto resp) -> Task<> { co_return; };
}

static HttpRouter::RouteResult match(const HttpRouter & router,
                                     HttpMethod method,
                                     const std::string & path)
{
    return router.route(method, path);
}

// ── Exact match ───────────────────────────────────────────────────────────────

// GET /hello → exact match
NITRO_TEST(router_exact_match)
{
    HttpRouter router;
    router.addRoute("/hello", { "GET" }, dummyHandler());

    auto result = match(router, methods::Get, "/hello");
    NITRO_CHECK(result.handler != nullptr);
    NITRO_CHECK(result.reason == HttpRouter::RouteResult::Reason::Ok);
    NITRO_CHECK(result.params.empty());
    co_return;
}

// GET /world → no route registered for this path
NITRO_TEST(router_exact_no_match)
{
    HttpRouter router;
    router.addRoute("/hello", { "GET" }, dummyHandler());

    auto result = match(router, methods::Get, "/world");
    NITRO_CHECK(result.handler == nullptr);
    NITRO_CHECK(result.reason == HttpRouter::RouteResult::Reason::NotFound);
    co_return;
}

// GET /data → route registered as POST, method mismatch → 405
NITRO_TEST(router_method_mismatch)
{
    HttpRouter router;
    router.addRoute("/data", { "POST" }, dummyHandler());

    auto result = match(router, methods::Get, "/data");
    NITRO_CHECK(result.handler == nullptr);
    NITRO_CHECK(result.reason == HttpRouter::RouteResult::Reason::MethodNotAllowed);
    co_return;
}

// GET /nothing → no route at all → 404
NITRO_TEST(router_not_found)
{
    HttpRouter router;
    router.addRoute("/data", { "GET" }, dummyHandler());

    auto result = match(router, methods::Get, "/nothing");
    NITRO_CHECK(result.handler == nullptr);
    NITRO_CHECK(result.reason == HttpRouter::RouteResult::Reason::NotFound);
    co_return;
}

// multi-method: GET and POST both registered via initializer_list
NITRO_TEST(router_multi_method)
{
    HttpRouter router;
    router.addRoute("/data", { "GET", "POST" }, dummyHandler());

    NITRO_CHECK(match(router, methods::Get, "/data").reason == HttpRouter::RouteResult::Reason::Ok);
    NITRO_CHECK(match(router, methods::Post, "/data").reason == HttpRouter::RouteResult::Reason::Ok);
    auto result = match(router, methods::Delete, "/data");
    NITRO_CHECK(result.handler == nullptr);
    NITRO_CHECK(result.reason == HttpRouter::RouteResult::Reason::MethodNotAllowed);
    co_return;
}

// param route: wrong method → 405
NITRO_TEST(router_param_method_not_allowed)
{
    HttpRouter router;
    router.addRoute("/users/:id", { "GET" }, dummyHandler());

    auto result = match(router, methods::Delete, "/users/42");
    NITRO_CHECK(result.handler == nullptr);
    NITRO_CHECK(result.reason == HttpRouter::RouteResult::Reason::MethodNotAllowed);
    co_return;
}

// regex route: wrong method → 405
NITRO_TEST(router_regex_method_not_allowed)
{
    HttpRouter router;
    router.addRouteRegex(R"(/items/(\d+))", { "GET" }, dummyHandler());

    auto result = match(router, methods::Post, "/items/123");
    NITRO_CHECK(result.handler == nullptr);
    NITRO_CHECK(result.reason == HttpRouter::RouteResult::Reason::MethodNotAllowed);
    co_return;
}

// ── Param match ─────────────────────────────────────────────────────────────

// GET /users/42 → /users/:id, single param
NITRO_TEST(router_param_match)
{
    HttpRouter router;
    router.addRoute("/users/:id", { "GET" }, dummyHandler());

    auto result = match(router, methods::Get, "/users/42");
    NITRO_CHECK(result.handler != nullptr);
    NITRO_CHECK(result.reason == HttpRouter::RouteResult::Reason::Ok);
    NITRO_CHECK_EQ(result.params.at("id"), "42");
    co_return;
}

// GET /users/1/posts/99 → /users/:uid/posts/:pid, two params
NITRO_TEST(router_multi_param_match)
{
    HttpRouter router;
    router.addRoute("/users/:uid/posts/:pid", { "GET" }, dummyHandler());

    auto result = match(router, methods::Get, "/users/1/posts/99");
    NITRO_CHECK(result.handler != nullptr);
    NITRO_CHECK(result.reason == HttpRouter::RouteResult::Reason::Ok);
    NITRO_CHECK_EQ(result.params.at("uid"), "1");
    NITRO_CHECK_EQ(result.params.at("pid"), "99");
    co_return;
}

// ── Wildcard match ────────────────────────────────────────────────────────────

// GET /files/a/b/c.txt → /files/*path, wildcard captures multiple segments
NITRO_TEST(router_wildcard_match)
{
    HttpRouter router;
    router.addRoute("/files/*path", { "GET" }, dummyHandler());

    auto result = match(router, methods::Get, "/files/a/b/c.txt");
    NITRO_CHECK(result.handler != nullptr);
    NITRO_CHECK(result.reason == HttpRouter::RouteResult::Reason::Ok);
    NITRO_CHECK_EQ(result.params.at("path"), "a/b/c.txt");
    co_return;
}

// GET /files/readme.txt → /files/*path, wildcard captures single segment
NITRO_TEST(router_wildcard_single_segment)
{
    HttpRouter router;
    router.addRoute("/files/*path", { "GET" }, dummyHandler());

    auto result = match(router, methods::Get, "/files/readme.txt");
    NITRO_CHECK(result.handler != nullptr);
    NITRO_CHECK(result.reason == HttpRouter::RouteResult::Reason::Ok);
    NITRO_CHECK_EQ(result.params.at("path"), "readme.txt");
    co_return;
}

// ── Regex match ───────────────────────────────────────────────────────────────

// GET /items/123 → regex /items/(\d+), capture group $1
NITRO_TEST(router_regex_match)
{
    HttpRouter router;
    router.addRouteRegex(R"(/items/(\d+))", { "GET" }, dummyHandler());

    auto result = match(router, methods::Get, "/items/123");
    NITRO_CHECK(result.handler != nullptr);
    NITRO_CHECK(result.reason == HttpRouter::RouteResult::Reason::Ok);
    NITRO_CHECK_EQ(result.params.at("$1"), "123");
    co_return;
}

// GET /items/abc → regex /items/(\d+), non-digit fails to match
NITRO_TEST(router_regex_no_match)
{
    HttpRouter router;
    router.addRouteRegex(R"(/items/(\d+))", { "GET" }, dummyHandler());

    auto result = match(router, methods::Get, "/items/abc");
    NITRO_CHECK(result.handler == nullptr);
    NITRO_CHECK(result.reason == HttpRouter::RouteResult::Reason::NotFound);
    co_return;
}

// GET /users/42 → regex /(\w+)/(\d+), two capture groups $1 $2
NITRO_TEST(router_regex_multi_capture)
{
    HttpRouter router;
    router.addRouteRegex(R"(/(\w+)/(\d+))", { "GET" }, dummyHandler());

    auto result = match(router, methods::Get, "/users/42");
    NITRO_CHECK(result.handler != nullptr);
    NITRO_CHECK(result.reason == HttpRouter::RouteResult::Reason::Ok);
    NITRO_CHECK_EQ(result.params.at("$1"), "users");
    NITRO_CHECK_EQ(result.params.at("$2"), "42");
    co_return;
}

// ── Segment count mismatch ──────────────────────────────────────────────────

// GET /id/123/456 → /id/:id, extra segment causes no match
NITRO_TEST(router_param_segment_count_mismatch)
{
    HttpRouter router;
    router.addRoute("/id/:id", { "GET" }, dummyHandler());

    auto result = match(router, methods::Get, "/id/123/456");
    NITRO_CHECK(result.handler == nullptr);
    NITRO_CHECK(result.reason == HttpRouter::RouteResult::Reason::NotFound);
    co_return;
}

// ── Empty router ───────────────────────────────────────────────────────────────

// GET /anything → empty router, no routes registered
NITRO_TEST(router_empty_no_match)
{
    HttpRouter router;
    auto result = router.route(methods::Get, "/anything");
    NITRO_CHECK(!result);
    co_return;
}

// GET /api:v1 → colon not at segment start, must match as exact route
NITRO_TEST(router_colon_not_at_segment_start)
{
    HttpRouter router;
    router.addRoute("/api:v1", { "GET" }, dummyHandler());

    auto result = match(router, methods::Get, "/api:v1");
    NITRO_CHECK(result.handler != nullptr);
    NITRO_CHECK(result.reason == HttpRouter::RouteResult::Reason::Ok);
    NITRO_CHECK(result.params.empty());
    co_return;
}

// path exceeding 2048 bytes → rejected, no match
NITRO_TEST(router_path_too_long)
{
    HttpRouter router;
    router.addRoute("/*path", { "GET" }, dummyHandler());

    std::string longPath(2049, 'a');
    longPath[0] = '/';
    auto result = router.route(methods::Get, longPath);
    NITRO_CHECK(!result);
    co_return;
}

// path with more than 32 segments on a param route → recursion guard kicks in, no match
NITRO_TEST(router_too_many_segments)
{
    HttpRouter router;
    // register a param route deep enough to trigger the recursion guard
    std::string pattern;
    for (int i = 0; i < 33; ++i)
        pattern += "/:seg" + std::to_string(i);
    router.addRoute(pattern, { "GET" }, dummyHandler());

    std::string deepPath;
    for (int i = 0; i < 33; ++i)
        deepPath += "/a";
    auto result = router.route(methods::Get, deepPath);
    NITRO_CHECK(result.reason == HttpRouter::RouteResult::Reason::NotFound);
    co_return;
}

// ── Priority: exact > param > wildcard ───────────────────────────────────────

// GET /users/me → /users/me and /users/:id both registered, exact takes priority
NITRO_TEST(router_exact_beats_param)
{
    HttpRouter router;
    bool exactCalled = false;
    router.addRoute("/users/me", { "GET" }, [&exactCalled](auto req, auto resp) -> Task<> {
        exactCalled = true;
        co_return;
    });
    router.addRoute("/users/:id", { "GET" }, dummyHandler());

    auto result = match(router, methods::Get, "/users/me");
    NITRO_CHECK(result.handler != nullptr);
    NITRO_CHECK(result.reason == HttpRouter::RouteResult::Reason::Ok);
    NITRO_CHECK(result.params.empty());
    co_return;
}

// GET /a/hello → /a/:b and /a/*rest both registered, param takes priority over wildcard
NITRO_TEST(router_param_beats_wildcard)
{
    HttpRouter router;
    router.addRoute("/a/:b", { "GET" }, dummyHandler());
    router.addRoute("/a/*rest", { "GET" }, dummyHandler());

    auto result = match(router, methods::Get, "/a/hello");
    NITRO_CHECK(result.handler != nullptr);
    NITRO_CHECK(result.reason == HttpRouter::RouteResult::Reason::Ok);
    NITRO_CHECK(result.params.count("b"));
    co_return;
}

// same path registered twice with different methods → both work, wrong method → 405
NITRO_TEST(router_addroute_merges_methods)
{
    HttpRouter router;
    router.addRoute("/data", { "GET" }, dummyHandler());
    router.addRoute("/data", { "POST" }, dummyHandler());

    NITRO_CHECK(match(router, methods::Get, "/data").reason == HttpRouter::RouteResult::Reason::Ok);
    NITRO_CHECK(match(router, methods::Post, "/data").reason == HttpRouter::RouteResult::Reason::Ok);
    NITRO_CHECK(match(router, methods::Delete, "/data").reason == HttpRouter::RouteResult::Reason::MethodNotAllowed);
    co_return;
}

// same regex pattern registered twice with different methods → both work
NITRO_TEST(router_addregex_merges_methods)
{
    HttpRouter router;
    router.addRouteRegex(R"(/items/(\d+))", { "GET" }, dummyHandler());
    router.addRouteRegex(R"(/items/(\d+))", { "POST" }, dummyHandler());

    NITRO_CHECK(match(router, methods::Get, "/items/1").reason == HttpRouter::RouteResult::Reason::Ok);
    NITRO_CHECK(match(router, methods::Post, "/items/1").reason == HttpRouter::RouteResult::Reason::Ok);
    NITRO_CHECK(match(router, methods::Delete, "/items/1").reason == HttpRouter::RouteResult::Reason::MethodNotAllowed);
    co_return;
}

// wildcard route: wrong method → 405
NITRO_TEST(router_wildcard_method_not_allowed)
{
    HttpRouter router;
    router.addRoute("/files/*path", { "GET" }, dummyHandler());

    auto result = match(router, methods::Post, "/files/a/b/c.txt");
    NITRO_CHECK(result.handler == nullptr);
    NITRO_CHECK(result.reason == HttpRouter::RouteResult::Reason::MethodNotAllowed);
    co_return;
}

// two param routes under same parent with different param names → both resolve correctly
NITRO_TEST(router_param_name_no_collision)
{
    HttpRouter router;
    router.addRoute("/users/:id", { "GET" }, dummyHandler());
    router.addRoute("/users/:uid/posts/:pid", { "GET" }, dummyHandler());

    auto r1 = match(router, methods::Get, "/users/42");
    NITRO_CHECK(r1.reason == HttpRouter::RouteResult::Reason::Ok);
    NITRO_CHECK(r1.params.count("id"));
    NITRO_CHECK_EQ(r1.params.at("id"), "42");

    auto r2 = match(router, methods::Get, "/users/1/posts/99");
    NITRO_CHECK(r2.reason == HttpRouter::RouteResult::Reason::Ok);
    NITRO_CHECK(r2.params.count("uid"));
    NITRO_CHECK_EQ(r2.params.at("uid"), "1");
    NITRO_CHECK_EQ(r2.params.at("pid"), "99");
    co_return;
}

// ── HttpMethod strong type ────────────────────────────────────────────────────

// register with methods::Get (strong type) instead of {"GET"}
NITRO_TEST(router_register_with_strong_method)
{
    HttpRouter router;
    router.addRoute("/hello", methods::Get, dummyHandler());

    NITRO_CHECK(match(router, methods::Get, "/hello").reason == HttpRouter::RouteResult::Reason::Ok);
    NITRO_CHECK(match(router, methods::Post, "/hello").reason == HttpRouter::RouteResult::Reason::MethodNotAllowed);
    co_return;
}

// addRoute / addRouteRegex with invalid method (_Invalid or unknown string) throws
NITRO_TEST(router_invalid_method_throws)
{
    HttpRouter router;
    NITRO_CHECK_THROWS(router.addRoute("/hello", methods::_Invalid, dummyHandler()));
    NITRO_CHECK_THROWS(router.addRoute("/hello", { "WRONG" }, dummyHandler()));
    NITRO_CHECK_THROWS(router.addRouteRegex(R"(/items/(\d+))", methods::_Invalid, dummyHandler()));
    NITRO_CHECK_THROWS(router.addRouteRegex(R"(/items/(\d+))", { "WRONG" }, dummyHandler()));
    co_return;
}

int main(int argc, char ** argv)
{
    return nitrocoro::test::run_all(argc, argv);
}
