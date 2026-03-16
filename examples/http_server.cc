/**
 * @file http_server.cc
 * @brief Simple HTTP server test
 */
#include <getopt.h>
#include <nitrocoro/core/Scheduler.h>
#include <nitrocoro/http/Form.h>
#include <nitrocoro/http/HttpServer.h>
#include <nitrocoro/utils/Debug.h>
#include <thread>
#include <vector>

using namespace nitrocoro;
using namespace nitrocoro::http;

Task<> server_main(uint16_t port)
{
    HttpServer server(port);

    server.route("/", { "GET" }, [](auto req, auto resp) -> Task<> {
        resp->setStatus(StatusCode::k200OK);
        resp->setHeader("Content-Type", "text/html; charset=utf-8");
        co_await resp->end("<h1>Hello, World!</h1>");
    });

    server.route("/large", { "GET" }, [](IncomingRequestPtr req, ServerResponsePtr resp) -> Task<> {
        resp->setStatus(StatusCode::k200OK);
        resp->setHeader("Content-Type", "text/html; charset=utf-8");
        std::string largeBody(1024 * 1024, 'a');
        co_await resp->end(largeBody);
    });

    server.route("/hello", { "GET" }, [](IncomingRequestPtr req, ServerResponsePtr resp) -> Task<> {
        auto & name = req->getQuery("name");
        std::string body = "Hello, ";
        body += name.empty() ? "Guest" : name;
        body += "!";

        resp->setStatus(StatusCode::k200OK);
        resp->setHeader("Content-Type", "text/plain");
        co_await resp->end(body);
    });

    server.route("/sleep", { "GET" }, [](IncomingRequestPtr req, ServerResponsePtr resp) -> Task<> {
        utils::StringBuffer bodyBuf;
        co_await req->readToEnd(bodyBuf);
        co_await sleep(std::chrono::seconds(3));
        resp->setStatus(StatusCode::k200OK);
        resp->setHeader("Content-Type", "text/plain");
        co_await resp->end("wakeup after 3 seconds");
    });

    server.route("/echo", { "POST" }, [](IncomingRequestPtr req, ServerResponsePtr resp) -> Task<> {
        utils::StringBuffer bodyBuf;
        co_await req->readToEnd(bodyBuf);
        resp->setStatus(StatusCode::k200OK);
        resp->setHeader("Content-Type", "text/plain");
        co_await resp->end(bodyBuf.view());
    });

    server.route("/form", { "POST" }, [](IncomingRequestPtr req, ServerResponsePtr resp) -> Task<> {
        utils::StringBuffer bodyBuf;
        co_await req->readToEnd(bodyBuf);

        // Parse form data
        auto formData = http::parseFormData(bodyBuf.view());

        std::string response = "Form data received:\n";
        for (const auto & [key, value] : formData)
        {
            response += (key + " = " + value + "\n");
        }

        resp->setStatus(StatusCode::k200OK);
        resp->setHeader("Content-Type", "text/plain");
        co_await resp->end(response);
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

    printf("=== HTTP Server Test === threads=%zu\n"
           "Try:\n"
           "  curl http://localhost:%hu/\n"
           "  curl http://localhost:%hu/hello?name=Alice\n"
           "  curl -X POST -d 'test data' http://localhost:%hu/echo\n"
           "  curl -X POST -d 'name=Alice&age=25' http://localhost:%hu/form\n",
           threadCount, port, port, port, port);
    for (auto & t : threads)
        t.join();

    return 0;
}
