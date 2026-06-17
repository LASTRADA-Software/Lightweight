// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <coroutine>
#include <exception>
#include <utility>
#include <variant>

namespace Lightweight::Async
{

template <typename T = void>
class Task;

namespace detail
{

    /// Awaiter used at a Task's final suspension point.
    ///
    /// On completion it performs a symmetric transfer back to the awaiting coroutine
    /// (the continuation), or to @c std::noop_coroutine() when the Task was launched
    /// without a continuation (e.g. a detached/root task).
    struct TaskFinalAwaiter
    {
        [[nodiscard]] bool await_ready() const noexcept
        {
            return false;
        }

        template <typename Promise>
        [[nodiscard]] std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> coro) const noexcept
        {
            return coro.promise().Continuation();
        }

        void await_resume() const noexcept {}
    };

    /// Shared promise machinery for @ref Task independent of the result type.
    class TaskPromiseBase
    {
      public:
        /// Tasks are lazy: the body does not run until the Task is awaited (or driven by SyncWait).
        [[nodiscard]] std::suspend_always initial_suspend() noexcept
        {
            return {};
        }

        [[nodiscard]] TaskFinalAwaiter final_suspend() noexcept
        {
            return {};
        }

        /// Records the coroutine to resume once this Task completes.
        void SetContinuation(std::coroutine_handle<> continuation) noexcept
        {
            _continuation = continuation;
        }

        /// Returns the continuation to symmetric-transfer to, or a no-op handle if none.
        [[nodiscard]] std::coroutine_handle<> Continuation() const noexcept
        {
            return _continuation ? _continuation : std::noop_coroutine();
        }

      private:
        std::coroutine_handle<> _continuation {};
    };

    /// Stores the outcome of a coroutine promise — either a produced value of type @c T or a captured
    /// exception — and hands it back exactly once.
    ///
    /// Shared by @ref TaskPromise and @c SyncWaitPromise so the value/exception plumbing (the variant,
    /// the move-vs-copy @c return_value overloads, and the rethrow-or-move @ref Take) lives in one
    /// place rather than being duplicated across each promise type and its @c void specialization.
    ///
    /// @tparam T The value type produced by the coroutine (use the @c void specialization for none).
    template <typename T>
    class CoroutineResult
    {
      public:
        /// Captures the in-flight exception (call from @c promise.unhandled_exception()).
        void SetException() noexcept
        {
            _result.template emplace<2>(std::current_exception());
        }

        /// Stores the produced value (call from @c promise.return_value()).
        void SetValue(T const& value)
        {
            _result.template emplace<1>(value);
        }

        /// Stores the produced value (call from @c promise.return_value()).
        void SetValue(T&& value) noexcept(std::is_nothrow_move_constructible_v<T>)
        {
            _result.template emplace<1>(std::move(value));
        }

        /// Moves out the produced value, or rethrows the captured exception.
        [[nodiscard]] T Take()
        {
            if (_result.index() == 2)
                std::rethrow_exception(std::get<2>(_result));
            return std::move(std::get<1>(_result));
        }

      private:
        std::variant<std::monostate, T, std::exception_ptr> _result;
    };

    /// @c void specialization of @ref CoroutineResult: stores only a possible exception.
    template <>
    class CoroutineResult<void>
    {
      public:
        /// Captures the in-flight exception (call from @c promise.unhandled_exception()).
        void SetException() noexcept
        {
            _exception = std::current_exception();
        }

        /// Rethrows the captured exception, if any.
        void Take() const
        {
            if (_exception)
                std::rethrow_exception(_exception);
        }

      private:
        std::exception_ptr _exception {};
    };

    /// Promise type for @c Task<T> with a non-void result.
    ///
    /// @tparam T The value type produced by the coroutine.
    template <typename T>
    class TaskPromise final: public TaskPromiseBase
    {
      public:
        Task<T> get_return_object() noexcept;

        void unhandled_exception() noexcept
        {
            _result.SetException();
        }

        void return_value(T const& value)
        {
            _result.SetValue(value);
        }

        void return_value(T&& value) noexcept(std::is_nothrow_move_constructible_v<T>)
        {
            _result.SetValue(std::move(value));
        }

        /// Moves out the produced value, or rethrows the captured exception.
        [[nodiscard]] T Take()
        {
            return _result.Take();
        }

      private:
        CoroutineResult<T> _result;
    };

    /// Promise specialization for @c Task<void>.
    template <>
    class TaskPromise<void> final: public TaskPromiseBase
    {
      public:
        Task<void> get_return_object() noexcept;

        void unhandled_exception() noexcept
        {
            _result.SetException();
        }

        void return_void() noexcept {}

        /// Rethrows the captured exception, if any.
        void Take() const
        {
            _result.Take();
        }

      private:
        CoroutineResult<void> _result;
    };

} // namespace detail

/// A lazy, move-only C++23 coroutine task.
///
/// A @c Task<T> represents an asynchronous computation that yields a value of type @c T
/// (or nothing for @c Task<void>). It is @b lazy — the coroutine body does not start
/// executing until the Task is @c co_await-ed by another coroutine or driven to completion
/// by @c SyncWait / @c SyncWaitPumping. Awaiting uses symmetric transfer, so chains of
/// awaits do not grow the stack. Exceptions thrown inside the body are captured and
/// rethrown at the awaiting site (parity with the throwing synchronous API).
///
/// @tparam T The value type produced by the task (defaults to @c void).
template <typename T>
class [[nodiscard]] Task
{
  public:
    /// The coroutine promise type required by the C++ coroutine machinery.
    using promise_type = detail::TaskPromise<T>;

    /// The typed coroutine handle owned by this Task.
    using Handle = std::coroutine_handle<promise_type>;

    /// Constructs an empty Task that owns no coroutine.
    Task() noexcept = default;

    /// Adopts ownership of the coroutine identified by @p handle (used by the promise).
    /// @param handle The coroutine handle to take ownership of.
    explicit Task(Handle handle) noexcept:
        _handle { handle }
    {
    }

    /// Move-constructs from @p other, leaving it empty.
    /// @param other The Task to move from.
    Task(Task&& other) noexcept:
        _handle { std::exchange(other._handle, {}) }
    {
    }

    /// Move-assigns from @p other, destroying any currently-owned coroutine first.
    /// @param other The Task to move from.
    /// @return A reference to this Task.
    Task& operator=(Task&& other) noexcept
    {
        if (this != &other)
        {
            if (_handle)
                _handle.destroy();
            _handle = std::exchange(other._handle, {});
        }
        return *this;
    }

    Task(Task const&) = delete;
    Task& operator=(Task const&) = delete;

    ~Task()
    {
        if (_handle)
            _handle.destroy();
    }

    /// @return true if this Task owns a coroutine frame.
    [[nodiscard]] bool IsValid() const noexcept
    {
        return static_cast<bool>(_handle);
    }

    /// @return true if the Task is empty or has run to completion.
    [[nodiscard]] bool IsReady() const noexcept
    {
        return !_handle || _handle.done();
    }

    /// @return the underlying coroutine handle (used by SyncWait and executors).
    [[nodiscard]] Handle GetHandle() const noexcept
    {
        return _handle;
    }

    /// Awaits this Task: suspends the awaiting coroutine, runs this Task, and yields its result
    /// (or rethrows its exception) once it completes.
    /// @return An awaiter for this Task.
    auto operator co_await() && noexcept
    {
        struct Awaiter
        {
            Handle coro;

            [[nodiscard]] bool await_ready() const noexcept
            {
                return !coro || coro.done();
            }

            [[nodiscard]] std::coroutine_handle<> await_suspend(std::coroutine_handle<> awaiting) noexcept
            {
                coro.promise().SetContinuation(awaiting);
                return coro;
            }

            decltype(auto) await_resume()
            {
                return coro.promise().Take();
            }
        };
        return Awaiter { _handle };
    }

  private:
    Handle _handle {};
};

namespace detail
{
    template <typename T>
    Task<T> TaskPromise<T>::get_return_object() noexcept
    {
        return Task<T> { std::coroutine_handle<TaskPromise<T>>::from_promise(*this) };
    }

    inline Task<void> TaskPromise<void>::get_return_object() noexcept
    {
        return Task<void> { std::coroutine_handle<TaskPromise<void>>::from_promise(*this) };
    }
} // namespace detail

} // namespace Lightweight::Async
