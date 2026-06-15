// SPDX-License-Identifier: Apache-2.0

#include "ThreadPoolExecutor.hpp"

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>

#include <exec/async_scope.hpp>
#include <exec/static_thread_pool.hpp>
#include <stdexec/execution.hpp>

namespace Lightweight::Async
{

namespace
{
    /// Validates @p threadCount and narrows it to the @c std::uint32_t the stdexec pool expects.
    /// @param threadCount Requested worker count.
    /// @return @p threadCount as a @c std::uint32_t.
    /// @throws std::invalid_argument if @p threadCount is 0 or exceeds @c std::uint32_t.
    std::uint32_t ToWorkerCount(std::size_t threadCount)
    {
        if (threadCount == 0)
            throw std::invalid_argument {
                "ThreadPoolExecutor: threadCount must be >= 1 (a pool with no workers never runs posted work)."
            };
        if (threadCount > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()))
            throw std::invalid_argument { "ThreadPoolExecutor: threadCount exceeds the supported maximum." };
        return static_cast<std::uint32_t>(threadCount);
    }
} // namespace

/// stdexec-backed state for @ref ThreadPoolExecutor. The @c scope is declared after the @c pool so it
/// is destroyed first: ~ThreadPoolExecutor drains it (sync_wait(on_empty())) before the pool's own
/// destructor stops and joins the workers.
struct ThreadPoolExecutor::Impl
{
    exec::static_thread_pool pool;
    exec::async_scope scope;

    explicit Impl(std::uint32_t workerCount):
        pool { workerCount }
    {
    }
};

ThreadPoolExecutor::ThreadPoolExecutor(std::size_t threadCount):
    _threadCount { threadCount },
    _impl { std::make_unique<Impl>(ToWorkerCount(threadCount)) }
{
}

ThreadPoolExecutor::~ThreadPoolExecutor()
{
    // Drain: block until every spawned work item has completed, restoring the "drain then join"
    // teardown barrier the hand-rolled pool used to provide. async_scope additionally requires the
    // scope to be empty before it is destroyed.
    stdexec::sync_wait(_impl->scope.on_empty());
}

void ThreadPoolExecutor::Post(Work work)
{
    // Spawn `schedule(pool) | then(run work)` into the scope. The `then` callable is noexcept so the
    // resulting sender never completes with set_error — async_scope::spawn requires that. An
    // exception escaping a work item therefore terminates, exactly as it did when it escaped the old
    // worker-thread loop.
    _impl->scope.spawn(stdexec::schedule(_impl->pool.get_scheduler())
                       | stdexec::then([work = std::move(work)]() mutable noexcept {
                             if (work)
                                 work();
                         }));
}

void ThreadPoolExecutor::Resume(std::coroutine_handle<> handle)
{
    Post([handle] {
        if (handle)
            handle.resume();
    });
}

} // namespace Lightweight::Async
