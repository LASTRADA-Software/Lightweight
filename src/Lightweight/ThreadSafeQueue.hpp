// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "BlockingQueue.hpp"

#include <cstddef>
#include <utility>

namespace Lightweight
{

/// Thread-safe queue with blocking wait semantics.
///
/// This queue allows multiple producer and consumer threads to safely enqueue
/// and dequeue items. Consumers block on WaitAndPop until an item is available
/// or the queue is marked as finished.
///
/// It is a thin facade over @ref detail::BlockingQueue (the shared blocking-FIFO primitive), so the
/// locking/wait logic lives in exactly one place rather than being duplicated here and in the async
/// layer's @c WorkQueue.
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
        _queue.Push(std::move(item));
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
        return _queue.WaitAndPop(item);
    }

    /// Signals that no more items will be added.
    ///
    /// After this call, WaitAndPop will return false once the queue is empty.
    /// All waiting consumers will be notified.
    void MarkFinished()
    {
        _queue.MarkFinished();
    }

    /// Checks if the queue is empty.
    ///
    /// @return true if the queue is currently empty.
    [[nodiscard]] bool Empty() const
    {
        return _queue.Empty();
    }

    /// Returns the current size of the queue.
    ///
    /// @return The number of items currently in the queue.
    [[nodiscard]] size_t Size() const
    {
        return _queue.Size();
    }

  private:
    detail::BlockingQueue<T> _queue;
};

} // namespace Lightweight
