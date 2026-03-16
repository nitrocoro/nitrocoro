/**
 * @file cookie_test.cc
 * @brief Tests for Cookie
 */
#include <nitrocoro/http/Cookie.h>
#include <nitrocoro/http/HttpClient.h>
#include <nitrocoro/http/HttpMessageAccessor.h>
#include <nitrocoro/http/HttpServer.h>
#include <nitrocoro/testing/Test.h>

#include "../src/HttpParser.h"

using namespace nitrocoro;
using namespace nitrocoro::http;
using namespace std::chrono_literals;

// ── Cookie::toString ──────────────────────────────────────────────────────────

NITRO_TEST(cookie_tostring_basic)
{
    Cookie c{ .name = "session", .value = "abc123" };
    NITRO_CHECK_EQ(c.toString(), "session=abc123");
    co_return;
}

NITRO_TEST(cookie_tostring_all_attributes)
{
    Cookie c{
        .name = "id",
        .value = "42",
        .domain = "example.com",
        .path = "/",
        .expires = "Thu, 01 Jan 2099 00:00:00 GMT",
        .maxAge = 3600,
        .sameSite = Cookie::SameSite::Lax,
        .secure = true,
        .httpOnly = true,
    };
    std::string s = c.toString();
    NITRO_CHECK(s.find("id=42") == 0);
    NITRO_CHECK(s.find("Expires=Thu, 01 Jan 2099 00:00:00 GMT") != std::string::npos);
    NITRO_CHECK(s.find("Max-Age=3600") != std::string::npos);
    NITRO_CHECK(s.find("Domain=example.com") != std::string::npos);
    NITRO_CHECK(s.find("Path=/") != std::string::npos);
    NITRO_CHECK(s.find("Secure") != std::string::npos);
    NITRO_CHECK(s.find("HttpOnly") != std::string::npos);
    NITRO_CHECK(s.find("SameSite=Lax") != std::string::npos);
    co_return;
}

NITRO_TEST(cookie_tostring_samesite_variants)
{
    auto strict = Cookie{ .name = "a", .value = "1", .sameSite = Cookie::SameSite::Strict }.toString();
    auto lax = Cookie{ .name = "a", .value = "1", .sameSite = Cookie::SameSite::Lax }.toString();
    auto none = Cookie{ .name = "a", .value = "1", .sameSite = Cookie::SameSite::None }.toString();
    auto unset = Cookie{ .name = "a", .value = "1", .sameSite = Cookie::SameSite::Unset }.toString();
    NITRO_CHECK(strict.find("SameSite=Strict") != std::string::npos);
    NITRO_CHECK(lax.find("SameSite=Lax") != std::string::npos);
    NITRO_CHECK(none.find("SameSite=None") != std::string::npos);
    NITRO_CHECK(unset.find("SameSite") == std::string::npos);
    co_return;
}

NITRO_TEST(cookie_tostring_no_optional_attributes)
{
    Cookie c{ .name = "x", .value = "y" };
    std::string s = c.toString();
    NITRO_CHECK(s.find("Expires") == std::string::npos);
    NITRO_CHECK(s.find("Max-Age") == std::string::npos);
    NITRO_CHECK(s.find("Domain") == std::string::npos);
    NITRO_CHECK(s.find("Path") == std::string::npos);
    NITRO_CHECK(s.find("Secure") == std::string::npos);
    NITRO_CHECK(s.find("HttpOnly") == std::string::npos);
    NITRO_CHECK(s.find("SameSite") == std::string::npos);
    co_return;
}

// ── Cookie::formatExpires ─────────────────────────────────────────────────────

NITRO_TEST(cookie_format_expires)
{
    // 2000-01-01 00:00:00 UTC = 946684800
    auto tp = std::chrono::system_clock::from_time_t(946684800);
    NITRO_CHECK_EQ(Cookie::formatExpires(tp), "Sat, 01 Jan 2000 00:00:00 GMT");
    co_return;
}

// ── Cookie::fromString ────────────────────────────────────────────────────────

NITRO_TEST(cookie_fromstring_basic)
{
    auto c = Cookie::fromString("session=abc123");
    NITRO_CHECK_EQ(c.name, "session");
    NITRO_CHECK_EQ(c.value, "abc123");
    co_return;
}

NITRO_TEST(cookie_fromstring_all_attributes)
{
    auto c = Cookie::fromString("id=42; Domain=example.com; "
                                "Path=/app; Max-Age=3600; "
                                "Expires=Thu, 01 Jan 2099 00:00:00 GMT; "
                                "Secure; HttpOnly; SameSite=None");
    NITRO_CHECK_EQ(c.name, "id");
    NITRO_CHECK_EQ(c.value, "42");
    NITRO_CHECK_EQ(c.domain, "example.com");
    NITRO_CHECK_EQ(c.path, "/app");
    NITRO_CHECK_EQ(c.maxAge, 3600);
    NITRO_CHECK_EQ(c.expires, "Thu, 01 Jan 2099 00:00:00 GMT");
    NITRO_CHECK(c.secure);
    NITRO_CHECK(c.httpOnly);
    NITRO_CHECK(c.sameSite == Cookie::SameSite::None);
    co_return;
}

NITRO_TEST(cookie_fromstring_samesite_case_insensitive)
{
    auto c1 = Cookie::fromString("a=1; SameSite=strict");
    auto c2 = Cookie::fromString("a=1; samesite=LAX");
    auto c3 = Cookie::fromString("a=1; SAMESITE=None");
    NITRO_CHECK(c1.sameSite == Cookie::SameSite::Strict);
    NITRO_CHECK(c2.sameSite == Cookie::SameSite::Lax);
    NITRO_CHECK(c3.sameSite == Cookie::SameSite::None);
    co_return;
}

NITRO_TEST(cookie_fromstring_invalid)
{
    auto c = Cookie::fromString("invalid");
    NITRO_CHECK(c.name.empty());
    co_return;
}

NITRO_TEST(cookie_roundtrip)
{
    Cookie original{
        .name = "token",
        .value = "xyz",
        .domain = "example.com",
        .path = "/",
        .maxAge = 86400,
        .sameSite = Cookie::SameSite::Lax,
        .secure = true,
        .httpOnly = true,
    };
    auto parsed = Cookie::fromString(original.toString());
    NITRO_CHECK_EQ(parsed.name, original.name);
    NITRO_CHECK_EQ(parsed.value, original.value);
    NITRO_CHECK_EQ(parsed.domain, original.domain);
    NITRO_CHECK_EQ(parsed.path, original.path);
    NITRO_CHECK_EQ(parsed.maxAge, original.maxAge);
    NITRO_CHECK(parsed.sameSite == original.sameSite);
    NITRO_CHECK_EQ(parsed.secure, original.secure);
    NITRO_CHECK_EQ(parsed.httpOnly, original.httpOnly);
    co_return;
}

// ── Request cookie parsing ────────────────────────────────────────────────────

NITRO_TEST(cookie_request_parse_single)
{
    HttpParser<HttpRequest> parser;
    parser.parseLine("GET / HTTP/1.1");
    parser.parseLine("Cookie: session=abc123");
    parser.parseLine("");
    auto result = parser.extractResult();
    NITRO_CHECK(!result.error());
    NITRO_CHECK_EQ(result.message.cookies.at("session"), "abc123");
    co_return;
}

NITRO_TEST(cookie_request_parse_multiple)
{
    HttpParser<HttpRequest> parser;
    parser.parseLine("GET / HTTP/1.1");
    parser.parseLine("Cookie: session=abc123; user=john; theme=dark");
    parser.parseLine("");
    auto result = parser.extractResult();
    NITRO_CHECK(!result.error());
    NITRO_CHECK_EQ(result.message.cookies.at("session"), "abc123");
    NITRO_CHECK_EQ(result.message.cookies.at("user"), "john");
    NITRO_CHECK_EQ(result.message.cookies.at("theme"), "dark");
    co_return;
}

NITRO_TEST(cookie_request_parse_trim_spaces)
{
    HttpParser<HttpRequest> parser;
    parser.parseLine("GET / HTTP/1.1");
    parser.parseLine("Cookie:  session = abc123 ;  user = john ");
    parser.parseLine("");
    auto result = parser.extractResult();
    NITRO_CHECK(!result.error());
    NITRO_CHECK_EQ(result.message.cookies.at("session"), "abc123");
    NITRO_CHECK_EQ(result.message.cookies.at("user"), "john");
    co_return;
}

NITRO_TEST(cookie_request_accessor)
{
    HttpParser<HttpRequest> parser;
    parser.parseLine("GET / HTTP/1.1");
    parser.parseLine("Cookie: a=1; b=2");
    parser.parseLine("");
    HttpRequestAccessor accessor(parser.extractResult().message);
    NITRO_CHECK_EQ(accessor.getCookie("a"), "1");
    NITRO_CHECK_EQ(accessor.getCookie("b"), "2");
    NITRO_CHECK(accessor.getCookie("missing").empty());
    co_return;
}

// ── Response Set-Cookie parsing ───────────────────────────────────────────────

NITRO_TEST(cookie_response_parser_basic)
{
    HttpParser<HttpResponse> parser;
    parser.parseLine("HTTP/1.1 200 OK");
    parser.parseLine("Set-Cookie: session=abc123; Path=/; HttpOnly");
    parser.parseLine("");
    auto result = parser.extractResult();
    NITRO_CHECK(!result.error());
    NITRO_CHECK_EQ(result.message.cookies.size(), 1);
    const auto & c = result.message.cookies[0];
    NITRO_CHECK_EQ(c.name, "session");
    NITRO_CHECK_EQ(c.value, "abc123");
    NITRO_CHECK_EQ(c.path, "/");
    NITRO_CHECK(c.httpOnly);
    NITRO_CHECK(!c.secure);
    co_return;
}

NITRO_TEST(cookie_response_parser_all_attributes)
{
    HttpParser<HttpResponse> parser;
    parser.parseLine("HTTP/1.1 200 OK");
    parser.parseLine("Set-Cookie: id=42; Domain=example.com; Path=/app; Max-Age=3600; Secure; HttpOnly; SameSite=Strict");
    parser.parseLine("");
    auto result = parser.extractResult();
    NITRO_CHECK(!result.error());
    NITRO_CHECK_EQ(result.message.cookies.size(), 1);
    const auto & c = result.message.cookies[0];
    NITRO_CHECK_EQ(c.name, "id");
    NITRO_CHECK_EQ(c.value, "42");
    NITRO_CHECK_EQ(c.domain, "example.com");
    NITRO_CHECK_EQ(c.path, "/app");
    NITRO_CHECK_EQ(c.maxAge, 3600);
    NITRO_CHECK(c.secure);
    NITRO_CHECK(c.httpOnly);
    NITRO_CHECK(c.sameSite == Cookie::SameSite::Strict);
    co_return;
}

NITRO_TEST(cookie_response_parser_multiple)
{
    HttpParser<HttpResponse> parser;
    parser.parseLine("HTTP/1.1 200 OK");
    parser.parseLine("Set-Cookie: a=1");
    parser.parseLine("Set-Cookie: b=2");
    parser.parseLine("");
    auto result = parser.extractResult();
    NITRO_CHECK(!result.error());
    NITRO_CHECK_EQ(result.message.cookies.size(), 2);
    co_return;
}

// ── Integration ───────────────────────────────────────────────────────────────

NITRO_TEST(cookie_integration_server_set)
{
    uint16_t port = 19901;
    Scheduler::current()->spawn([port]() -> Task<> {
        HttpServer server(port);
        server.route("/set", { "GET" }, [](auto req, auto resp) -> Task<> {
            resp->addCookie(Cookie{
                .name = "session",
                .value = "abc123",
                .path = "/",
                .httpOnly = true,
            });
            co_await resp->end("ok");
        });
        co_await server.start();
    });

    co_await sleep(10ms);

    HttpClient client;
    auto resp = co_await client.get("http://127.0.0.1:" + std::to_string(port) + "/set");
    NITRO_CHECK_EQ(resp.statusCode(), 200);
    NITRO_REQUIRE_EQ(resp.cookies().size(), 1);
    NITRO_CHECK_EQ(resp.cookies()[0].name, "session");
    NITRO_CHECK_EQ(resp.cookies()[0].value, "abc123");
    NITRO_CHECK_EQ(resp.cookies()[0].path, "/");
    NITRO_CHECK(resp.cookies()[0].httpOnly);
    co_return;
}

NITRO_TEST(cookie_integration_client_send)
{
    uint16_t port = 19902;
    Scheduler::current()->spawn([port]() -> Task<> {
        HttpServer server(port);
        server.route("/echo", { "GET" }, [](auto req, auto resp) -> Task<> {
            co_await resp->end(req->getCookie("token"));
        });
        co_await server.start();
    });

    co_await sleep(10ms);

    HttpClient client;
    auto [req, respFuture] = co_await client.stream(methods::Get,
                                                    "http://127.0.0.1:" + std::to_string(port) + "/echo");
    req.setCookie("token", "secret");
    co_await req.end();

    auto resp = co_await respFuture.get();
    auto complete = co_await resp.toCompleteResponse();
    NITRO_CHECK_EQ(complete.statusCode(), 200);
    NITRO_CHECK_EQ(complete.body(), "secret");
    co_return;
}

NITRO_TEST(cookie_integration_multiple_set_cookies)
{
    uint16_t port = 19903;
    Scheduler::current()->spawn([port]() -> Task<> {
        HttpServer server(port);
        server.route("/multi", { "GET" }, [](auto req, auto resp) -> Task<> {
            resp->addCookie(Cookie{ .name = "a", .value = "1" });
            resp->addCookie(Cookie{ .name = "b", .value = "2", .secure = true });
            resp->addCookie(Cookie{ .name = "c", .value = "3", .sameSite = Cookie::SameSite::Strict });
            co_await resp->end();
        });
        co_await server.start();
    });

    co_await sleep(10ms);

    HttpClient client;
    auto resp = co_await client.get("http://127.0.0.1:" + std::to_string(port) + "/multi");
    NITRO_CHECK_EQ(resp.statusCode(), 200);
    NITRO_REQUIRE_EQ(resp.cookies().size(), 3);
    NITRO_CHECK_EQ(resp.cookies()[0].name, "a");
    NITRO_CHECK_EQ(resp.cookies()[1].name, "b");
    NITRO_CHECK(resp.cookies()[1].secure);
    NITRO_CHECK_EQ(resp.cookies()[2].name, "c");
    NITRO_CHECK(resp.cookies()[2].sameSite == Cookie::SameSite::Strict);
    co_return;
}

int main(int argc, char ** argv)
{
    return nitrocoro::test::run_all(argc, argv);
}
