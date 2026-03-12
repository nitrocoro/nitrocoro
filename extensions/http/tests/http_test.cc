/**
 * @file http_test.cc
 * @brief Tests for HttpServer, HttpClient, and HttpRouter.
 */
#include <nitrocoro/http/HttpClient.h>
#include <nitrocoro/http/HttpRouter.h>
#include <nitrocoro/http/HttpServer.h>
#include <nitrocoro/net/InetAddress.h>
#include <nitrocoro/net/TcpConnection.h>
#include <nitrocoro/testing/Test.h>

using namespace nitrocoro;
using namespace nitrocoro::http;
using namespace std::chrono_literals;

// ── Helpers ───────────────────────────────────────────────────────────────────

static SharedFuture<> start_server(HttpServer & server)
{
    Scheduler::current()->spawn([&server]() -> Task<> { co_await server.start(); });
    return server.started();
}

// ── Tests ─────────────────────────────────────────────────────────────────────

/** GET route returns 200 with expected body. */
NITRO_TEST(http_get_hello)
{
    HttpServer server(0);
    server.route("/hello", { "GET" }, [](auto && req, auto && resp) -> Task<> {
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
    server.route("/echo", { "POST" }, [](auto && req, auto && resp) -> Task<> {
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
    server.route("/greet", { "GET" }, [](auto && req, auto && resp) -> Task<> {
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
    server.route("/headers", { "GET" }, [](auto && req, auto && resp) -> Task<> {
        const auto & ua = req.getHeader(HttpHeader::NameCode::UserAgent);
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

    co_await server.started();
    co_await server.stop();
    co_await sleep(10ms);

    NITRO_CHECK(stopped);
}

/** Multiple sequential requests on the same server (keep-alive path). */
NITRO_TEST(http_multiple_requests)
{
    HttpServer server(0);
    int count = 0;
    server.route("/count", { "GET" }, [&count](auto && req, auto && resp) -> Task<> {
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
    router->addRoute("/ping", { "GET" }, [](auto && req, auto && resp) -> Task<> {
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

/** Wrong method on a registered path returns 405. */
NITRO_TEST(router_method_mismatch_405)
{
    HttpServer server(0);
    server.route("/data", { "POST" }, [](auto && req, auto && resp) -> Task<> {
        co_await resp.end("ok");
    });
    co_await start_server(server);

    HttpClient client;
    auto resp = co_await client.get("http://127.0.0.1:" + std::to_string(server.listeningPort()) + "/data");
    NITRO_CHECK_EQ(resp.statusCode(), StatusCode::k405MethodNotAllowed);

    co_await server.stop();
}

/** Path with percent-encoded characters is decoded before routing. */
NITRO_TEST(http_path_percent_encoding)
{
    HttpServer server(0);
    server.route("/hello world", { "GET" }, [](auto && req, auto && resp) -> Task<> {
        co_await resp.end(req.path());
    });
    co_await start_server(server);

    HttpClient client;
    auto resp = co_await client.get(
        "http://127.0.0.1:" + std::to_string(server.listeningPort()) + "/hello%20world");
    NITRO_CHECK_EQ(resp.statusCode(), StatusCode::k200OK);
    NITRO_CHECK_EQ(resp.body(), "/hello world");

    co_await server.stop();
}

/** Query value with %20 and + are both decoded to space. */
NITRO_TEST(http_query_decode)
{
    HttpServer server(0);
    server.route("/q", { "GET" }, [](auto && req, auto && resp) -> Task<> {
        co_await resp.end(req.getQuery("a") + "|" + req.getQuery("b"));
    });
    co_await start_server(server);

    HttpClient client;
    auto resp = co_await client.get(
        "http://127.0.0.1:" + std::to_string(server.listeningPort()) + "/q?a=hello%20world&b=hello+world");
    NITRO_CHECK_EQ(resp.statusCode(), StatusCode::k200OK);
    NITRO_CHECK_EQ(resp.body(), "hello world|hello world");

    co_await server.stop();
}

/** Path with invalid percent-encoded sequence is kept as-is. */
NITRO_TEST(http_path_invalid_encoding)
{
    HttpServer server(0);
    server.route("/foo%zz", { "GET" }, [](auto && req, auto && resp) -> Task<> {
        co_await resp.end(req.path());
    });
    co_await start_server(server);

    HttpClient client;
    auto resp = co_await client.get(
        "http://127.0.0.1:" + std::to_string(server.listeningPort()) + "/foo%zz");
    NITRO_CHECK_EQ(resp.statusCode(), StatusCode::k200OK);
    NITRO_CHECK_EQ(resp.body(), "/foo%zz");

    co_await server.stop();
}

/** Handler throws: connection is closed, server continues accepting new connections. */
NITRO_TEST(http_handler_throws)
{
    HttpServer server(0);
    server.route("/throw", { "GET" }, [](auto && req, auto && resp) -> Task<> {
        throw std::runtime_error("handler error");
    });
    server.route("/ok", { "GET" }, [](auto && req, auto && resp) -> Task<> {
        co_await resp.end("ok");
    });
    co_await start_server(server);

    std::string base = "http://127.0.0.1:" + std::to_string(server.listeningPort());

    // Handler throws: connection should be closed, client gets an exception
    NITRO_CHECK_THROWS(co_await HttpClient{}.get(base + "/throw"));

    // Server still works after handler threw
    auto resp = co_await HttpClient{}.get(base + "/ok");
    NITRO_CHECK_EQ(resp.statusCode(), StatusCode::k200OK);
    NITRO_CHECK_EQ(resp.body(), "ok");

    co_await server.stop();
}

/** Chunked POST followed by GET on the same keep-alive connection. */
NITRO_TEST(http_chunked_keepalive)
{
    HttpServer server(0);
    server.route("/echo", { "POST" }, [](auto && req, auto && resp) -> Task<> {
        auto complete = co_await req.toCompleteRequest();
        co_await resp.end(complete.body());
    });
    server.route("/ping", { "GET" }, [](auto && req, auto && resp) -> Task<> {
        co_await resp.end("pong");
    });
    co_await start_server(server);

    auto conn = co_await net::TcpConnection::connect(
        net::InetAddress("127.0.0.1", server.listeningPort()));

    // Chunked POST
    std::string req1 = "POST /echo HTTP/1.1\r\n"
                       "Host: 127.0.0.1\r\n"
                       "Transfer-Encoding: chunked\r\n"
                       "\r\n"
                       "5\r\nhello\r\n"
                       "0\r\n"
                       "\r\n";
    co_await conn->write(req1.data(), req1.size());

    // Read response 1
    std::string resp1;
    char buf[4096];
    while (resp1.find("\r\n\r\n") == std::string::npos)
    {
        size_t n = co_await conn->read(buf, sizeof(buf));
        resp1.append(buf, n);
    }
    auto clPos = resp1.find("content-length: ");
    NITRO_REQUIRE(clPos != std::string::npos);
    size_t cl = std::stoul(resp1.substr(clPos + 16));
    size_t headerEnd = resp1.find("\r\n\r\n") + 4;
    while (resp1.size() - headerEnd < cl)
    {
        size_t n = co_await conn->read(buf, sizeof(buf));
        resp1.append(buf, n);
    }
    NITRO_CHECK(resp1.find("200 OK") != std::string::npos);
    NITRO_CHECK_EQ(resp1.substr(headerEnd, cl), "hello");

    // GET on same connection
    std::string req2 = "GET /ping HTTP/1.1\r\n"
                       "Host: 127.0.0.1\r\n"
                       "\r\n";
    co_await conn->write(req2.data(), req2.size());

    std::string resp2;
    while (resp2.find("\r\n\r\n") == std::string::npos)
    {
        size_t n = co_await conn->read(buf, sizeof(buf));
        resp2.append(buf, n);
    }
    auto cl2Pos = resp2.find("content-length: ");
    NITRO_REQUIRE(cl2Pos != std::string::npos);
    size_t cl2 = std::stoul(resp2.substr(cl2Pos + 16));
    size_t headerEnd2 = resp2.find("\r\n\r\n") + 4;
    while (resp2.size() - headerEnd2 < cl2)
    {
        size_t n = co_await conn->read(buf, sizeof(buf));
        resp2.append(buf, n);
    }
    NITRO_CHECK(resp2.find("200 OK") != std::string::npos);
    NITRO_CHECK_EQ(resp2.substr(headerEnd2, cl2), "pong");

    co_await server.stop();
}

int main(int argc, char ** argv)
{
    return nitrocoro::test::run_all(argc, argv);
}
