/**
 * @file CancelToken.h
 * @brief CancelSource / CancelToken — cooperative cancellation for coroutines.
 */
#pragma once

#include <nitrocoro/core/LockFreeList.h>
#include <nitrocoro/core/Scheduler.h>
#include <nitrocoro/core/Task.h>

#include <atomic>
#include <coroutine>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace nitrocoro
{

class CancelToken;
class CancelRegistration;

namespace detail
{

struct CancelState
{
    struct WaiterNode : LockFreeListNode
    {
        std::coroutine_handle<> handle;
        Scheduler * sched;
    };

    std::atomic<LockFreeListNode *> waiters_{ nullptr };

    struct Callback
    {
        std::function<void()> fn;
        Scheduler * sched;                        // dispatch target; nullptr = call inline
        std::shared_ptr<std::atomic_flag> active; // cleared on Reg destruction
    };

    // callbacks protected by mutex (cancel() may be called from any thread)
    std::mutex cbMutex_;
    std::vector<Callback> callbacks_;

    bool isCancelled() const noexcept
    {
        return LockFreeListNode::closed(waiters_);
    }

    void cancel()
    {
        auto * head = static_cast<WaiterNode *>(LockFreeListNode::close(waiters_));
        if (head == reinterpret_cast<WaiterNode *>(LockFreeListNode::kClosed))
            return; // already cancelled

        // fire callbacks
        decltype(callbacks_) cbs;
        {
            std::lock_guard lock(cbMutex_);
            cbs.swap(callbacks_);
        }
        for (auto & cb : cbs)
        {
            if (cb.sched)
                cb.sched->dispatch([fn = std::move(cb.fn), active = cb.active]() mutable {
                    if (!active->test_and_set())
                        fn();
                });
            else if (!cb.active->test_and_set())
                cb.fn();
        }

        // resume waiters
        for (auto * n = head; n;)
        {
            auto * next = static_cast<WaiterNode *>(n->next_);
            if (n->sched)
                n->sched->schedule(n->handle);
            else
                n->handle.resume();
            n = next;
        }
    }

    std::shared_ptr<std::atomic_flag> addCallback(std::function<void()> cb, Scheduler * sched = Scheduler::current())
    {
        auto active = std::make_shared<std::atomic_flag>();
        bool cancelled = false;
        {
            std::lock_guard lock(cbMutex_);
            if (isCancelled())
                cancelled = true;
            else
                callbacks_.push_back({ std::move(cb), sched, active });
        }
        if (cancelled)
        {
            if (sched)
                sched->dispatch(std::move(cb));
            else
                cb();
            return nullptr;
        }
        return active;
    }
};

} // namespace detail

// ─── CancelRegistration ──────────────────────────────────────────────────────

/**
 * RAII guard that deregisters a cancel callback on destruction.
 *
 * Thread-safety:
 *   CancelRegistration must be destroyed on the same thread that owns the
 *   callback's captured resources. If destroyed from a different thread while
 *   the callback is executing (dispatched via a scheduler), the captured
 *   objects may be destroyed mid-execution, causing undefined behaviour.
 *
 *   The typical coroutine pattern — holding Reg as a local variable and
 *   letting it go out of scope on the registering coroutine's thread — is
 *   always safe.
 *
 * Example (correct):
 * @code
 *   Task<> myCoroutine(CancelToken token) {
 *       SomeResource res;
 *       // reg is destroyed when leaving this scope, on this coroutine's thread
 *       auto reg = token.onCancel([&res] { res.cancel(); });
 *       co_await someAsyncOp();
 *   } // reg destroyed here — safe
 * @endcode
 *
 * Example (incorrect — do not do this):
 * @code
 *   auto reg = token.onCancel([&res] { res.cancel(); });
 *   std::thread([r = std::move(reg)] {}).detach(); // reg destroyed on another thread — UB
 *
 *   // Also incorrect: switching scheduler after registering
 *   auto reg = token.onCancel([&res] { res.cancel(); });
 *   co_await otherScheduler->switch_to();           // reg now lives on otherScheduler's thread — UB
 * @endcode
 */

class CancelRegistration
{
public:
    CancelRegistration() = default;

    explicit CancelRegistration(std::shared_ptr<std::atomic_flag> active)
        : active_(std::move(active))
    {
    }

    ~CancelRegistration()
    {
        unregister();
    }

    void unregister() noexcept
    {
        if (active_)
        {
            active_->test_and_set();
            active_.reset();
        }
    }

    CancelRegistration(CancelRegistration &&) noexcept = default;
    CancelRegistration & operator=(CancelRegistration &&) noexcept = default;
    CancelRegistration(const CancelRegistration &) = delete;
    CancelRegistration & operator=(const CancelRegistration &) = delete;

private:
    std::shared_ptr<std::atomic_flag> active_;
};

// ─── CancelToken ─────────────────────────────────────────────────────────────

class CancelToken
{
public:
    CancelToken() = default; // None — never cancelled

    // NOLINTNEXTLINE(google-explicit-constructor)
    CancelToken(std::chrono::steady_clock::duration dur);

    explicit operator bool() const noexcept { return state_ != nullptr; }

    bool isCancelled() const noexcept
    {
        return state_ && state_->isCancelled();
    }

    // Registers a callback to be invoked on cancellation on the calling thread.
    // Returns a CancelRegistration that deregisters the callback on destruction.
    // The returned CancelRegistration must not be moved to or destroyed on another thread.
    // See CancelRegistration for thread-safety requirements.
    [[nodiscard]] CancelRegistration onCancel(std::function<void()> cb)
    {
        if (!state_)
            return {};
        return CancelRegistration{ state_->addCallback(std::move(cb), Scheduler::current()) };
    }

    struct [[nodiscard]] CancelledAwaiter
    {
        std::shared_ptr<detail::CancelState> state_;
        detail::CancelState::WaiterNode node_;

        bool await_ready() const noexcept
        {
            return !state_ || LockFreeListNode::closed(state_->waiters_);
        }

        bool await_suspend(std::coroutine_handle<> h) noexcept
        {
            node_.handle = h;
            node_.sched = Scheduler::current();
            return LockFreeListNode::push(state_->waiters_, &node_);
        }

        void await_resume() noexcept {}
    };

    [[nodiscard]] CancelledAwaiter cancelled()
    {
        return CancelledAwaiter{ state_ };
    }

private:
    friend class CancelSource;

    explicit CancelToken(std::shared_ptr<detail::CancelState> state)
        : state_(std::move(state))
    {
    }

    std::shared_ptr<detail::CancelState> state_;
};

// ─── CancelSource ─────────────────────────────────────────────────────────────

class CancelSource
{
public:
    explicit CancelSource(Scheduler * sched = Scheduler::current())
        : state_(std::make_shared<detail::CancelState>())
        , sched_(sched)
    {
    }

    explicit CancelSource(std::chrono::steady_clock::duration dur, Scheduler * sched = Scheduler::current())
        : CancelSource(sched)
    {
        cancelAfter(dur);
    }

    ~CancelSource() = default;

    CancelToken token() const { return CancelToken(state_); }

    void cancel() { state_->cancel(); }

    bool isCancelled() const noexcept { return state_->isCancelled(); }

    void cancelAfter(std::chrono::steady_clock::duration dur)
    {
        auto * sched = sched_;
        std::weak_ptr<detail::CancelState> weak = state_;
        sched->spawn([weak, dur, sched]() -> Task<> {
            co_await sched->sleep_for(dur);
            if (auto s = weak.lock())
                s->cancel();
        });
    }

private:
    std::shared_ptr<detail::CancelState> state_;
    Scheduler * sched_;
};

inline CancelToken::CancelToken(std::chrono::steady_clock::duration dur)
    : CancelToken(CancelSource(dur).token())
{
}

} // namespace nitrocoro
