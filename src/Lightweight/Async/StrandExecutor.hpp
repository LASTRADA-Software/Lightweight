// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "../Api.hpp"
#include "Executor.hpp"
#include "detail/SerialWorkQueue.hpp"

#include <memory>

namespace Lightweight::Async
{

/// Serializes work over an underlying executor (an Asio-style "strand").
///
/// All work posted to a given strand runs one item at a time, in FIFO order, even though
/// the items execute on the underlying executor's worker threads. This guarantees that a
/// single ODBC connection — which is not safe for concurrent use — is touched by only one
/// thread at a time. A strand does not own a thread; it borrows the underlying executor.
///
/// The strand's mutable state lives in a heap @c State held by a @c std::shared_ptr. Each
/// in-flight drain closure keeps a copy of that pointer, so the state outlives the
/// @c StrandExecutor wrapper itself until the last drain returns. This makes the strand safe
/// to destroy (or to replace, e.g. via @c SqlConnection::EnableAsync) while a drain is still
/// running on a worker thread — the closure only ever touches @c State, never the wrapper.
class LIGHTWEIGHT_API StrandExecutor final: public IExecutor, public IResumeScheduler
{
  public:
    /// Constructs a strand layered over @p underlying.
    ///
    /// @param underlying The executor that actually runs the serialized work.
    explicit StrandExecutor(IExecutor& underlying);

    StrandExecutor(StrandExecutor const&) = delete;
    StrandExecutor& operator=(StrandExecutor const&) = delete;
    StrandExecutor(StrandExecutor&&) = delete;
    StrandExecutor& operator=(StrandExecutor&&) = delete;
    ~StrandExecutor() override = default;

    void Post(Work work) override;
    void Resume(std::coroutine_handle<> handle) override;

  private:
    /// Mutable strand state, heap-allocated so in-flight drain closures can keep it alive
    /// independently of the @c StrandExecutor wrapper's lifetime. The serialized FIFO and its
    /// drain-active flag live in @ref detail::SerialDrainQueue, which guards both under one lock.
    struct State
    {
        IExecutor& underlying; ///< Borrowed executor that runs the serialized work.
        detail::SerialDrainQueue queue;

        explicit State(IExecutor& underlyingExecutor) noexcept:
            underlying { underlyingExecutor }
        {
        }
    };

    /// Schedules a single drain closure on @p state->underlying that drains @p state->pending
    /// to completion. The closure captures a copy of @p state, keeping it alive while it runs.
    static void ScheduleDrain(std::shared_ptr<State> state);

    std::shared_ptr<State> _state;
};

} // namespace Lightweight::Async
