// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <utility>

namespace Lightweight::detail
{

/// Thread-safe blocking FIFO primitive shared by @c ThreadSafeQueue and the async executors' @c
/// WorkQueue.
///
/// It encapsulates the one `std::mutex + std::condition_variable + std::deque<T>` dance both of those
/// types would otherwise hand-roll, so a fix to the locking/wait logic lives in exactly one place. The
/// public facades expose only the subset of operations they need:
///  - @c ThreadSafeQueue uses @ref Push / @ref WaitAndPop / @ref MarkFinished (a blocking MPMC queue
///    that drains and then reports completion).
///  - @c WorkQueue (async layer) uses @ref Push / @ref TryPop / the predicate @ref WaitAndPop / @ref
///    Wake plus the strand-drain bookkeeping (@ref PushAndClaimDrain / @ref PopOrEndDrain), which must
///    be mutated under the @b same lock as the queue to close the push-vs-end-drain race.
///
/// The @c _finished and @c _draining flags are independent: a given facade uses one and leaves the
/// other permanently false, so neither perturbs the other's behavior.
///
/// @tparam T The element type stored in the queue.
template <typename T>
class BlockingQueue
{
  public:
    /// Enqueues @p item and notifies one blocked waiter.
    void Push(T item)
    {
        {
            std::scoped_lock const lock(_mutex);
            _queue.push_back(std::move(item));
        }
        _condition.notify_one();
    }

    /// Pops the front item without blocking.
    /// @param out Receives the popped item when one is available.
    /// @return true if an item was popped; false if the queue was empty.
    bool TryPop(T& out)
    {
        std::scoped_lock const lock(_mutex);
        if (_queue.empty())
            return false;
        out = std::move(_queue.front());
        _queue.pop_front();
        return true;
    }

    /// Blocks until an item is available, the queue is finished, or @p wake returns true, then pops.
    ///
    /// @tparam Wake A predicate callable returning something contextually convertible to bool.
    /// @param out Receives the popped item when one is available.
    /// @param wake Extra wake condition, re-checked under the lock on every notification (e.g. an
    ///        external completion predicate). It must be cheap and must not touch this queue's lock.
    ///        The flag it reads must be published before a paired @ref Wake call.
    /// @return true if an item was popped; false if @p wake / finished fired with the queue empty.
    template <typename Wake>
    bool WaitAndPop(T& out, Wake wake)
    {
        std::unique_lock lock(_mutex);
        _condition.wait(lock, [&] { return !_queue.empty() || _finished || wake(); });
        if (_queue.empty())
            return false;
        out = std::move(_queue.front());
        _queue.pop_front();
        return true;
    }

    /// Blocks until an item is available or the queue is marked finished and empty, then pops.
    /// @param out Receives the popped item when one is available.
    /// @return true if an item was popped; false if the queue is finished and empty.
    bool WaitAndPop(T& out)
    {
        return WaitAndPop(out, [] { return false; });
    }

    /// Wakes all blocked waiters so they re-evaluate their wake condition.
    ///
    /// Briefly takes the lock before notifying so a waiter that has already evaluated its predicate
    /// but not yet blocked cannot miss the notification — the flag that waiter reads must be published
    /// (e.g. an atomic release store) before this call.
    void Wake()
    {
        {
            std::scoped_lock const lock(_mutex);
        }
        _condition.notify_all();
    }

    /// Signals that no more items will be added; @ref WaitAndPop returns false once the queue is empty.
    void MarkFinished()
    {
        {
            std::scoped_lock const lock(_mutex);
            _finished = true;
        }
        _condition.notify_all();
    }

    /// @return true if the queue is currently empty.
    [[nodiscard]] bool Empty() const
    {
        std::scoped_lock const lock(_mutex);
        return _queue.empty();
    }

    /// @return the number of queued items.
    [[nodiscard]] std::size_t Size() const
    {
        std::scoped_lock const lock(_mutex);
        return _queue.size();
    }

    /// Strand support: enqueues @p item and, if no drain is currently active, claims the drain.
    ///
    /// @param item The item to enqueue (consumed).
    /// @return true if the caller should schedule a drain (it just transitioned idle -> draining);
    ///         false if a drain is already active and will pick up this item.
    bool PushAndClaimDrain(T item)
    {
        std::scoped_lock const lock(_mutex);
        _queue.push_back(std::move(item));
        if (_draining)
            return false;
        _draining = true;
        return true;
    }

    /// Strand support: pops the next item, or ends the drain when the queue is empty.
    ///
    /// @param out Receives the popped item when one is available.
    /// @return true if an item was popped; false if the queue was empty (the drain flag is then
    ///         cleared under the lock so the next @ref PushAndClaimDrain re-schedules a drain).
    bool PopOrEndDrain(T& out)
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
    mutable std::mutex _mutex;
    std::condition_variable _condition;
    std::deque<T> _queue;
    bool _finished = false; ///< ThreadSafeQueue-only: set by MarkFinished(); makes WaitAndPop drain-then-stop.
    bool _draining = false; ///< Strand-only: true while a single drain closure is scheduled/running.
};

} // namespace Lightweight::detail
