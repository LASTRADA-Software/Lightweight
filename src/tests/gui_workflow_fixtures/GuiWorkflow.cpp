// SPDX-License-Identifier: Apache-2.0
//
// Fixture plugin for the dbtool-gui workflow tests: three reversible migrations in releases 1.0.0 and
// 2.0.0, so the tests can apply up to a release and roll back to one. Its file name lacks the `Plugin`
// suffix, so plugin discovery skips it; tests copy it under a `*Plugin` name into a scratch plugins directory.

#include <Lightweight/SqlMigration.hpp>
#include <Lightweight/SqlQuery/Migrate.hpp>

using namespace Lightweight;

LIGHTWEIGHT_MIGRATION_PLUGIN()

LIGHTWEIGHT_SQL_MIGRATION_REVERSIBLE(20240101000001, "Create workflow_a")
{
    using namespace SqlColumnTypeDefinitions;
    plan.CreateTable("workflow_a").PrimaryKey("id", Integer()).Column("name", NVarchar(40));
}

LIGHTWEIGHT_SQL_MIGRATION_DOWN(20240101000001)
{
    plan.DropTable("workflow_a");
}

LIGHTWEIGHT_SQL_MIGRATION_REVERSIBLE(20240101000002, "Create workflow_b")
{
    using namespace SqlColumnTypeDefinitions;
    plan.CreateTable("workflow_b").PrimaryKey("id", Integer()).Column("name", NVarchar(40));
}

LIGHTWEIGHT_SQL_MIGRATION_DOWN(20240101000002)
{
    plan.DropTable("workflow_b");
}

LIGHTWEIGHT_SQL_MIGRATION_REVERSIBLE(20240101000003, "Create workflow_c")
{
    using namespace SqlColumnTypeDefinitions;
    plan.CreateTable("workflow_c").PrimaryKey("id", Integer()).Column("name", NVarchar(40));
}

LIGHTWEIGHT_SQL_MIGRATION_DOWN(20240101000003)
{
    plan.DropTable("workflow_c");
}

LIGHTWEIGHT_SQL_RELEASE("1.0.0", 20240101000002);
LIGHTWEIGHT_SQL_RELEASE("2.0.0", 20240101000003);
