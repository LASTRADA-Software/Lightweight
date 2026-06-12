// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "CancellationToken.hpp"
#include "Executor.hpp"
#include "Task.hpp"

#include <exception>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

namespace Lightweight::Async
{

namespace detail
{

    template <typename F>
    using OffloadResult = std::invoke_result_t<F&>;

    /// Awaitable that runs a blocking callable on an executor and resumes elsewhere.
    ///
    /// Lives in the awaiting coroutine's frame for the duration of the suspension, so the
    /// worker thread may safely write the result/exception into it before resuming.
    template <typename F>
    class OffloadAwaitable
    {
      public:
        using Result = OffloadResult<F>;

        OffloadAwaitable(IExecutor& offload, IResumeScheduler& resume, F fn, CancellationToken token):
            _offload { offload },
            _resume { resume },
            _fn { std::move(fn) },
            _token { std::move(token) }
        {
        }

        OffloadAwaitable(OffloadAwaitable&&) = default;
        OffloadAwaitable(OffloadAwaitable const&) = delete;
        OffloadAwaitable& operator=(OffloadAwaitable const&) = delete;
        OffloadAwaitable& operator=(OffloadAwaitable&&) = delete;
        ~OffloadAwaitable() = default;

        [[nodiscard]] bool await_ready() const noexcept
        {
            return false;
        }

        void await_suspend(std::coroutine_handle<> awaiting)
        {
            _offload.Post([this, awaiting]() mutable {
                Run();
                _resume.Resume(awaiting);
            });
        }

        Result await_resume()
        {
            if (_error)
                std::rethrow_exception(_error);
            if constexpr (!std::is_void_v<Result>)
                return std::move(*_value);
        }

      private:
        void Run() noexcept
        {
            try
            {
                if (_token.IsCancellationRequested())
                    throw OperationCancelledError {};
                if constexpr (std::is_void_v<Result>)
                    _fn();
                else
                    _value.emplace(_fn());
            }
            catch (...)
            {
                _error = std::current_exception();
            }
        }

        IExecutor& _offload;
        IResumeScheduler& _resume;
        F _fn;
        CancellationToken _token;
        std::conditional_t<std::is_void_v<Result>, std::monostate, std::optional<Result>> _value {};
        std::exception_ptr _error {};
    };

    /// Drives a by-value offload awaitable to completion.
    ///
    /// Taking the awaitable @b by value (rather than the executors by reference) keeps this
    /// coroutine free of reference parameters; the executor references live inside the
    /// awaitable, which is stored in this coroutine's frame for the duration of the await.
    template <typename Awaitable>
    Task<typename Awaitable::Result> RunOffloadTask(Awaitable awaitable)
    {
        if constexpr (std::is_void_v<typename Awaitable::Result>)
            co_await awaitable;
        else
            co_return co_await awaitable;
    }

} // namespace detail

/// Offloads a blocking callable to an executor and resumes the awaiting coroutine elsewhere.
///
/// @p fn runs on @p offload (typically a connection strand over the DB worker pool). When it
/// finishes, the awaiting coroutine is resumed via @p resume (typically the app's run loop),
/// so coroutine logic continues on the app thread while only the blocking call ran on a
/// worker. Exceptions thrown by @p fn are captured and rethrown on resume (parity with the
/// synchronous, throwing API). If cancellation is already requested when the work is about to
/// run, the operation completes with @ref OperationCancelledError.
///
/// @tparam F A callable invocable with no arguments.
/// @param offload The executor to run @p fn on.
/// @param resume The scheduler used to resume the awaiting coroutine.
/// @param fn The blocking callable (consumed).
/// @param token Optional cancellation token.
/// @return A Task producing @p fn's result.
template <typename F>
[[nodiscard]] Task<detail::OffloadResult<F>> Async(IExecutor& offload,
                                                   IResumeScheduler& resume,
                                                   F fn,
                                                   CancellationToken token = {})
{
    return detail::RunOffloadTask(detail::OffloadAwaitable<F> { offload, resume, std::move(fn), std::move(token) });
}

} // namespace Lightweight::Async
