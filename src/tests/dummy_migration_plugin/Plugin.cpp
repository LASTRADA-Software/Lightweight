// SPDX-License-Identifier: Apache-2.0

#include <Lightweight/SqlMigration.hpp>
#include <Lightweight/SqlQuery/Migrate.hpp>

#include <iostream>

using namespace Lightweight;

// Define the plugin entry point
LIGHTWEIGHT_MIGRATION_PLUGIN()

LIGHTWEIGHT_SQL_MIGRATION(20230101000000, "Initial Migration")
{
    plan.CreateTable("dummy_users")
        .PrimaryKey("id", SqlColumnTypeDefinitions::Integer())
        .Column("name", SqlColumnTypeDefinitions::Varchar(50));
}

struct Migration_20230102000000: public Lightweight::SqlMigration::MigrationBase
{
    Migration_20230102000000():
        Lightweight::SqlMigration::MigrationBase(Lightweight::SqlMigration::MigrationTimestamp { 20230102000000 },
                                                 "Add Email Column")
    {
    }

    void Up(Lightweight::SqlMigrationQueryBuilder& plan) const override
    {
        plan.AlterTable("dummy_users").AddColumn("email", SqlColumnTypeDefinitions::Varchar(100));
    }

    void Down(Lightweight::SqlMigrationQueryBuilder& plan) const override
    {
        plan.AlterTable("dummy_users").DropColumn("email");
    }

    [[nodiscard]] bool HasDownImplementation() const noexcept override
    {
        return true;
    }
};

static Migration_20230102000000 migration_20230102000000;
