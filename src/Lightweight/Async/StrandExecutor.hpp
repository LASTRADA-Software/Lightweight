// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "../Api.hpp"
#include "Executor.hpp"

#include <deque>
#include <mutex>

namespace Lightweight::Async
{

/// Serializes work over an underlying executor (an Asio-style "strand").
///
/// All work posted to a given strand runs one item at a time, in FIFO order, even though
/// the items execute on the underlying executor's worker threads. This guarantees that a
/// single ODBC connection — which is not safe for concurrent use — is touched by only one
/// thread at a time. A strand does not own a thread; it borrows the underlying executor.
class LIGHTWEIGHT_API StrandExecutor final: public IExecutor, public IResumeScheduler
{
  public:
    /// Constructs a strand layered over @p underlying.
    ///
    /// @param underlying The executor that actually runs the serialized work.
    explicit StrandExecutor(IExecutor& underlying) noexcept:
        _underlying { underlying }
    {
    }

    StrandExecutor(StrandExecutor const&) = delete;
    StrandExecutor& operator=(StrandExecutor const&) = delete;
    StrandExecutor(StrandExecutor&&) = delete;
    StrandExecutor& operator=(StrandExecutor&&) = delete;
    ~StrandExecutor() override = default;

    void Post(Work work) override;
    void Resume(std::coroutine_handle<> handle) override;

  private:
    void ScheduleDrain();

    IExecutor& _underlying;
    std::mutex _mutex;
    std::deque<Work> _pending;
    bool _running = false;
};

} // namespace Lightweight::Async
