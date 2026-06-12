// SPDX-License-Identifier: Apache-2.0

#include "ThreadPoolExecutor.hpp"

#include <ranges>
#include <utility>

namespace Lightweight::Async
{

ThreadPoolExecutor::ThreadPoolExecutor(std::size_t threadCount)
{
    _threads.reserve(threadCount);
    for ([[maybe_unused]] auto const index: std::views::iota(std::size_t { 0 }, threadCount))
    {
        _threads.emplace_back([this] {
            Work work;
            while (_queue.WaitAndPop(work))
                work();
        });
    }
}

ThreadPoolExecutor::~ThreadPoolExecutor()
{
    _queue.MarkFinished();
    for (auto& thread: _threads)
        if (thread.joinable())
            thread.join();
}

void ThreadPoolExecutor::Post(Work work)
{
    _queue.Push(std::move(work));
}

void ThreadPoolExecutor::Resume(std::coroutine_handle<> handle)
{
    _queue.Push([handle] { handle.resume(); });
}

} // namespace Lightweight::Async
