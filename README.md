# NitroCoro

[中文](README_zh.md) | [English]

A **coroutine async I/O runtime** built on C++20 coroutines, with a high-performance, elegantly designed core and a
full-stack ecosystem via official extensions.

> [!NOTE]
> Under active development.
> - Cross-platform planned (currently Linux only, epoll)
> - Core API is stabilizing
> - Lifecycle management is not yet complete
> - Extension API is still being explored, no stability guarantee

## Design Goals

The ultimate goal is a high-performance network application framework — from bare async I/O all the way up to a full web
framework. NitroCoro is the core runtime that everything builds on, designed around three principles:

- **High-performance core** — minimal design, zero-overhead abstractions, `cmake .. && make`
- **Out-of-the-box HTTP** — HTTP/1.1 server and client included by default
- **Extensible ecosystem** — TLS, HTTP/2, WebSocket, databases via official extensions

## Features

- **Native coroutine support** — async I/O and task scheduling via C++20 coroutines, no callbacks
- **Coroutine Scheduler** — epoll-based event loop with timer support and cross-thread wakeup
- **Coroutine primitives** — Task, Future, Promise, Mutex, Generator
- **TCP networking** — TcpServer, TcpConnection, async DNS resolution
- **HTTP/1.1** — HTTP server with routing and client, streaming request/response body

See the full [Feature Status](#feature-status) for detailed feature status.

## Architecture

```
web application framework (planned)
    ↑
nitrocoro (this repo)
├── core        Scheduler / Task / Future / Mutex / Generator
├── io          Channel / Stream interface
├── net         TcpServer / TcpConnection / DNS
└── extensions/
    ├── http        HTTP/1.1 server + client          [default ON]
    ├── tls         TLS via OpenSSL                   [default OFF]
    ├── http2       HTTP/2                            [default OFF]
    └── websocket   WebSocket                         [default OFF]

github.com/nitrocoro/ (planned, version-independent)
├── nitrocoro-pg
├── nitrocoro-redis
└── nitrocoro-mysql
```

## Requirements

- C++20 compiler (GCC 13+ recommended, 10+ minimum; Clang 16+)
    - GCC 10–12 can compile but contain coroutine bugs (e.g. incorrect lifetime of `shared_ptr` captured in coroutine
      lambdas)
- CMake 3.15+
- Linux (epoll) — cross-platform (Windows, macOS) planned

## Quick Start

```bash
git clone https://github.com/nitrocoro/nitrocoro
cd nitrocoro && mkdir build && cd build
cmake .. && make
```

```bash
./examples/tcp_echo_server 8888
./examples/http_server 8080   # curl http://localhost:8080/
./examples/http_client http://example.com/
```

## Example

```cpp
#include <nitrocoro/http/HttpServer.h>
using namespace nitrocoro;
using namespace nitrocoro::http;

Task<> run()
{
    HttpServer server(8080);
    server.route("/", {"GET"}, [](IncomingRequestPtr req, ServerResponsePtr resp) -> Task<> {
        co_await resp->end("<h1>Hello, NitroCoro!</h1>");
    });
    co_await server.start();
}

int main()
{
    Scheduler scheduler;
    scheduler.spawn([]() -> Task<> { co_await run(); });
    scheduler.run();
}
```

## Benchmark

Benchmarked `examples/http_server` against a [Drogon](https://github.com/drogonframework/drogon) server.

- Both running 4 threads, Release build
- Machine: Intel Core Ultra 7 255H, 16 cores, 32GB RAM
- Command: `wrk -t4 -c100 -d30s`

NitroCoro reaches the performance level of [Drogon](https://github.com/drogonframework/drogon).

|                                                                 | Route          | QPS         | Latency (avg) | Transfer       |
|-----------------------------------------------------------------|----------------|-------------|---------------|----------------|
| **NitroCoro** (4 threads)                                       | `/`            | **997,678** | 108.26 μs     | 96.10 MB/s     |
| [Drogon](https://github.com/drogonframework/drogon) (4 threads) | `/`            | 968,551     | 104.51 μs     | 93.29 MB/s     |
| **NitroCoro** (4 threads)                                       | `/large` (1MB) | **27,975**  | 3.00 ms       | **27.32 GB/s** |
| [Drogon](https://github.com/drogonframework/drogon) (4 threads) | `/large` (1MB) | 22,851      | 4.37 ms       | 22.32 GB/s     |

<details>
<summary>drogon_server code</summary>

```cpp
#include <drogon/drogon.h>
#include <iostream>
#include <string>

int main(int argc, char* argv[])
{
    int port = 8081;
    int threads = 1;

    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "-p" && i + 1 < argc) {
            port = std::stoi(argv[++i]);
        } else if (std::string(argv[i]) == "-t" && i + 1 < argc) {
            threads = std::stoi(argv[++i]);
        }
    }

    drogon::app()
        .registerHandler("/", [](drogon::HttpRequestPtr req) -> drogon::Task<drogon::HttpResponsePtr> {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setBody("<h1>Hello, World!</h1>");
            co_return resp;
        })
        .registerHandler("/large", [](drogon::HttpRequestPtr req) -> drogon::Task<drogon::HttpResponsePtr> {
            auto resp = drogon::HttpResponse::newHttpResponse();
            std::string largeBody(1024 * 1024, 'a');
            resp->setBody(largeBody);
            co_return resp;
        })
        .registerHandler("/hello", [](drogon::HttpRequestPtr req) -> drogon::Task<drogon::HttpResponsePtr> {
            auto name = req->getParameter("name");
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setBody("Hello, " + name + "!");
            co_return resp;
        })
        .setThreadNum(threads)
        .enableServerHeader(false)
        .enableDateHeader(false)
        .addListener("0.0.0.0", port)
        .run();
}
```

</details>

## Feature Status

> ✅ Stable &nbsp;|&nbsp; 🛠️ In Progress &nbsp;|&nbsp; 📋 Planned

### Coroutine Runtime

| Feature                  | Description                                                                                         | Status |
|--------------------------|-----------------------------------------------------------------------------------------------------|--------|
| Task\<T\>                | Universal coroutine return type, supports `co_await` / `co_return`, automatic exception propagation | ✅      |
| Scheduler                | One event loop per thread, drives coroutine scheduling and I/O                                      | ✅      |
| Timer                    | Suspend coroutine for a duration or until a time point, non-blocking                                | ✅      |
| Cross-thread dispatch    | Coroutine migration and wakeup across multiple Schedulers                                           | ✅      |
| Cooperative cancellation | Send cancellation signal to coroutines via CancelToken, supports timed auto-cancel                  | ✅      |
| Timeout wrapper          | Attach a timeout to any awaitable, throws on expiry                                                 | ✅      |
| Coroutine generator      | Lazy sequence via `co_yield`, pulled on demand                                                      | 🛠️    |
| Coroutine Channel        | epoll fd wrapper, foundation for coroutine-native async I/O                                         | ✅      |
| CallbackChannel          | Callback-driven channel for integrating third-party async libraries                                 | ✅      |
| Multi-thread helpers     | Simplified startup and management of multi-threaded event loops                                     | 📋     |

### Synchronization Primitives

| Feature          | Description                                                         | Status |
|------------------|---------------------------------------------------------------------|--------|
| Coroutine Future | Promise/Future for passing async values between coroutines          | ✅      |
| Coroutine Mutex  | Coroutine-level mutex, suspends waiters instead of blocking threads | ✅      |

### TCP Networking

| Feature        | Description                                                         | Status |
|----------------|---------------------------------------------------------------------|--------|
| TCP Server     | Async accept loop, spawns a coroutine per connection, graceful stop | ✅      |
| TCP Connection | Coroutine-based TCP read/write, RAII lifetime management            | ✅      |
| Async DNS      | Non-blocking DNS resolution                                         | ✅      |
| URL parsing    | Parse scheme / host / port / path / query                           | ✅      |
| IPv6 support   | Full IPv6 address and connection support                            | 🛠️    |

### HTTP/1.1 (extension, default ON)

| Feature          | Description                                                                        | Status |
|------------------|------------------------------------------------------------------------------------|--------|
| HTTP Server      | Register routes and start HTTP service, handlers are coroutines                    | ✅      |
| HTTP Router      | Exact match, path parameters (`:name`), wildcard (`*name`), regex routes           | ✅      |
| Request reading  | Streaming read of headers, query params, and body                                  | ✅      |
| Response writing | Streaming write of status, headers, and body                                       | ✅      |
| HTTP Client      | Simple API (get/post/request) and streaming API, supports injecting StreamUpgrader | 🛠️    |
| More             | Cookie, Session, timeout, etc.                                                     | 🛠️    |

### TLS (extension, default OFF)

| Feature         | Description                                           | Status |
|-----------------|-------------------------------------------------------|--------|
| TLS stream      | Transparent TLS wrapper over TCP, drop-in replacement | ✅      |
| TLS config      | Certificate, private key, and verification policy     | ✅      |
| HTTPS support   | Inject TLS provider into HttpServer to enable HTTPS   | ✅      |
| OpenSSL backend | TLS provider implementation via OpenSSL               | ✅      |
| Botan backend   | TLS provider implementation via Botan                 | 📋     |

### HTTP/2 (extension)

| Feature       | Description                | Status |
|---------------|----------------------------|--------|
| HTTP/2 Server | Server-side HTTP/2 support | 📋     |
| HTTP/2 Client | Client-side HTTP/2 support | 📋     |

### WebSocket (extension, default OFF)

| Feature              | Description                                                   | Status |
|----------------------|---------------------------------------------------------------|--------|
| WebSocket Server     | WebSocket service via HTTP upgrade, register message handlers | ✅      |
| WebSocket Connection | Send messages, close connection, receive message callbacks    | ✅      |
| WebSocket Client     | Initiate WebSocket connection, send and receive messages      | 📋     |

### PostgreSQL (extension, default OFF)

| Feature         | Description                                 | Status |
|-----------------|---------------------------------------------|--------|
| Async query     | Coroutine-based SQL execution, non-blocking | ✅      |
| Connection pool | Automatic connection reuse and return       | ✅      |
| Transaction     | Coroutine-based begin / commit / rollback   | ✅      |
| Notification    | PostgreSQL Listen/Notify mechanism          | 📋     |

### MySQL (external: [nitrocoro-mysql](https://github.com/nitrocoro/nitrocoro-mysql))

| Feature       | Description                                                  | Status |
|---------------|--------------------------------------------------------------|--------|
| MySQL support | Coroutine MySQL client with connection pool and transactions | 📋     |

### Redis (external: [nitrocoro-redis](https://github.com/nitrocoro/nitrocoro-redis))

| Feature         | Description                                  | Status |
|-----------------|----------------------------------------------|--------|
| Async commands  | Coroutine-based Redis commands, non-blocking | ✅      |
| Connection pool | Automatic connection reuse and return        | ✅      |
| Lua scripting   | Execute Lua scripts via EVAL                 | ✅      |

### Cross-platform

| Feature         | Description          | Status |
|-----------------|----------------------|--------|
| Windows support | IOCP-based backend   | 📋     |
| macOS support   | kqueue-based backend | 📋     |

### Testing Framework

| Feature                | Description                                                                           | Status |
|------------------------|---------------------------------------------------------------------------------------|--------|
| Async tests            | Test body is a coroutine, can `co_await` directly                                     | ✅      |
| Multi-level assertions | CHECK (soft) / REQUIRE (abort test) / MANDATE (fatal), with EQ / NE / THROWS variants | ✅      |
| Expected failure       | Mark tests expected to fail (XFAIL)                                                   | ✅      |
| Run control            | Filter by name, verbose output, list tests                                            | ✅      |

### Logging

| Feature    | Description                                                                 | Status |
|------------|-----------------------------------------------------------------------------|--------|
| Log macros | ERROR / INFO / DEBUG / TRACE, printf-style, DEBUG/TRACE stripped in Release | 🛠️    |

## License

MIT License
