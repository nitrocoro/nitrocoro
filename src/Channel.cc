/**
 * @file Channel.cc
 * @brief Implementation of Channel
 */
#include <nitrocoro/io/Channel.h>

#include <nitrocoro/core/Scheduler.h>
#include <nitrocoro/utils/Debug.h>

#include <cassert>
#include <sys/epoll.h>

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

    scheduler->dispatch([id = id_, fd, weakState = std::weak_ptr(state_), scheduler]() {
        if (auto _ = weakState.lock())
        {
            scheduler->setIoHandler(fd, id, [scheduler, weakState](int fd_, uint32_t ev) {
                if (auto state = weakState.lock())
                {
                    assert(fd_ == state->fd);
                    handleIoEvents(scheduler, state.get(), ev);
                }
            });
        }
    });
}

Channel::~Channel() noexcept
{
    scheduler_->dispatch([fd = fd_, id = id_, scheduler = scheduler_, guard = std::move(guard_)]() {
        scheduler->removeIo(fd, id);
        // guard auto released
    });
}

void Channel::handleIoEvents(Scheduler * scheduler, IoState * state, uint32_t ev)
{
    if (ev & EPOLLERR)
    {
        NITRO_TRACE("socket %d EPOLLERR", state->fd);
        state->errored = true;
        if (state->readableWaiter)
        {
            auto h = state->readableWaiter;
            state->readableWaiter = nullptr;
            scheduler->schedule(h);
        }
        if (state->writableWaiter)
        {
            auto h = state->writableWaiter;
            state->writableWaiter = nullptr;
            scheduler->schedule(h);
        }
    }

    if (ev & EPOLLRDHUP)
    {
        state->peerClosed = true;
        // 直接调用回调
        if (state->peerClosedCallback)
        {
            state->peerClosedCallback();
        }
    }

    if (ev & (EPOLLIN | EPOLLHUP | EPOLLRDHUP))
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
    return state_->readable || state_->errored;
}

bool Channel::ReadableAwaiter::await_suspend(std::coroutine_handle<> h) noexcept
{
    if (state_->readable || state_->errored)
        return false;
    state_->readableWaiter = h;
    return true;
}

void Channel::ReadableAwaiter::await_resume() noexcept
{
}

bool Channel::WritableAwaiter::await_ready() noexcept
{
    return state_->writable || state_->errored;
}

bool Channel::WritableAwaiter::await_suspend(std::coroutine_handle<> h) noexcept
{
    if (state_->writable || state_->errored)
        return false;
    state_->writableWaiter = h;
    return true;
}

void Channel::WritableAwaiter::await_resume() noexcept
{
}

void Channel::enableReading()
{
    if (!(events_ & EPOLLIN))
    {
        events_ |= EPOLLIN;
        scheduler_->updateIo(fd_, id_, events_, triggerMode_);
    }
}

void Channel::disableReading()
{
    if (events_ & EPOLLIN)
    {
        events_ &= ~EPOLLIN;
        scheduler_->updateIo(fd_, id_, events_, triggerMode_);
    }
}

void Channel::enableWriting()
{
    if (!(events_ & EPOLLOUT))
    {
        events_ |= EPOLLOUT;
        scheduler_->updateIo(fd_, id_, events_, triggerMode_);
    }
}

void Channel::disableWriting()
{
    if (events_ & EPOLLOUT)
    {
        events_ &= ~EPOLLOUT;
        scheduler_->updateIo(fd_, id_, events_, triggerMode_);
    }
}

void Channel::disableAll()
{
    if (events_ != 0)
    {
        events_ = 0;
        scheduler_->updateIo(fd_, id_, events_, triggerMode_);
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

// ============================================================
// CallbackChannel
// ============================================================

#include <nitrocoro/io/CallbackChannel.h>

namespace nitrocoro::io
{

struct CallbackChannel::State
{
    std::function<void()> onReadable;
    std::function<void()> onWritable;
    std::function<void()> onClose;
    std::function<void()> onError;
};

CallbackChannel::CallbackChannel(int fd, Scheduler * scheduler)
    : id_(Scheduler::nextIoId())
    , fd_(fd)
    , scheduler_(scheduler)
    , state_(std::make_shared<State>())
{
    scheduler->dispatch([fd, id = id_, weakState = std::weak_ptr(state_), scheduler]() {
        if (auto _ = weakState.lock())
        {
            scheduler->setIoHandler(fd, id, [weakState](int, uint32_t ev) {
                if (auto state = weakState.lock())
                    handleIoEvents(state.get(), ev);
            });
        }
    });
}

CallbackChannel::~CallbackChannel() noexcept
{
    scheduler_->dispatch([fd = fd_, id = id_, scheduler = scheduler_, guard = std::move(guard_)]() {
        scheduler->removeIo(fd, id);
        // guard auto released
    });
}

void CallbackChannel::handleIoEvents(State * state, uint32_t ev)
{
    if ((ev & EPOLLHUP) && !(ev & EPOLLIN) && state->onClose)
        state->onClose();
    if ((ev & EPOLLERR) && state->onError)
        state->onError();
    if ((ev & EPOLLIN) && state->onReadable)
        state->onReadable();
    if ((ev & EPOLLOUT) && state->onWritable)
        state->onWritable();
}

void CallbackChannel::setEvents(uint32_t newEvents)
{
    if (events_ == newEvents)
        return;
    events_ = newEvents;
    scheduler_->updateIo(fd_, id_, events_, TriggerMode::LevelTriggered);
}

void CallbackChannel::enableReading()
{
    setEvents(events_ | EPOLLIN);
}
void CallbackChannel::disableReading()
{
    setEvents(events_ & ~EPOLLIN);
}
void CallbackChannel::enableWriting()
{
    setEvents(events_ | EPOLLOUT);
}
void CallbackChannel::disableWriting()
{
    setEvents(events_ & ~EPOLLOUT);
}
void CallbackChannel::disableAll()
{
    setEvents(0);
}

void CallbackChannel::setReadableCallback(std::function<void()> cb)
{
    state_->onReadable = std::move(cb);
}
void CallbackChannel::setWritableCallback(std::function<void()> cb)
{
    state_->onWritable = std::move(cb);
}
void CallbackChannel::setCloseCallback(std::function<void()> cb)
{
    state_->onClose = std::move(cb);
}
void CallbackChannel::setErrorCallback(std::function<void()> cb)
{
    state_->onError = std::move(cb);
}

} // namespace nitrocoro::io
