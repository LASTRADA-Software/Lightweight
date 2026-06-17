// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "Async.hpp"
#include "Executor.hpp"
#include "StrandExecutor.hpp"

#include <stop_token>
#include <utility>

namespace Lightweight::Async
{

/// Per-connection asynchronous execution backend.
///
/// A backend owns (or references) the execution context used to run a connection's blocking
/// ODBC work and to resume the awaiting coroutine. Currently the only implementation is
/// @ref ThreadOffloadBackend (portable; offloads to a worker thread). A native event backend
/// (Windows + SQL Server) is planned behind this same interface.
///
/// The backend is selected once per connection (see @c SqlConnection::EnableAsync) and used by
/// all of that connection's async methods.
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
/// @param token Optional cancellation token (a default-constructed @c std::stop_token is non-cancellable).
/// @return A Task producing @p fn's result.
template <typename F>
[[nodiscard]] Task<detail::OffloadResult<F>> RunAsync(IAsyncBackend& backend, F fn, std::stop_token token = {})
{
    return Async(backend.Strand(), backend.ResumeScheduler(), std::move(fn), std::move(token));
}

} // namespace Lightweight::Async
