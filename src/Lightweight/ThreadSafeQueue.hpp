// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <condition_variable>
#include <deque>
#include <mutex>

namespace Lightweight
{

/// Thread-safe queue with blocking wait semantics.
///
/// This queue allows multiple producer and consumer threads to safely enqueue
/// and dequeue items. Consumers block on WaitAndPop until an item is available
/// or the queue is marked as finished.
///
/// @tparam T The type of items stored in the queue.
template <typename T>
class ThreadSafeQueue
{
  public:
    /// Pushes an item onto the queue and notifies one waiting consumer.
    ///
    /// @param item The item to push onto the queue.
    void Push(T item)
    {
        {
            std::scoped_lock lock(_mutex);
            _queue.push_back(std::move(item));
        }
        _condition.notify_one();
    }

    /// Blocks until an item is available or the queue is finished.
    ///
    /// This method will block the calling thread until either:
    /// - An item becomes available in the queue, or
    /// - The queue has been marked as finished and is empty.
    ///
    /// @param item Output parameter to receive the popped item.
    /// @return true if an item was successfully popped, false if the queue is finished and empty.
    bool WaitAndPop(T& item)
    {
        std::unique_lock lock(_mutex);
        _condition.wait(lock, [this] { return !_queue.empty() || _finished; });
        if (_queue.empty())
            return false;
        item = std::move(_queue.front());
        _queue.pop_front();
        return true;
    }

    /// Signals that no more items will be added.
    ///
    /// After this call, WaitAndPop will return false once the queue is empty.
    /// All waiting consumers will be notified.
    void MarkFinished()
    {
        {
            std::scoped_lock lock(_mutex);
            _finished = true;
        }
        _condition.notify_all();
    }

    /// Checks if the queue is empty.
    ///
    /// @return true if the queue is currently empty.
    [[nodiscard]] bool Empty() const
    {
        std::scoped_lock lock(_mutex);
        return _queue.empty();
    }

    /// Returns the current size of the queue.
    ///
    /// @return The number of items currently in the queue.
    [[nodiscard]] size_t Size() const
    {
        std::scoped_lock lock(_mutex);
        return _queue.size();
    }

  private:
    std::deque<T> _queue;
    mutable std::mutex _mutex;
    std::condition_variable _condition;
    bool _finished = false;
};

} // namespace Lightweight
