/**
 * @file TcpConnection.cc
 * @brief Implementation of TcpConnection
 */
#include <nitrocoro/core/Scheduler.h>
#include <nitrocoro/io/adapters/BufferReader.h>
#include <nitrocoro/io/adapters/BufferWriter.h>
#include <nitrocoro/net/InetAddress.h>
#include <nitrocoro/net/Socket.h>
#include <nitrocoro/net/TcpConnection.h>

namespace nitrocoro::net
{

using nitrocoro::Scheduler;
using nitrocoro::Task;
using nitrocoro::io::IoChannel;
using nitrocoro::io::adapters::BufferReader;
using nitrocoro::io::adapters::BufferWriter;
using nitrocoro::net::Socket;

struct Connector
{
    Connector(const sockaddr * addr, socklen_t addrLen)
        : addr_(addr)
        , addrLen_(addrLen)
    {
    }

    IoChannel::IoStatus operator()(int fd, IoChannel * channel)
    {
        if (connecting_)
        {
            int error = 0;
            socklen_t len = sizeof(error);
            if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &len) < 0)
            {
                return IoChannel::IoStatus::Error;
            }
            if (error == 0)
            {
                channel->disableWriting();
                return IoChannel::IoStatus::Success;
            }
            else if (error == EINPROGRESS || error == EALREADY)
            {
                return IoChannel::IoStatus::NeedWrite;
            }
            else
            {
                return IoChannel::IoStatus::Error;
            }
        }

        int ret = ::connect(fd, addr_, addrLen_);
        if (ret == 0)
        {
            channel->disableWriting();
            return IoChannel::IoStatus::Success;
        }
        int lastErrno = errno;
        switch (lastErrno)
        {
            case EISCONN:
                channel->disableWriting();
                return IoChannel::IoStatus::Success;
            case EINPROGRESS:
            case EALREADY:
                connecting_ = true;
                channel->enableWriting();
                return IoChannel::IoStatus::NeedWrite;
            case EINTR:
                return IoChannel::IoStatus::Retry;

            default:
                return IoChannel::IoStatus::Error;
        }
    }

private:
    const sockaddr * addr_;
    socklen_t addrLen_;

    bool connecting_{ false };
};

Task<TcpConnectionPtr> TcpConnection::connect(const InetAddress & addr)
{
    int fd = ::socket(addr.family(), SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0)
        throw std::runtime_error("Failed to create socket");
    auto socket = std::make_shared<Socket>(fd);
    auto channelPtr = std::make_unique<IoChannel>(fd);
    channelPtr->setGuard(socket);
    socklen_t addrLen = addr.isIpV6() ? sizeof(sockaddr_in6) : sizeof(sockaddr_in);
    Connector connector(addr.getSockAddr(), addrLen);
    auto result = co_await channelPtr->performWrite(&connector);
    if (result != IoChannel::IoResult::Success)
        throw std::runtime_error("TCP connect failed");
    co_return std::make_shared<TcpConnection>(std::move(channelPtr), std::move(socket));
}

TcpConnection::TcpConnection(std::unique_ptr<IoChannel> channelPtr, std::shared_ptr<Socket> socket)
    : socket_(std::move(socket))
    , ioChannelPtr_(std::move(channelPtr))
{
    state_ = State::Connected;
    ioChannelPtr_->enableReading();
}

TcpConnection::~TcpConnection() = default;

Task<size_t> TcpConnection::read(void * buf, size_t len)
{
    BufferReader reader(buf, len);
    auto result = co_await ioChannelPtr_->performRead(&reader);
    if (result == IoChannel::IoResult::Eof)
    {
        if (state_ == State::LocalShutdown)
            state_ = State::Closed;
        else
            state_ = State::PeerShutdown;
        co_return 0;
    }
    if (result != IoChannel::IoResult::Success)
    {
        state_ = State::Closed;
        throw std::runtime_error("TCP read error");
    }
    co_return reader.readLen();
}

Task<size_t> TcpConnection::write(const void * buf, size_t len)
{
    BufferWriter writer(buf, len);
    auto result = co_await ioChannelPtr_->performWrite(&writer);
    if (result == IoChannel::IoResult::Eof)
    {
        state_ = State::Closed;
        co_return 0;
    }
    if (result != IoChannel::IoResult::Success)
    {
        state_ = State::Closed;
        throw std::runtime_error("TCP write error");
    }
    co_return len;
}

Task<> TcpConnection::shutdown()
{
    co_await ioChannelPtr_->scheduler()->switch_to();

    if (state_ == State::LocalShutdown || state_ == State::Closed)
        co_return;

    if (state_ == State::PeerShutdown)
        state_ = State::Closed;
    else
        state_ = State::LocalShutdown;

    socket_->shutdownWrite();
}

Task<> TcpConnection::forceClose()
{
    co_await ioChannelPtr_->scheduler()->switch_to();

    state_ = State::Closed;
    ioChannelPtr_->disableAll();
    ioChannelPtr_->cancelAll();
}

} // namespace nitrocoro::net
