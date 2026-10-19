// SPDX-License-Identifier: Apache-2.0
//
// Fixture plugin whose seed migration uses `Insert(...).IfNotExists(...)`. Its file name
// lacks the `Plugin` suffix, so plugin discovery skips it; test_dbtool.py copies it under
// a `*Plugin` name into a scratch plugins directory to check how skipped rows are reported.

#include <Lightweight/SqlMigration.hpp>
#include <Lightweight/SqlQuery/Migrate.hpp>

using namespace Lightweight;

LIGHTWEIGHT_MIGRATION_PLUGIN()

LIGHTWEIGHT_SQL_MIGRATION(20240301000000, "Create Categories")
{
    using namespace SqlColumnTypeDefinitions;
    plan.CreateTable("Categories").PrimaryKey("Id", Integer()).RequiredColumn("Name", NVarchar(50));
}

static SqlMigration::Migration<20240301000001> const seedRowsMigration(
    "Seed Categories",
    [](SqlMigrationQueryBuilder& plan) {
        plan.Insert("Categories").Set("Id", 100).Set("Name", "Books").IfNotExists({ "Id" });
        plan.Insert("Categories").Set("Id", 200).Set("Name", "Grüße").IfNotExists({ "Id" });
        plan.Insert("Categories").Set("Id", 201).Set("Name", "Toys").IfNotExists({ "Id" });
    },
    [](SqlMigrationQueryBuilder& plan) { plan.Delete("Categories").Where("Id", ">=", 100); });
