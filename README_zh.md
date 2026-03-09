# NitroCoro

[中文](README_zh.md) | [English](README.md)

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

详细功能进度请查看 [功能进度](#功能进度)。

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

|                                                           | 路由             | QPS         | 延迟（avg）   | 传输速率           |
|-----------------------------------------------------------|----------------|-------------|-----------|----------------|
| **NitroCoro**（4 线程）                                       | `/`            | **997,678** | 108.26 μs | 96.10 MB/s     |
| [Drogon](https://github.com/drogonframework/drogon)（4 线程） | `/`            | 968,551     | 104.51 μs | 93.29 MB/s     |
| **NitroCoro**（4 线程）                                       | `/large` (1MB) | **27,975**  | 3.00 ms   | **27.32 GB/s** |
| [Drogon](https://github.com/drogonframework/drogon)（4 线程） | `/large` (1MB) | 22,851      | 4.37 ms   | 22.32 GB/s     |

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

## 功能进度

> ✅ 趋于稳定 &nbsp;|&nbsp; 🛠️ 开发中 &nbsp;|&nbsp; 📋 规划中

### 协程运行时

| 功能              | 简介                                           | 进度  |
|-----------------|----------------------------------------------|-----|
| 通用协程 Task       | 协程的通用返回类型，支持 `co_await` / `co_return`，异常自动传播 | ✅   |
| 调度器 Scheduler   | 每线程一个调度器，驱动协程调度与 I/O                         | ✅   |
| 定时器             | 协程挂起等待指定时长或时间点，不阻塞线程                         | ✅   |
| 跨线程调度           | 多个 Scheduler 间协程迁移与跨线程唤醒                     | ✅   |
| 协作式取消           | 通过 CancelToken 向协程发送取消信号，支持定时自动取消            | ✅   |
| 超时包装            | 为任意 awaitable 附加超时，超时后抛出异常                   | ✅   |
| 协程生成器           | 用 `co_yield` 生成惰性序列，调用方按需拉取                  | 🛠️ |
| 协程 Channel      | epoll 文件描述符封装，协程式异步 I/O 基础设施                 | ✅   |
| CallbackChannel | 传统回调驱动 channel，用于集成第三方异步库                    | ✅   |
| 多线程封装           | 简化多线程事件循环的启动与管理                              | 📋  |

### 同步原语

| 功能        | 简介                          | 进度 |
|-----------|-----------------------------|----|
| 协程 Future | 协程版 Promise/Future，非阻塞等待异步值 | ✅  |
| 协程 Mutex  | 协程级互斥锁，等待时挂起协程而非阻塞线程        | ✅  |

### TCP 网络

| 功能      | 简介                                              | 进度  |
|---------|-------------------------------------------------|-----|
| TCP 服务端 | 异步 accept 循环，每个连接独立 spawn 协程处理，支持优雅停止           | ✅   |
| TCP 连接  | 协程式 TCP 读写，RAII 管理连接生命周期                        | ✅   |
| 异步 DNS  | 非阻塞域名解析                                         | ✅   |
| URL 解析  | 解析 URL 各字段（scheme / host / port / path / query） | ✅   |
| IPv6 支持 | 完整支持 IPv6 地址格式和连接                               | 🛠️ |

### HTTP/1.1（扩展，默认开启）

| 功能       | 简介                            | 进度  |
|----------|-------------------------------|-----|
| HTTP 服务端 | 注册路由并启动 HTTP 服务，handler 为协程函数 | ✅   |
| HTTP 路由  | 路径匹配，支持路径参数                   | ✅   |
| 请求读取     | 流式读取请求 header、query 参数和 body  | ✅   |
| 响应写入     | 流式写入响应状态、header 和 body        | ✅   |
| HTTP 客户端 | 发起异步 HTTP/1.1 请求              | 🛠️ |

### TLS（扩展，默认关闭）

| 功能         | 简介                                     | 进度 |
|------------|----------------------------------------|----|
| TLS 流      | 透明包装 TCP 连接为 TLS，可直接替换 TCP 使用          | ✅  |
| TLS 配置     | 证书、私钥、验证策略配置                           | ✅  |
| HTTPS 支持   | 为 HttpServer 注入 TLS provider 以启用 HTTPS | ✅  |
| OpenSSL 后端 | 基于 OpenSSL 的 TLS provider 实现           | ✅  |
| Botan 后端   | 基于 Botan 的 TLS provider 实现             | 📋 |

### HTTP/2（扩展）

| 功能         | 简介                 | 进度 |
|------------|--------------------|----|
| HTTP/2 服务端 | 基于 HTTP/2 协议的服务端支持 | 📋 |
| HTTP/2 客户端 | 基于 HTTP/2 协议的客户端支持 | 📋 |

### WebSocket（扩展，默认关闭）

| 功能            | 简介                                          | 进度  |
|---------------|---------------------------------------------|-----|
| WebSocket 服务端 | 基于 HTTP upgrade 的 WebSocket 服务，注册消息 handler | 🛠️ |
| WebSocket 连接  | 发送消息、关闭连接、接收消息回调                            | 🛠️ |
| WebSocket 客户端 | 发起 WebSocket 连接，收发消息                        | 📋  |

### PostgreSQL（扩展，默认关闭）

| 功能   | 简介                              | 进度 |
|------|---------------------------------|----|
| 异步查询 | 协程式执行 SQL 查询，不阻塞线程              | ✅  |
| 连接池  | 自动管理连接复用与归还                     | ✅  |
| 事务   | 协程式事务 begin / commit / rollback | ✅  |

### MySQL（外部扩展 [nitrocoro-mysql](https://github.com/nitrocoro/nitrocoro-mysql)）

| 功能       | 简介                    | 进度 |
|----------|-----------------------|----|
| MySQL 支持 | 协程式 MySQL 客户端，含连接池与事务 | 📋 |

### Redis（外部扩展 [nitrocoro-redis](https://github.com/nitrocoro/nitrocoro-redis)）

| 功能     | 简介                   | 进度 |
|--------|----------------------|----|
| 异步命令   | 协程式执行 Redis 命令，不阻塞线程 | ✅  |
| 连接池    | 自动管理连接复用与归还          | ✅  |
| Lua 脚本 | 通过 EVAL 执行 Lua 脚本    | ✅  |

### 跨平台

| 功能         | 简介               | 进度 |
|------------|------------------|----|
| Windows 支持 | 基于 IOCP 的跨平台后端   | 📋 |
| macOS 支持   | 基于 kqueue 的跨平台后端 | 📋 |

### 测试框架

| 功能   | 简介                                                           | 进度 |
|------|--------------------------------------------------------------|----|
| 异步测试 | 测试体本身是协程，可直接 `co_await` 异步操作                                 | ✅  |
| 多级断言 | CHECK（软）/ REQUIRE（中止测试）/ MANDATE（致命）三级，含 EQ / NE / THROWS 变体 | ✅  |
| 预期失败 | 标记预期失败的测试（XFAIL）                                             | ✅  |
| 运行控制 | 按名称过滤、详细输出、列举测试                                              | ✅  |

### 日志

| 功能    | 简介                                            | 进度  |
|-------|-----------------------------------------------|-----|
| 分级日志宏 | ERROR / INFO / DEBUG / TRACE 四级，暂时用 printf 代替 | 🛠️ |

## 许可证

MIT License
