// SPDX-License-Identifier: Apache-2.0
//
// MigrationRunner runs the plugins' pre-migrate steps before the first migration: under the
// migration lock for an apply run, without it and in dry-run mode for a dry run. A step that
// throws or a cancel during the steps ends the run before any migration. The steps and the
// migrations show as rows of the runner's task model. The runner drives a local
// MigrationManager against a scratch SQLite database.

#include "../MigrationRunner.hpp"
#include "MigrationRunnerTestSupport.hpp"

#include <Lightweight/ProgressTask.hpp>
#include <Lightweight/SqlConnection.hpp>
#include <Lightweight/SqlMigration.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <ranges>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <QtCore/QCoreApplication>
#include <QtCore/QElapsedTimer>
#include <QtTest/QSignalSpy>

using namespace std::chrono_literals;
using DbtoolGui::MigrationRunner;
using DbtoolGui::ProgressTaskModel;
using DbtoolGui::Tests::Logged;
using DbtoolGui::Tests::MigrationLockState;
using DbtoolGui::Tests::NoOpMigration;
using DbtoolGui::Tests::ScratchDatabase;
using DbtoolGui::Tests::WaitFor;
using Lightweight::ProgressTask;
using Lightweight::SqlConnection;
using Lightweight::SqlMigration::ExecutionMode;
using Lightweight::SqlMigration::MigrationManager;

namespace
{

/// What a step saw when it ran.
struct StepObservation
{
    ExecutionMode mode = ExecutionMode::Execute;
    std::size_t pending = 0;
    std::string lock;
};

/// Registers a step titled "Recorder" that records what it saw.
void AddRecorder(MigrationManager& manager, std::vector<StepObservation>& observations)
{
    manager.AddPreMigrateStep(
        "Recorder",
        {},
        [&observations](
            SqlConnection& connection, MigrationManager& stepManager, ProgressTask& /*task*/, ExecutionMode mode) {
            observations.push_back({ .mode = mode,
                                     .pending = stepManager.GetPending().size(),
                                     .lock = MigrationLockState(connection.ConnectionString()) });
        });
}

/// The titles of the task model's rows.
QStringList TaskTitles(ProgressTaskModel const& tasks)
{
    auto titles = QStringList {};
    for (auto const row: std::views::iota(0, tasks.rowCount()))
        titles.append(tasks.data(tasks.index(row, 0), ProgressTaskModel::TitleRole).toString());
    return titles;
}

/// Whether every row of the task model has finished.
bool AllFinished(ProgressTaskModel const& tasks)
{
    return std::ranges::all_of(std::views::iota(0, tasks.rowCount()), [&tasks](int row) {
        return tasks.data(tasks.index(row, 0), ProgressTaskModel::FinishedRole).toBool();
    });
}

} // namespace

TEST_CASE("MigrationRunner runs the pre-migrate steps under the lock before the first migration",
          "[dbtool-gui][pre-migrate]")
{
    ScratchDatabase const database;
    NoOpMigration const migration { 20990101000001 };
    MigrationManager manager;
    manager.AddMigration(&migration);
    auto observations = std::vector<StepObservation> {};
    AddRecorder(manager, observations);
    MigrationRunner runner;
    runner.SetManager(&manager);

    QSignalSpy done(&runner, &MigrationRunner::finished);
    runner.applyUpTo(QString {});
    REQUIRE(WaitFor(done));
    QCoreApplication::processEvents();

    CHECK(done.first().at(0).toBool());
    REQUIRE(observations.size() == 1);
    CHECK(observations[0].mode == ExecutionMode::Execute);
    CHECK(observations[0].pending == 1);
    CHECK(observations[0].lock == "held");
    CHECK(manager.GetPending().empty());
    CHECK(TaskTitles(*runner.tasks()) == QStringList { "Recorder", "Migrations" });
    CHECK(AllFinished(*runner.tasks()));
}

TEST_CASE("MigrationRunner runs the steps without the lock and changes nothing in a dry run", "[dbtool-gui][pre-migrate]")
{
    ScratchDatabase const database;
    NoOpMigration const migration { 20990101000001 };
    MigrationManager manager;
    manager.AddMigration(&migration);
    auto observations = std::vector<StepObservation> {};
    AddRecorder(manager, observations);
    MigrationRunner runner;
    runner.SetManager(&manager);

    QSignalSpy done(&runner, &MigrationRunner::finished);
    runner.dryRunUpTo(QString {});
    REQUIRE(WaitFor(done));
    QCoreApplication::processEvents();

    CHECK(done.first().at(0).toBool());
    REQUIRE(observations.size() == 1);
    CHECK(observations[0].mode == ExecutionMode::DryRun);
    CHECK(observations[0].lock == "free");
    CHECK(manager.GetPending().size() == 1);
    CHECK(TaskTitles(*runner.tasks()) == QStringList { "Recorder" });
}

TEST_CASE("A pre-migrate step that throws fails the run before any migration", "[dbtool-gui][pre-migrate]")
{
    ScratchDatabase const database;
    NoOpMigration const migration { 20990101000001 };
    MigrationManager manager;
    manager.AddMigration(&migration);
    manager.AddPreMigrateStep(
        "Recorder",
        {},
        [](SqlConnection& /*connection*/, MigrationManager& /*manager*/, ProgressTask& /*task*/, ExecutionMode) {
            throw std::runtime_error("boom");
        });
    MigrationRunner runner;
    runner.SetManager(&manager);
    QSignalSpy logs(&runner, &MigrationRunner::logLine);
    QSignalSpy started(&runner, &MigrationRunner::migrationStarted);

    QSignalSpy done(&runner, &MigrationRunner::finished);
    runner.applyUpTo(QString {});
    REQUIRE(WaitFor(done));
    QCoreApplication::processEvents();

    CHECK_FALSE(done.first().at(0).toBool());
    // One log line per failure: the failed step's own.
    CHECK(Logged(logs, QStringLiteral("failed Recorder: boom")));
    CHECK(std::ranges::count_if(logs, [](QList<QVariant> const& entry) { return entry.at(0).toString().contains("boom"); })
          == 1);
    CHECK(started.isEmpty());
    CHECK(manager.GetPending().size() == 1);
    // The failed step's row stays, red with its reason, after the run ended.
    auto const& tasks = *runner.tasks();
    REQUIRE(TaskTitles(tasks) == QStringList { "Recorder" });
    CHECK(tasks.data(tasks.index(0, 0), ProgressTaskModel::FailedRole).toBool());
    CHECK(tasks.data(tasks.index(0, 0), ProgressTaskModel::FailureReasonRole).toString() == "boom");
}

TEST_CASE("Cancel during a pre-migrate step applies no migration", "[dbtool-gui][pre-migrate]")
{
    ScratchDatabase const database;
    NoOpMigration const migration { 20990101000001 };
    MigrationManager manager;
    manager.AddMigration(&migration);
    manager.AddPreMigrateStep(
        "Recorder", {}, [](SqlConnection& /*connection*/, MigrationManager& /*manager*/, ProgressTask& task, ExecutionMode) {
            auto const deadline = std::chrono::steady_clock::now() + 5s;
            while (!task.IsCancellationRequested() && std::chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for(10ms);
        });
    MigrationRunner runner;
    runner.SetManager(&manager);

    QSignalSpy done(&runner, &MigrationRunner::finished);
    runner.applyUpTo(QString {});
    QElapsedTimer waited;
    waited.start();
    while (runner.tasks()->rowCount() != 1 && waited.elapsed() < 5000)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    REQUIRE(runner.tasks()->rowCount() == 1);
    runner.cancel();
    REQUIRE(WaitFor(done));

    CHECK_FALSE(done.first().at(0).toBool());
    CHECK(done.first().at(1).toString()
          == QStringLiteral("Cancelled during the pre-migrate steps; no migration was applied."));
    CHECK_FALSE(TaskTitles(*runner.tasks()).contains(QStringLiteral("Migrations")));
    CHECK(manager.GetPending().size() == 1);
}

TEST_CASE("A rollback starts with the previous run's tasks cleared", "[dbtool-gui][pre-migrate]")
{
    ScratchDatabase const database;
    NoOpMigration const migration { 20990101000001 };
    MigrationManager manager;
    manager.AddMigration(&migration);
    auto observations = std::vector<StepObservation> {};
    AddRecorder(manager, observations);
    MigrationRunner runner;
    runner.SetManager(&manager);

    QSignalSpy applied(&runner, &MigrationRunner::finished);
    runner.applyUpTo(QString {});
    REQUIRE(WaitFor(applied));
    QCoreApplication::processEvents();
    REQUIRE(runner.tasks()->rowCount() == 2);

    QSignalSpy rolledBack(&runner, &MigrationRunner::finished);
    runner.rollbackToRelease(QStringLiteral("9.9.9"));
    REQUIRE(WaitFor(rolledBack));
    QCoreApplication::processEvents();
    CHECK(runner.tasks()->rowCount() == 0);
}

TEST_CASE("applySelected runs the steps too", "[dbtool-gui][pre-migrate]")
{
    ScratchDatabase const database;
    NoOpMigration const migration { 20990101000001 };
    MigrationManager manager;
    manager.AddMigration(&migration);
    auto observations = std::vector<StepObservation> {};
    AddRecorder(manager, observations);
    MigrationRunner runner;
    runner.SetManager(&manager);

    QSignalSpy done(&runner, &MigrationRunner::finished);
    runner.applySelected(QStringList { QStringLiteral("20990101000001") });
    REQUIRE(WaitFor(done));
    QCoreApplication::processEvents();

    CHECK(done.first().at(0).toBool());
    REQUIRE(observations.size() == 1);
    CHECK(observations[0].mode == ExecutionMode::Execute);
    CHECK(observations[0].pending == 1);
    CHECK(observations[0].lock == "held");
    CHECK(manager.GetPending().empty());
    CHECK(TaskTitles(*runner.tasks()) == QStringList { "Recorder", "Migrations" });
    CHECK(AllFinished(*runner.tasks()));
}
