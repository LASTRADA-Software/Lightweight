// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "../Executor.hpp"

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <utility>

namespace Lightweight::Async::detail
{

/// Single-consumer blocking FIFO of @ref Work items used by @ref ManualExecutor.
///
/// @ref ManualExecutor is a @e resume target, not an offloader: the coroutine continuations it
/// receives run directly on the thread that pumps it (@ref ManualExecutor::Run / @ref
/// ManualExecutor::Drain / @ref ManualExecutor::RunOne / @ref ManualExecutor::RunUntil), never on
/// stdexec. (Blocking work is offloaded elsewhere, to a @ref ThreadPoolExecutor.) That pump still
/// needs a tiny thread-safe queue a single owning thread can block on (@ref WaitAndPop), step
/// (@ref TryPop), and be woken from (@ref Wake) when its stop flag flips. stdexec's @c run_loop
/// cannot express the predicate-gated @c RunUntil pump that @c SyncWaitPumping relies on, so this
/// stays hand-rolled.
///
/// This is deliberately a focused subset of the former shared @c BlockingQueue — no MPMC
/// completion flag, no strand bookkeeping — keeping exactly the operations the pump needs.
class PumpQueue
{
  public:
    /// Enqueues @p work and notifies one blocked waiter.
    /// @param work The work item to enqueue (consumed).
    void Push(Work work)
    {
        {
            std::scoped_lock const lock(_mutex);
            _queue.push_back(std::move(work));
        }
        _condition.notify_one();
    }

    /// Pops the front item without blocking.
    /// @param out Receives the popped item when one is available.
    /// @return true if an item was popped; false if the queue was empty.
    bool TryPop(Work& out)
    {
        std::scoped_lock const lock(_mutex);
        if (_queue.empty())
            return false;
        out = std::move(_queue.front());
        _queue.pop_front();
        return true;
    }

    /// Blocks until an item is available or @p wake returns true, then pops the front item.
    ///
    /// @tparam Wake A predicate callable returning something contextually convertible to bool.
    /// @param out Receives the popped item when one is available.
    /// @param wake Extra wake condition, re-checked under the lock on every notification (e.g. a
    ///        stop flag or an external completion predicate). It must be cheap and must not touch
    ///        this queue's lock. The flag it reads must be published before a paired @ref Wake call.
    /// @return true if an item was popped; false if @p wake fired with the queue empty.
    template <typename Wake>
    bool WaitAndPop(Work& out, Wake wake)
    {
        std::unique_lock lock(_mutex);
        _condition.wait(lock, [&] { return !_queue.empty() || wake(); });
        if (_queue.empty())
            return false;
        out = std::move(_queue.front());
        _queue.pop_front();
        return true;
    }

    /// Wakes all blocked waiters so they re-evaluate their wake condition.
    ///
    /// Briefly takes the lock before notifying so a waiter that has already evaluated its predicate
    /// but not yet blocked cannot miss the notification — the flag that waiter reads must be
    /// published (e.g. an atomic release store) before this call.
    void Wake()
    {
        {
            std::scoped_lock const lock(_mutex);
        }
        _condition.notify_all();
    }

    /// @return the number of queued items.
    [[nodiscard]] std::size_t Size() const
    {
        std::scoped_lock const lock(_mutex);
        return _queue.size();
    }

  private:
    mutable std::mutex _mutex;
    std::condition_variable _condition;
    std::deque<Work> _queue;
};

/// Serialized-drain FIFO of @ref Work items used by @ref StrandExecutor.
///
/// Guards the pending queue and a single "a drain is scheduled/running" flag under @b one lock so
/// the push-vs-end-drain race is closed without a second mutex: @ref PushAndClaimDrain enqueues and
/// atomically decides whether the caller must schedule a fresh drain, and @ref PopOrEndDrain pops
/// the next item or clears the flag when the queue empties. The drain closure runs on the strand's
/// underlying (now stdexec-backed) executor, so the strand owns no thread of its own.
class SerialDrainQueue
{
  public:
    /// Enqueues @p work and, if no drain is currently active, claims the drain.
    ///
    /// @param work The work item to enqueue (consumed).
    /// @return true if the caller should schedule a drain (it just transitioned idle -> draining);
    ///         false if a drain is already active and will pick up this item.
    bool PushAndClaimDrain(Work work)
    {
        std::scoped_lock const lock(_mutex);
        _queue.push_back(std::move(work));
        if (_draining)
            return false;
        _draining = true;
        return true;
    }

    /// Pops the next item, or ends the drain when the queue is empty.
    ///
    /// @param out Receives the popped item when one is available.
    /// @return true if an item was popped; false if the queue was empty (the drain flag is then
    ///         cleared under the lock so the next @ref PushAndClaimDrain re-schedules a drain).
    bool PopOrEndDrain(Work& out)
    {
        std::scoped_lock const lock(_mutex);
        if (_queue.empty())
        {
            _draining = false;
            return false;
        }
        out = std::move(_queue.front());
        _queue.pop_front();
        return true;
    }

  private:
    std::mutex _mutex;
    std::deque<Work> _queue;
    bool _draining = false; ///< true while a single drain closure is scheduled/running.
};

} // namespace Lightweight::Async::detail
