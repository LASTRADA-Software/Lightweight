// SPDX-License-Identifier: Apache-2.0
//
// Fixture plugin for the post-migrate and post-init hooks. It registers no
// migrations; each hook appends one line per call to the file named by
// LIGHTWEIGHT_TEST_POST_MIGRATE_LOG / LIGHTWEIGHT_TEST_POST_INIT_LOG, so
// test_dbtool.py can count the calls per dbtool command. Without those
// variables the hooks do nothing, which keeps the plugin inert for every other
// test sharing the plugins directory. With LIGHTWEIGHT_TEST_PRE_MIGRATE_LOG set, its post-init hook
// registers the pre-migrate step "Recorder step", which appends one line per call to that file.

#include <Lightweight/SqlAdvisoryLock.hpp>
#include <Lightweight/SqlConnection.hpp>
#include <Lightweight/SqlMigration.hpp>
#include <Lightweight/SqlScopedLock.hpp>

#include <chrono>
#include <cstdlib>
#include <format>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
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

} // namespace

LIGHTWEIGHT_MIGRATION_PLUGIN()

LIGHTWEIGHT_MIGRATION_PLUGIN_POSTINIT(connection, manager)
{
    RegisterRecorderStep(manager);
    static_cast<void>(connection);
    auto const logPath = Environment("LIGHTWEIGHT_TEST_POST_INIT_LOG");
    if (logPath.empty())
        return;
    auto log = std::ofstream { std::string { logPath }, std::ios::app };
    log << std::format("post-init migrations={}\n", manager.GetAllMigrations().size());
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
