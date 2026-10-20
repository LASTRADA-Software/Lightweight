// SPDX-License-Identifier: Apache-2.0
//
// MigrationRunner takes the migration lock for apply, apply-selected and rollback runs under
// `MigrationLocking::Acquire`, the default, and refuses within the lock timeout while another
// connection holds it. Under `MigrationLocking::Skip` the same runs go ahead without the lock, and
// the backup requirement still refuses a run whose required backup is missing. The runner drives a
// local MigrationManager against a scratch SQLite database.

#include "../MigrationRunner.hpp"
#include "MigrationRunnerTestSupport.hpp"

#include <Lightweight/SqlConnection.hpp>
#include <Lightweight/SqlMigration.hpp>
#include <Lightweight/SqlScopedLock.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <PluginIngestion.hpp>
#include <QtCore/QCoreApplication>
#include <QtCore/QElapsedTimer>
#include <QtTest/QSignalSpy>

using DbtoolGui::MigrationLocking;
using DbtoolGui::MigrationRunner;
using DbtoolGui::Tests::Logged;
using DbtoolGui::Tests::MigrationLockState;
using DbtoolGui::Tests::NoOpMigration;
using DbtoolGui::Tests::ScratchDatabase;
using DbtoolGui::Tests::WaitFor;
using Lightweight::SqlConnection;
using Lightweight::SqlMigration::MigrationManager;
using Lightweight::SqlMigration::MigrationTimestamp;

namespace
{

/// A pending migration whose Up() and Down() do nothing, so it can be applied and rolled back.
class ReversibleNoOpMigration final: public Lightweight::SqlMigration::MigrationBase
{
  public:
    explicit ReversibleNoOpMigration(std::uint64_t timestamp):
        Lightweight::SqlMigration::MigrationBase(MigrationTimestamp { timestamp }, "Reversible no-op migration")
    {
    }

    void Up(Lightweight::SqlMigrationQueryBuilder& /*plan*/) const override {}
    void Down(Lightweight::SqlMigrationQueryBuilder& /*plan*/) const override {}
    [[nodiscard]] bool HasDownImplementation() const noexcept override
    {
        return true;
    }
};

/// A post-migrate hook stub recording the migration lock state it observed from a second connection.
MigrationRunner::PostMigrateHook LockStateRecorder(std::vector<std::string>& lockStates)
{
    return [&lockStates](MigrationManager& manager, std::function<void(std::string_view)> const& /*logError*/) {
        lockStates.push_back(MigrationLockState(manager.GetDataMapper().Connection().ConnectionString()));
        return true;
    };
}

/// Holds the migration lock on a connection of its own, as another migrator would, until destroyed.
class OtherMigrator
{
  public:
    OtherMigrator():
        _connection(SqlConnection::DefaultConnectionString()),
        _lock(_connection, Lightweight::Tools::MigrationLockName, std::chrono::seconds { 5 })
    {
    }

  private:
    SqlConnection _connection;
    Lightweight::SqlScopedLock _lock;
};

/// Runs @p start and waits for the run's `finished`.
/// @return The run's `ok` and summary.
std::pair<bool, QString> RunToEnd(MigrationRunner& runner, std::function<void()> const& start)
{
    QSignalSpy done(&runner, &MigrationRunner::finished);
    start();
    REQUIRE(WaitFor(done, 30000));
    QCoreApplication::processEvents();
    return { done.first().at(0).toBool(), done.first().at(1).toString() };
}

} // namespace

TEST_CASE("Skip applies without the migration lock", "[dbtool-gui][locking]")
{
    ScratchDatabase const database;
    NoOpMigration const first { 20990101000001 };
    NoOpMigration const second { 20990101000002 };
    MigrationManager manager;
    manager.AddMigration(&first);
    manager.AddMigration(&second);
    MigrationRunner runner;
    runner.SetManager(&manager);
    runner.setLocking(MigrationLocking::Skip);
    auto lockStates = std::vector<std::string> {};
    runner.setPostMigrateHook(LockStateRecorder(lockStates));
    QSignalSpy logs(&runner, &MigrationRunner::logLine);

    auto const selected = RunToEnd(runner, [&] { runner.applySelected(QStringList { QStringLiteral("20990101000001") }); });
    CHECK(selected.first);
    auto const upTo = RunToEnd(runner, [&] { runner.applyUpTo(QString {}); });
    CHECK(upTo.first);

    CHECK(manager.GetPending().empty());
    CHECK(lockStates == std::vector<std::string> { "free", "free" });
    CHECK(Logged(logs, QStringLiteral("Running without the migration lock")));
}

TEST_CASE("Acquire applies under the migration lock", "[dbtool-gui][locking]")
{
    ScratchDatabase const database;
    NoOpMigration const migration { 20990101000001 };
    MigrationManager manager;
    manager.AddMigration(&migration);
    MigrationRunner runner;
    runner.SetManager(&manager);
    auto lockStates = std::vector<std::string> {};
    runner.setPostMigrateHook(LockStateRecorder(lockStates));

    auto const outcome = RunToEnd(runner, [&] { runner.applyUpTo(QString {}); });

    CHECK(outcome.first);
    CHECK(manager.GetPending().empty());
    CHECK(lockStates == std::vector<std::string> { "held" });
}

TEST_CASE("Acquire refuses an apply while another connection holds the lock; Skip applies", "[dbtool-gui][locking]")
{
    ScratchDatabase const database;
    NoOpMigration const migration { 20990101000001 };
    MigrationManager manager;
    manager.AddMigration(&migration);
    MigrationRunner runner;
    runner.SetManager(&manager);
    runner.setLockTimeout(std::chrono::milliseconds { 200 });
    OtherMigrator const otherMigrator;

    auto const refused = RunToEnd(runner, [&] { runner.applyUpTo(QString {}); });
    CHECK_FALSE(refused.first);
    CHECK(refused.second == QStringLiteral("Could not acquire the migration lock."));
    CHECK(manager.GetPending().size() == 1);

    runner.setLocking(MigrationLocking::Skip);
    auto const applied = RunToEnd(runner, [&] { runner.applyUpTo(QString {}); });
    CHECK(applied.first);
    CHECK(manager.GetPending().empty());
}

TEST_CASE("rollback under Acquire fails fast while another connection holds the lock", "[dbtool-gui][locking]")
{
    ScratchDatabase const database;
    ReversibleNoOpMigration const first { 20990101000001 };
    ReversibleNoOpMigration const second { 20990101000002 };
    MigrationManager manager;
    manager.AddMigration(&first);
    manager.AddMigration(&second);
    manager.RegisterRelease("1.0.0", MigrationTimestamp { 20990101000001 });
    MigrationRunner runner;
    runner.SetManager(&manager);
    REQUIRE(RunToEnd(runner, [&] { runner.applyUpTo(QString {}); }).first);
    REQUIRE(manager.GetPending().empty());

    runner.setLockTimeout(std::chrono::milliseconds { 200 });
    OtherMigrator const otherMigrator;
    auto clock = QElapsedTimer {};
    clock.start();
    auto const refused = RunToEnd(runner, [&] { runner.rollbackToRelease(QStringLiteral("1.0.0")); });

    CHECK_FALSE(refused.first);
    CHECK(refused.second == QStringLiteral("Could not acquire the migration lock."));
    CHECK(clock.elapsed() < 10000);
    CHECK(manager.GetPending().empty());
}

TEST_CASE("rollback under Skip goes ahead while another connection holds the lock", "[dbtool-gui][locking]")
{
    ScratchDatabase const database;
    ReversibleNoOpMigration const first { 20990101000001 };
    ReversibleNoOpMigration const second { 20990101000002 };
    MigrationManager manager;
    manager.AddMigration(&first);
    manager.AddMigration(&second);
    manager.RegisterRelease("1.0.0", MigrationTimestamp { 20990101000001 });
    MigrationRunner runner;
    runner.SetManager(&manager);
    REQUIRE(RunToEnd(runner, [&] { runner.applyUpTo(QString {}); }).first);

    runner.setLocking(MigrationLocking::Skip);
    OtherMigrator const otherMigrator;
    auto const rolledBack = RunToEnd(runner, [&] { runner.rollbackToRelease(QStringLiteral("1.0.0")); });

    CHECK(rolledBack.first);
    REQUIRE(manager.GetPending().size() == 1);
    CHECK(manager.GetPending().front()->GetTimestamp().value == 20990101000002);
}

TEST_CASE("Skip still refuses an apply whose required backup is missing", "[dbtool-gui][locking]")
{
    ScratchDatabase const database;
    NoOpMigration const migration { 20990101000001 };
    MigrationManager manager;
    manager.AddMigration(&migration);
    manager.RegisterRelease("3.0.0", MigrationTimestamp { 20990101000001 }, { .requiresBackup = true });
    MigrationRunner runner;
    runner.SetManager(&manager);
    runner.setLocking(MigrationLocking::Skip);

    auto const refused = RunToEnd(runner, [&] { runner.applyUpTo(QString {}); });

    CHECK_FALSE(refused.first);
    CHECK(refused.second.contains(QStringLiteral("A backup is required")));
    CHECK(manager.GetPending().size() == 1);
}
