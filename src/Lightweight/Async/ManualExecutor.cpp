// SPDX-License-Identifier: Apache-2.0

#include "ManualExecutor.hpp"

#include <utility>

namespace Lightweight::Async
{

void ManualExecutor::Post(Work work)
{
    {
        std::scoped_lock const lock(_mutex);
        _queue.push_back(std::move(work));
    }
    _condition.notify_one();
}

void ManualExecutor::Resume(std::coroutine_handle<> handle)
{
    Post([handle] { handle.resume(); });
}

bool ManualExecutor::RunOne()
{
    Work work;
    {
        std::scoped_lock const lock(_mutex);
        if (_queue.empty())
            return false;
        work = std::move(_queue.front());
        _queue.pop_front();
    }
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
    while (true)
    {
        Work work;
        {
            std::unique_lock lock(_mutex);
            _condition.wait(lock, [this] { return !_queue.empty() || _stopped; });
            if (_queue.empty())
                return;
            work = std::move(_queue.front());
            _queue.pop_front();
        }
        work();
    }
}

void ManualExecutor::Stop()
{
    {
        std::scoped_lock const lock(_mutex);
        _stopped = true;
    }
    _condition.notify_all();
}

std::size_t ManualExecutor::PendingCount() const
{
    std::scoped_lock const lock(_mutex);
    return _queue.size();
}

} // namespace Lightweight::Async
