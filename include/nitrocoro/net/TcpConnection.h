/**
 * @file TcpConnection.h
 * @brief RAII wrapper for TCP connection file descriptor
 */
#pragma once

#include <nitrocoro/core/Mutex.h>
#include <nitrocoro/core/Task.h>
#include <nitrocoro/io/Channel.h>
#include <nitrocoro/net/InetAddress.h>
#include <nitrocoro/net/Socket.h>

namespace nitrocoro::net
{

using nitrocoro::Mutex;
using nitrocoro::Task;
using nitrocoro::io::Channel;
using nitrocoro::net::Socket;
class TcpConnection;
using TcpConnectionPtr = std::shared_ptr<TcpConnection>;

class TcpConnection
{
public:
    static Task<TcpConnectionPtr> connect(const InetAddress & addr);

    TcpConnection(std::unique_ptr<Channel>, std::shared_ptr<Socket>, InetAddress localAddr, InetAddress peerAddr);
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

    State state() const { return state_->load(); }
    const InetAddress & localAddr() const { return localAddr_; }
    const InetAddress & peerAddr() const { return peerAddr_; }

private:
    std::shared_ptr<Socket> socket_;
    std::unique_ptr<Channel> ioChannelPtr_;
    std::shared_ptr<std::atomic<State>> state_;
    InetAddress localAddr_;
    InetAddress peerAddr_;
};

} // namespace nitrocoro::net
