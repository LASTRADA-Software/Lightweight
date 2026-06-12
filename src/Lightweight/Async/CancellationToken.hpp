// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <utility>
#include <vector>

namespace Lightweight::Async
{

/// Thrown when an asynchronous operation is abandoned because cancellation was requested
/// before (or instead of) it producing a result.
class OperationCancelledError: public std::runtime_error
{
  public:
    OperationCancelledError():
        std::runtime_error { "Asynchronous operation was cancelled." }
    {
    }
};

/// A cooperative cancellation token shared by value.
///
/// A default-constructed token is @b non-cancellable (it never reports cancellation and
/// allocates nothing) — this is the cheap default for async methods that are not given a
/// token. Obtain a cancellable token via @ref Create; copies share the same cancellation
/// state, so a caller can keep one copy and @ref Request cancellation on the operation
/// holding another.
class CancellationToken
{
  public:
    /// Constructs a non-cancellable token.
    CancellationToken() noexcept = default;

    /// @return a fresh, cancellable token backed by new shared state.
    [[nodiscard]] static CancellationToken Create()
    {
        CancellationToken token;
        token._state = std::make_shared<State>();
        return token;
    }

    /// @return true if this token can ever transition to the cancelled state.
    [[nodiscard]] bool CanBeCancelled() const noexcept
    {
        return static_cast<bool>(_state);
    }

    /// @return true if cancellation has been requested.
    [[nodiscard]] bool IsCancellationRequested() const noexcept
    {
        return _state && _state->cancelled.load(std::memory_order_acquire);
    }

    /// Requests cancellation and runs any registered callbacks exactly once.
    void Request()
    {
        if (!_state)
            return;
        std::vector<std::move_only_function<void()>> callbacks;
        {
            std::scoped_lock const lock(_state->mutex);
            if (_state->cancelled.exchange(true, std::memory_order_acq_rel))
                return; // already requested
            callbacks.swap(_state->callbacks);
        }
        for (auto& callback: callbacks)
            if (callback)
                callback();
    }

    /// Registers @p callback to run when cancellation is requested.
    ///
    /// If cancellation has already been requested, the callback runs immediately on the
    /// calling thread. On a non-cancellable token the callback is dropped.
    ///
    /// @param callback The callback to invoke on cancellation (consumed).
    void OnCancel(std::move_only_function<void()> callback)
    {
        if (!_state)
            return;
        {
            std::scoped_lock const lock(_state->mutex);
            if (!_state->cancelled.load(std::memory_order_acquire))
            {
                _state->callbacks.push_back(std::move(callback));
                return;
            }
        }
        if (callback)
            callback();
    }

  private:
    struct State
    {
        std::atomic<bool> cancelled { false };
        std::mutex mutex;
        std::vector<std::move_only_function<void()>> callbacks;
    };

    std::shared_ptr<State> _state;
};

} // namespace Lightweight::Async
