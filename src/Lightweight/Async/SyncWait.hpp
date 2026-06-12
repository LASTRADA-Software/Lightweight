// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "Task.hpp"

#include <atomic>
#include <semaphore>
#include <type_traits>
#include <variant>

namespace Lightweight::Async
{

namespace detail
{

    /// One-shot completion signal used to bridge a coroutine back to a blocking caller.
    ///
    /// Supports both a blocking wait (@ref Wait, used by @ref SyncWait) and a pollable
    /// flag (@ref IsSet, used by @ref SyncWaitPumping which drives an executor instead of
    /// blocking).
    class SyncWaitEvent
    {
      public:
        void Set() noexcept
        {
            _set.store(true, std::memory_order_release);
            _semaphore.release();
        }

        void Wait() noexcept
        {
            _semaphore.acquire();
        }

        [[nodiscard]] bool IsSet() const noexcept
        {
            return _set.load(std::memory_order_acquire);
        }

      private:
        std::atomic<bool> _set { false };
        std::binary_semaphore _semaphore { 0 };
    };

    template <typename T>
    class SyncWaitTask;

    /// Promise for the internal driver coroutine used by SyncWait.
    ///
    /// The completion signal is raised from the @c final_suspend awaiter (i.e. only once
    /// the driver has fully suspended), so the blocking caller can safely destroy the
    /// coroutine frame without racing the resuming thread.
    template <typename T>
    class SyncWaitPromise
    {
      public:
        SyncWaitTask<T> get_return_object() noexcept;

        std::suspend_always initial_suspend() noexcept
        {
            return {};
        }

        auto final_suspend() noexcept
        {
            struct FinalAwaiter
            {
                [[nodiscard]] bool await_ready() const noexcept
                {
                    return false;
                }
                void await_suspend(std::coroutine_handle<SyncWaitPromise> coro) const noexcept
                {
                    coro.promise().Signal();
                }
                void await_resume() const noexcept {}
            };
            return FinalAwaiter {};
        }

        void unhandled_exception() noexcept
        {
            _result.template emplace<2>(std::current_exception());
        }

        void return_value(T&& value) noexcept(std::is_nothrow_move_constructible_v<T>)
        {
            _result.template emplace<1>(std::move(value));
        }

        void return_value(T const& value)
        {
            _result.template emplace<1>(value);
        }

        [[nodiscard]] T Take()
        {
            if (_result.index() == 2)
                std::rethrow_exception(std::get<2>(_result));
            return std::move(std::get<1>(_result));
        }

        void Bind(SyncWaitEvent& signal) noexcept
        {
            _event = &signal;
        }

        void Signal() const noexcept
        {
            _event->Set();
        }

      private:
        SyncWaitEvent* _event = nullptr;
        std::variant<std::monostate, T, std::exception_ptr> _result;
    };

    template <>
    class SyncWaitPromise<void>
    {
      public:
        SyncWaitTask<void> get_return_object() noexcept;

        std::suspend_always initial_suspend() noexcept
        {
            return {};
        }

        auto final_suspend() noexcept
        {
            struct FinalAwaiter
            {
                [[nodiscard]] bool await_ready() const noexcept
                {
                    return false;
                }
                void await_suspend(std::coroutine_handle<SyncWaitPromise> coro) const noexcept
                {
                    coro.promise().Signal();
                }
                void await_resume() const noexcept {}
            };
            return FinalAwaiter {};
        }

        void unhandled_exception() noexcept
        {
            _exception = std::current_exception();
        }

        void return_void() noexcept {}

        void Take() const
        {
            if (_exception)
                std::rethrow_exception(_exception);
        }

        void Bind(SyncWaitEvent& signal) noexcept
        {
            _event = &signal;
        }

        void Signal() const noexcept
        {
            _event->Set();
        }

      private:
        SyncWaitEvent* _event = nullptr;
        std::exception_ptr _exception {};
    };

    /// Internal RAII owner of the SyncWait driver coroutine.
    template <typename T>
    class SyncWaitTask
    {
      public:
        using promise_type = SyncWaitPromise<T>;
        using Handle = std::coroutine_handle<promise_type>;

        explicit SyncWaitTask(Handle handle) noexcept:
            _handle { handle }
        {
        }

        SyncWaitTask(SyncWaitTask&& other) noexcept:
            _handle { std::exchange(other._handle, {}) }
        {
        }

        SyncWaitTask(SyncWaitTask const&) = delete;
        SyncWaitTask& operator=(SyncWaitTask const&) = delete;
        SyncWaitTask& operator=(SyncWaitTask&&) = delete;

        ~SyncWaitTask()
        {
            if (_handle)
                _handle.destroy();
        }

        void Start(SyncWaitEvent& signal)
        {
            _handle.promise().Bind(signal);
            _handle.resume();
        }

        [[nodiscard]] promise_type& Promise() noexcept
        {
            return _handle.promise();
        }

      private:
        Handle _handle;
    };

    template <typename T>
    SyncWaitTask<T> SyncWaitPromise<T>::get_return_object() noexcept
    {
        return SyncWaitTask<T> { std::coroutine_handle<SyncWaitPromise<T>>::from_promise(*this) };
    }

    inline SyncWaitTask<void> SyncWaitPromise<void>::get_return_object() noexcept
    {
        return SyncWaitTask<void> { std::coroutine_handle<SyncWaitPromise<void>>::from_promise(*this) };
    }

    template <typename T>
    SyncWaitTask<T> MakeSyncWaitTask(Task<T> task)
    {
        if constexpr (std::is_void_v<T>)
            co_await std::move(task);
        else
            co_return co_await std::move(task);
    }

} // namespace detail

/// Blocks the calling thread until @p task completes and returns its result (or rethrows).
///
/// Use this from non-coroutine code (e.g. @c main, tests) when the task resumes on a
/// thread-backed scheduler. If the task resumes on a @ref ManualExecutor that only the
/// calling thread pumps, use @ref SyncWaitPumping instead to avoid a deadlock.
///
/// @tparam T The task's result type.
/// @param task The task to drive to completion (consumed).
/// @return The task's result, or @c void.
template <typename T>
T SyncWait(Task<T> task)
{
    detail::SyncWaitEvent signal;
    auto driver = detail::MakeSyncWaitTask<T>(std::move(task));
    driver.Start(signal);
    signal.Wait();
    return driver.Promise().Take();
}

/// Drives @p task to completion by pumping @p executor on the calling thread.
///
/// This is the single-threaded counterpart to @ref SyncWait: rather than blocking, it runs
/// the executor's queued work (including the task's resumption) until the task finishes.
/// Intended for an app/event-loop thread and for deterministic tests using a
/// @ref ManualExecutor.
///
/// @tparam T The task's result type.
/// @tparam Executor An executor exposing @c RunUntil(predicate).
/// @param task The task to drive to completion (consumed).
/// @param executor The executor to pump (typically a @ref ManualExecutor).
/// @return The task's result, or @c void.
template <typename T, typename Executor>
T SyncWaitPumping(Task<T> task, Executor& executor)
{
    detail::SyncWaitEvent signal;
    auto driver = detail::MakeSyncWaitTask<T>(std::move(task));
    driver.Start(signal);
    executor.RunUntil([&signal] { return signal.IsSet(); });
    return driver.Promise().Take();
}

} // namespace Lightweight::Async
