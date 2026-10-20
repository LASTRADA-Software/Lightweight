// SPDX-License-Identifier: Apache-2.0
//
// Fixture plugin with one migration that requires a backup on its own, outside any release. Its file
// name lacks the `Plugin` suffix, so plugin discovery skips it; tests opt in by copying.

#include <Lightweight/SqlMigration.hpp>
#include <Lightweight/SqlQuery/Migrate.hpp>

using namespace Lightweight;

LIGHTWEIGHT_MIGRATION_PLUGIN()

namespace
{

SqlMigration::Migration<20240401000000> const requiresBackupMigration(
    "Requires a backup",
    SqlMigration::MigrationMetadata { .requiresBackup = true },
    [](SqlMigrationQueryBuilder& plan) {
        using namespace SqlColumnTypeDefinitions;
        plan.CreateTable("requires_backup_fixture").PrimaryKey("id", Integer());
    },
    [](SqlMigrationQueryBuilder& plan) { plan.DropTable("requires_backup_fixture"); });

} // namespace
