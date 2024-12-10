// SPDX-License-Identifier: Apache-2.0

#include "Utils.hpp"

#include <Lightweight/SqlConnection.hpp>
#include <Lightweight/SqlMigration.hpp>

#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cstdlib>

using namespace std::string_view_literals;

namespace std
{
LIGHTWEIGHT_API std::ostream& operator<<(std::ostream& os, SqlMigration::MigrationTimestamp const& timestamp)
{
    return os << "MigrationTimestamp(" << timestamp.value << ")";
}
}

class SqlMigrationTestFixture: public SqlTestFixture
{
  public:
    SqlMigrationTestFixture():
        SqlTestFixture()
    {
        SqlMigration::MigrationManager::GetInstance().RemoveAllMigrations();
    }
};

// This is how a migration could look like
LIGHTWEIGHT_SQL_MIGRATION(20170816112233, "create users")
{
    using namespace SqlColumnTypeDefinitions;

    // clang-format off
    plan.CreateTable("users")
        .PrimaryKey("id", Guid())
        .RequiredColumn("name", Varchar(50)).Unique().Index()
        .RequiredColumn("email", Varchar(100)).Unique().Index()
        .Column("password", Varchar(100))
        .Timestamps();

    plan.AlterTable("users").AddColumn("age", Integer());
    plan.AlterTable("users").AddColumn("is_active", Bool());
    // clang-format on
}

TEST_CASE_METHOD(SqlMigrationTestFixture, "access global migration macro", "[SqlMigration]")
{
    SqlMigration::MigrationBase const& migration = LIGHTWEIGHT_MIGRATION_INSTANCE(20170816112233);
    auto& migrationManager = SqlMigration::MigrationManager::GetInstance();

    // Explicitly adding the migration to the manager, as in the test fixture, the manager is cleared before each test
    migrationManager.AddMigration(&migration);

    // migrationManager.ApplyPendingMigrations();
    migrationManager.CreateMigrationHistory();
    auto appliedIds = migrationManager.GetAppliedMigrationIds();
    CAPTURE(appliedIds);
    CHECK(appliedIds.empty());

    migrationManager.ApplySingleMigration(migration.GetTimestamp());

    appliedIds = migrationManager.GetAppliedMigrationIds();
    CAPTURE(appliedIds);
    CHECK(appliedIds.size() == 1);
    CHECK(appliedIds.at(0).value == 2017'08'16'11'22'33);

    // auto conn = SqlConnection {};
    // SqlMigrationQueryBuilder builder = SqlQueryBuilder(conn.QueryFormatter()).Migration();
    // migration.Execute(builder);
    // auto const sqlQueryString = builder.GetPlan().ToSql();
    //
    // CAPTURE(sqlQueryString);
    // CHECK(migration.GetTimestamp().value == 2017'08'16'11'22'33);
    // CHECK(migration.GetTitle() == "create users");
}

TEST_CASE_METHOD(SqlMigrationTestFixture, "CreateTable", "[SqlMigration]")
{
    using namespace SqlColumnTypeDefinitions;

    // clang-format off
    auto createTablesMigration = SqlMigration::Migration(
        SqlMigration::MigrationTimestamp { 202412102211 },
        "description here", 
        [](SqlMigrationQueryBuilder& plan) {
            plan.CreateTable("users")
                .PrimaryKey("id", Guid())
                .RequiredColumn("name", Varchar(50)).Unique().Index()
                .RequiredColumn("email", Varchar(100)).Unique().Index()
                .Column("password", Varchar(100))
                .Timestamps();
        }
    );
    // clang-format on

    CHECK(SqlMigration::MigrationManager::GetInstance().GetAllMigrations().size() == 1);
}
