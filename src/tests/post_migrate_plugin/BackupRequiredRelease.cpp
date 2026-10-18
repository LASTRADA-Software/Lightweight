// SPDX-License-Identifier: Apache-2.0
//
// Fixture plugin with one migration in a release that requires a backup. Its file name lacks the
// `Plugin` suffix, so plugin discovery skips it; tests copy it under a `*Plugin` name into a scratch
// plugins directory to exercise the backup requirement.

#include <Lightweight/SqlMigration.hpp>
#include <Lightweight/SqlQuery/Migrate.hpp>

using namespace Lightweight;

LIGHTWEIGHT_MIGRATION_PLUGIN()

LIGHTWEIGHT_SQL_MIGRATION_REVERSIBLE(20240201000000, "Backup Required Fixture Migration")
{
    using namespace SqlColumnTypeDefinitions;
    plan.CreateTable("backup_required_fixture").PrimaryKey("id", Integer());
}

// Reversible, because the tests roll it back and apply it again.
LIGHTWEIGHT_SQL_MIGRATION_DOWN(20240201000000)
{
    plan.DropTable("backup_required_fixture");
}

LIGHTWEIGHT_SQL_RELEASE("3.0.0", 20240201000000, { .requiresBackup = true });
