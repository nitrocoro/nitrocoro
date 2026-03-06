/**
 * @file Socket.cc
 * @brief Implementation of Socket
 */
#include <nitrocoro/net/Socket.h>

#include <nitrocoro/utils/Debug.h>

#include <cerrno>
#include <cstring>
#include <sys/socket.h>
#include <unistd.h>

namespace nitrocoro::net
{

Socket::~Socket() noexcept
{
    if (fd_ >= 0)
        ::close(fd_);
}

Socket & Socket::operator=(Socket && other) noexcept
{
    if (this != &other)
    {
        if (fd_ >= 0)
            ::close(fd_);
        fd_ = other.fd_;
        other.fd_ = -1;
    }
    return *this;
}

void Socket::shutdownWrite() noexcept
{
    if (::shutdown(fd_, SHUT_WR) < 0)
        NITRO_ERROR("shutdownWrite fd %d failed: %s", fd_, strerror(errno));
}

} // namespace nitrocoro::net
