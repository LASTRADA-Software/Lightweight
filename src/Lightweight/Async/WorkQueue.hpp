// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "../BlockingQueue.hpp"
#include "Executor.hpp"

#include <cstddef>
#include <utility>

namespace Lightweight::Async::detail
{

/// Thread-safe FIFO of @ref Work items shared by the manual and strand executors.
///
/// A thin facade over @ref Lightweight::detail::BlockingQueue (the shared blocking-FIFO primitive,
/// also backing @c ThreadSafeQueue), exposing just the operations the async executors layer their
/// scheduling on top of:
///  - @ref ManualExecutor blocks in @ref WaitAndPop, drains via @ref TryPop, and wakes blocked
///    pumpers via @ref Wake when its stop flag flips.
///  - @ref StrandExecutor serializes a single drain via @ref PushAndClaimDrain / @ref PopOrEndDrain;
///    the drain-active bookkeeping lives under the @b same lock as the queue (in the shared core),
///    which closes the push-vs-end-drain race without a second mutex.
class WorkQueue
{
  public:
    /// Enqueues @p work and notifies one blocked waiter.
    void Push(Work work)
    {
        _queue.Push(std::move(work));
    }

    /// Pops the front item without blocking.
    /// @param out Receives the popped item when one is available.
    /// @return true if an item was popped; false if the queue was empty.
    bool TryPop(Work& out)
    {
        return _queue.TryPop(out);
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
        return _queue.WaitAndPop(out, std::move(wake));
    }

    /// Wakes all blocked waiters so they re-evaluate their wake condition.
    ///
    /// Briefly takes the lock before notifying so a waiter that has already evaluated its predicate
    /// but not yet blocked cannot miss the notification — the flag that waiter reads must be
    /// published (e.g. an atomic release store) before this call.
    void Wake()
    {
        _queue.Wake();
    }

    /// @return the number of queued items.
    [[nodiscard]] std::size_t Size() const
    {
        return _queue.Size();
    }

    /// Strand support: enqueues @p work and, if no drain is currently active, claims the drain.
    ///
    /// @param work The work item to enqueue (consumed).
    /// @return true if the caller should schedule a drain (it just transitioned idle -> draining);
    ///         false if a drain is already active and will pick up this item.
    bool PushAndClaimDrain(Work work)
    {
        return _queue.PushAndClaimDrain(std::move(work));
    }

    /// Strand support: pops the next item, or ends the drain when the queue is empty.
    ///
    /// @param out Receives the popped item when one is available.
    /// @return true if an item was popped; false if the queue was empty (the drain flag is then
    ///         cleared under the lock so the next @ref PushAndClaimDrain re-schedules a drain).
    bool PopOrEndDrain(Work& out)
    {
        return _queue.PopOrEndDrain(out);
    }

  private:
    Lightweight::detail::BlockingQueue<Work> _queue;
};

} // namespace Lightweight::Async::detail
