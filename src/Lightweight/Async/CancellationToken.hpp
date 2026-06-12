// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <atomic>
#include <memory>
#include <stdexcept>

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
///
/// Cancellation is honored cooperatively: the offload runtime checks @ref IsCancellationRequested
/// once before a step is dispatched and completes it with @ref OperationCancelledError if set. A
/// request that arrives after a step has begun running does not interrupt the in-flight blocking
/// call.
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

    /// @return true if cancellation has been requested.
    [[nodiscard]] bool IsCancellationRequested() const noexcept
    {
        return _state && _state->cancelled.load(std::memory_order_acquire);
    }

    /// Requests cancellation. Idempotent and thread-safe; a no-op on a non-cancellable token.
    void Request() noexcept
    {
        if (_state)
            _state->cancelled.store(true, std::memory_order_release);
    }

  private:
    struct State
    {
        std::atomic<bool> cancelled { false };
    };

    std::shared_ptr<State> _state;
};

} // namespace Lightweight::Async
