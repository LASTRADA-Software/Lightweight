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
    // Enqueue and, if no drain is currently active, claim it and schedule one. The claim is made
    // under the queue's lock (atomically with the push), closing the push-vs-end-drain race.
    if (_state->queue.PushAndClaimDrain(std::move(work)))
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
        Work work;
        while (state->queue.PopOrEndDrain(work))
            work();
    });
}

} // namespace Lightweight::Async
