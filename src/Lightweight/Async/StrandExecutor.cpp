// SPDX-License-Identifier: Apache-2.0

#include "StrandExecutor.hpp"

#include <utility>

namespace Lightweight::Async
{

void StrandExecutor::Post(Work work)
{
    bool scheduleDrain = false;
    {
        std::scoped_lock const lock(_mutex);
        _pending.push_back(std::move(work));
        if (!_running)
        {
            _running = true;
            scheduleDrain = true;
        }
    }
    if (scheduleDrain)
        ScheduleDrain();
}

void StrandExecutor::Resume(std::coroutine_handle<> handle)
{
    Post([handle] { handle.resume(); });
}

void StrandExecutor::ScheduleDrain()
{
    _underlying.Post([this] {
        while (true)
        {
            Work work;
            {
                std::scoped_lock const lock(_mutex);
                if (_pending.empty())
                {
                    _running = false;
                    return;
                }
                work = std::move(_pending.front());
                _pending.pop_front();
            }
            work();
        }
    });
}

} // namespace Lightweight::Async
