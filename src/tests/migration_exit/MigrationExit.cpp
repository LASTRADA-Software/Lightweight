// SPDX-License-Identifier: Apache-2.0

// Regression test for https://github.com/LASTRADA-Software/Lightweight/issues/649.
//
// A process that applies migrations and then returns from main() must exit cleanly. The
// MigrationManager singleton is constructed during static initialisation (by the migration below),
// owns a DataMapper, and so closes SQL statements and a connection during static destruction, after
// main() returned. Those destructors report to the SQL logger, which must therefore still be alive.
//
// The test passes iff this process exits with status 0; it aborted with "pure virtual method called"
// before the fix.

#include <Lightweight/SqlConnection.hpp>
#include <Lightweight/SqlMigration.hpp>
#include <Lightweight/SqlQuery/Migrate.hpp>

#include <cstdlib>
#include <exception>
#include <print>
#include <span>
#include <string_view>

using namespace Lightweight;

LIGHTWEIGHT_SQL_MIGRATION(20260101000000, "Create migration_exit_table")
{
    plan.CreateTable("migration_exit_table").PrimaryKey("id", SqlColumnTypeDefinitions::Integer());
}

int main(int argc, char const* argv[])
{
    auto const args = std::span(argv, static_cast<std::size_t>(argc));
    if (args.size() != 2)
    {
        std::println(stderr, "Usage: {} <ODBC connection string>", args[0]);
        return EXIT_FAILURE;
    }

    try
    {
        SqlConnection::SetDefaultConnectionString(SqlConnectionString { std::string(args[1]) });

        auto& manager = SqlMigration::MigrationManager::GetInstance();
        manager.CreateMigrationHistory();
        manager.ApplyPendingMigrations();

        // A rerun applies nothing, but CreateMigrationHistory() has still opened the manager's DataMapper.
        // Deliberately no CloseDataMapper(): the manager's DataMapper is torn down after main().
    }
    catch (std::exception const& ex)
    {
        std::println(stderr, "Migration failed: {}", ex.what());
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
