// SPDX-License-Identifier: Apache-2.0
#pragma once

// Opt-in bridge: adapt a Lightweight Async::Task<T> into an NVIDIA stdexec sender so library
// coroutines can flow into std::execution (P2300) sender/receiver pipelines (then, let_value,
// when_all, sync_wait, ...).
//
// This is the ONLY public Lightweight header that includes the stdexec headers directly. It is
// deliberately NOT pulled in by <Lightweight/Lightweight.hpp> nor by the C++20 module
// (Lightweight.cppm), so the core library stays stdexec-free behind its pimpl. Include this header
// only in translation units that already depend on stdexec, and link STDEXEC::stdexec yourself
// (an installed Lightweight links stdexec PRIVATE and does not re-export it). See docs/async.md.

#include "CancellationToken.hpp"
#include "Task.hpp"

#include <coroutine>
#include <exception>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include <stdexec/execution.hpp>

namespace Lightweight::Async
{

namespace detail
{

    template <typename Receiver, typename T>
    class SenderDriver;

    /// Final-suspension awaiter for the sender driver coroutine: delivers the captured outcome to the
    /// connected receiver once the driver has fully suspended (mirroring @c SyncWaitFinalAwaiter so the
    /// completion is published from a fully-suspended frame, never re-entrantly mid-body).
    struct SenderFinalAwaiter
    {
        [[nodiscard]] bool await_ready() const noexcept
        {
            return false;
        }

        template <typename Promise>
        void await_suspend(std::coroutine_handle<Promise> coro) const noexcept
        {
            coro.promise().Complete();
        }

        void await_resume() const noexcept {}
    };

    /// Promise base for the sender driver coroutine, parameterized on the connected receiver type.
    ///
    /// The driver coroutine (see @c MakeSenderDriver) simply @c co_await s the user's Task; this
    /// promise captures the produced value or exception and, at @c final_suspend, routes it to the
    /// receiver via stdexec's completion channels:
    /// - normal value -> @c stdexec::set_value(receiver, value...)
    /// - @c OperationCancelledError -> @c stdexec::set_stopped(receiver) (parity with the library's
    ///   cooperative cancellation contract, see @ref CancellationToken.hpp)
    /// - any other exception -> @c stdexec::set_error(receiver, std::exception_ptr)
    ///
    /// @tparam Receiver The connected stdexec receiver type.
    template <typename Receiver>
    class SenderPromiseBase
    {
      public:
        std::suspend_always initial_suspend() noexcept
        {
            return {};
        }

        SenderFinalAwaiter final_suspend() noexcept
        {
            return {};
        }

        void unhandled_exception() noexcept
        {
            _error = std::current_exception();
        }

        /// Binds the receiver the captured outcome is delivered to. Called by the operation state
        /// before the driver is started.
        /// @param receiver The connected receiver (stored by pointer; outlives the operation).
        void BindReceiver(Receiver& receiver) noexcept
        {
            _receiver = &receiver;
        }

        /// If the Task completed by throwing, delivers that exception to the receiver's stopped
        /// (cancellation) or error channel and returns @c true; otherwise leaves the receiver untouched
        /// and returns @c false so the derived promise can deliver its value.
        /// @return @c true if an exception was delivered, @c false if the Task produced a value.
        [[nodiscard]] bool TryDeliverError() noexcept
        {
            if (!_error)
                return false;
            try
            {
                std::rethrow_exception(_error);
            }
            catch (OperationCancelledError const&)
            {
                stdexec::set_stopped(std::move(*_receiver));
            }
            catch (...)
            {
                stdexec::set_error(std::move(*_receiver), std::current_exception());
            }
            return true;
        }

        /// @return The bound receiver (valid once @ref BindReceiver has been called).
        [[nodiscard]] Receiver& BoundReceiver() noexcept
        {
            return *_receiver;
        }

      private:
        Receiver* _receiver = nullptr;
        std::exception_ptr _error {};
    };

    /// Sender driver promise for a non-void Task result.
    ///
    /// Value/exception storage is delegated to @ref CoroutineResult (the same primitive @c TaskPromise
    /// uses), so the value channel is fed from @c CoroutineResult::Take rather than a bare
    /// @c std::optional access — keeping the delivery path free of unchecked-optional reads.
    template <typename Receiver, typename T>
    class SenderPromise final: public SenderPromiseBase<Receiver>
    {
      public:
        SenderDriver<Receiver, T> get_return_object() noexcept;

        void return_value(T&& value) noexcept(std::is_nothrow_move_constructible_v<T>)
        {
            _result.SetValue(std::move(value));
        }

        void return_value(T const& value)
        {
            _result.SetValue(value);
        }

        /// Delivers the captured outcome to the receiver (called from @c final_suspend).
        void Complete() noexcept
        {
            if (!this->TryDeliverError())
                stdexec::set_value(std::move(this->BoundReceiver()), _result.Take());
        }

      private:
        CoroutineResult<T> _result;
    };

    /// Sender driver promise for a @c Task<void> result.
    template <typename Receiver>
    class SenderPromise<Receiver, void> final: public SenderPromiseBase<Receiver>
    {
      public:
        SenderDriver<Receiver, void> get_return_object() noexcept;

        void return_void() noexcept {}

        /// Delivers the captured outcome to the receiver (called from @c final_suspend).
        void Complete() noexcept
        {
            if (!this->TryDeliverError())
                stdexec::set_value(std::move(this->BoundReceiver()));
        }
    };

    /// RAII owner of the sender driver coroutine. Lives inside the stdexec operation state.
    template <typename Receiver, typename T>
    class SenderDriver
    {
      public:
        using promise_type = SenderPromise<Receiver, T>;
        using Handle = std::coroutine_handle<promise_type>;

        explicit SenderDriver(Handle handle) noexcept:
            _handle { handle }
        {
        }

        SenderDriver(SenderDriver&& other) noexcept:
            _handle { std::exchange(other._handle, {}) }
        {
        }

        SenderDriver(SenderDriver const&) = delete;
        SenderDriver& operator=(SenderDriver const&) = delete;
        SenderDriver& operator=(SenderDriver&&) = delete;

        ~SenderDriver()
        {
            if (_handle)
                _handle.destroy();
        }

        /// Binds the receiver and resumes the driver, kicking off the Task. Lazy until called.
        void Start(Receiver& receiver)
        {
            _handle.promise().BindReceiver(receiver);
            _handle.resume();
        }

      private:
        Handle _handle;
    };

    template <typename Receiver, typename T>
    SenderDriver<Receiver, T> SenderPromise<Receiver, T>::get_return_object() noexcept
    {
        return SenderDriver<Receiver, T> { std::coroutine_handle<SenderPromise<Receiver, T>>::from_promise(*this) };
    }

    template <typename Receiver>
    SenderDriver<Receiver, void> SenderPromise<Receiver, void>::get_return_object() noexcept
    {
        return SenderDriver<Receiver, void> { std::coroutine_handle<SenderPromise<Receiver, void>>::from_promise(*this) };
    }

    /// The driver coroutine: awaits the user's Task; its promise captures the outcome and delivers it
    /// to the receiver at @c final_suspend.
    template <typename Receiver, typename T>
    SenderDriver<Receiver, T> MakeSenderDriver(Task<T> task)
    {
        if constexpr (std::is_void_v<T>)
            co_await std::move(task);
        else
            co_return co_await std::move(task);
    }

    /// stdexec operation state owning the driver coroutine and the receiver. Created by @c connect.
    template <typename Receiver, typename T>
    class TaskOperationState
    {
      public:
        TaskOperationState(Task<T> task, Receiver receiver):
            _driver { MakeSenderDriver<Receiver, T>(std::move(task)) },
            _receiver { std::move(receiver) }
        {
        }

        TaskOperationState(TaskOperationState const&) = delete;
        TaskOperationState& operator=(TaskOperationState const&) = delete;
        TaskOperationState(TaskOperationState&&) = delete;
        TaskOperationState& operator=(TaskOperationState&&) = delete;
        ~TaskOperationState() = default;

        /// stdexec start customization: kicks off the Task. The operation state (and therefore the
        /// driver frame and receiver) is guaranteed by stdexec to outlive the asynchronous operation.
        void start() & noexcept
        {
            _driver.Start(_receiver);
        }

      private:
        SenderDriver<Receiver, T> _driver;
        Receiver _receiver;
    };

    /// Completion signatures for a @ref TaskSender of value type @c T: a value channel carrying @c T,
    /// an error channel carrying @c std::exception_ptr, and a stopped channel for cancelled tasks.
    /// Specialized for @c void because @c set_value_t(void) is ill-formed (a function type may not
    /// have a @c void parameter); the void value channel carries no argument.
    template <typename T>
    struct TaskCompletionSignatures
    {
        using type = stdexec::completion_signatures<stdexec::set_value_t(T),
                                                    stdexec::set_error_t(std::exception_ptr),
                                                    stdexec::set_stopped_t()>;
    };

    template <>
    struct TaskCompletionSignatures<void>
    {
        using type = stdexec::completion_signatures<stdexec::set_value_t(),
                                                    stdexec::set_error_t(std::exception_ptr),
                                                    stdexec::set_stopped_t()>;
    };

    /// stdexec sender wrapping a lazy @ref Task. @c connect builds a @ref TaskOperationState.
    template <typename T>
    class TaskSender
    {
      public:
        using sender_concept = stdexec::sender_t;

        using completion_signatures = TaskCompletionSignatures<T>::type;

        explicit TaskSender(Task<T> task) noexcept:
            _task { std::move(task) }
        {
        }

        /// stdexec connect customization: pairs the wrapped Task with @p receiver into an operation
        /// state. Consumes the sender (the Task is move-only), so connect is rvalue-qualified.
        /// @param receiver The receiver to deliver the Task's completion to.
        /// @return The operation state to be @c start ed.
        template <typename Receiver>
        TaskOperationState<std::decay_t<Receiver>, T> connect(Receiver&& receiver) &&
        {
            return TaskOperationState<std::decay_t<Receiver>, T> { std::move(_task), std::forward<Receiver>(receiver) };
        }

      private:
        Task<T> _task;
    };

} // namespace detail

/// Adapts a lazy @ref Task into an stdexec sender, so a Lightweight coroutine result can flow into
/// any @c std::execution (P2300) sender pipeline — @c stdexec::then, @c let_value, @c when_all,
/// @c sync_wait, etc.
///
/// The Task remains lazy: it does not start until the resulting sender is connected and started by a
/// receiver (e.g. by @c stdexec::sync_wait or an enclosing sender). On completion the Task's outcome
/// is mapped onto stdexec's completion channels:
/// - a produced value completes the value channel (@c set_value);
/// - a thrown @ref OperationCancelledError completes the stopped channel (@c set_stopped), matching
///   the library's cooperative-cancellation contract;
/// - any other exception completes the error channel (@c set_error) carrying a @c std::exception_ptr.
///
/// @tparam T The Task's result type (@c void is supported).
/// @param task The Task to adapt (consumed — it is move-only).
/// @return An stdexec sender that, when started, drives @p task and reports its result.
template <typename T>
[[nodiscard]] auto AsSender(Task<T> task) noexcept
{
    return detail::TaskSender<T> { std::move(task) };
}

} // namespace Lightweight::Async
