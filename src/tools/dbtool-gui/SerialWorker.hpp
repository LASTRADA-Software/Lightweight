// SPDX-License-Identifier: Apache-2.0
//
// `SerialWorker` runs jobs one after another on a single pool thread. The migration runner and the
// controller's database and plugin jobs share one, so the shared ODBC connection never has two users.

#pragma once

#include <chrono>
#include <exception>
#include <functional>

#include <QtCore/QThreadPool>

namespace DbtoolGui
{

/// A one-thread job queue: jobs run in the order they were enqueued, never two at once.
class SerialWorker final
{
  public:
    /// Creates the queue with its one thread, started on the first job.
    SerialWorker();
    SerialWorker(SerialWorker const&) = delete;
    SerialWorker(SerialWorker&&) = delete;
    SerialWorker& operator=(SerialWorker const&) = delete;
    SerialWorker& operator=(SerialWorker&&) = delete;
    /// Waits for every enqueued job to finish.
    ~SerialWorker();

    /// Queues @p job behind every job enqueued before it.
    /// @param job The work; runs on the worker thread.
    /// @param onFailure Runs on the worker thread with the exception @p job threw, if any.
    void Enqueue(std::function<void()> job, std::function<void(std::exception const&)> onFailure);

    /// Blocks until every enqueued job has finished or @p timeout ran out.
    /// @param timeout How long to wait; `std::chrono::milliseconds::max()` waits for as long as it takes.
    /// @return Whether the queue is empty and no job runs.
    [[nodiscard]] bool WaitForDone(std::chrono::milliseconds timeout);

  private:
    QThreadPool _pool;
};

} // namespace DbtoolGui
