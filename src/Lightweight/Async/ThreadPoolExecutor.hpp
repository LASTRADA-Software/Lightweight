// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "../Api.hpp"
#include "../ThreadSafeQueue.hpp"
#include "Executor.hpp"

#include <cstddef>
#include <thread>
#include <vector>

namespace Lightweight::Async
{

/// A fixed-size pool of worker threads that run posted work concurrently.
///
/// This is the default "DB worker" offload target: blocking ODBC calls are posted here and
/// executed on a worker thread while the awaiting coroutine is suspended. The destructor
/// drains and joins all workers, providing a natural teardown barrier — the pool must
/// therefore outlive every coroutine that can resume on it.
class LIGHTWEIGHT_API ThreadPoolExecutor final: public IExecutor, public IResumeScheduler
{
  public:
    /// Constructs the pool and starts @p threadCount worker threads.
    ///
    /// @param threadCount Number of worker threads to start (must be >= 1).
    explicit ThreadPoolExecutor(std::size_t threadCount);

    ThreadPoolExecutor(ThreadPoolExecutor const&) = delete;
    ThreadPoolExecutor& operator=(ThreadPoolExecutor const&) = delete;
    ThreadPoolExecutor(ThreadPoolExecutor&&) = delete;
    ThreadPoolExecutor& operator=(ThreadPoolExecutor&&) = delete;

    /// Signals all workers to finish and joins them.
    ~ThreadPoolExecutor() override;

    void Post(Work work) override;
    void Resume(std::coroutine_handle<> handle) override;

    /// @return the number of worker threads.
    [[nodiscard]] std::size_t ThreadCount() const noexcept
    {
        return _threads.size();
    }

  private:
    ThreadSafeQueue<Work> _queue;
    std::vector<std::thread> _threads;
};

} // namespace Lightweight::Async
