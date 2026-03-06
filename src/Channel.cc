/**
 * @file Channel.cc
 * @brief Implementation of Channel
 */
#include <nitrocoro/io/Channel.h>

#include <nitrocoro/core/Scheduler.h>
#include <nitrocoro/utils/Debug.h>

#include <cassert>
#include <cstring>
#include <sys/epoll.h>
#include <sys/socket.h>

namespace nitrocoro::io
{

Channel::Channel(int fd, TriggerMode mode, Scheduler * scheduler)
    : id_(Scheduler::nextIoId())
    , fd_(fd)
    , scheduler_(scheduler)
    , triggerMode_(mode)
    , state_(std::make_shared<IoState>(fd))
{
    assert(scheduler_ != nullptr);

    scheduler->schedule([id = id_, fd, weakState = std::weak_ptr(state_), scheduler]() {
        if (auto _ = weakState.lock())
        {
            scheduler->setIoHandler(id, fd, [scheduler, weakState](int fd, uint32_t ev) {
                if (auto state = weakState.lock())
                {
                    assert(fd == state->fd);
                    handleIoEvents(scheduler, state.get(), ev);
                }
            });
        }
    });
}

Channel::~Channel() noexcept
{
    scheduler_->schedule([id = id_, scheduler = scheduler_, guard = std::move(guard_)]() mutable {
        scheduler->removeIo(id);
        guard.reset();
    });
}

void Channel::handleIoEvents(Scheduler * scheduler, IoState * state, uint32_t ev)
{
    if ((ev & EPOLLHUP) && !(ev & EPOLLIN))
    {
        // peer closed, and no more bytes to read
        NITRO_TRACE("Peer closed, fd %d", state->fd);
        // TODO: handle close
    }

    if (ev & EPOLLERR) // (POLLNVAL | POLLERR)
    {
        int error = 0;
        socklen_t len = sizeof(error);
        if (getsockopt(state->fd, SOL_SOCKET, SO_ERROR, &error, &len) < 0)
        {
            NITRO_ERROR("getsockopt failed on socket %d: %s", state->fd, strerror(errno));
        }
        else if (error == 0)
        {
            NITRO_ERROR("socket %d EPOLLERR but no error", state->fd);
        }
        else if (error == ECONNRESET || error == EPIPE)
        {
            NITRO_TRACE("socket %d connection closed (error=%d)", state->fd, error);
        }
        else
        {
            NITRO_ERROR("socket %d error %d: %s", state->fd, error, strerror(error));
        }
        // TODO: mark error
    }

    if (ev & EPOLLIN) // (POLLIN | POLLPRI | POLLRDHUP)
    {
        state->readable = true;
        if (state->readableWaiter)
        {
            auto h = state->readableWaiter;
            state->readableWaiter = nullptr;
            scheduler->schedule(h);
        }
    }
    if (ev & EPOLLOUT) // WIN32: if ((ev & POLLOUT) && !(ev & POLLHUP))
    {
        NITRO_DEBUG("Handle write fd %d writable = %d", state->fd, state->writable);
        state->writable = true;
        if (state->writableWaiter)
        {
            auto h = state->writableWaiter;
            state->writableWaiter = nullptr;
            scheduler->schedule(h);
        }
    }
}

bool Channel::ReadableAwaiter::await_ready() noexcept
{
    return state_->readable;
}

bool Channel::ReadableAwaiter::await_suspend(std::coroutine_handle<> h) noexcept
{
    if (state_->readable)
    {
        return false;
    }
    else
    {
        state_->readableWaiter = h;
        return true;
    }
}

void Channel::ReadableAwaiter::await_resume() noexcept
{
}

bool Channel::WritableAwaiter::await_ready() noexcept
{
    return state_->writable;
}

bool Channel::WritableAwaiter::await_suspend(std::coroutine_handle<> h) noexcept
{
    if (state_->writable)
    {
        return false;
    }
    else
    {
        state_->writableWaiter = h;
        return true;
    }
}

void Channel::WritableAwaiter::await_resume() noexcept
{
}

void Channel::enableReading()
{
    if (!(events_ & EPOLLIN))
    {
        events_ |= EPOLLIN;
        scheduler_->updateIo(id_, fd_, events_, triggerMode_);
    }
}

void Channel::disableReading()
{
    if (events_ & EPOLLIN)
    {
        events_ &= ~EPOLLIN;
        scheduler_->updateIo(id_, fd_, events_, triggerMode_);
    }
}

void Channel::enableWriting()
{
    if (!(events_ & EPOLLOUT))
    {
        events_ |= EPOLLOUT;
        scheduler_->updateIo(id_, fd_, events_, triggerMode_);
    }
}

void Channel::disableWriting()
{
    if (events_ & EPOLLOUT)
    {
        events_ &= ~EPOLLOUT;
        scheduler_->updateIo(id_, fd_, events_, triggerMode_);
    }
}

void Channel::disableAll()
{
    if (events_ != 0)
    {
        events_ = 0;
        scheduler_->updateIo(id_, fd_, events_, triggerMode_);
    }
}

void Channel::cancelRead()
{
    if (state_->readableWaiter)
    {
        state_->readCanceled = true;
        auto h = state_->readableWaiter;
        state_->readableWaiter = nullptr;
        scheduler_->schedule(h);
    }
}

void Channel::cancelWrite()
{
    if (state_->writableWaiter)
    {
        state_->writeCanceled = true;
        auto h = state_->writableWaiter;
        state_->writableWaiter = nullptr;
        scheduler_->schedule(h);
    }
}

void Channel::cancelAll()
{
    cancelRead();
    cancelWrite();
}

} // namespace nitrocoro::io
