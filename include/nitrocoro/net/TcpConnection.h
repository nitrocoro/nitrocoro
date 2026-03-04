/**
 * @file TcpConnection.h
 * @brief RAII wrapper for TCP connection file descriptor
 */
#pragma once

#include <nitrocoro/core/Mutex.h>
#include <nitrocoro/core/Task.h>
#include <nitrocoro/io/IoChannel.h>
#include <nitrocoro/net/InetAddress.h>
#include <nitrocoro/net/Socket.h>

namespace nitrocoro::net
{

using nitrocoro::Mutex;
using nitrocoro::Task;
using nitrocoro::io::IoChannel;
using nitrocoro::net::Socket;
class TcpConnection;
using TcpConnectionPtr = std::shared_ptr<TcpConnection>;

class TcpConnection
{
public:
    static Task<TcpConnectionPtr> connect(const InetAddress & addr);

    explicit TcpConnection(std::unique_ptr<IoChannel>, std::shared_ptr<Socket>);
    ~TcpConnection();

    TcpConnection(const TcpConnection &) = delete;
    TcpConnection & operator=(const TcpConnection &) = delete;
    TcpConnection(TcpConnection &&) = delete;
    TcpConnection & operator=(TcpConnection &&) = delete;

    Task<size_t> read(void * buf, size_t len);
    Task<size_t> write(const void * buf, size_t len);

    Task<> shutdown();
    Task<> forceClose();

    enum class State
    {
        None,
        Connected,
        LocalShutdown,
        PeerShutdown,
        Closed
    };

    State state() const { return state_; }

private:
    std::shared_ptr<Socket> socket_;
    std::unique_ptr<IoChannel> ioChannelPtr_;
    State state_ = State::None;
};

} // namespace nitrocoro::net
