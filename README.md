# NitroCoro

[中文](README_zh.md) | English

A **coroutine async I/O runtime** built on C++20 coroutines, with a high-performance, elegantly designed core and a full-stack ecosystem via official extensions.

> [!NOTE]
> Under active development.
> - Cross-platform planned (currently Linux only, epoll)
> - Core API is stabilizing
> - Lifecycle management is not yet complete
> - Extension API is still being explored, no stability guarantee

## Design Goals

The ultimate goal is a high-performance network application framework — from bare async I/O all the way up to a full web framework. NitroCoro is the core runtime that everything builds on, designed around three principles:

- **High-performance core** — minimal design, zero-overhead abstractions, `cmake .. && make`
- **Out-of-the-box HTTP** — HTTP/1.1 server and client included by default
- **Extensible ecosystem** — TLS, HTTP/2, WebSocket, databases via official extensions

## Features

- **Native coroutine support** — async I/O and task scheduling via C++20 coroutines, no callbacks
- **Coroutine Scheduler** — epoll-based event loop with timer support and cross-thread wakeup
- **Coroutine primitives** — Task, Future, Promise, Mutex, Generator
- **TCP networking** — TcpServer, TcpConnection, async DNS resolution
- **HTTP/1.1** — HTTP server with routing and client, streaming request/response body

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
  - GCC 10–12 can compile but contain coroutine bugs (e.g. incorrect lifetime of `shared_ptr` captured in coroutine lambdas)
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
    server.route("/", {"GET"}, [](HttpIncomingStream<HttpRequest> && req, HttpOutgoingStream<HttpResponse> && resp) -> Task<> {
        co_await resp.end("<h1>Hello, NitroCoro!</h1>");
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

| | Route | QPS | Latency (avg) | Transfer |
|---|---|---|---|---|
| **NitroCoro** (4 threads) | `/` | **997,678** | 108.26 μs | 96.10 MB/s |
| [Drogon](https://github.com/drogonframework/drogon) (4 threads) | `/` | 968,551 | 104.51 μs | 93.29 MB/s |
| **NitroCoro** (4 threads) | `/large` (1MB) | **27,975** | 3.00 ms | **27.32 GB/s** |
| [Drogon](https://github.com/drogonframework/drogon) (4 threads) | `/large` (1MB) | 22,851 | 4.37 ms | 22.32 GB/s |

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

## Project Structure

```
nitrocoro/
├── include/nitrocoro/
│   ├── core/           Task, Scheduler, Future, Mutex, Generator
│   ├── net/            TcpServer, TcpConnection, DNS
│   ├── io/             Channel, Stream, adapters
│   └── utils/          Debug macros, buffers
├── src/                Core implementation
├── extensions/
│   └── http/           HTTP/1.1 extension
├── examples/
└── tests/
```

## Roadmap

- [ ] TLS extension (OpenSSL)
- [ ] HTTP/2 extension
- [ ] WebSocket extension
- [ ] `install()` + `find_package()` support
- [ ] Cross-platform (Windows, macOS)
- [ ] Upper-layer web application framework

## License

MIT License
