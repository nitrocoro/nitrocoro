/**
 * @file http2_server.cc
 * @brief HTTP/2 server example (h2c plaintext)
 */
#include <getopt.h>
#include <nitrocoro/core/Scheduler.h>
#include <nitrocoro/http/HttpServer.h>
#include <nitrocoro/http2/Http2Server.h>
#include <thread>
#include <vector>

using namespace nitrocoro;
using namespace nitrocoro::http;

Task<> server_main(uint16_t port)
{
    HttpServer server({ .port = port, .send_date_header = false });

    http2::enableHttp2(server);

    server.route("/", { "GET" }, [](auto req, auto resp) {
        resp->setStatus(StatusCode::k200OK);
        resp->setHeader("Content-Type", "text/html; charset=utf-8");
        resp->setBody("<h1>Hello, HTTP/2!</h1>");
    });

    server.route("/hello", { "GET" }, [](IncomingRequestPtr req, ServerResponsePtr resp) {
        auto & name = req->getQuery("name");
        std::string body = "Hello, ";
        body += name.empty() ? "Guest" : name;
        body += "!";
        resp->setStatus(StatusCode::k200OK);
        resp->setHeader("Content-Type", "text/plain");
        resp->setBody(body);
    });

    server.route("/echo", { "POST" }, [](IncomingRequestPtr req, ServerResponsePtr resp) -> Task<> {
        utils::StringBuffer buf;
        co_await req->readToEnd(buf);
        resp->setStatus(StatusCode::k200OK);
        resp->setHeader("Content-Type", "text/plain");
        resp->setBody(buf.extract());
    });

    co_await server.start();
}

int main(int argc, char * argv[])
{
    uint16_t port = 8080;
    size_t threadCount = 1;

    int opt;
    while ((opt = getopt(argc, argv, "p:t:")) != -1)
    {
        switch (opt)
        {
            case 'p':
                port = std::stoi(optarg);
                break;
            case 't':
                threadCount = std::stoi(optarg);
                break;
            default:
                fprintf(stderr, "Usage: %s [-p port] [-t threads]\n", argv[0]);
                return 1;
        }
    }

    auto runWorker = [port]() {
        Scheduler scheduler;
        scheduler.spawn([port]() -> Task<> { co_await server_main(port); });
        scheduler.run();
    };

    std::vector<std::thread> threads;
    threads.reserve(threadCount);
    for (size_t i = 0; i < threadCount; ++i)
        threads.emplace_back(runWorker);

    printf("=== HTTP/2 Server (h2c) === port=%hu threads=%zu\n"
           "Try (requires curl with HTTP/2 support):\n"
           "  curl --http2-prior-knowledge http://localhost:%hu/\n"
           "  curl --http2-prior-knowledge http://localhost:%hu/hello?name=Alice\n"
           "  curl --http2-prior-knowledge -X POST -d 'hello' http://localhost:%hu/echo\n",
           port, threadCount, port, port, port);

    for (auto & t : threads)
        t.join();

    return 0;
}
