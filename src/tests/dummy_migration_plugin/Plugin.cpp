// SPDX-License-Identifier: Apache-2.0

#include <Lightweight/SqlMigration.hpp>
#include <Lightweight/SqlQuery/Migrate.hpp>

using namespace Lightweight;

// Define the plugin entry point
LIGHTWEIGHT_MIGRATION_PLUGIN()

LIGHTWEIGHT_SQL_MIGRATION(20230101000000, "Initial Migration")
{
    plan.CreateTable("dummy_users")
        .PrimaryKey("id", SqlColumnTypeDefinitions::Integer())
        .Column("name", SqlColumnTypeDefinitions::Varchar(50));
}

// A reversible migration: both Up and Down are declared together as a
// single registration, so there is exactly one source of truth for the
// migration's behaviour.
static SqlMigration::Migration<20230102000000> const migration_20230102000000(
    "Add Email Column",
    [](SqlMigrationQueryBuilder& plan) {
        plan.AlterTable("dummy_users").AddColumn("email", SqlColumnTypeDefinitions::Varchar(100));
    },
    [](SqlMigrationQueryBuilder& plan) { plan.AlterTable("dummy_users").DropColumn("email"); });

// Declare a release marker for plugin 1. Releases expose a version -> highest-timestamp
// mapping that dbtool surfaces via `releases` and `rollback-to-release`.
LIGHTWEIGHT_SQL_RELEASE("1.0.0", 20230102000000);
