/**
 * @file stress_server.cc
 * @brief Stress test server — TCP echo + HTTP /ping, multiple ports
 *
 * Usage:
 *   ./stress_server [-t threads] [-p tcp_base_port] [-P http_base_port] [-n port_count]
 *
 * Defaults: tcp=9001, http=9101, ports=4
 * Example:  -n 4 opens tcp 9001-9004, http 9101-9104
 */
#include <nitrocoro/core/Scheduler.h>
#include <nitrocoro/http/HttpServer.h>
#include <nitrocoro/net/TcpConnection.h>
#include <nitrocoro/net/TcpServer.h>

#include <atomic>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

using namespace nitrocoro;
using namespace nitrocoro::net;
using namespace nitrocoro::http;

static std::atomic<int64_t> g_tcp_conns{ 0 };   // current active
static std::atomic<int64_t> g_tcp_total{ 0 };   // cumulative accepted
static std::atomic<int64_t> g_tcp_closed{ 0 };  // cumulative closed
static std::atomic<int64_t> g_tcp_bytes{ 0 };   // cumulative bytes transferred
static std::atomic<int64_t> g_http_reqs{ 0 };

// ── TCP echo ──────────────────────────────────────────────────────────────────

Task<> tcp_handler(TcpConnectionPtr conn)
{
    ++g_tcp_conns;
    ++g_tcp_total;
    char buf[4096];
    while (true)
    {
        size_t n = co_await conn->read(buf, sizeof(buf));
        if (n == 0)
            break;
        g_tcp_bytes += (int64_t)n;
        co_await conn->write(buf, n);
        g_tcp_bytes += (int64_t)n;
    }
    --g_tcp_conns;
    ++g_tcp_closed;
}

Task<> run_tcp(uint16_t port)
{
    try
    {
        TcpServer server(port);
        printf("[TCP ] listening on :%hu\n", port);
        co_await server.start(tcp_handler);
    }
    catch (const std::exception & e)
    {
        fprintf(stderr, "[TCP ] :%hu failed: %s\n", port, e.what());
    }
}

// ── HTTP ──────────────────────────────────────────────────────────────────────

Task<> run_http(uint16_t port)
{
    try
    {
        HttpServer server({ .port = port, .send_date_header = false });

        server.route("/ping", { "GET" }, [](IncomingRequestPtr, ServerResponsePtr resp) {
            ++g_http_reqs;
            resp->setStatus(StatusCode::k200OK);
            resp->setBody("pong");
        });

        printf("[HTTP] listening on :%hu  (GET /ping)\n", port);
        co_await server.start();
    }
    catch (const std::exception & e)
    {
        fprintf(stderr, "[HTTP] :%hu failed: %s\n", port, e.what());
    }
}

// ── Stats reporter ────────────────────────────────────────────────────────────

Task<> stats_reporter()
{
    int64_t prev_bytes     = 0;
    int64_t prev_http_reqs = 0;

    while (true)
    {
        co_await sleep(std::chrono::seconds(5));

        int64_t bytes      = g_tcp_bytes.load();
        int64_t http_reqs  = g_http_reqs.load();
        double  tx_mbps    = (bytes - prev_bytes) / 5.0 / (1024.0 * 1024.0);
        int64_t http_qps   = (http_reqs - prev_http_reqs) / 5;
        prev_bytes         = bytes;
        prev_http_reqs     = http_reqs;

        printf("[stats] tcp: active=%" PRId64 "  total=%" PRId64 "  closed=%" PRId64 "  tx=%.2f MB/s"
               "  |  http: qps=%" PRId64 "  total=%" PRId64 "\n",
               g_tcp_conns.load(), g_tcp_total.load(), g_tcp_closed.load(), tx_mbps,
               http_qps, http_reqs);
    }
}

// ── main ──────────────────────────────────────────────────────────────────────

int main(int argc, char * argv[])
{
    uint16_t tcp_base  = 19001;
    uint16_t http_base = 19101;
    int      port_count = 10;
    size_t   threads    = 1;

    for (int i = 1; i < argc; ++i)
    {
        if      (!strcmp(argv[i], "-p") && i + 1 < argc)  tcp_base   = (uint16_t)atoi(argv[++i]);
        else if (!strcmp(argv[i], "-P") && i + 1 < argc)  http_base  = (uint16_t)atoi(argv[++i]);
        else if (!strcmp(argv[i], "-n") && i + 1 < argc)  port_count = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-t") && i + 1 < argc)  threads    = (size_t)atoi(argv[++i]);
    }

    printf("stress_server  threads=%zu  tcp=:%hu~%hu  http=:%hu~%hu\n",
           threads,
           tcp_base,  (uint16_t)(tcp_base  + port_count - 1),
           http_base, (uint16_t)(http_base + port_count - 1));

    auto worker = [tcp_base, http_base, port_count]() {
        Scheduler sched;
        for (int i = 0; i < port_count; ++i)
        {
            uint16_t tp = (uint16_t)(tcp_base  + i);
            uint16_t hp = (uint16_t)(http_base + i);
            sched.spawn([tp]() -> Task<> { co_await run_tcp(tp); });
            sched.spawn([hp]() -> Task<> { co_await run_http(hp); });
        }
        sched.spawn([]() -> Task<> { co_await stats_reporter(); });
        sched.run();
    };

    std::vector<std::thread> pool;
    pool.reserve(threads);
    for (size_t i = 0; i < threads; ++i)
        pool.emplace_back(worker);
    for (auto & t : pool)
        t.join();
}
