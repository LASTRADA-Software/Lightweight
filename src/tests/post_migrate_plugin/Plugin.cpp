// SPDX-License-Identifier: Apache-2.0
//
// Fixture plugin for the post-migrate and post-init hooks. It registers no
// migrations; each hook appends one line per call to the file named by
// LIGHTWEIGHT_TEST_POST_MIGRATE_LOG / LIGHTWEIGHT_TEST_POST_INIT_LOG, so
// test_dbtool.py can count the calls per dbtool command. Without those
// variables the hooks do nothing, which keeps the plugin inert for every other
// test sharing the plugins directory.

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

} // namespace

LIGHTWEIGHT_MIGRATION_PLUGIN()

LIGHTWEIGHT_MIGRATION_PLUGIN_POSTINIT(connection, manager)
{
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
