// SPDX-License-Identifier: Apache-2.0
//
// Fixture plugin for the post-migrate and post-init hooks. It registers no
// migrations; each hook appends one line per call to the file named by
// LIGHTWEIGHT_TEST_POST_MIGRATE_LOG / LIGHTWEIGHT_TEST_POST_INIT_LOG, so
// test_dbtool.py can count the calls per dbtool command. Without those
// variables the hooks do nothing, which keeps the plugin inert for every other
// test sharing the plugins directory. With LIGHTWEIGHT_TEST_PRE_MIGRATE_LOG set, its post-init hook
// registers the pre-migrate step "Recorder step", which appends one line per call to that file.
// The LIGHTWEIGHT_TEST_PLUGIN_LOAD_*, LIGHTWEIGHT_TEST_POST_INIT_* and LIGHTWEIGHT_TEST_LONG_STEP_*
// variables slow down, log or fail the plugin load, the post-init hook and a long sub-tasked step.

#include <Lightweight/SqlAdvisoryLock.hpp>
#include <Lightweight/SqlConnection.hpp>
#include <Lightweight/SqlMigration.hpp>
#include <Lightweight/SqlScopedLock.hpp>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <fstream>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>

#if defined(_MSC_VER)
    // C4996 flags getenv as unsafe; the plugin only reads test-harness variables.
    #pragma warning(disable : 4996)
#endif

using namespace Lightweight;

namespace
{

/// Reads an environment variable; empty when unset.
std::string_view Environment(char const* name)
{
    auto const* const value = std::getenv(name);
    return value != nullptr ? std::string_view { value } : std::string_view {};
}

/// Reads an environment variable as a non-negative integer; @p fallback when unset or not a number.
int EnvironmentNumber(char const* name, int fallback = 0)
{
    auto const text = Environment(name);
    auto value = 0;
    auto const [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc {} && end == text.data() + text.size() && value >= 0 ? value : fallback;
}

/// Sleeps for the milliseconds the environment variable @p name holds, if any.
void SleepFor(char const* name)
{
    std::this_thread::sleep_for(std::chrono::milliseconds { EnvironmentNumber(name) });
}

/// Appends @p line to the file the environment variable @p name names, if any.
void AppendLine(char const* name, std::string_view line)
{
    auto const path = Environment(name);
    if (path.empty())
        return;
    auto log = std::ofstream { std::string { path }, std::ios::app };
    log << line << '\n';
}

/// Probes the migration lock from a second connection: "held" when someone else holds it,
/// "free" when nobody does, and "error: <message>" when the probe itself failed.
std::string MigrationLockState(SqlConnection const& connection)
{
    auto probeConnection = SqlConnection { connection.ConnectionString() };
    auto const probe =
        SqlScopedLock::TryConstruct(probeConnection, "lightweight_migration", std::chrono::milliseconds { 100 });
    if (probe.has_value())
        return "free";
    if (probe.error().reason == SqlLockFailureReason::Timeout)
        return "held";
    return "error: " + probe.error().message;
}

/// Registers "Recorder step" when LIGHTWEIGHT_TEST_PRE_MIGRATE_LOG names a file: it logs its execution
/// mode, the pending count and the lock state, needs a backup with
/// LIGHTWEIGHT_TEST_PRE_MIGRATE_REQUIRES_BACKUP and throws with LIGHTWEIGHT_TEST_PRE_MIGRATE_FAIL.
void RegisterRecorderStep(SqlMigration::MigrationManager& manager)
{
    auto logPath = std::string { Environment("LIGHTWEIGHT_TEST_PRE_MIGRATE_LOG") };
    if (logPath.empty())
        return;
    auto options = SqlMigration::PreMigrateStepOptions {
        .requiresBackup =
            [](SqlConnection&, SqlMigration::MigrationManager&) {
                SleepFor("LIGHTWEIGHT_TEST_PRE_MIGRATE_REQUIRES_BACKUP_DELAY_MS");
                return !Environment("LIGHTWEIGHT_TEST_PRE_MIGRATE_REQUIRES_BACKUP").empty();
            },
    };
    manager.AddPreMigrateStep("Recorder step",
                              std::move(options),
                              [logPath = std::move(logPath)](SqlConnection& connection,
                                                             SqlMigration::MigrationManager& stepManager,
                                                             ProgressTask& progress,
                                                             SqlMigration::ExecutionMode mode) {
                                  progress.Advance(0, "recording");
                                  {
                                      auto log = std::ofstream { logPath, std::ios::app };
                                      log << std::format("pre-migrate dryRun={} pending={} lock={}\n",
                                                         mode == SqlMigration::ExecutionMode::DryRun ? 1 : 0,
                                                         stepManager.GetPending().size(),
                                                         MigrationLockState(connection));
                                  }
                                  if (!Environment("LIGHTWEIGHT_TEST_PRE_MIGRATE_FAIL").empty())
                                      throw std::runtime_error(
                                          "pre-migrate failure requested by LIGHTWEIGHT_TEST_PRE_MIGRATE_FAIL");
                              });
}

/// Registers "Long step" when LIGHTWEIGHT_TEST_LONG_STEP_MS is set: LIGHTWEIGHT_TEST_LONG_STEP_UNITS
/// sub-tasks (4 when unset) that share that time in 50 ms slices and stop early on cancellation; the
/// unit LIGHTWEIGHT_TEST_LONG_STEP_FAIL_AT fails its sub-task and throws.
void RegisterLongStep(SqlMigration::MigrationManager& manager)
{
    if (Environment("LIGHTWEIGHT_TEST_LONG_STEP_MS").empty())
        return;
    auto const unitCount = std::max(1, EnvironmentNumber("LIGHTWEIGHT_TEST_LONG_STEP_UNITS", 4));
    auto const unitDuration = std::chrono::milliseconds { EnvironmentNumber("LIGHTWEIGHT_TEST_LONG_STEP_MS") / unitCount };
    auto const failingUnit = EnvironmentNumber("LIGHTWEIGHT_TEST_LONG_STEP_FAIL_AT");
    manager.AddPreMigrateStep(
        "Long step",
        {},
        [unitCount, unitDuration, failingUnit](
            SqlConnection&, SqlMigration::MigrationManager&, ProgressTask& progress, SqlMigration::ExecutionMode) {
            constexpr auto slice = std::chrono::milliseconds { 50 };
            progress.SetTotal(static_cast<std::uint64_t>(unitCount));
            for (auto const unit: std::views::iota(1, unitCount + 1))
            {
                auto const unitTask = progress.BeginSubTask(std::format("Unit {} of {}", unit, unitCount),
                                                            static_cast<std::uint64_t>(unitDuration.count()));
                for (auto remaining = unitDuration; remaining > std::chrono::milliseconds::zero(); remaining -= slice)
                {
                    if (progress.IsCancellationRequested())
                        return;
                    auto const pause = std::min(slice, remaining);
                    std::this_thread::sleep_for(pause);
                    unitTask->Advance(static_cast<std::uint64_t>(pause.count()));
                }
                if (unit == failingUnit)
                {
                    auto const reason = std::format("unit {} failed on purpose", unit);
                    unitTask->Fail(reason);
                    throw std::runtime_error(reason);
                }
                progress.Advance(1);
            }
        });
}

} // namespace

// Written out instead of LIGHTWEIGHT_MIGRATION_PLUGIN() so a test can slow down and count plugin loads.
extern "C" LIGHTWEIGHT_EXPORT Lightweight::SqlMigration::MigrationManager* AcquireMigrationManager()
{
    AppendLine("LIGHTWEIGHT_TEST_PLUGIN_LOAD_BEGIN_LOG", "begin");
    SleepFor("LIGHTWEIGHT_TEST_PLUGIN_LOAD_DELAY_MS");
    AppendLine("LIGHTWEIGHT_TEST_PLUGIN_LOAD_LOG", "load");
    return &SqlMigration::MigrationManager::GetInstance();
}

LIGHTWEIGHT_MIGRATION_PLUGIN_POSTINIT(connection, manager)
{
    SleepFor("LIGHTWEIGHT_TEST_POST_INIT_DELAY_MS");
    RegisterRecorderStep(manager);
    RegisterLongStep(manager);
    static_cast<void>(connection);
    AppendLine("LIGHTWEIGHT_TEST_POST_INIT_LOG", std::format("post-init migrations={}", manager.GetAllMigrations().size()));
    if (!Environment("LIGHTWEIGHT_TEST_POST_INIT_FAIL").empty())
        throw std::runtime_error("post-init failure requested by LIGHTWEIGHT_TEST_POST_INIT_FAIL");
}

LIGHTWEIGHT_MIGRATION_PLUGIN_POSTMIGRATE(connection, manager)
{
    auto const logPath = Environment("LIGHTWEIGHT_TEST_POST_MIGRATE_LOG");
    if (logPath.empty())
        return;

    {
        auto log = std::ofstream { std::string { logPath }, std::ios::app };
        log << std::format("post-migrate pending={} lock={}\n", manager.GetPending().size(), MigrationLockState(connection));
    }

    if (!Environment("LIGHTWEIGHT_TEST_POST_MIGRATE_FAIL").empty())
        throw std::runtime_error("post-migrate failure requested by LIGHTWEIGHT_TEST_POST_MIGRATE_FAIL");
}
