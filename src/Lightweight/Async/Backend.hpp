// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "Async.hpp"
#include "Executor.hpp"
#include "StrandExecutor.hpp"

#include <utility>

namespace Lightweight::Async
{

/// Per-connection asynchronous execution backend.
///
/// A backend owns (or references) the execution context used to run a connection's blocking
/// ODBC work and to resume the awaiting coroutine. Two implementations exist:
///  - @ref ThreadOffloadBackend — portable; offloads to a worker thread. Always available.
///  - the native event backend (Windows + SQL Server) — selected per-connection when the
///    driver advertises async-notification support.
///
/// The backend is selected once per connection (see @c MakeAsyncBackend) and used by all of
/// that connection's async methods.
class IAsyncBackend
{
  public:
    IAsyncBackend() = default;
    IAsyncBackend(IAsyncBackend const&) = delete;
    IAsyncBackend& operator=(IAsyncBackend const&) = delete;
    IAsyncBackend(IAsyncBackend&&) = delete;
    IAsyncBackend& operator=(IAsyncBackend&&) = delete;
    virtual ~IAsyncBackend() = default;

    /// The serializing executor for this connection; blocking work is offloaded here so the
    /// connection's ODBC handle is only ever touched by one thread at a time.
    [[nodiscard]] virtual StrandExecutor& Strand() noexcept = 0;

    /// The scheduler used to resume coroutines after a blocking step completes (typically the
    /// application's run loop).
    [[nodiscard]] virtual IResumeScheduler& ResumeScheduler() noexcept = 0;

    /// @return true if this backend uses native driver async I/O instead of thread offload.
    [[nodiscard]] virtual bool IsNative() const noexcept = 0;
};

/// Runs a whole synchronous operation on @p backend's strand, resuming on its scheduler.
///
/// This is the coarse-grained workhorse used by the high-level async methods: the entire
/// synchronous body (parameter binding, the ODBC call, and post-processing) runs as one
/// closure on the connection's strand — never split across threads — and the coroutine
/// resumes on the app thread with the result.
///
/// @tparam F A callable invocable with no arguments.
/// @param backend The connection's async backend.
/// @param fn The synchronous operation to run (consumed).
/// @param token Optional cancellation token.
/// @return A Task producing @p fn's result.
template <typename F>
[[nodiscard]] Task<detail::OffloadResult<F>> RunAsync(IAsyncBackend& backend, F fn, CancellationToken token = {})
{
    return Async(backend.Strand(), backend.ResumeScheduler(), std::move(fn), std::move(token));
}

} // namespace Lightweight::Async
