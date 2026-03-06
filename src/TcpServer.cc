/**
 * @file TcpServer.cc
 * @brief Implementation of coroutine-based TCP server
 */
#include <nitrocoro/net/TcpServer.h>

#include <nitrocoro/core/Scheduler.h>
#include <nitrocoro/net/Socket.h>
#include <nitrocoro/net/TcpConnection.h>
#include <nitrocoro/utils/Debug.h>

#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdexcept>
#include <sys/socket.h>

namespace nitrocoro::net
{
using io::Channel;
using net::Socket;

TcpServer::TcpServer(uint16_t port, Scheduler * scheduler)
    : port_(port)
    , scheduler_(scheduler)
    , stopPromise_(scheduler)
    , stopFuture_(stopPromise_.get_future().share())
{
    setup_socket();
}

TcpServer::~TcpServer() = default;

void TcpServer::setup_socket()
{
    int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0)
        throw std::runtime_error("Failed to create socket");
    listenSocketPtr_ = std::make_shared<Socket>(fd);

    int opt = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);
    if (::bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0)
        throw std::runtime_error(std::string("Failed to bind socket: ") + strerror(errno));

    if (port_ == 0)
    {
        socklen_t addrLen = sizeof(addr);
        if (::getsockname(fd, reinterpret_cast<sockaddr *>(&addr), &addrLen) == 0)
            port_ = ntohs(addr.sin_port);
    }
}

struct Acceptor
{
    Channel::IoStatus operator()(int fd, Channel *)
    {
        socklen_t len = sizeof(clientAddr_);
        int connfd = ::accept4(fd, reinterpret_cast<struct sockaddr *>(&clientAddr_), &len, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (connfd >= 0)
        {
            socket_ = std::make_shared<Socket>(connfd);
            return Channel::IoStatus::Success;
        }
        switch (errno)
        {
            case EAGAIN:
#if EAGAIN != EWOULDBLOCK
            case EWOULDBLOCK:
#endif
                return Channel::IoStatus::NeedRead;
            case EINTR:
                return Channel::IoStatus::Retry;
            default:
                return Channel::IoStatus::Error;
        }
    }

    std::shared_ptr<Socket> takeSocket() { return std::move(socket_); }
    const struct sockaddr_in & clientAddr() const { return clientAddr_; }

private:
    struct sockaddr_in clientAddr_{};
    std::shared_ptr<Socket> socket_;
};

Task<> TcpServer::start(ConnectionHandler handler)
{
    co_await scheduler_->switch_to();
    if (started_.exchange(true))
    {
        throw std::logic_error("TcpServer already started");
    }

    if (::listen(listenSocketPtr_->fd(), 128) < 0)
    {
        stopped_.store(true);
        stopPromise_.set_value();
        throw std::runtime_error(std::string("Failed to listen: ") + strerror(errno));
    }
    NITRO_DEBUG("TcpServer listening on port %hu", port_);

    auto handlerPtr = std::make_shared<ConnectionHandler>(std::move(handler));
    std::weak_ptr<ConnectionSet> weakConnSet{ connSetPtr_ };
    listenChannel_ = std::make_unique<Channel>(listenSocketPtr_->fd(), TriggerMode::LevelTriggered, scheduler_);
    listenChannel_->setGuard(listenSocketPtr_);
    listenChannel_->enableReading();
    while (!stopped_.load())
    {
        Acceptor acceptor;
        auto result = co_await listenChannel_->performRead(&acceptor);
        if (result == Channel::IoResult::Canceled)
        {
            NITRO_DEBUG("TcpServer::close() called, break accepting loop");
            break;
        }
        if (result != Channel::IoResult::Success)
        {
            NITRO_ERROR("Accept error: IoResult=%d", static_cast<int>(result));
            break;
        }

        NITRO_DEBUG("Accepted connection");
        auto socket = acceptor.takeSocket();
        auto ioChannelPtr = std::make_unique<Channel>(socket->fd(), TriggerMode::EdgeTriggered, scheduler_);
        ioChannelPtr->setGuard(socket);
        auto connPtr = std::make_shared<TcpConnection>(std::move(ioChannelPtr), socket);
        connSetPtr_->insert(connPtr);
        scheduler_->spawn([scheduler = scheduler_, handlerPtr, connPtr, weakConnSet]() mutable -> Task<> {
            try
            {
                co_await (*handlerPtr)(connPtr);
            }
            catch (const std::exception & ex)
            {
                NITRO_ERROR("TcpServer handler unhandled exception: %s", ex.what());
            }
            catch (...)
            {
                NITRO_ERROR("TcpServer handler unknown exception");
            }
            co_await scheduler->switch_to();
            // Handler returned — connection's logical lifetime is over.
            // Erase from the set so stop() no longer tries to shut it down.
            if (auto connSetPtr = weakConnSet.lock())
            {
                connSetPtr->erase(connPtr);
            }
        });
    }
    listenChannel_->disableAll();
    stopPromise_.set_value();
    NITRO_DEBUG("TcpServer::start() quit");
}

Task<> TcpServer::stop()
{
    co_await scheduler_->switch_to();
    if (stopped_.exchange(true))
        co_return;

    NITRO_DEBUG("TcpServer::stop() requested");
    listenChannel_->disableAll(); // stop listening first
    listenChannel_->cancelAll();

    std::vector<TcpConnectionPtr> conns(connSetPtr_->begin(), connSetPtr_->end());
    for (auto & c : conns)
    {
        co_await c->shutdown();
    }
    co_await stopFuture_.get();
}

Task<> TcpServer::wait() const
{
    co_await stopFuture_.get();
}

} // namespace nitrocoro::net
