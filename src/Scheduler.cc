/**
 * @file Scheduler.cc
 * @brief Native coroutine scheduler implementation
 */
#include <nitrocoro/core/Scheduler.h>

#include <nitrocoro/io/Channel.h>
#include <nitrocoro/utils/Debug.h>

#include <cassert>
#include <csignal>
#include <cstring>
#include <stdexcept>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>

#define nitrocoro_SCHEDULER_ASSERT_IN_OWN_THREAD() \
    assert(isInOwnThread() && "Must be called in its own thread")

namespace nitrocoro
{

static constexpr int64_t kDefaultTimeoutMs = 10000;

Scheduler::Scheduler()
{
    if (current_ != nullptr)
    {
        throw std::logic_error("CoroScheduler already exists in this thread");
    }

    signal(SIGPIPE, SIG_IGN);
    epollFd_ = epoll_create1(EPOLL_CLOEXEC);
    if (epollFd_ < 0)
    {
        throw std::runtime_error("Failed to create epoll");
    }
    wakeupFd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (wakeupFd_ < 0)
    {
        close(epollFd_);
        throw std::runtime_error("Failed to create wakeup fd");
    }
    current_ = this;
}

Scheduler::~Scheduler()
{
    if (current_ == this)
        current_ = nullptr;
    if (wakeupFd_ >= 0)
        close(wakeupFd_);
    if (epollFd_ >= 0)
        close(epollFd_);

    wakeupChannel_.reset(); // Channel destructor will access readyQueue_
}

Scheduler * Scheduler::current() noexcept
{
    return current_;
}

void Scheduler::run()
{
    threadId_ = std::this_thread::get_id();
    wakeupChannel_ = std::make_unique<io::Channel>(wakeupFd_, TriggerMode::LevelTriggered, this);
    wakeupChannel_->enableReading();

    running_.store(true, std::memory_order_release);

    while (running_.load(std::memory_order_acquire))
    {
        int timeout_ms = static_cast<int>(get_next_timeout());
        process_io_events(timeout_ms);
        process_timers();
        process_ready_queue();
    }
}

void Scheduler::stop()
{
    running_.store(false, std::memory_order_release);
    if (!isInOwnThread())
    {
        wakeup();
    }
}

TimerAwaiter Scheduler::sleep_for(double seconds)
{
    auto when = std::chrono::steady_clock::now() + std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::duration<double>(seconds));
    return TimerAwaiter{ this, when };
}

TimerAwaiter Scheduler::sleep_for(std::chrono::steady_clock::duration dur)
{
    auto when = std::chrono::steady_clock::now() + dur;
    return TimerAwaiter{ this, when };
}

TimerAwaiter Scheduler::sleep_until(TimePoint when)
{
    return TimerAwaiter{ this, when };
}

SchedulerAwaiter Scheduler::switch_to() noexcept
{
    return SchedulerAwaiter{ this };
}

void Scheduler::schedule(std::coroutine_handle<> handle)
{
    readyQueue_.push([handle]() { handle.resume(); });
    if (!isInOwnThread())
    {
        wakeup();
    }
}

void Scheduler::schedule_at(TimePoint when, std::coroutine_handle<> handle)
{
    pendingTimers_.push(Timer{ when, handle });
    if (!isInOwnThread())
    {
        wakeup();
    }
}

void Scheduler::process_ready_queue()
{
    while (auto func = readyQueue_.pop())
    {
        (*func)();
    }
}

void Scheduler::process_io_events(int timeout_ms)
{
    epoll_event events[128];
    // TODO: abstract poller
    int n = epoll_wait(epollFd_, events, 128, timeout_ms);

    for (int i = 0; i < n; ++i)
    {
        int fd = events[i].data.fd;
        uint32_t ev = events[i].events;
        if (fd == wakeupChannel_->fd())
        {
            uint64_t dummy;
            ssize_t ret = read(wakeupFd_, &dummy, sizeof(dummy));
            if (ret < 0)
                NITRO_ERROR("wakeup read error: %s", strerror(errno));
            continue;
        }

        auto iter = ioContexts_.find(fd);
        if (iter == ioContexts_.end())
        {
            NITRO_ERROR("fd %d not found!!!", fd);
            continue;
        }
        auto * ctx = &iter->second;
        NITRO_TRACE("fd %d event %x: IN: %x, OUT: %x, ERR: %x",
                    fd,
                    ev,
                    ev & EPOLLIN,
                    ev & EPOLLOUT,
                    ev & (EPOLLERR | EPOLLHUP));

        ctx->handler(fd, ev);
    }
}

int64_t Scheduler::get_next_timeout()
{
    while (auto timer = pendingTimers_.pop())
    {
        timers_.push(std::move(*timer));
    }

    if (timers_.empty())
        return kDefaultTimeoutMs;

    auto now = std::chrono::steady_clock::now();
    auto next = timers_.top().when;

    if (next <= now)
        return 0;

    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(next - now);
    return ms.count();
}

void Scheduler::process_timers()
{
    if (timers_.empty())
        return;

    auto now = std::chrono::steady_clock::now();

    while (!timers_.empty() && timers_.top().when <= now)
    {
        auto timer = std::move(timers_.top());
        timers_.pop();
        readyQueue_.push([handle = timer.handle]() { handle.resume(); });
    }
}

void Scheduler::wakeup()
{
    uint64_t val = 1;
    ssize_t result = write(wakeupFd_, &val, sizeof(val));
    if (result < 0)
    {
        NITRO_ERROR("wakeup write error: %s", strerror(errno));
    }
}

void Scheduler::setIoHandler(int fd, uint64_t id, Scheduler::IoEventHandler handler)
{
    nitrocoro_SCHEDULER_ASSERT_IN_OWN_THREAD();

    auto iter = ioContexts_.find(fd);
    if (iter != ioContexts_.end())
    {
        if (iter->second.id != id)
        {
            if (id < iter->second.id)
            {
                NITRO_TRACE("fd %d reuse detected, ignoring stale operation (cur id=%lu, op id=%lu)", fd, iter->second.id, id);
                return;
            }
            NITRO_TRACE("fd %d reuse detected, evicting stale IoContext (cur id=%lu, op id=%lu)", fd, iter->second.id, id);
            iter->second = IoContext{ fd, id, std::move(handler) };
            return;
        }

        assert(iter->second.handler == nullptr);
        iter->second.handler = std::move(handler);
    }
    else
    {
        ioContexts_.emplace(fd, IoContext{ fd, id, std::move(handler) });
    }
}

void Scheduler::updateIo(int fd, uint64_t id, uint32_t events, TriggerMode mode)
{
    nitrocoro_SCHEDULER_ASSERT_IN_OWN_THREAD();

    IoContext * ctx;
    auto iter = ioContexts_.find(fd);
    if (iter != ioContexts_.end())
    {
        if (iter->second.id != id)
        {
            if (id < iter->second.id)
            {
                NITRO_TRACE("fd %d reuse detected, ignoring stale operation (cur id=%lu, op id=%lu)", fd, iter->second.id, id);
                return;
            }
            NITRO_TRACE("fd %d reuse detected, evicting stale IoContext (cur id=%lu, op id=%lu)", fd, iter->second.id, id);
            iter->second = IoContext{ fd, id, {} };
        }
        ctx = &iter->second;
    }
    else
    {
        ctx = &ioContexts_.emplace(fd, IoContext{ fd, id, {} }).first->second;
    }

    if (events == 0)
    {
        if (ctx->addedToEpoll)
        {
            epoll_event ev{};
            if (::epoll_ctl(epollFd_, EPOLL_CTL_DEL, fd, &ev) < 0)
            {
                NITRO_ERROR("Failed to call EPOLL_CTL_DEL on epoll %d fd %d errno %d: %s", epollFd_, fd, errno, strerror(errno));
                // throw std::runtime_error("Failed to call EPOLL_CTL_DEL on epoll");
            }
            ctx->addedToEpoll = false;
        }
        return;
    }

    epoll_event ev{};
    ev.events = events | EPOLLRDHUP | (mode == TriggerMode::EdgeTriggered ? EPOLLET : 0);
    ev.data.fd = fd;

    int op = ctx->addedToEpoll ? EPOLL_CTL_MOD : EPOLL_CTL_ADD;
    if (::epoll_ctl(epollFd_, op, fd, &ev) < 0)
    {
        NITRO_ERROR("epoll_ctl %s fd %d ev %d error: %s",
                    ctx->addedToEpoll ? "MOD" : "ADD", fd, events, strerror(errno));
        throw std::runtime_error(ctx->addedToEpoll ? "Failed to call EPOLL_CTL_MOD on epoll" : "Failed to call EPOLL_CTL_ADD on epoll");
    }

    ctx->addedToEpoll = true;
}

void Scheduler::removeIo(int fd, uint64_t id)
{
    nitrocoro_SCHEDULER_ASSERT_IN_OWN_THREAD();

    auto iter = ioContexts_.find(fd);
    assert(iter != ioContexts_.end());
    if (iter->second.id != id)
    {
        if (id > iter->second.id)
            NITRO_ERROR("fd %d removeIo: unexpected op id > cur id (cur id=%lu, op id=%lu)\n", fd, iter->second.id, id);
        NITRO_TRACE("fd %d reuse detected, ignoring stale operation (cur id=%lu, op id=%lu)", fd, iter->second.id, id);
        return;
    }

    auto ctx = std::move(iter->second);
    ioContexts_.erase(iter);

    if (!ctx.addedToEpoll)
    {
        return;
    }
    epoll_event ev{};
    ev.events = 0;
    ev.data.fd = fd;
    if (::epoll_ctl(epollFd_, EPOLL_CTL_DEL, ctx.fd, &ev) < 0)
    {
        NITRO_TRACE("Failed to call EPOLL_CTL_DEL on epoll %d fd %d, error = %d", epollFd_, ctx.fd, errno);
    }
}

bool Scheduler::isInOwnThread() const noexcept
{
    return std::this_thread::get_id() == threadId_;
}

void TimerAwaiter::await_suspend(std::coroutine_handle<> h) noexcept
{
    sched_->schedule_at(when_, h);
}

bool SchedulerAwaiter::await_ready() const noexcept
{
    return scheduler_->isInOwnThread();
}

void SchedulerAwaiter::await_suspend(std::coroutine_handle<> h) noexcept
{
    scheduler_->schedule(h);
}

} // namespace nitrocoro
