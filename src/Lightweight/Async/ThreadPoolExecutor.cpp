// SPDX-License-Identifier: Apache-2.0

#include "ThreadPoolExecutor.hpp"

#include <cstdint>
#include <cstdio>
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
        // std::cmp_greater compares safely across the signed/width boundary without a cast, and is not
        // a constant comparison the compiler can flag as tautological where size_t == uint32_t (32-bit).
        if (std::cmp_greater(threadCount, std::numeric_limits<std::uint32_t>::max()))
            throw std::invalid_argument { "ThreadPoolExecutor: threadCount exceeds the supported maximum." };
        return static_cast<std::uint32_t>(threadCount);
    }

    /// Spawns `schedule(pool) | then(fn)` into @p scope. @p fn must be @c noexcept so the resulting
    /// sender never completes with set_error (an @c async_scope::spawn requirement); an exception
    /// escaping @p fn therefore terminates, exactly as it did when it escaped the old worker loop.
    /// @param scope The async scope that tracks the spawned work for the destructor drain.
    /// @param pool The thread pool whose scheduler runs the work.
    /// @param fn The noexcept callable to run on a pool worker.
    template <typename Fn>
    void SpawnOn(exec::async_scope& scope, exec::static_thread_pool& pool, Fn fn)
    {
        scope.spawn(stdexec::schedule(pool.get_scheduler()) | stdexec::then(std::move(fn)));
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
    //
    // A destructor is implicitly noexcept, and sync_wait can throw (e.g. bad_alloc constructing its
    // wait state). Swallow it: there is nothing safe to do from a destructor, and letting it escape
    // would call std::terminate. The residual risk is that on such a failure the scope may still be
    // non-empty when ~Impl runs; this is an OOM-only corner that we cannot do better about here.
    try
    {
        stdexec::sync_wait(_impl->scope.on_empty());
    }
    catch (...)
    {
        // A destructor must not throw. The only realistic cause is OOM allocating sync_wait's wait
        // state; we cannot drain further, so emit a best-effort diagnostic and continue teardown
        // rather than letting the exception escape and call std::terminate.
        std::fputs("ThreadPoolExecutor: failed to drain in-flight work during destruction.\n", stderr);
    }
}

void ThreadPoolExecutor::Post(Work work)
{
    SpawnOn(_impl->scope, _impl->pool, [work = std::move(work)]() mutable noexcept {
        if (work)
            work();
    });
}

void ThreadPoolExecutor::Resume(std::coroutine_handle<> handle)
{
    // Spawn the resume directly rather than wrapping it in a Work and routing through Post(): this
    // honors IResumeScheduler's contract of avoiding a std::function allocation (and the extra
    // indirection) on the coroutine-resume path.
    SpawnOn(_impl->scope, _impl->pool, [handle]() noexcept {
        if (handle)
            handle.resume();
    });
}

} // namespace Lightweight::Async
