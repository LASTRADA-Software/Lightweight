// SPDX-License-Identifier: Apache-2.0

#include <Lightweight/SqlMigration.hpp>
#include <Lightweight/SqlQuery/Migrate.hpp>

using namespace Lightweight;

// Define the plugin entry point
LIGHTWEIGHT_MIGRATION_PLUGIN()

// A reversible migration from the second plugin. Both Up and Down are
// declared in a single registration (no separate out-of-line macro) so
// the migration has exactly one source of truth.
// Timestamp is later than the first plugin's migrations.
static SqlMigration::Migration<20230201000000> const migration_20230201000000(
    "Second Plugin Migration",
    [](SqlMigrationQueryBuilder& plan) {
        plan.CreateTable("plugin2_table")
            .PrimaryKey("id", SqlColumnTypeDefinitions::Integer())
            .Column("description", SqlColumnTypeDefinitions::Varchar(255));
    },
    [](SqlMigrationQueryBuilder& plan) { plan.DropTable("plugin2_table"); });

// Release marker for plugin 2's higher timestamp. dbtool merges releases from every plugin.
LIGHTWEIGHT_SQL_RELEASE("2.0.0", 20230201000000);
