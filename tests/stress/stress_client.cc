/**
 * @file stress_client.cc
 * @brief Stress test client — ramp up to N concurrent connections across multiple ports
 *
 * Usage:
 *   ./stress_client --mode tcp|http
 *                   [--host 127.0.0.1]
 *                   [--port PORT]       base port (default: tcp=9001, http=9101)
 *                   [--ports N]         number of ports, round-robin (default 4)
 *                   [--conns N]         target concurrent connections (default 1000)
 *                   [--ramp-rate R]     new conns/sec (default 100)
 *                   [--duration D]      seconds to hold after ramp (default 30)
 *                   [--threads T]       scheduler threads (default 1)
 */
#include <nitrocoro/core/Scheduler.h>
#include <nitrocoro/http/HttpClient.h>
#include <nitrocoro/net/InetAddress.h>
#include <nitrocoro/net/TcpConnection.h>

#include <atomic>
#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

using namespace nitrocoro;
using namespace nitrocoro::net;
using namespace nitrocoro::http;
using Clock = std::chrono::steady_clock;

// ── Config ────────────────────────────────────────────────────────────────────

struct Config
{
    enum class Mode { Tcp, Http };
    Mode        mode       = Mode::Tcp;
    std::string host       = "127.0.0.1";
    uint16_t    port       = 19001;   // base port
    int         port_count = 10;
    int         conns      = 10000;
    int         ramp_rate  = 100;
    int         duration   = 30;
    int         threads    = 1;
};

// ── Stats ─────────────────────────────────────────────────────────────────────

struct Stats
{
    std::atomic<int64_t> connected{ 0 };
    std::atomic<int64_t> errors{ 0 };
    std::atomic<int64_t> requests{ 0 };
    std::atomic<int64_t> latency_us_sum{ 0 };

    void record(int64_t us) { ++requests; latency_us_sum += us; }
};

static Stats g_stats;
static std::atomic<bool> g_done{ false };

// ── Per-connection workers ────────────────────────────────────────────────────

Task<> tcp_worker(InetAddress addr)
{
    TcpConnectionPtr conn;
    try
    {
        conn = co_await TcpConnection::connect(addr);
    }
    catch (...)
    {
        ++g_stats.errors;
        co_return;
    }

    ++g_stats.connected;
    const char ping[] = "ping";
    char buf[4];

    while (!g_done)
    {
        try
        {
            auto t0 = Clock::now();
            co_await conn->write(ping, 4);
            size_t n = co_await conn->read(buf, sizeof(buf));
            if (n == 0)
                break;
            auto us = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - t0).count();
            g_stats.record(us);
        }
        catch (...)
        {
            ++g_stats.errors;
            break;
        }
    }

    --g_stats.connected;
}

Task<> http_worker(std::string base_url)
{
    HttpClient client(base_url);
    bool counted = false;

    while (!g_done)
    {
        bool failed = false;
        try
        {
            auto t0 = Clock::now();
            co_await client.get("/ping");
            auto us = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - t0).count();
            if (!counted) { ++g_stats.connected; counted = true; }
            g_stats.record(us);
        }
        catch (...)
        {
            ++g_stats.errors;
            if (counted) { --g_stats.connected; counted = false; }
            failed = true;
        }
        if (failed)
            co_await sleep(std::chrono::milliseconds(500));
    }

    if (counted)
        --g_stats.connected;
}

// ── Ramp controller ───────────────────────────────────────────────────────────

Task<> ramp_controller(Config cfg)
{
    auto * sched    = Scheduler::current();
    auto   interval = std::chrono::microseconds(1'000'000 / cfg.ramp_rate);

    printf("[ramp] spawning %d conns across %d ports at %d/sec...\n",
           cfg.conns, cfg.port_count, cfg.ramp_rate);

    for (int i = 0; i < cfg.conns; ++i)
    {
        uint16_t port = (uint16_t)(cfg.port + (i % cfg.port_count));

        if (cfg.mode == Config::Mode::Tcp)
        {
            InetAddress addr(cfg.host, port);
            sched->spawn([addr]() -> Task<> { co_await tcp_worker(addr); });
        }
        else
        {
            std::string url = "http://" + cfg.host + ":" + std::to_string(port);
            sched->spawn([url]() -> Task<> { co_await http_worker(url); });
        }

        co_await sleep(interval);
    }

    printf("[ramp] done — holding for %d seconds\n", cfg.duration);
    co_await sleep(std::chrono::seconds(cfg.duration));

    g_done = true;
    co_await sleep(std::chrono::seconds(1));
    sched->stop();
}

// ── Progress reporter ─────────────────────────────────────────────────────────

Task<> reporter(Config cfg)
{
    auto    t_start  = Clock::now();
    int64_t prev_reqs = 0;

    while (!g_done)
    {
        co_await sleep(std::chrono::seconds(1));

        int64_t reqs  = g_stats.requests.load();
        int64_t delta = reqs - prev_reqs;
        prev_reqs     = reqs;

        int64_t lat_avg_us = (reqs > 0) ? g_stats.latency_us_sum.load() / reqs : 0;
        double  elapsed    = std::chrono::duration<double>(Clock::now() - t_start).count();

        printf("[t=%4.0fs] conns=%5" PRId64 "/%d  qps=%7" PRId64 "  avg_lat=%5.2fms  errors=%" PRId64 "\n",
               elapsed,
               g_stats.connected.load(), cfg.conns,
               delta,
               lat_avg_us / 1000.0,
               g_stats.errors.load());
    }
}

// ── main ──────────────────────────────────────────────────────────────────────

static void print_usage(const char * prog)
{
    fprintf(stderr,
            "Usage: %s --mode tcp|http [options]\n"
            "  --host H       server host (default 127.0.0.1)\n"
            "  --port P       base port (default: tcp=9001, http=9101)\n"
            "  --ports N      number of ports, round-robin (default 4)\n"
            "  --conns N      target concurrent connections (default 1000)\n"
            "  --ramp-rate R  new connections per second (default 100)\n"
            "  --duration D   seconds to hold after ramp (default 30)\n"
            "  --threads T    client scheduler threads (default 1)\n",
            prog);
}

int main(int argc, char * argv[])
{
    Config cfg;
    bool mode_set = false;

    for (int i = 1; i < argc; ++i)
    {
        if      (!strcmp(argv[i], "--mode")      && i + 1 < argc)
        {
            ++i;
            if      (!strcmp(argv[i], "tcp"))  cfg.mode = Config::Mode::Tcp;
            else if (!strcmp(argv[i], "http")) cfg.mode = Config::Mode::Http;
            else { fprintf(stderr, "Unknown mode: %s\n", argv[i]); return 1; }
            mode_set = true;
        }
        else if (!strcmp(argv[i], "--host")      && i + 1 < argc)  cfg.host       = argv[++i];
        else if (!strcmp(argv[i], "--port")      && i + 1 < argc)  cfg.port       = (uint16_t)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--ports")     && i + 1 < argc)  cfg.port_count = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--conns")     && i + 1 < argc)  cfg.conns      = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--ramp-rate") && i + 1 < argc)  cfg.ramp_rate  = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--duration")  && i + 1 < argc)  cfg.duration   = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--threads")   && i + 1 < argc)  cfg.threads    = atoi(argv[++i]);
        else { print_usage(argv[0]); return 1; }
    }

    if (!mode_set) { print_usage(argv[0]); return 1; }

    printf("stress_client  mode=%s  host=%s  port=%hu~%hu  conns=%d  ramp=%d/s  duration=%ds  threads=%d\n",
           cfg.mode == Config::Mode::Tcp ? "tcp" : "http",
           cfg.host.c_str(),
           cfg.port, (uint16_t)(cfg.port + cfg.port_count - 1),
           cfg.conns, cfg.ramp_rate, cfg.duration, cfg.threads);

    std::vector<std::thread> pool;
    pool.reserve(cfg.threads);
    for (int i = 0; i < cfg.threads; ++i)
    {
        pool.emplace_back([i, cfg]() {
            Scheduler sched;
            if (i == 0)
            {
                sched.spawn([cfg]() -> Task<> { co_await reporter(cfg); });
                sched.spawn([cfg]() -> Task<> { co_await ramp_controller(cfg); });
            }
            sched.run();
        });
    }
    for (auto & t : pool)
        t.join();

    int64_t reqs       = g_stats.requests.load();
    int64_t lat_avg_us = (reqs > 0) ? g_stats.latency_us_sum.load() / reqs : 0;
    printf("\n=== Final ===\n"
           "  total requests : %" PRId64 "\n"
           "  total errors   : %" PRId64 "\n"
           "  avg latency    : %.2f ms\n",
           reqs, g_stats.errors.load(), lat_avg_us / 1000.0);

    return 0;
}
