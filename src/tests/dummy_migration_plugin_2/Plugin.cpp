// SPDX-License-Identifier: Apache-2.0

#include <Lightweight/SqlMigration.hpp>
#include <Lightweight/SqlQuery/Migrate.hpp>

using namespace Lightweight;

// Define the plugin entry point
LIGHTWEIGHT_MIGRATION_PLUGIN()

// A migration from the second plugin.
// Timestamp is later than the first plugin's migrations.
LIGHTWEIGHT_SQL_MIGRATION(20230201000000, "Second Plugin Migration")
{
    plan.CreateTable("plugin2_table")
        .PrimaryKey("id", SqlColumnTypeDefinitions::Integer())
        .Column("description", SqlColumnTypeDefinitions::Varchar(255));
}
