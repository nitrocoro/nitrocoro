/**
 * @file http_test.cc
 * @brief Tests for HttpServer, HttpClient, and HttpRouter.
 */
#include <nitrocoro/http/HttpClient.h>
#include <nitrocoro/http/HttpRouter.h>
#include <nitrocoro/http/HttpServer.h>
#include <nitrocoro/testing/Test.h>

using namespace nitrocoro;
using namespace nitrocoro::http;
using namespace std::chrono_literals;

// ── Helpers ───────────────────────────────────────────────────────────────────

static Task<HttpServer *> start_server(HttpServer & server)
{
    Scheduler::current()->spawn([&server]() -> Task<> { co_await server.start(); });
    co_await sleep(50ms);
    co_return &server;
}

// ── Tests ─────────────────────────────────────────────────────────────────────

/** GET route returns 200 with expected body. */
NITRO_TEST(http_get_hello)
{
    HttpServer server(0);
    server.route("GET", "/hello", [](auto && req, auto && resp) -> Task<> {
        co_await resp.end("hello world");
    });
    co_await start_server(server);

    HttpClient client;
    auto resp = co_await client.get("http://127.0.0.1:" + std::to_string(server.listeningPort()) + "/hello");
    NITRO_CHECK_EQ(resp.statusCode(), StatusCode::k200OK);
    NITRO_CHECK_EQ(resp.body(), "hello world");

    co_await server.stop();
}

/** POST route reads request body and echoes it back. */
NITRO_TEST(http_post_echo)
{
    HttpServer server(0);
    server.route("POST", "/echo", [](auto && req, auto && resp) -> Task<> {
        auto complete = co_await req.toCompleteRequest();
        co_await resp.end(complete.body());
    });
    co_await start_server(server);

    HttpClient client;
    auto resp = co_await client.post(
        "http://127.0.0.1:" + std::to_string(server.listeningPort()) + "/echo", "ping");
    NITRO_CHECK_EQ(resp.statusCode(), StatusCode::k200OK);
    NITRO_CHECK_EQ(resp.body(), "ping");

    co_await server.stop();
}

/** Unregistered route returns 404. */
NITRO_TEST(http_404)
{
    HttpServer server(0);
    co_await start_server(server);

    HttpClient client;
    auto resp = co_await client.get("http://127.0.0.1:" + std::to_string(server.listeningPort()) + "/missing");
    NITRO_CHECK_EQ(resp.statusCode(), StatusCode::k404NotFound);

    co_await server.stop();
}

/** Handler can read query parameters from the request. */
NITRO_TEST(http_query_params)
{
    HttpServer server(0);
    server.route("GET", "/greet", [](auto && req, auto && resp) -> Task<> {
        co_await resp.end("Hello, " + req.getQuery("name") + "!");
    });
    co_await start_server(server);

    HttpClient client;
    auto resp = co_await client.get(
        "http://127.0.0.1:" + std::to_string(server.listeningPort()) + "/greet?name=World");
    NITRO_CHECK_EQ(resp.statusCode(), StatusCode::k200OK);
    NITRO_CHECK_EQ(resp.body(), "Hello, World!");

    co_await server.stop();
}

/** Handler can read request headers; response headers are visible to client. */
NITRO_TEST(http_headers)
{
    HttpServer server(0);
    server.route("GET", "/headers", [](auto && req, auto && resp) -> Task<> {
        auto ua = req.getHeader(HttpHeader::NameCode::UserAgent);
        resp.setHeader(HttpHeader::NameCode::ContentType, "text/plain");
        co_await resp.end(ua.empty() ? "no-ua" : "has-ua");
    });
    co_await start_server(server);

    HttpClient client;
    auto resp = co_await client.get(
        "http://127.0.0.1:" + std::to_string(server.listeningPort()) + "/headers");
    NITRO_CHECK_EQ(resp.statusCode(), StatusCode::k200OK);
    NITRO_CHECK_EQ(resp.getHeader(HttpHeader::NameCode::ContentType), "text/plain");

    co_await server.stop();
}

/** stop() causes start() to return cleanly. */
NITRO_TEST(http_server_stop)
{
    HttpServer server(0);
    bool stopped = false;

    Scheduler::current()->spawn([TEST_CTX, &server, &stopped]() -> Task<> {
        co_await server.start();
        stopped = true;
    });

    co_await sleep(50ms);
    co_await server.stop();
    co_await sleep(50ms);

    NITRO_CHECK(stopped);
}

/** Multiple sequential requests on the same server (keep-alive path). */
NITRO_TEST(http_multiple_requests)
{
    HttpServer server(0);
    int count = 0;
    server.route("GET", "/count", [&count](auto && req, auto && resp) -> Task<> {
        co_await resp.end(std::to_string(++count));
    });
    co_await start_server(server);

    std::string base = "http://127.0.0.1:" + std::to_string(server.listeningPort()) + "/count";
    HttpClient client;
    for (int i = 1; i <= 3; ++i)
    {
        auto resp = co_await client.get(base);
        NITRO_CHECK_EQ(resp.statusCode(), StatusCode::k200OK);
        NITRO_CHECK_EQ(resp.body(), std::to_string(i));
    }

    co_await server.stop();
}

/** Shared router: two servers on different ports serve the same routes. */
NITRO_TEST(router_shared_across_servers)
{
    auto router = std::make_shared<HttpRouter>();
    router->route("GET", "/ping", [](auto && req, auto && resp) -> Task<> {
        co_await resp.end("pong");
    });

    HttpServer s1(0, router);
    HttpServer s2(0, router);
    co_await start_server(s1);
    co_await start_server(s2);

    HttpClient client;
    auto r1 = co_await client.get("http://127.0.0.1:" + std::to_string(s1.listeningPort()) + "/ping");
    auto r2 = co_await client.get("http://127.0.0.1:" + std::to_string(s2.listeningPort()) + "/ping");
    NITRO_CHECK_EQ(r1.statusCode(), StatusCode::k200OK);
    NITRO_CHECK_EQ(r1.body(), "pong");
    NITRO_CHECK_EQ(r2.statusCode(), StatusCode::k200OK);
    NITRO_CHECK_EQ(r2.body(), "pong");

    co_await s1.stop();
    co_await s2.stop();
}

/** Wrong method on a registered path returns 404. */
NITRO_TEST(router_method_mismatch_404)
{
    HttpServer server(0);
    server.route("POST", "/data", [](auto && req, auto && resp) -> Task<> {
        co_await resp.end("ok");
    });
    co_await start_server(server);

    HttpClient client;
    auto resp = co_await client.get("http://127.0.0.1:" + std::to_string(server.listeningPort()) + "/data");
    NITRO_CHECK_EQ(resp.statusCode(), StatusCode::k404NotFound);

    co_await server.stop();
}

int main(int argc, char ** argv)
{
    return nitrocoro::test::run_all(argc, argv);
}
