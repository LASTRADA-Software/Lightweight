// SPDX-License-Identifier: Apache-2.0

#include "ManualExecutor.hpp"

#include <utility>

namespace Lightweight::Async
{

void ManualExecutor::Post(Work work)
{
    _queue.Push(std::move(work));
}

void ManualExecutor::Resume(std::coroutine_handle<> handle)
{
    Post([handle] { handle.resume(); });
}

bool ManualExecutor::RunOne()
{
    Work work;
    if (!_queue.TryPop(work))
        return false;
    work();
    return true;
}

std::size_t ManualExecutor::Drain()
{
    std::size_t count = 0;
    while (RunOne())
        ++count;
    return count;
}

void ManualExecutor::Run()
{
    // Pump until Stop() is requested and the queue has drained. The stop request is the wake
    // condition; the queue's WaitAndPop still hands back any items already queued before returning
    // false.
    Work work;
    while (_queue.WaitAndPop(work, [this] { return _stopSource.stop_requested(); }))
        work();
}

void ManualExecutor::Stop()
{
    // Publish the stop request before waking, so a pumper blocked in Run() observes it
    // (WorkQueue::Wake takes the lock first to avoid a lost wakeup). std::stop_source's request_stop()
    // establishes the necessary happens-before with stop_requested() observed under the queue lock.
    _stopSource.request_stop();
    _queue.Wake();
}

std::size_t ManualExecutor::PendingCount() const
{
    return _queue.Size();
}

} // namespace Lightweight::Async
