/**
 * @file Channel.h
 * @brief Channel abstraction for managing fd I/O operations
 * @note Only supports one concurrent reader and one concurrent writer per fd
 *
 * THREAD SAFETY:
 * All member accesses are serialized by the single-threaded Scheduler event loop.
 * The execution order is: process_io_events() -> process_timers() -> resume_ready_coros().
 * handleReadable/handleWritable are called in process_io_events(), while coroutines
 * (performReadImpl/performWriteImpl and awaiters) execute in resume_ready_coros().
 * These phases never overlap, ensuring no race conditions on member variables.
 *
 * CRITICAL: Any future modifications MUST preserve this serialization guarantee.
 * Do NOT introduce concurrent access to Channel members from multiple threads.
 */
#pragma once

#include <coroutine>
#include <memory>
#include <nitrocoro/core/Scheduler.h>
#include <nitrocoro/core/Task.h>
#include <nitrocoro/core/Types.h>

namespace nitrocoro::io
{

using nitrocoro::Scheduler;
using nitrocoro::Task;

class Channel;
using IoChannelPtr = std::shared_ptr<Channel>;

class Channel
{
public:
    explicit Channel(int fd, TriggerMode mode = TriggerMode::EdgeTriggered, Scheduler * scheduler = Scheduler::current());
    ~Channel() noexcept;

    Channel(const Channel &) = delete;
    Channel & operator=(const Channel &) = delete;
    Channel(Channel &&) = delete;
    Channel & operator=(Channel &&) = delete;

    /**
     * @brief Defer the destruction of a resource until after this channel is removed from epoll.
     *
     * Channel destruction is asynchronous: the epoll removal (EPOLL_CTL_DEL) is posted to the
     * scheduler queue and executes later. If the underlying fd is closed before that point, the OS
     * may reuse the fd number for a new connection, and the deferred EPOLL_CTL_DEL would then
     * silently remove the wrong fd from epoll.
     *
     * To prevent this, pass a shared_ptr to the resource that owns the fd (e.g. Socket, PgConn).
     * Channel holds a copy of the shared_ptr and releases it only after removeIo() completes,
     * guaranteeing that the fd is not closed until epoll cleanup is done.
     *
     * The caller retains its own shared_ptr; this method does not transfer ownership.
     */
    void setGuard(std::shared_ptr<void> guard) { guard_ = std::move(guard); }

    uint64_t id() const { return id_; }
    int fd() const { return fd_; }
    Scheduler * scheduler() const { return scheduler_; }
    TriggerMode triggerMode() const { return triggerMode_; }
    uint32_t events() const { return events_; }
    bool errored() const { return state_->errored; }
    bool peerClosed() const { return state_->peerClosed; }

    void setPeerClosedCallback(std::function<void()> callback)
    {
        state_->peerClosedCallback = std::move(callback);
    }

    // Following methods MUST be called from Scheduler's thread
    void enableReading();
    void enableWriting();
    void disableReading();
    void disableWriting();
    void disableAll();

    // Returned by adapters/lambdas to drive the performImpl loop
    enum class IoStatus
    {
        Success,
        NeedRead,  // wait for readable, then retry
        NeedWrite, // wait for writable, then retry
        Retry,
        Eof,  // read() returned 0: peer closed write direction
        Error // ECONNRESET, EPIPE, or other fatal errors
    };

    // Returned by perform() / performRead() / performWrite() to callers
    enum class IoResult
    {
        Success,
        Eof,     // read() returned 0: peer closed write direction
        Error,   // ECONNRESET, EPIPE, or other fatal errors
        Canceled // operation was canceled via cancelRead()/cancelWrite()
    };

    enum class WaitHint
    {
        Read,  // wait for readable before first invocation
        Write, // wait for writable before first invocation
        None   // invoke immediately
    };

    // Adapter pointer overload: perform(&reader) / perform(&writer)
    template <typename Adapter>
        requires std::invocable<Adapter, int, Channel *>
    Task<IoResult> perform(Adapter * adapter, WaitHint hint = WaitHint::None)
    {
        co_return co_await performImpl(adapter, hint);
    }

    // Callable overload: perform(lambda)
    template <typename Func>
        requires std::invocable<Func, int, Channel *>
                 && std::same_as<std::invoke_result_t<Func, int, Channel *>, IoStatus>
                 && (!std::is_pointer_v<Func>)
    Task<IoResult> perform(Func && func, WaitHint hint = WaitHint::None)
    {
        co_return co_await performImpl(std::forward<Func>(func), hint);
    }

    template <typename T>
    Task<IoResult> performRead(T && t)
    {
        co_return co_await perform(std::forward<T>(t), WaitHint::Read);
    }

    template <typename T>
    Task<IoResult> performWrite(T && t)
    {
        co_return co_await perform(std::forward<T>(t), WaitHint::Write);
    }

    void cancelRead();
    void cancelWrite();
    void cancelAll();

private:
    struct IoState
    {
        int fd{ -1 };
        bool readable{ false };
        bool writable{ true };
        bool errored{ false };
        bool peerClosed{ false };
        std::coroutine_handle<> readableWaiter;
        std::coroutine_handle<> writableWaiter;
        bool readCanceled{ false };
        bool writeCanceled{ false };
        std::function<void()> peerClosedCallback;
    };

    // Called by Scheduler::process_io_events() when epoll reports events
    static void handleIoEvents(Scheduler * scheduler, IoState * state, uint32_t ev);

    struct [[nodiscard]] ReadableAwaiter
    {
        IoState * state_;

        bool await_ready() noexcept;
        bool await_suspend(std::coroutine_handle<> h) noexcept;
        void await_resume() noexcept;
    };

    struct [[nodiscard]] WritableAwaiter
    {
        IoState * state_;

        bool await_ready() noexcept;
        bool await_suspend(std::coroutine_handle<> h) noexcept;
        void await_resume() noexcept;
    };

    template <typename T>
    Task<IoResult> performImpl(T && func, WaitHint hint)
    {
        co_await scheduler_->switch_to();

        WaitHint pendingWait = hint;
        while (true)
        {
            if (pendingWait == WaitHint::Read && !state_->readable)
            {
                co_await ReadableAwaiter{ state_.get() };
                if (state_->readCanceled)
                {
                    state_->readCanceled = false;
                    co_return IoResult::Canceled;
                }
            }
            else if (pendingWait == WaitHint::Write && !state_->writable)
            {
                co_await WritableAwaiter{ state_.get() };
                if (state_->writeCanceled)
                {
                    state_->writeCanceled = false;
                    co_return IoResult::Canceled;
                }
            }

            IoStatus status;
            if constexpr (std::is_pointer_v<std::remove_reference_t<T>>)
                status = (*func)(fd_, this);
            else
                status = func(fd_, this);

            switch (status)
            {
                case IoStatus::Success:
                    co_return IoResult::Success;

                case IoStatus::Eof:
                    co_return IoResult::Eof;

                case IoStatus::Error:
                    co_return IoResult::Error;

                case IoStatus::NeedRead:
                    state_->readable = false;
                    pendingWait = WaitHint::Read;
                    break;

                case IoStatus::NeedWrite:
                    state_->writable = false;
                    pendingWait = WaitHint::Write;
                    break;

                case IoStatus::Retry:
                    pendingWait = WaitHint::None;
                    break;
            }
        }
    }

    const uint64_t id_;
    int fd_{ -1 };
    Scheduler * scheduler_{ nullptr };
    TriggerMode triggerMode_{ TriggerMode::EdgeTriggered };
    std::shared_ptr<void> guard_;

    // All members below are accessed only within Scheduler's single thread.
    // No synchronization primitives needed due to serialized execution model.
    uint32_t events_{ 0 };
    std::shared_ptr<IoState> state_;
};

} // namespace nitrocoro::io
