// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <coroutine>
#include <functional>

namespace Lightweight::Async
{

/// A move-only unit of deferred work scheduled on an @ref IExecutor.
using Work = std::move_only_function<void()>;

/// Interface for an executor that runs posted work items.
///
/// Executors are injected (dependency injection) and owned by the caller; the async layer
/// only ever holds references to them. Every implementation's @ref Post is thread-safe.
class IExecutor
{
  public:
    IExecutor() = default;
    IExecutor(IExecutor const&) = delete;
    IExecutor& operator=(IExecutor const&) = delete;
    IExecutor(IExecutor&&) = delete;
    IExecutor& operator=(IExecutor&&) = delete;
    virtual ~IExecutor() = default;

    /// Schedules @p work to run on the executor. Thread-safe.
    ///
    /// @param work The work item to enqueue (consumed).
    virtual void Post(Work work) = 0;
};

/// Interface for scheduling the resumption of a suspended coroutine.
///
/// Kept separate from @ref IExecutor::Post so resumption can be expressed as a bare
/// coroutine handle, which lets implementations avoid wrapping every resume in a
/// @ref Work allocation on hot paths.
class IResumeScheduler
{
  public:
    IResumeScheduler() = default;
    IResumeScheduler(IResumeScheduler const&) = delete;
    IResumeScheduler& operator=(IResumeScheduler const&) = delete;
    IResumeScheduler(IResumeScheduler&&) = delete;
    IResumeScheduler& operator=(IResumeScheduler&&) = delete;
    virtual ~IResumeScheduler() = default;

    /// Schedules @p handle to be resumed. Thread-safe.
    ///
    /// @param handle The coroutine to resume.
    virtual void Resume(std::coroutine_handle<> handle) = 0;
};

/// An executor that runs work synchronously on the calling thread.
///
/// Useful for tests and for degenerate single-threaded configurations where no thread
/// hand-off is desired. Note that with synchronous ODBC drivers an @ref InlineExecutor
/// used as the offload target will block the calling thread for the duration of the call.
class InlineExecutor final: public IExecutor, public IResumeScheduler
{
  public:
    void Post(Work work) override
    {
        if (work)
            work();
    }

    void Resume(std::coroutine_handle<> handle) override
    {
        if (handle)
            handle.resume();
    }
};

} // namespace Lightweight::Async
