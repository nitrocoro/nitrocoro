/**
 * @file http_pipeline_test.cc
 * @brief Tests for HTTP/1.1 pipelining over a single keep-alive connection.
 */
#include <nitrocoro/http/HttpServer.h>
#include <nitrocoro/net/InetAddress.h>
#include <nitrocoro/net/TcpConnection.h>
#include <nitrocoro/testing/Test.h>

using namespace nitrocoro;
using namespace nitrocoro::http;

static SharedFuture<> start_server(HttpServer & server)
{
    Scheduler::current()->spawn([&server]() -> Task<> { co_await server.start(); });
    return server.started();
}

// Read one complete HTTP/1.1 response (content-length) from shared buf.
static Task<std::pair<std::string, std::string>> readResponse(
    net::TcpConnectionPtr conn, std::string & buf)
{
    char tmp[4096];

    while (buf.find("\r\n\r\n") == std::string::npos)
    {
        size_t n = co_await conn->read(tmp, sizeof(tmp));
        if (n == 0)
            throw std::runtime_error("connection closed");
        buf.append(tmp, n);
    }

    size_t headerEnd = buf.find("\r\n\r\n") + 4;
    auto clPos = buf.find("content-length: ");
    if (clPos == std::string::npos)
        throw std::runtime_error("no content-length in response");
    size_t cl = std::stoul(buf.substr(clPos + 16));

    while (buf.size() - headerEnd < cl)
    {
        size_t n = co_await conn->read(tmp, sizeof(tmp));
        if (n == 0)
            throw std::runtime_error("connection closed");
        buf.append(tmp, n);
    }

    std::string headers = buf.substr(0, headerEnd);
    std::string body = buf.substr(headerEnd, cl);
    buf = buf.substr(headerEnd + cl);
    co_return { headers, body };
}

/** N pipelined GET requests sent at once, responses read in order. */
NITRO_TEST(http_pipeline_gets)
{
    HttpServer server(0);
    server.route("/echo", { "GET" }, [](auto && req, auto && resp) -> Task<> {
        co_await resp.end(req.getQuery("v"));
    });
    co_await start_server(server);

    auto conn = co_await net::TcpConnection::connect(
        net::InetAddress("127.0.0.1", server.listeningPort()));

    constexpr int N = 5;
    std::string reqs;
    for (int i = 0; i < N; ++i)
        reqs += "GET /echo?v=" + std::to_string(i) + " HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n";
    co_await conn->write(reqs.data(), reqs.size());

    std::string buf;
    for (int i = 0; i < N; ++i)
    {
        auto [h, b] = co_await readResponse(conn, buf);
        NITRO_CHECK(h.find("200 OK") != std::string::npos);
        NITRO_CHECK_EQ(b, std::to_string(i));
    }

    co_await server.stop();
}

/** Pipelined chunked POSTs followed by GETs, all sent at once. */
NITRO_TEST(http_pipeline_chunked_then_gets)
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

    constexpr int N = 10;
    std::string reqs;
    for (int i = 0; i < N; ++i)
    {
        std::string body = "msg" + std::to_string(i);
        std::string chunkSize = std::to_string(body.size());
        reqs.append("POST /echo HTTP/1.1\r\nHost: 127.0.0.1\r\nTransfer-Encoding: chunked\r\n\r\n")
            .append(chunkSize)
            .append("\r\n")
            .append(body)
            .append("\r\n0\r\n\r\n");
        reqs.append("GET /ping HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n");
    }
    co_await conn->write(reqs.data(), reqs.size());

    std::string buf;
    for (int i = 0; i < N; ++i)
    {
        auto [h1, b1] = co_await readResponse(conn, buf);
        NITRO_CHECK(h1.find("200 OK") != std::string::npos);
        NITRO_CHECK_EQ(b1, "msg" + std::to_string(i));

        auto [h2, b2] = co_await readResponse(conn, buf);
        NITRO_CHECK(h2.find("200 OK") != std::string::npos);
        NITRO_CHECK_EQ(b2, "pong");
    }

    co_await server.stop();
}

/** Pipelined content-length POSTs sent at once. */
NITRO_TEST(http_pipeline_content_length_posts)
{
    HttpServer server(0);
    server.route("/echo", { "POST" }, [](auto && req, auto && resp) -> Task<> {
        auto complete = co_await req.toCompleteRequest();
        co_await resp.end(complete.body());
    });
    co_await start_server(server);

    auto conn = co_await net::TcpConnection::connect(
        net::InetAddress("127.0.0.1", server.listeningPort()));

    constexpr int N = 5;
    std::string reqs;
    for (int i = 0; i < N; ++i)
    {
        std::string body = "msg" + std::to_string(i);
        reqs.append("POST /echo HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Length: ")
            .append(std::to_string(body.size()))
            .append("\r\n\r\n")
            .append(body);
    }
    co_await conn->write(reqs.data(), reqs.size());

    std::string buf;
    for (int i = 0; i < N; ++i)
    {
        auto [h, b] = co_await readResponse(conn, buf);
        NITRO_CHECK(h.find("200 OK") != std::string::npos);
        NITRO_CHECK_EQ(b, "msg" + std::to_string(i));
    }

    co_await server.stop();
}

int main(int argc, char ** argv)
{
    return nitrocoro::test::run_all(argc, argv);
}
