// SPDX-License-Identifier: Apache-2.0

#include "StrandExecutor.hpp"

#include <utility>

namespace Lightweight::Async
{

StrandExecutor::StrandExecutor(IExecutor& underlying):
    _state { std::make_shared<State>(underlying) }
{
}

void StrandExecutor::Post(Work work)
{
    bool scheduleDrain = false;
    {
        std::scoped_lock const lock(_state->mutex);
        _state->pending.push_back(std::move(work));
        if (!_state->running)
        {
            _state->running = true;
            scheduleDrain = true;
        }
    }
    if (scheduleDrain)
        ScheduleDrain(_state);
}

void StrandExecutor::Resume(std::coroutine_handle<> handle)
{
    Post([handle] { handle.resume(); });
}

void StrandExecutor::ScheduleDrain(std::shared_ptr<State> state)
{
    // Grab the underlying reference before moving the shared_ptr into the closure. The closure
    // holds its own copy of `state`, so the State (mutex/pending/running) outlives this
    // StrandExecutor wrapper if it is destroyed or replaced while the drain is still running.
    IExecutor& underlying = state->underlying;
    underlying.Post([state = std::move(state)] {
        while (true)
        {
            Work work;
            {
                std::scoped_lock const lock(state->mutex);
                if (state->pending.empty())
                {
                    state->running = false;
                    return;
                }
                work = std::move(state->pending.front());
                state->pending.pop_front();
            }
            work();
        }
    });
}

} // namespace Lightweight::Async
