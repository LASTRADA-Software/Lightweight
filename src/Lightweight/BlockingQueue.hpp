// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <utility>

namespace Lightweight::detail
{

/// Thread-safe blocking FIFO primitive backing @c ThreadSafeQueue.
///
/// It encapsulates the `std::mutex + std::condition_variable + std::deque<T>` dance so the
/// locking/wait logic lives in exactly one place: @ref Push enqueues and wakes a consumer, @ref
/// WaitAndPop blocks until an item is available or the queue is finished, and @ref MarkFinished
/// drains-then-stops. (The async executors deliberately do not share this primitive — they own
/// the smaller, purpose-built queues in @c Async/detail/SerialWorkQueue.hpp.)
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

    /// Blocks until an item is available or the queue is marked finished and empty, then pops.
    /// @param out Receives the popped item when one is available.
    /// @return true if an item was popped; false if the queue is finished and empty.
    bool WaitAndPop(T& out)
    {
        std::unique_lock lock(_mutex);
        _condition.wait(lock, [&] { return !_queue.empty() || _finished; });
        if (_queue.empty())
            return false;
        out = std::move(_queue.front());
        _queue.pop_front();
        return true;
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

  private:
    mutable std::mutex _mutex;
    std::condition_variable _condition;
    std::deque<T> _queue;
    bool _finished = false; ///< Set by MarkFinished(); makes WaitAndPop drain-then-stop.
};

} // namespace Lightweight::detail
