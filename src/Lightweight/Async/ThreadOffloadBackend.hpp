// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "Backend.hpp"
#include "Executor.hpp"
#include "StrandExecutor.hpp"

namespace Lightweight::Async
{

/// Portable async backend that offloads blocking ODBC work to a worker thread.
///
/// Every blocking operation for the connection runs on a per-connection @ref StrandExecutor
/// (so the connection is serialized), and the awaiting coroutine resumes on the injected
/// resume scheduler. This backend works with every ODBC driver on every platform and is the
/// fallback whenever native driver async is unavailable.
///
/// This type is header-only (all members are inline), so it is intentionally not marked with
/// the DLL export macro.
class ThreadOffloadBackend final: public IAsyncBackend
{
  public:
    /// Constructs the backend.
    ///
    /// @param dbWorkers The shared worker-thread pool that actually runs blocking work.
    /// @param resume The scheduler used to resume coroutines (typically the app run loop).
    /// @note Not @c noexcept: constructing the strand allocates its shared state.
    ThreadOffloadBackend(IExecutor& dbWorkers, IResumeScheduler& resume):
        _strand { dbWorkers },
        _resume { resume }
    {
    }

    [[nodiscard]] StrandExecutor& Strand() noexcept override
    {
        return _strand;
    }

    [[nodiscard]] IResumeScheduler& ResumeScheduler() noexcept override
    {
        return _resume;
    }

  private:
    StrandExecutor _strand;
    IResumeScheduler& _resume;
};

} // namespace Lightweight::Async
