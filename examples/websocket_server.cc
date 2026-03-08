/**
 * @file websocket_server.cc
 * @brief WebSocket echo server example
 *
 * Usage:
 *   ./websocket_server [-p port]
 *
 * Test with websocat:
 *   websocat ws://localhost:8080/ws
 */
#include <nitrocoro/core/Scheduler.h>
#include <nitrocoro/http/HttpServer.h>
#include <nitrocoro/websocket/WsServer.h>

#include <cstdio>

using namespace nitrocoro;
using namespace nitrocoro::http;
using namespace nitrocoro::websocket;

Task<> run(uint16_t port)
{
    HttpServer server(port);
    WsServer ws;

    ws.route("/ws", [](WsConnection & conn) -> Task<> {
        printf("client connected\n");
        while (auto msg = co_await conn.receive())
        {
            printf("recv: %.*s\n", (int)msg->text().size(), msg->text().data());
            co_await conn.sendText(msg->text());
        }
        printf("client disconnected\n");
    });

    ws.attachTo(server);
    co_await server.start();
}

int main(int argc, char * argv[])
{
    uint16_t port = 8080;
    for (int i = 1; i < argc; ++i)
        if (std::string(argv[i]) == "-p" && i + 1 < argc)
            port = std::stoi(argv[++i]);

    printf("WebSocket server listening on ws://localhost:%hu/ws\n", port);

    Scheduler scheduler;
    scheduler.spawn([port]() -> Task<> { co_await run(port); });
    scheduler.run();
}
