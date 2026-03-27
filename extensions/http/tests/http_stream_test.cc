/**
 * @file http_stream_test.cc
 * @brief Tests for streaming HTTP request and response bodies
 */
#include <nitrocoro/http/HttpClient.h>
#include <nitrocoro/http/HttpServer.h>
#include <nitrocoro/testing/Test.h>

using namespace nitrocoro;
using namespace nitrocoro::http;

static SharedFuture<> start_server(HttpServer & server)
{
    Scheduler::current()->spawn([&server]() -> Task<> { co_await server.start(); });
    return server.started();
}

/** Server uses setBody(BodyStream) to send a chunked response; client receives all chunks joined. */
NITRO_TEST(server_streaming_response)
{
    HttpServer server(0);

    server.route("/stream", { "GET" }, [](auto req, auto resp) {
        std::vector<std::string> chunks = { "hello", " ", "world" };
        resp->setBody([chunks = std::move(chunks)](auto & writer) -> Task<> {
            for (auto && chunk : chunks)
            {
                co_await sleep(std::chrono::milliseconds(10));
                co_await writer.write(chunk);
            }
        });
    });
    co_await start_server(server);

    HttpClient client;
    auto resp = co_await client.get("http://127.0.0.1:" + std::to_string(server.listeningPort()) + "/stream");
    NITRO_CHECK_EQ(resp.statusCode(), StatusCode::k200OK);
    NITRO_CHECK_EQ(resp.body(), "hello world");

    co_await server.stop();
}

/** Client uses stream() with a streaming request body; server echoes the complete body back. */
NITRO_TEST(client_streaming_request)
{
    HttpServer server(0);

    server.route("/echo", { "POST" }, [](auto req, auto resp) -> Task<> {
        auto complete = co_await req->toCompleteRequest();
        resp->setBody(complete.body());
    });
    co_await start_server(server);

    std::string data = "Hello World from streaming client!";
    std::vector<std::string> chunks = { data.substr(0, 6), data.substr(6, 6), data.substr(12) };

    HttpClient client;
    auto [req, respFuture] = co_await client.stream(
        methods::Post, "http://127.0.0.1:" + std::to_string(server.listeningPort()) + "/echo");

    req.setBody([chunks = std::move(chunks)](auto & writer) -> Task<> {
        for (auto && chunk : chunks)
        {
            co_await writer.write(chunk);
        }
    });
    co_await req.flush();

    auto response = co_await respFuture.get();
    auto complete = co_await response.toCompleteResponse();
    NITRO_CHECK_EQ(complete.statusCode(), StatusCode::k200OK);
    NITRO_CHECK_EQ(complete.body(), data);

    co_await server.stop();
}

int main(int argc, char ** argv)
{
    return nitrocoro::test::run_all(argc, argv);
}
