// SPDX-License-Identifier: Apache-2.0
//
// Helpers shared by the MigrationRunner tests: a scratch SQLite database, the migration lock's
// state seen from outside the runner, signal waiting and a migration that does nothing.

#pragma once

#include "SqliteDriver.hpp"

#include <Lightweight/SqlAdvisoryLock.hpp>
#include <Lightweight/SqlConnectInfo.hpp>
#include <Lightweight/SqlConnection.hpp>
#include <Lightweight/SqlMigration.hpp>
#include <Lightweight/SqlScopedLock.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <string>

#include <PluginIngestion.hpp>
#include <QtCore/QTemporaryDir>
#include <QtTest/QSignalSpy>

namespace DbtoolGui::Tests
{

/// Points the default connection at a scratch SQLite database for one test and restores the previous default.
class ScratchDatabase
{
  public:
    ScratchDatabase():
        _previous(Lightweight::SqlConnection::DefaultConnectionString())
    {
        auto const database = _directory.filePath(QStringLiteral("runner.db")).toStdString();
        Lightweight::SqlConnection::SetDefaultConnectionString(Lightweight::SqlConnectionString {
            .value = std::string { "Driver={" } + SqliteDriver + "};Database=" + database });
    }

    ~ScratchDatabase()
    {
        Lightweight::SqlConnection::SetDefaultConnectionString(_previous);
    }

    ScratchDatabase(ScratchDatabase const&) = delete;
    ScratchDatabase& operator=(ScratchDatabase const&) = delete;
    ScratchDatabase(ScratchDatabase&&) = delete;
    ScratchDatabase& operator=(ScratchDatabase&&) = delete;

  private:
    QTemporaryDir _directory;
    Lightweight::SqlConnectionString _previous;
};

/// The migration lock's state as seen from a second connection: "held", "free" or "error: <message>".
[[nodiscard]] inline std::string MigrationLockState(Lightweight::SqlConnectionString const& connectionString)
{
    auto probeConnection = Lightweight::SqlConnection { connectionString };
    auto const probe = Lightweight::SqlScopedLock::TryConstruct(
        probeConnection, Lightweight::Tools::MigrationLockName, std::chrono::milliseconds { 100 });
    if (probe.has_value())
        return "free";
    if (probe.error().reason == Lightweight::SqlLockFailureReason::Timeout)
        return "held";
    return "error: " + probe.error().message;
}

/// Spins the event loop until `spy` has at least one emission or `ms` ran out.
[[nodiscard]] inline bool WaitFor(QSignalSpy& spy, int ms = 10000)
{
    return spy.count() > 0 || spy.wait(ms);
}

/// Whether any emitted log line contains `text`.
[[nodiscard]] inline bool Logged(QSignalSpy const& logs, QString const& text)
{
    return std::ranges::any_of(logs,
                               [&text](QList<QVariant> const& entry) { return entry.at(0).toString().contains(text); });
}

/// A pending migration whose Up() does nothing, so applying it succeeds.
class NoOpMigration: public Lightweight::SqlMigration::MigrationBase
{
  public:
    explicit NoOpMigration(std::uint64_t timestamp):
        Lightweight::SqlMigration::MigrationBase(Lightweight::SqlMigration::MigrationTimestamp { timestamp },
                                                 "No-op migration")
    {
    }

    ~NoOpMigration() override = default;
    NoOpMigration(NoOpMigration const&) = delete;
    NoOpMigration& operator=(NoOpMigration const&) = delete;
    NoOpMigration(NoOpMigration&&) = delete;
    NoOpMigration& operator=(NoOpMigration&&) = delete;

    void Up(Lightweight::SqlMigrationQueryBuilder& /*plan*/) const override {}
};

} // namespace DbtoolGui::Tests
