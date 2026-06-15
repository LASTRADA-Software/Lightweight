// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "../Api.hpp"
#include "Executor.hpp"

#include <cstddef>
#include <memory>

namespace Lightweight::Async
{

/// A fixed-size pool of worker threads that run posted work concurrently.
///
/// This is the default "DB worker" offload target: blocking ODBC calls are posted here and
/// executed on a worker thread while the awaiting coroutine is suspended. The pool is built on
/// stdexec's @c exec::static_thread_pool (the C++26 @c std::execution scheduler model); each posted
/// @ref Work item is spawned as a @c schedule|then sender into an @c exec::async_scope so the
/// destructor can wait for every in-flight item to finish, draining and joining. The pool must
/// therefore outlive every coroutine that can resume on it.
///
/// The stdexec machinery lives in a pimpl defined in the translation unit, so this public header
/// pulls in no stdexec headers — keeping them out of the C++20 module's global module fragment and
/// off every downstream consumer that does not opt in.
class LIGHTWEIGHT_API ThreadPoolExecutor final: public IExecutor, public IResumeScheduler
{
  public:
    /// Constructs the pool and starts @p threadCount worker threads.
    ///
    /// @param threadCount Number of worker threads to start (must be >= 1).
    /// @throws std::invalid_argument if @p threadCount is 0.
    explicit ThreadPoolExecutor(std::size_t threadCount);

    ThreadPoolExecutor(ThreadPoolExecutor const&) = delete;
    ThreadPoolExecutor& operator=(ThreadPoolExecutor const&) = delete;
    ThreadPoolExecutor(ThreadPoolExecutor&&) = delete;
    ThreadPoolExecutor& operator=(ThreadPoolExecutor&&) = delete;

    /// Waits for all in-flight work to drain, then stops and joins the worker threads.
    ~ThreadPoolExecutor() override;

    void Post(Work work) override;
    void Resume(std::coroutine_handle<> handle) override;

    /// @return the number of worker threads.
    [[nodiscard]] std::size_t ThreadCount() const noexcept
    {
        return _threadCount;
    }

  private:
    /// Holds the stdexec @c static_thread_pool and @c async_scope; defined in the .cpp so the
    /// stdexec headers never reach this public header (nor the module's global module fragment).
    struct Impl;

    std::size_t _threadCount;    ///< Configured worker count (exposed via ThreadCount()).
    std::unique_ptr<Impl> _impl; ///< stdexec pool + scope; destroyed (and drained) in ~ThreadPoolExecutor.
};

} // namespace Lightweight::Async
