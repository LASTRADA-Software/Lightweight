// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "../Api.hpp"
#include "Executor.hpp"
#include "WorkQueue.hpp"

#include <cstddef>
#include <stop_token>

namespace Lightweight::Async
{

/// An executor that runs work only when explicitly pumped by the owning thread.
///
/// This is the "app thread" / event-loop resume target for the single-threaded model:
/// blocking ODBC work is offloaded to a @ref ThreadPoolExecutor, and the coroutine's
/// continuation is posted back here so that all user-visible coroutine logic resumes on the
/// one thread that drives this executor (via @ref Run, @ref Drain, @ref RunOne or
/// @ref RunUntil). All members are thread-safe to call; the pumping members
/// (@ref Run / @ref Drain / @ref RunOne / @ref RunUntil) are intended for a single
/// consumer thread.
class LIGHTWEIGHT_API ManualExecutor final: public IExecutor, public IResumeScheduler
{
  public:
    ManualExecutor() = default;
    ManualExecutor(ManualExecutor const&) = delete;
    ManualExecutor& operator=(ManualExecutor const&) = delete;
    ManualExecutor(ManualExecutor&&) = delete;
    ManualExecutor& operator=(ManualExecutor&&) = delete;
    ~ManualExecutor() override = default;

    void Post(Work work) override;
    void Resume(std::coroutine_handle<> handle) override;

    /// Runs at most one queued work item without blocking.
    /// @return true if an item was run, false if the queue was empty.
    bool RunOne();

    /// Runs all currently-runnable work until the queue is empty, without blocking.
    /// @return the number of work items executed.
    std::size_t Drain();

    /// Blocks pumping work until @ref Stop is called and the queue has drained.
    void Run();

    /// Requests @ref Run to return once the queue is empty.
    void Stop();

    /// @return the number of currently-queued work items.
    [[nodiscard]] std::size_t PendingCount() const;

    /// Pumps work until @p predicate returns true.
    ///
    /// Blocks the calling thread between work items, waking when new work is posted. The
    /// predicate must be cheap and must not acquire this executor's internal lock. This is
    /// the driver used by @c SyncWaitPumping for the single-threaded model.
    ///
    /// @tparam Predicate A callable returning something contextually convertible to bool.
    /// @param predicate Stop condition, re-checked between work items.
    template <typename Predicate>
    void RunUntil(Predicate predicate)
    {
        // NOTE: RunUntil deliberately ignores the stop request — it must pump strictly until the
        // predicate is satisfied (the awaited task completes). Honoring Stop() here would return early
        // with the predicate still false, causing the caller (SyncWaitPumping) to read an unfinished
        // result. The stop request governs Run() (the event-loop pump), not RunUntil. The wake
        // condition is the predicate alone; wakeups are delivered by the work posted to the queue.
        while (!predicate())
        {
            Work work;
            if (!_queue.WaitAndPop(work, predicate))
                return;
            work();
        }
    }

  private:
    detail::WorkQueue _queue;
    std::stop_source _stopSource; ///< Requested by Stop(); read by Run()'s wake condition.
};

} // namespace Lightweight::Async
