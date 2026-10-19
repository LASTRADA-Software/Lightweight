// SPDX-License-Identifier: Apache-2.0
//
// Fixture plugin that registers the way an application's migration plugin may: a static
// initializer installs a compat policy on the migration manager, migrations register
// themselves, and a post-init hook logs through the manager. The policy is a
// `std::function` whose target lives in this library, so the host must drop it before
// unloading the plugin. Its file name lacks the `Plugin` suffix, so plugin discovery
// skips it; test_dbtool.py copies it under a `*Plugin` name into a scratch directory.

#include <Lightweight/SqlMigration.hpp>
#include <Lightweight/SqlQuery/Migrate.hpp>
#include <Lightweight/SqlQuery/MigrationPlan.hpp>

#include <format>
#include <set>
#include <string>

using namespace Lightweight;

namespace
{

/// Marks every migration of this fixture with a compat flag.
std::set<std::string> FixtureCompatPolicy(SqlMigration::MigrationBase const& migration)
{
    static_cast<void>(migration);
    return { std::string { CompatFlagLupTruncateName } };
}

/// Installs `FixtureCompatPolicy` when the library is loaded.
struct PolicyInstaller
{
    PolicyInstaller()
    {
        SqlMigration::MigrationManager::GetInstance().SetCompatPolicy(&FixtureCompatPolicy);
    }
};

[[maybe_unused]] PolicyInstaller const installPolicy {};

} // namespace

LIGHTWEIGHT_MIGRATION_PLUGIN()

LIGHTWEIGHT_MIGRATION_PLUGIN_POSTINIT(connection, manager)
{
    static_cast<void>(connection);
    manager.Log(std::format("Compat policy fixture: {} migrations", manager.GetAllMigrations().size()));
}

LIGHTWEIGHT_SQL_MIGRATION(20250101000000, "Compat Fixture Create Table")
{
    plan.CreateTable("compat_fixture").PrimaryKey("id", SqlColumnTypeDefinitions::Integer());
}

LIGHTWEIGHT_SQL_MIGRATION(20250101000001, "Compat Fixture Add Column")
{
    plan.AlterTable("compat_fixture").AddColumn("name", SqlColumnTypeDefinitions::Varchar(50));
}

LIGHTWEIGHT_SQL_MIGRATION(20250101000002, "Compat Fixture Add Second Column")
{
    plan.AlterTable("compat_fixture").AddColumn("note", SqlColumnTypeDefinitions::Varchar(50));
}

LIGHTWEIGHT_SQL_RELEASE("1.0.0", 20250101000002);
