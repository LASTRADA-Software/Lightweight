// SPDX-License-Identifier: Apache-2.0
//
// The fixture plugins the workflow tests drive, loaded the way the GUI loads plugins: copied under a
// `*Plugin` name into a scratch directory, ingested into the migration manager and, where they
// register steps, given their post-init hooks on a scratch SQLite database.

#include "../../../tests/RecordingProgressTask.hpp"
#include "MigrationRunnerTestSupport.hpp"
#include "ScopedVariable.hpp"

#include <Lightweight/SqlMigration.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

#include <PluginIngestion.hpp>
#include <QtCore/QElapsedTimer>
#include <QtCore/QTemporaryDir>
#include <QtCore/QtEnvironmentVariables>

using DbtoolGui::Tests::ScopedVariable;
using DbtoolGui::Tests::ScratchDatabase;
using Lightweight::SqlMigration::ExecutionMode;
using Lightweight::SqlMigration::MigrationManager;
using Lightweight::SqlMigration::PreMigrateStepError;

namespace
{

/// One fixture plugin loaded into the process-wide migration manager from a scratch directory; unloaded on destruction.
class LoadedFixture
{
  public:
    explicit LoadedFixture(char const* modulePath)
    {
        auto const module = std::filesystem::path { modulePath };
        auto const directory = Directory();
        std::filesystem::copy_file(module, directory / ("FixturePlugin" + module.extension().string()));
        auto const directories = std::array { directory };
        plugins = Lightweight::Tools::IngestPlugins(directories, Lightweight::Tools::DefaultPluginLoader(), Manager());
    }

    LoadedFixture(LoadedFixture const&) = delete;
    LoadedFixture(LoadedFixture&&) = delete;
    LoadedFixture& operator=(LoadedFixture const&) = delete;
    LoadedFixture& operator=(LoadedFixture&&) = delete;

    ~LoadedFixture()
    {
        Lightweight::Tools::UnloadPlugins(plugins, Manager());
        Manager().CloseDataMapper();
    }

    /// The manager the plugin was merged into.
    [[nodiscard]] static MigrationManager& Manager()
    {
        return MigrationManager::GetInstance();
    }

    /// Runs the plugin's post-init hook on the scratch database, failing the test on an error.
    void RunPostInitHooks()
    {
        auto errors = std::vector<std::string> {};
        Lightweight::Tools::RunPluginPostInitHooks(plugins,
                                                   Manager().GetDataMapper().Connection(),
                                                   Manager(),
                                                   [&errors](std::string_view message) { errors.emplace_back(message); });
        REQUIRE(errors.empty());
    }

    /// The scratch directory, for files the test wants next to the plugin.
    [[nodiscard]] std::filesystem::path Directory() const
    {
        return std::filesystem::path { _directory.path().toStdWString() };
    }

    std::vector<Lightweight::Tools::LoadedPlugin> plugins;

  private:
    QTemporaryDir _directory;
};

/// The number of lines in @p file; 0 when it does not exist.
[[nodiscard]] int LineCount(std::filesystem::path const& file)
{
    auto stream = std::ifstream { file };
    auto lines = 0;
    for (auto line = std::string {}; std::getline(stream, line);)
        ++lines;
    return lines;
}

/// Whether one of @p events starts with @p prefix.
[[nodiscard]] bool HasEvent(std::vector<std::string> const& events, std::string_view prefix)
{
    return std::ranges::any_of(events, [prefix](std::string const& event) { return event.starts_with(prefix); });
}

} // namespace

TEST_CASE("the recorder logs one load per plugin load, after the load delay", "[dbtool-gui][fixtures]")
{
    QTemporaryDir logDirectory;
    auto const loadLog = std::filesystem::path { logDirectory.path().toStdWString() } / "load.log";
    ScopedVariable const delay { "LIGHTWEIGHT_TEST_PLUGIN_LOAD_DELAY_MS", "200" };
    ScopedVariable const log { "LIGHTWEIGHT_TEST_PLUGIN_LOAD_LOG", loadLog.string() };

    QElapsedTimer elapsed;
    elapsed.start();
    LoadedFixture const fixture { DBTOOL_GUI_WORKFLOW_FIXTURE_RECORDER };

    CHECK(elapsed.elapsed() >= 200);
    CHECK(LineCount(loadLog) == 1);
}

TEST_CASE("the long step reports one sub-task per unit and fails at the requested unit", "[dbtool-gui][fixtures]")
{
    ScratchDatabase const database;
    ScopedVariable const duration { "LIGHTWEIGHT_TEST_LONG_STEP_MS", "150" };
    ScopedVariable const units { "LIGHTWEIGHT_TEST_LONG_STEP_UNITS", "3" };
    ScopedVariable const failAt { "LIGHTWEIGHT_TEST_LONG_STEP_FAIL_AT", "2" };
    LoadedFixture fixture { DBTOOL_GUI_WORKFLOW_FIXTURE_RECORDER };
    fixture.RunPostInitHooks();

    auto journal = RecordingProgressTask::Journal {};
    {
        RecordingProgressTask root { journal };
        CHECK_THROWS_AS(LoadedFixture::Manager().RunPreMigrateSteps(root, ExecutionMode::Execute), PreMigrateStepError);
    }

    CHECK(HasEvent(journal.events, "begin Long step/Unit 1 of 3"));
    CHECK(HasEvent(journal.events, "begin Long step/Unit 2 of 3"));
    CHECK_FALSE(HasEvent(journal.events, "begin Long step/Unit 3 of 3"));
    CHECK_FALSE(HasEvent(journal.events, "fail Long step/Unit 1 of 3"));
    CHECK(HasEvent(journal.events, "fail Long step/Unit 2 of 3 unit 2 failed on purpose"));
}

TEST_CASE("the recorder's requested post-init failure reaches the host as an error", "[dbtool-gui][fixtures]")
{
    ScratchDatabase const database;
    ScopedVariable const fail { "LIGHTWEIGHT_TEST_POST_INIT_FAIL", "1" };
    LoadedFixture const fixture { DBTOOL_GUI_WORKFLOW_FIXTURE_RECORDER };

    auto errors = std::vector<std::string> {};
    Lightweight::Tools::RunPluginPostInitHooks(fixture.plugins,
                                               LoadedFixture::Manager().GetDataMapper().Connection(),
                                               LoadedFixture::Manager(),
                                               [&errors](std::string_view message) { errors.emplace_back(message); });

    REQUIRE(errors.size() == 1);
    CHECK(errors[0].contains("post-init failure requested by LIGHTWEIGHT_TEST_POST_INIT_FAIL"));
}

TEST_CASE("the workflow fixture declares releases 1.0.0 and 2.0.0 over three reversible migrations",
          "[dbtool-gui][fixtures]")
{
    LoadedFixture const fixture { DBTOOL_GUI_WORKFLOW_FIXTURE_WORKFLOW };
    auto const& manager = LoadedFixture::Manager();

    auto const& releases = manager.GetAllReleases();
    REQUIRE(releases.size() == 2);
    CHECK(releases[0].version == "1.0.0");
    CHECK(releases[0].highestTimestamp.value == 20240101000002);
    CHECK(releases[1].version == "2.0.0");
    CHECK(releases[1].highestTimestamp.value == 20240101000003);

    auto const& migrations = manager.GetAllMigrations();
    REQUIRE(migrations.size() == 3);
    CHECK(std::ranges::all_of(migrations, [](auto const* migration) { return migration->HasDownImplementation(); }));
    CHECK(manager.GetMigrationsForRelease("1.0.0").size() == 2);
    CHECK(manager.GetMigrationsForRelease("2.0.0").size() == 1);
}

TEST_CASE("the unicode candidate step needs no backup on SQLite", "[dbtool-gui][fixtures]")
{
    ScratchDatabase const database;
    LoadedFixture fixture { DBTOOL_GUI_WORKFLOW_FIXTURE_UNICODE_CANDIDATE };
    fixture.RunPostInitHooks();
    auto& manager = LoadedFixture::Manager();

    auto const steps = manager.GetPreMigrateSteps();
    REQUIRE(steps.size() == 1);
    CHECK(steps[0].title == "Widen text columns");
    CHECK(steps[0].declaresBackupRequirement);
    CHECK_FALSE(manager.BackupRequirementFor({}).required);

    auto journal = RecordingProgressTask::Journal {};
    {
        RecordingProgressTask root { journal };
        CHECK(manager.RunPreMigrateSteps(root, ExecutionMode::Execute)
              == Lightweight::SqlMigration::PreMigrateStepsOutcome::Completed);
    }
    CHECK(HasEvent(journal.events, "log Widen text columns info No text columns to upgrade."));
}

TEST_CASE("the bulk fixture registers the requested number of migrations", "[dbtool-gui][fixtures]")
{
    ScopedVariable const count { "LIGHTWEIGHT_TEST_BULK_MIGRATIONS", "250" };
    LoadedFixture const fixture { DBTOOL_GUI_WORKFLOW_FIXTURE_BULK };

    CHECK(LoadedFixture::Manager().GetAllMigrations().size() == 250);
}
