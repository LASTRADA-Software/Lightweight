// SPDX-License-Identifier: Apache-2.0
//
// The busy guard between the three runners must be MUTUAL.
//
// `ManagedBackupController` has always consulted a busy probe, but
// `MigrationRunner` and `BackupRunner` only ever looked at their own `_phase`.
// That made the guard one-way: a migration, or an ad-hoc restore, could start
// *during* a managed backup — producing a torn archive that was then committed
// and marked "ok". These tests pin the missing direction: both runners refuse
// a mutating operation while the probe reports another run in flight, and say
// so in the log.
//
// AppController wires all three probes to each other; see
// `AppControllerTests.cpp` for the wiring-level coverage.

#include "../BackupRunner.hpp"
#include "../MigrationRunner.hpp"

#include <Lightweight/SqlMigration.hpp>

#include <catch2/catch_test_macros.hpp>

#include <QtTest/QSignalSpy>

using DbtoolGui::BackupRunner;
using DbtoolGui::MigrationRunner;

TEST_CASE("BackupRunner refuses a backup while the busy probe reports another run", "[dbtool-gui][busy-guard]")
{
    BackupRunner runner;
    int probeCalls = 0;
    runner.setBusyProbe([&probeCalls] {
        ++probeCalls;
        return true;
    });
    QSignalSpy logs(&runner, &BackupRunner::logLine);

    runner.runBackup(QStringLiteral("/nonexistent/should-never-be-written.zip"));

    CHECK(probeCalls == 1);
    CHECK(runner.phase() == BackupRunner::Phase::Idle);
    REQUIRE(logs.count() == 1);
    CHECK(logs.first().at(0).toString().contains("busy"));
}

TEST_CASE("BackupRunner refuses a restore while the busy probe reports another run", "[dbtool-gui][busy-guard]")
{
    BackupRunner runner;
    runner.setBusyProbe([] { return true; });
    QSignalSpy logs(&runner, &BackupRunner::logLine);

    // The destructive direction matters most: an ad-hoc restore run during a
    // managed backup rewrites the database the archive is being read from.
    runner.runRestore(QStringLiteral("/nonexistent/should-never-be-read.zip"));

    CHECK(runner.phase() == BackupRunner::Phase::Idle);
    REQUIRE(logs.count() == 1);
    CHECK(logs.first().at(0).toString().contains("busy"));
}

TEST_CASE("MigrationRunner refuses mutating runs while the busy probe reports another run", "[dbtool-gui][busy-guard]")
{
    MigrationRunner runner;
    // A manager must be wired or the runner refuses before it ever consults the
    // probe. The singleton is never touched: every call below is refused by the
    // guard, so no migration is collected and no SQL is executed.
    runner.SetManager(&Lightweight::SqlMigration::MigrationManager::GetInstance());
    int probeCalls = 0;
    runner.setBusyProbe([&probeCalls] {
        ++probeCalls;
        return true;
    });
    QSignalSpy logs(&runner, &MigrationRunner::logLine);

    runner.applyUpTo(QString {});
    runner.applySelected(QStringList { QStringLiteral("20240101000000") });
    runner.rollbackToRelease(QStringLiteral("1.0.0"));

    CHECK(probeCalls == 3);
    CHECK(runner.phase() == MigrationRunner::Phase::Idle);
    REQUIRE(logs.count() == 3);
    for (auto const& entry: logs)
        CHECK(entry.at(0).toString().contains("busy"));

    runner.SetManager(nullptr);
}

TEST_CASE("MigrationRunner without a busy probe keeps its previous behaviour", "[dbtool-gui][busy-guard]")
{
    // The probe is optional: an unwired runner must not start refusing runs.
    MigrationRunner runner;
    QSignalSpy logs(&runner, &MigrationRunner::logLine);

    // No manager wired -> refused, but silently (that is the pre-existing
    // "nothing to run" path, not a busy refusal).
    runner.applyUpTo(QString {});

    CHECK(runner.phase() == MigrationRunner::Phase::Idle);
    CHECK(logs.count() == 0);
}
