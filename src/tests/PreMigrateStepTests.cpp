// SPDX-License-Identifier: Apache-2.0

#include "RecordingProgressTask.hpp"
#include "Utils.hpp"

#include <Lightweight/SqlMigration.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <format>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

using namespace Lightweight;
using SqlMigration::ExecutionMode;
using SqlMigration::MigrationManager;
using SqlMigration::PreMigrateStepError;
using SqlMigration::PreMigrateStepOptions;
using SqlMigration::PreMigrateStepsOutcome;

namespace
{

/// Starts every test with a manager that holds no migrations, releases or steps.
class PreMigrateFixture: public SqlTestFixture
{
  public:
    PreMigrateFixture()
    {
        auto& manager = MigrationManager::GetInstance();
        manager.RemoveAllMigrations();
        manager.RemoveAllReleases();
        manager.RemoveAllPreMigrateSteps();
    }
    PreMigrateFixture(PreMigrateFixture const&) = delete;
    PreMigrateFixture(PreMigrateFixture&&) = delete;
    PreMigrateFixture& operator=(PreMigrateFixture const&) = delete;
    PreMigrateFixture& operator=(PreMigrateFixture&&) = delete;
    ~PreMigrateFixture() override
    {
        MigrationManager::GetInstance().RemoveAllPreMigrateSteps();
        MigrationManager::GetInstance().CloseDataMapper();
    }
};

/// A step that appends "<name> dryRun=<0|1>" to @p calls.
SqlMigration::PreMigrateStep RecordingStep(std::vector<std::string>& calls, std::string name)
{
    return [&calls, name = std::move(name)](SqlConnection&, MigrationManager&, ProgressTask& progress, ExecutionMode mode) {
        progress.Advance(0, "working");
        calls.push_back(std::format("{} dryRun={}", name, mode == ExecutionMode::DryRun ? 1 : 0));
    };
}

} // namespace

TEST_CASE_METHOD(PreMigrateFixture,
                 "Pre-migrate steps run in registration order, each under its own task",
                 "[SqlMigration][PreMigrate]")
{
    auto& manager = MigrationManager::GetInstance();
    auto calls = std::vector<std::string> {};
    manager.AddPreMigrateStep("A", {}, RecordingStep(calls, "A"));
    manager.AddPreMigrateStep(
        "B",
        PreMigrateStepOptions { .requiresBackup = [](SqlConnection&, MigrationManager&) { return true; } },
        RecordingStep(calls, "B"));

    auto const steps = manager.GetPreMigrateSteps();
    REQUIRE(steps.size() == 2);
    CHECK(steps[0].title == "A");
    CHECK_FALSE(steps[0].declaresBackupRequirement);
    CHECK(steps[1].title == "B");
    CHECK(steps[1].declaresBackupRequirement);

    auto journal = RecordingProgressTask::Journal {};
    auto root = RecordingProgressTask { journal };
    CHECK(manager.RunPreMigrateSteps(root, ExecutionMode::DryRun) == PreMigrateStepsOutcome::Completed);
    CHECK(calls == std::vector<std::string> { "A dryRun=1", "B dryRun=1" });
    CHECK(journal.events
          == std::vector<std::string> {
              "begin A total=0",
              "advance A +0 working",
              "end A",
              "begin B total=0",
              "advance B +0 working",
              "end B",
          });
}

TEST_CASE_METHOD(PreMigrateFixture, "A pre-migrate step that throws stops the remaining steps", "[SqlMigration][PreMigrate]")
{
    auto& manager = MigrationManager::GetInstance();
    auto calls = std::vector<std::string> {};
    manager.AddPreMigrateStep(
        "A", {}, [](SqlConnection&, MigrationManager&, ProgressTask&, ExecutionMode) { throw std::runtime_error("boom"); });
    manager.AddPreMigrateStep("B", {}, RecordingStep(calls, "B"));

    auto journal = RecordingProgressTask::Journal {};
    auto root = RecordingProgressTask { journal };
    try
    {
        std::ignore = manager.RunPreMigrateSteps(root, ExecutionMode::Execute);
        FAIL("RunPreMigrateSteps did not throw");
    }
    catch (PreMigrateStepError const& error)
    {
        CHECK(error.StepTitle() == "A");
        CHECK(std::string { error.what() } == "Pre-migrate step \"A\" failed: boom");
    }
    CHECK(calls.empty());
    CHECK(journal.events == std::vector<std::string> { "begin A total=0", "fail A boom", "end A" });
}

TEST_CASE_METHOD(PreMigrateFixture,
                 "A pre-migrate step that throws something else fails as an unknown exception",
                 "[SqlMigration][PreMigrate]")
{
    auto& manager = MigrationManager::GetInstance();
    manager.AddPreMigrateStep("A", {}, [](SqlConnection&, MigrationManager&, ProgressTask&, ExecutionMode) { throw 42; });

    auto journal = RecordingProgressTask::Journal {};
    auto root = RecordingProgressTask { journal };
    try
    {
        std::ignore = manager.RunPreMigrateSteps(root, ExecutionMode::Execute);
        FAIL("RunPreMigrateSteps did not throw");
    }
    catch (PreMigrateStepError const& error)
    {
        CHECK(error.StepTitle() == "A");
        CHECK(std::string { error.what() } == "Pre-migrate step \"A\" failed: unknown exception");
    }
    CHECK(journal.events == std::vector<std::string> { "begin A total=0", "fail A unknown exception", "end A" });
}

TEST_CASE_METHOD(PreMigrateFixture, "Cancellation stops before the next pre-migrate step", "[SqlMigration][PreMigrate]")
{
    auto& manager = MigrationManager::GetInstance();
    auto calls = std::vector<std::string> {};
    manager.AddPreMigrateStep("A", {}, RecordingStep(calls, "A"));
    manager.AddPreMigrateStep("B", {}, RecordingStep(calls, "B"));

    auto journal = RecordingProgressTask::Journal {};
    journal.cancelWhen = [](std::vector<std::string> const& events) {
        return std::ranges::contains(events, std::string { "end A" });
    };
    auto root = RecordingProgressTask { journal };
    CHECK(manager.RunPreMigrateSteps(root, ExecutionMode::Execute) == PreMigrateStepsOutcome::Cancelled);
    CHECK(calls == std::vector<std::string> { "A dryRun=0" });
}

TEST_CASE_METHOD(PreMigrateFixture, "A cancellation during the last step is reported too", "[SqlMigration][PreMigrate]")
{
    auto& manager = MigrationManager::GetInstance();
    auto cancelled = false;
    manager.AddPreMigrateStep(
        "Only", {}, [&cancelled](SqlConnection&, MigrationManager&, ProgressTask&, ExecutionMode) { cancelled = true; });
    auto journal = RecordingProgressTask::Journal {};
    journal.cancelWhen = [&cancelled](std::vector<std::string> const&) {
        return cancelled;
    };
    auto root = RecordingProgressTask { journal };
    CHECK(manager.RunPreMigrateSteps(root, ExecutionMode::Execute) == PreMigrateStepsOutcome::Cancelled);
}

TEST_CASE_METHOD(PreMigrateFixture,
                 "Pre-migrate step registration rejects empty steps and duplicate titles",
                 "[SqlMigration][PreMigrate]")
{
    auto& manager = MigrationManager::GetInstance();
    auto calls = std::vector<std::string> {};
    CHECK_THROWS_AS(manager.AddPreMigrateStep("Empty", {}, {}), std::invalid_argument);
    manager.AddPreMigrateStep("A", {}, RecordingStep(calls, "A"));
    CHECK_THROWS_AS(manager.AddPreMigrateStep("A", {}, RecordingStep(calls, "A")), std::invalid_argument);
    manager.RemoveAllPreMigrateSteps();
    CHECK(manager.GetPreMigrateSteps().empty());
}

TEST_CASE_METHOD(PreMigrateFixture,
                 "A step sees the migration connection and the pending migrations",
                 "[SqlMigration][PreMigrate]")
{
    auto& manager = MigrationManager::GetInstance();
    auto migration = SqlMigration::Migration<20990101000001> { "probe", [](SqlMigrationQueryBuilder&) {} };
    auto pendingSeen = std::size_t { 99 };
    auto sameConnection = false;
    manager.AddPreMigrateStep(
        "Probe", {}, [&](SqlConnection& connection, MigrationManager& stepManager, ProgressTask&, ExecutionMode) {
            pendingSeen = stepManager.GetPending().size();
            sameConnection = &connection == &stepManager.GetDataMapper().Connection();
        });
    auto root = NullProgressTask {};
    REQUIRE(manager.RunPreMigrateSteps(root, ExecutionMode::Execute) == PreMigrateStepsOutcome::Completed);
    CHECK(pendingSeen == 1);
    CHECK(sameConnection);
}
