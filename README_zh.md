# NitroCoro

中文 | [English](README.md)

基于 C++20 协程的**协程异步 I/O 运行时**，高性能、精巧设计的核心，通过官方扩展实现完整生态。

> [!NOTE]
> 项目开发中。
> - 跨平台支持计划中（目前仅支持 Linux epoll）
> - 核心 API 趋于稳定
> - 生命周期管理尚未完善
> - Extension 接口尚在探索中，暂无稳定保证

## 设计目标

最终目标是打造一个高性能网络应用框架——从底层异步 I/O 到完整的 Web 框架。NitroCoro 是整个生态的核心运行时，上层框架基于它构建。核心库围绕三个原则设计：

- **高性能核心** — 精简设计，零开销抽象，`cmake .. && make` 一键编译
- **开箱即用的 HTTP** — 默认包含 HTTP/1.1 服务器和客户端
- **可扩展生态** — TLS、HTTP/2、WebSocket、数据库通过官方扩展支持

## 特性

- **原生协程支持** — 使用 C++20 协程实现异步 I/O 和任务调度，无需回调函数
- **协程调度器** — 基于 epoll 的事件驱动调度器，支持定时器、跨线程唤醒
- **协程原语** — Task、Future、Promise、Mutex、Generator
- **TCP 网络** — TcpServer、TcpConnection、异步 DNS 解析
- **HTTP/1.1** — 带路由的 HTTP 服务器和客户端，支持请求/响应体流式读写

## 架构

```
Web 应用框架（规划中）
    ↑
nitrocoro（本仓库）
├── core        Scheduler / Task / Future / Mutex / Generator
├── io          Channel / Stream 接口
├── net         TcpServer / TcpConnection / DNS
└── extensions/
    ├── http        HTTP/1.1 服务器 + 客户端     [默认开启]
    ├── tls         TLS（依赖 OpenSSL）          [默认关闭]
    ├── http2       HTTP/2                       [默认关闭]
    └── websocket   WebSocket                    [默认关闭]

github.com/nitrocoro/（规划中，独立版本管理）
├── nitrocoro-pg
├── nitrocoro-redis
└── nitrocoro-mysql
```

## 系统要求

- C++20 编译器（GCC 13+ 推荐，最低 10+；Clang 16+）
  - GCC 10–12 可以编译，但存在协程相关 bug（例如协程 lambda 捕获 `shared_ptr` 时出现生命周期错误）
- CMake 3.15+
- Linux（epoll）— 跨平台（Windows、macOS）支持计划中

## 快速开始

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

## 示例

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

## 性能测试

对 `examples/http_server` 和 [Drogon](https://github.com/drogonframework/drogon) 进行对比测试。

- 两者均开启 4 线程、Release 编译
- 测试机器：Intel Core Ultra 7 255H，16 核心，32GB 内存
- wrk 参数：`wrk -t4 -c100 -d30s`

NitroCoro 达到了 [Drogon](https://github.com/drogonframework/drogon) 的性能水平。

| | 路由 | QPS | 延迟（avg） | 传输速率 |
|---|---|---|---|---|
| **NitroCoro**（4 线程） | `/` | **997,678** | 108.26 μs | 96.10 MB/s |
| [Drogon](https://github.com/drogonframework/drogon)（4 线程） | `/` | 968,551 | 104.51 μs | 93.29 MB/s |
| **NitroCoro**（4 线程） | `/large` (1MB) | **27,975** | 3.00 ms | **27.32 GB/s** |
| [Drogon](https://github.com/drogonframework/drogon)（4 线程） | `/large` (1MB) | 22,851 | 4.37 ms | 22.32 GB/s |

<details>
<summary>drogon_server 代码</summary>

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

## 项目结构

```
nitrocoro/
├── include/nitrocoro/
│   ├── core/           Task, Scheduler, Future, Mutex, Generator
│   ├── net/            TcpServer, TcpConnection, DNS
│   ├── io/             Channel, Stream, adapters
│   └── utils/          调试宏, 缓冲区工具
├── src/                核心实现
├── extensions/
│   └── http/           HTTP/1.1 扩展
├── examples/
└── tests/
```

## 路线图

- [ ] TLS 扩展（OpenSSL）
- [ ] HTTP/2 扩展
- [ ] WebSocket 扩展
- [ ] `install()` + `find_package()` 支持
- [ ] 跨平台支持（Windows、macOS）
- [ ] 上层 Web 应用框架

## 许可证

MIT License
