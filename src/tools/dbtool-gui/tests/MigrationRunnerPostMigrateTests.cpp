// SPDX-License-Identifier: Apache-2.0
//
// MigrationRunner runs the plugins' post-migrate hooks after every successful
// apply run, also when nothing was pending, while it still holds the migration
// lock, and it releases the lock before it reports the run as finished. Dry-runs
// and failed runs never reach the hook, and a failing or throwing hook fails the
// run. The hook is a stub; the runner drives a local MigrationManager
// against a scratch SQLite database.

#include "../MigrationRunner.hpp"
#include "SqliteDriver.hpp"

#include <Lightweight/SqlAdvisoryLock.hpp>
#include <Lightweight/SqlConnectInfo.hpp>
#include <Lightweight/SqlConnection.hpp>
#include <Lightweight/SqlLogger.hpp>
#include <Lightweight/SqlMigration.hpp>
#include <Lightweight/SqlScopedLock.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <PluginIngestion.hpp>
#include <QtCore/QTemporaryDir>
#include <QtTest/QSignalSpy>

using DbtoolGui::MigrationRunner;
using DbtoolGui::Tests::SqliteDriver;
using Lightweight::SqlConnection;
using Lightweight::SqlConnectionString;
using Lightweight::SqlMigration::MigrationManager;

namespace
{

/// Points the default connection at a scratch SQLite database for one test and restores the previous default.
class ScratchDatabase
{
  public:
    ScratchDatabase():
        _previous(SqlConnection::DefaultConnectionString())
    {
        auto const database = _directory.filePath(QStringLiteral("runner.db")).toStdString();
        SqlConnection::SetDefaultConnectionString(
            SqlConnectionString { .value = std::string { "Driver={" } + SqliteDriver + "};Database=" + database });
    }

    ~ScratchDatabase()
    {
        SqlConnection::SetDefaultConnectionString(_previous);
    }

    ScratchDatabase(ScratchDatabase const&) = delete;
    ScratchDatabase& operator=(ScratchDatabase const&) = delete;
    ScratchDatabase(ScratchDatabase&&) = delete;
    ScratchDatabase& operator=(ScratchDatabase&&) = delete;

  private:
    QTemporaryDir _directory;
    SqlConnectionString _previous;
};

/// The migration lock's state as seen from a second connection: "held", "free" or "error: <message>".
std::string MigrationLockState(SqlConnectionString const& connectionString)
{
    auto probeConnection = SqlConnection { connectionString };
    auto const probe = Lightweight::SqlScopedLock::TryConstruct(
        probeConnection, Lightweight::Tools::MigrationLockName, std::chrono::milliseconds { 100 });
    if (probe.has_value())
        return "free";
    if (probe.error().reason == Lightweight::SqlLockFailureReason::Timeout)
        return "held";
    return "error: " + probe.error().message;
}

/// The migration lock's state on the scratch database, probed from outside the runner.
std::string MigrationLockStateNow()
{
    return MigrationLockState(SqlConnection::DefaultConnectionString());
}

/// A hook stub recording, per call, the migration lock state it observed.
MigrationRunner::PostMigrateHook RecordingHook(std::vector<std::string>& lockStates, bool succeed = true)
{
    return [&lockStates, succeed](MigrationManager& manager, std::function<void(std::string_view)> const& logError) {
        lockStates.push_back(MigrationLockState(manager.GetDataMapper().Connection().ConnectionString()));
        if (!succeed)
            logError("seed failed");
        return succeed;
    };
}

/// Spins the event loop until `spy` has at least one emission or `ms` ran out.
[[nodiscard]] bool WaitFor(QSignalSpy& spy, int ms = 10000)
{
    return spy.count() > 0 || spy.wait(ms);
}

/// Whether any emitted log line contains `text`.
[[nodiscard]] bool Logged(QSignalSpy const& logs, QString const& text)
{
    return std::ranges::any_of(logs,
                               [&text](QList<QVariant> const& entry) { return entry.at(0).toString().contains(text); });
}

/// Stalls the release of the SQLite migration lock when a worker thread performs it, so a run
/// that reported `finished` before releasing the lock is caught every time, not only by chance.
class StallingLockReleaseLogger final: public Lightweight::SqlLogger::Null
{
  public:
    explicit StallingLockReleaseLogger(std::chrono::milliseconds stall):
        _testThread(std::this_thread::get_id()),
        _stall(stall),
        _previous(Lightweight::SqlLogger::GetLogger())
    {
        Lightweight::SqlLogger::SetLogger(*this);
    }

    ~StallingLockReleaseLogger() override
    {
        Lightweight::SqlLogger::SetLogger(_previous);
    }

    StallingLockReleaseLogger(StallingLockReleaseLogger const&) = delete;
    StallingLockReleaseLogger& operator=(StallingLockReleaseLogger const&) = delete;
    StallingLockReleaseLogger(StallingLockReleaseLogger&&) = delete;
    StallingLockReleaseLogger& operator=(StallingLockReleaseLogger&&) = delete;

    void OnExecuteDirect(std::string_view const& query) override
    {
        // Fires before the statement runs; the SQLite lock release is a DELETE on its bookkeeping table.
        if (std::this_thread::get_id() != _testThread && query.contains(R"(DELETE FROM "_lightweight_locks")"))
            std::this_thread::sleep_for(_stall);
    }

  private:
    std::thread::id _testThread;
    std::chrono::milliseconds _stall;
    Lightweight::SqlLogger& _previous;
};

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

/// A pending migration whose Up() fails, so applying it fails the run.
class FailingMigration: public Lightweight::SqlMigration::MigrationBase
{
  public:
    FailingMigration():
        Lightweight::SqlMigration::MigrationBase(Lightweight::SqlMigration::MigrationTimestamp { 20990101000000 },
                                                 "Failing migration")
    {
    }

    ~FailingMigration() override = default;
    FailingMigration(FailingMigration const&) = delete;
    FailingMigration& operator=(FailingMigration const&) = delete;
    FailingMigration(FailingMigration&&) = delete;
    FailingMigration& operator=(FailingMigration&&) = delete;

    void Up(Lightweight::SqlMigrationQueryBuilder& /*plan*/) const override
    {
        throw std::runtime_error("migration failed");
    }
};

} // namespace

TEST_CASE("MigrationRunner runs the post-migrate hook after an apply with nothing pending, under the migration lock",
          "[dbtool-gui][post-migrate]")
{
    ScratchDatabase const database;
    MigrationManager manager;
    MigrationRunner runner;
    runner.SetManager(&manager);
    std::vector<std::string> lockStates;
    runner.setPostMigrateHook(RecordingHook(lockStates));

    QSignalSpy upToDone(&runner, &MigrationRunner::finished);
    runner.applyUpTo(QString {});
    REQUIRE(WaitFor(upToDone));
    CHECK(upToDone.first().at(0).toBool());
    // The lock must already be released when `finished` arrives: the GUI refreshes on the same connection.
    CHECK(MigrationLockStateNow() == "free");

    QSignalSpy selectedDone(&runner, &MigrationRunner::finished);
    runner.applySelected(QStringList { QStringLiteral("20240101000000") });
    REQUIRE(WaitFor(selectedDone));
    CHECK(selectedDone.first().at(0).toBool());
    CHECK(MigrationLockStateNow() == "free");

    CHECK(lockStates == std::vector<std::string> { "held", "held" });
}

TEST_CASE("MigrationRunner runs the post-migrate hook once after applying migrations", "[dbtool-gui][post-migrate]")
{
    ScratchDatabase const database;
    NoOpMigration const first { 20990101000001 };
    NoOpMigration const second { 20990101000002 };
    MigrationManager manager;
    manager.AddMigration(&first);
    manager.AddMigration(&second);
    MigrationRunner runner;
    runner.SetManager(&manager);
    std::vector<std::string> lockStates;
    runner.setPostMigrateHook(RecordingHook(lockStates));

    QSignalSpy selectedDone(&runner, &MigrationRunner::finished);
    runner.applySelected(QStringList { QStringLiteral("20990101000001") });
    REQUIRE(WaitFor(selectedDone));
    CHECK(selectedDone.first().at(0).toBool());
    CHECK(selectedDone.first().at(1).toString() == QStringLiteral("Applied 1 migration(s)."));
    CHECK(MigrationLockStateNow() == "free");

    QSignalSpy upToDone(&runner, &MigrationRunner::finished);
    runner.applyUpTo(QString {});
    REQUIRE(WaitFor(upToDone));
    CHECK(upToDone.first().at(0).toBool());
    CHECK(upToDone.first().at(1).toString() == QStringLiteral("Applied 1 migration(s)."));
    CHECK(MigrationLockStateNow() == "free");

    CHECK(manager.GetPending().empty());
    CHECK(lockStates == std::vector<std::string> { "held", "held" });
}

TEST_CASE("MigrationRunner never runs the post-migrate hook for a dry-run", "[dbtool-gui][post-migrate]")
{
    ScratchDatabase const database;
    MigrationManager manager;
    MigrationRunner runner;
    runner.SetManager(&manager);
    std::vector<std::string> lockStates;
    runner.setPostMigrateHook(RecordingHook(lockStates));

    QSignalSpy upToDone(&runner, &MigrationRunner::finished);
    runner.dryRunUpTo(QString {});
    REQUIRE(WaitFor(upToDone));

    QSignalSpy selectedDone(&runner, &MigrationRunner::finished);
    runner.dryRunSelected(QStringList { QStringLiteral("20240101000000") });
    REQUIRE(WaitFor(selectedDone));

    CHECK(lockStates.empty());
}

TEST_CASE("MigrationRunner never runs the post-migrate hook after a failed migration", "[dbtool-gui][post-migrate]")
{
    ScratchDatabase const database;
    FailingMigration const failing;
    MigrationManager manager;
    manager.AddMigration(&failing);
    MigrationRunner runner;
    runner.SetManager(&manager);
    std::vector<std::string> lockStates;
    runner.setPostMigrateHook(RecordingHook(lockStates));

    QSignalSpy done(&runner, &MigrationRunner::finished);
    runner.applyUpTo(QString {});
    REQUIRE(WaitFor(done));

    CHECK_FALSE(done.first().at(0).toBool());
    CHECK(MigrationLockStateNow() == "free");
    CHECK(lockStates.empty());
}

TEST_CASE("MigrationRunner fails an apply run whose post-migrate hook fails", "[dbtool-gui][post-migrate]")
{
    ScratchDatabase const database;
    MigrationManager manager;
    MigrationRunner runner;
    runner.SetManager(&manager);
    std::vector<std::string> lockStates;
    runner.setPostMigrateHook(RecordingHook(lockStates, /*succeed=*/false));
    QSignalSpy logs(&runner, &MigrationRunner::logLine);

    QSignalSpy done(&runner, &MigrationRunner::finished);
    runner.applyUpTo(QString {});
    REQUIRE(WaitFor(done));

    CHECK_FALSE(done.first().at(0).toBool());
    CHECK(done.first().at(1).toString().contains(QStringLiteral("post-migrate")));
    CHECK(MigrationLockStateNow() == "free");
    CHECK(lockStates.size() == 1);
    CHECK(Logged(logs, QStringLiteral("seed failed")));
}

TEST_CASE("MigrationRunner fails, and stays usable after, an apply run whose post-migrate hook throws",
          "[dbtool-gui][post-migrate]")
{
    ScratchDatabase const database;
    MigrationManager manager;
    MigrationRunner runner;
    runner.SetManager(&manager);
    runner.setPostMigrateHook(
        [](MigrationManager& /*manager*/, std::function<void(std::string_view)> const& /*logError*/) -> bool {
            throw std::runtime_error("hook exploded");
        });
    QSignalSpy logs(&runner, &MigrationRunner::logLine);

    QSignalSpy done(&runner, &MigrationRunner::finished);
    runner.applyUpTo(QString {});
    REQUIRE(WaitFor(done));

    CHECK_FALSE(done.first().at(0).toBool());
    CHECK(runner.phase() == MigrationRunner::Phase::Idle);
    CHECK(MigrationLockStateNow() == "free");
    CHECK(Logged(logs, QStringLiteral("hook exploded")));

    // The runner accepts the next run instead of staying stuck in `Running`.
    std::vector<std::string> lockStates;
    runner.setPostMigrateHook(RecordingHook(lockStates));
    QSignalSpy secondDone(&runner, &MigrationRunner::finished);
    runner.applyUpTo(QString {});
    REQUIRE(WaitFor(secondDone));
    CHECK(secondDone.first().at(0).toBool());
    CHECK(lockStates == std::vector<std::string> { "held" });
}

TEST_CASE("MigrationRunner releases the migration lock before it reports the run as finished", "[dbtool-gui][post-migrate]")
{
    ScratchDatabase const database;
    NoOpMigration const pending { 20990101000003 };
    MigrationManager manager;
    manager.AddMigration(&pending);
    MigrationRunner runner;
    runner.SetManager(&manager);
    std::vector<std::string> lockStates;
    runner.setPostMigrateHook(RecordingHook(lockStates));
    // Far longer than the probe's 100 ms timeout: had `finished` been posted before the release,
    // the probe below would run while the stalled release still holds the lock.
    StallingLockReleaseLogger const stall { std::chrono::milliseconds { 500 } };

    QSignalSpy done(&runner, &MigrationRunner::finished);
    runner.applyUpTo(QString {});
    REQUIRE(WaitFor(done));

    CHECK(done.first().at(0).toBool());
    CHECK(MigrationLockStateNow() == "free");
    CHECK(lockStates == std::vector<std::string> { "held" });
}
