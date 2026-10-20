// SPDX-License-Identifier: Apache-2.0
//
// Counts database accesses process-wide, so a test can prove that a code path touches no database.

#pragma once

#include <Lightweight/SqlLogger.hpp>

#include <atomic>
#include <source_location>
#include <string>
#include <string_view>

namespace DbtoolGui::Tests
{

/// While started, installs itself as the process-wide `SqlLogger`, forwarding every call to the
/// logger it replaced and counting connection opens, errors, direct executions, prepares and
/// executions from any thread. Start and stop it while no worker job runs: swapping the
/// process-wide logger is not synchronised with a thread that is about to log.
class DbAccessProbe final: public Lightweight::SqlLogger
{
  public:
    /// Creates a stopped probe that forwards to the current logger.
    DbAccessProbe();
    DbAccessProbe(DbAccessProbe const&) = delete;
    DbAccessProbe(DbAccessProbe&&) = delete;
    DbAccessProbe& operator=(DbAccessProbe const&) = delete;
    DbAccessProbe& operator=(DbAccessProbe&&) = delete;
    /// Stops the probe if it still runs.
    ~DbAccessProbe() override;

    /// Resets the count and installs the probe in front of the current logger.
    void Start();

    /// @return The database accesses counted since `Start`.
    [[nodiscard]] int Count() const noexcept;

    /// Puts the replaced logger back; the count is kept.
    void Stop();

    void OnWarning(std::string_view const& message) override;
    void OnError(Lightweight::SqlError errorCode, std::source_location sourceLocation) override;
    void OnError(Lightweight::SqlErrorInfo const& errorInfo, std::source_location sourceLocation) override;
    void OnScopedTimerStart(std::string const& tag) override;
    void OnScopedTimerStop(std::string const& tag) override;
    void OnConnectionOpened(Lightweight::SqlConnection const& connection) override;
    void OnConnectionClosed(Lightweight::SqlConnection const& connection) override;
    void OnConnectionIdle(Lightweight::SqlConnection const& connection) override;
    void OnConnectionReuse(Lightweight::SqlConnection const& connection) override;
    void OnExecuteDirect(std::string_view const& query) override;
    void OnPrepare(std::string_view const& query) override;
    void OnBind(std::string_view const& name, std::string value) override;
    void OnExecute(std::string_view const& query) override;
    void OnExecuteBatch() override;
    void OnFetchRow() override;
    void OnFetchBlock(std::size_t rowsInBlock) override;
    void OnFetchEnd() override;

  private:
    void Hit() noexcept;

    std::atomic<Lightweight::SqlLogger*> _forward;
    bool _installed = false;
    std::atomic<int> _count = 0;
};

} // namespace DbtoolGui::Tests
