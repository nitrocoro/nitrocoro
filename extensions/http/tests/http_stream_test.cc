/**
 * @file http_stream_test.cc
 * @brief Test streaming HTTP client with echo server
 */
#include <nitrocoro/http/HttpClient.h>
#include <nitrocoro/http/HttpServer.h>
#include <nitrocoro/testing/Test.h>

using namespace nitrocoro;
using namespace nitrocoro::http;
using namespace std::chrono_literals;

Task<> echo_server(uint16_t port)
{
    HttpServer server(port);

    server.route("/stream-echo", { "POST" }, [](auto req, auto resp) -> Task<> {
        resp->setStatus(StatusCode::k200OK);
        resp->setHeader(HttpHeader::NameCode::ContentType, "text/plain");
        auto ctl = req->getHeader(HttpHeader::NameCode::ContentLength);
        if (!ctl.empty())
            resp->setHeader(HttpHeader::NameCode::ContentLength, std::string{ ctl });

        co_await resp->write({});

        while (true)
        {
            auto chunk = co_await req->read(1024);
            if (chunk.empty())
                break;
            co_await resp->write(chunk);
        }

        co_await resp->end();
    });

    co_await server.start();
}

Task<> run_stream_test(uint16_t port, bool useChunk, nitrocoro::test::TestCtxPtr TEST_CTX)
{
    co_await sleep(10ms);

    HttpClient client;
    auto [req, respFuture] = co_await client.stream(methods::Post, "http://127.0.0.1:" + std::to_string(port) + "/stream-echo");

    std::vector<std::string> reqChunks, respChunks;

    Promise<> finishPromise{ Scheduler::current() };
    Scheduler::current()->spawn([&]() -> Task<> {
        auto response = co_await respFuture.get();
        while (true)
        {
            try
            {
                auto chunk = co_await response.read(1024);
                if (chunk.empty())
                    break;
                respChunks.emplace_back(chunk);
            }
            catch (...)
            {
                break;
            }
        }
        finishPromise.set_value();
    });

    std::string data = "Hello World from streaming client!";
    if (useChunk)
        req.setHeader(HttpHeader::NameCode::TransferEncoding, "chunked");
    else
        req.setHeader(HttpHeader::NameCode::ContentLength, std::to_string(data.size()));

    size_t pos = 0, chunkSize = 6;
    while (pos < data.size())
    {
        size_t len = std::min(chunkSize, data.size() - pos);
        co_await sleep(10ms);
        co_await req.write(data.substr(pos, len));
        reqChunks.push_back(data.substr(pos, len));
        pos += len;
    }

    co_await req.end();
    co_await finishPromise.get_future().get();

    NITRO_REQUIRE_EQ(reqChunks.size(), respChunks.size());
    for (size_t i = 0; i < reqChunks.size(); ++i)
        NITRO_CHECK_EQ(reqChunks[i], respChunks[i]);
}

NITRO_TEST(stream_echo_chunked)
{
    uint16_t port = 9998;
    Scheduler::current()->spawn([port]() -> Task<> { co_await echo_server(port); });
    co_await run_stream_test(port, true, TEST_CTX);
}

NITRO_TEST(stream_echo_content_length)
{
    uint16_t port = 9999;
    Scheduler::current()->spawn([port]() -> Task<> { co_await echo_server(port); });
    co_await run_stream_test(port, false, TEST_CTX);
}

int main(int argc, char ** argv)
{
    return nitrocoro::test::run_all(argc, argv);
}
