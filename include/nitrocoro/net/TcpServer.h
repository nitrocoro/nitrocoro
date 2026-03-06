/**
 * @file TcpServer.h
 * @brief Coroutine-based TCP server component
 */
#pragma once

#include <nitrocoro/core/Future.h>
#include <nitrocoro/core/Scheduler.h>
#include <nitrocoro/core/Task.h>
#include <nitrocoro/net/Socket.h>
#include <nitrocoro/net/TcpConnection.h>

#include <atomic>
#include <functional>
#include <memory>
#include <unordered_set>

namespace nitrocoro::net
{

using nitrocoro::Scheduler;
using nitrocoro::Task;
using nitrocoro::io::Channel;

class TcpServer
{
public:
    /**
     * @brief Connection handler callback type.
     *
     * The handler owns the connection's logical lifetime: it must not return
     * until the connection is fully done (all reads/writes complete, peer
     * disconnected, or explicitly closed). Returning early while holding
     * external references to the connection bypasses stop() shutdown.
     */
    using ConnectionHandler = std::function<Task<>(std::shared_ptr<TcpConnection>)>;

    explicit TcpServer(uint16_t port, Scheduler * scheduler = Scheduler::current());
    ~TcpServer();

    /**
     * @brief Start accepting connections and invoke @p handler for each one.
     *
     * Suspends until stop() is called or a fatal accept error occurs.
     * Each handler runs as an independent spawned coroutine; the server
     * considers a connection closed when its handler returns.
     */
    Task<> start(ConnectionHandler handler);
    Task<> stop();
    Task<> wait() const;

    uint16_t port() const { return port_; }

private:
    void setup_socket();

    uint16_t port_;
    Scheduler * scheduler_;
    std::shared_ptr<net::Socket> listenSocketPtr_;
    std::atomic_bool started_{ false };
    std::atomic_bool stopped_{ false };
    Promise<> stopPromise_;
    SharedFuture<> stopFuture_;
    std::unique_ptr<Channel> listenChannel_;

    using ConnectionSet = std::unordered_set<TcpConnectionPtr>;
    std::shared_ptr<ConnectionSet> connSetPtr_{ std::make_shared<ConnectionSet>() };
};

} // namespace nitrocoro::net
