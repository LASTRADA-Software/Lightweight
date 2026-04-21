// SPDX-License-Identifier: Apache-2.0

#include <Lightweight/SqlMigration.hpp>
#include <Lightweight/SqlQuery/Migrate.hpp>

using namespace Lightweight;

// Define the plugin entry point
LIGHTWEIGHT_MIGRATION_PLUGIN()

// A migration from the second plugin.
// Timestamp is later than the first plugin's migrations.
LIGHTWEIGHT_SQL_MIGRATION_REVERSIBLE(20230201000000, "Second Plugin Migration")
{
    plan.CreateTable("plugin2_table")
        .PrimaryKey("id", SqlColumnTypeDefinitions::Integer())
        .Column("description", SqlColumnTypeDefinitions::Varchar(255));
}

LIGHTWEIGHT_SQL_MIGRATION_DOWN(20230201000000)
{
    plan.DropTable("plugin2_table");
}

// Release marker for plugin 2's higher timestamp. dbtool merges releases from every plugin.
LIGHTWEIGHT_SQL_RELEASE("2.0.0", 20230201000000);
