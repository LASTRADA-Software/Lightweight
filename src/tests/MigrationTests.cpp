// SPDX-License-Identifier: Apache-2.0

#include "Utils.hpp"

#include <Lightweight/Lightweight.hpp>

#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace std::string_view_literals;

namespace std
{
std::ostream& operator<<(std::ostream& os, Lightweight::SqlMigration::MigrationTimestamp const& timestamp)
{
    return os << "MigrationTimestamp(" << timestamp.value << ")";
}
} // namespace std

using namespace Lightweight;

class SqlMigrationTestFixture: public SqlTestFixture
{
  public:
    SqlMigrationTestFixture():
        SqlTestFixture()
    {
        Lightweight::SqlMigration::MigrationManager::GetInstance().RemoveAllMigrations();
    }
    SqlMigrationTestFixture(SqlMigrationTestFixture&&) = delete;
    SqlMigrationTestFixture(SqlMigrationTestFixture const&) = delete;
    SqlMigrationTestFixture& operator=(SqlMigrationTestFixture&&) = delete;
    SqlMigrationTestFixture& operator=(SqlMigrationTestFixture const&) = delete;

    ~SqlMigrationTestFixture() override
    {
        Lightweight::SqlMigration::MigrationManager::GetInstance().CloseDataMapper();
    }
};

// This is how a migration could look like
LIGHTWEIGHT_SQL_MIGRATION(20170816112233, "create users") // NOLINT(bugprone-throwing-static-initialization)
{
    using namespace Lightweight::SqlColumnTypeDefinitions;

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

    CHECK(migrationManager.ApplyPendingMigrations() == 1);

    CHECK(migrationManager.GetPending().empty());
    CHECK(migrationManager.GetAppliedMigrationIds().size() == 1);

    appliedIds = migrationManager.GetAppliedMigrationIds();
    CAPTURE(appliedIds);
    CHECK(appliedIds.size() == 1);
    CHECK(appliedIds.at(0).value == 2017'08'16'11'22'33);

    auto conn = SqlConnection {};
    SqlMigrationQueryBuilder builder = SqlQueryBuilder(conn.QueryFormatter()).Migration();
    migration.Up(builder);
    auto const sqlQueryString = builder.GetPlan().ToSql();

    CAPTURE(sqlQueryString);
    CHECK(migration.GetTimestamp().value == 2017'08'16'11'22'33);
    CHECK(migration.GetTitle() == "create users");

    // Call again, should be no-op
    CHECK(migrationManager.ApplyPendingMigrations() == 0);
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

    auto& migrationManager = SqlMigration::MigrationManager::GetInstance();

    migrationManager.CreateMigrationHistory();

    CHECK(migrationManager.GetAllMigrations().size() == 1);

    auto transaction = migrationManager.Transaction();
    CHECK(migrationManager.GetPending().size() == 1);
    CHECK(migrationManager.ApplyPendingMigrations() == 1);
    CHECK(migrationManager.GetPending().empty());
}

// #include <Lightweight/DataMapper/DataMapper.hpp>
namespace FKTests
{
struct Order;

struct Person
{
    Field<int64_t, PrimaryKey::AutoAssign> id;
    Field<SqlString<50>> name;
    Field<SqlString<100>> email;
    Field<std::optional<SqlString<100>>> password;
    Field<SqlDateTime> created_at = SqlDateTime::Now();
    Field<SqlDateTime> updated_at = SqlDateTime::Now();
    // HasMany<Order> orders;

    static constexpr auto TableName = "persons";
};

struct Order
{
    Field<int64_t, PrimaryKey::AutoAssign> id;
    BelongsTo<Member(Person::id), SqlRealName { "person_id" }> person;
    Field<SqlDateTime> created_at = SqlDateTime::Now();
    Field<SqlDateTime> updated_at = SqlDateTime::Now();

    static constexpr auto TableName = "orders";
};
} // namespace FKTests

TEST_CASE_METHOD(SqlMigrationTestFixture, "Migration with foreign key", "[SqlMigration]")
{
    using namespace SqlColumnTypeDefinitions;

    // clang-format off
    auto createPersonMigration = SqlMigration::Migration(
        SqlMigration::MigrationTimestamp { 202412102211 },
        "create persons",
        [](SqlMigrationQueryBuilder& plan) {
            plan.CreateTable("persons")
                .PrimaryKey("id", Bigint())
                .RequiredColumn("name", Varchar(50)).Unique().Index()
                .RequiredColumn("email", Varchar(100)).Unique().Index()
                .Column("password", Varchar(100))
                .Timestamps();
        }
    );

    auto createOrderMigration = SqlMigration::Migration(
        SqlMigration::MigrationTimestamp { 202412102212 },
        "create orders",
        [](SqlMigrationQueryBuilder& plan) {
            plan.CreateTable("orders")
                .PrimaryKey("id", Bigint())
                .ForeignKey("person_id", Bigint(), SqlForeignKeyReferenceDefinition { .tableName = "persons", .columnName = "id" })
                .Timestamps();
        }
    );
    // clang-format on

    auto& migrationManager = SqlMigration::MigrationManager::GetInstance();
    auto& dm = migrationManager.GetDataMapper();
    migrationManager.CreateMigrationHistory();
    migrationManager.ApplyPendingMigrations();

    auto person = FKTests::Person {};
    person.name = "John Doe";
    person.email = "john@doe.com";
    dm.Create(person);

    auto order = FKTests::Order {};
    order.person = person;
    dm.Create(order);
}

TEST_CASE_METHOD(SqlMigrationTestFixture, "Revert Migration", "[SqlMigration]")
{
    using namespace SqlColumnTypeDefinitions;

    auto reversibleMigration = SqlMigration::Migration(
        SqlMigration::MigrationTimestamp { 202412102213 },
        "reversible migration",
        [](SqlMigrationQueryBuilder& plan) { plan.CreateTable("reversible_table").PrimaryKey("id", Integer()); },
        [](SqlMigrationQueryBuilder& plan) { plan.DropTable("reversible_table"); });

    auto& migrationManager = SqlMigration::MigrationManager::GetInstance();
    migrationManager.CreateMigrationHistory();

    // Apply
    CHECK(migrationManager.ApplyPendingMigrations() == 1);

    // Verify Applied
    auto appliedIds = migrationManager.GetAppliedMigrationIds();
    CHECK(appliedIds.size() == 1);
    CHECK(appliedIds[0].value == 202412102213);

    // Verify Side Effect
    {
        auto conn = SqlConnection {};
        auto stmt = SqlStatement { conn };
        // Should not throw
        stmt.ExecuteDirect("SELECT count(*) FROM reversible_table");
    }

    // Revert
    migrationManager.RevertSingleMigration(reversibleMigration);

    // Verify Reverted
    appliedIds = migrationManager.GetAppliedMigrationIds();
    CHECK(appliedIds.empty());

    // Verify Side Effect Reverted
    {
        auto conn = SqlConnection {};
        auto stmt = SqlStatement { conn };
        auto const _ = ScopedSqlNullLogger {};
        // Should throw because table does not exist
        CHECK_THROWS_AS(stmt.ExecuteDirect("SELECT count(*) FROM reversible_table"), SqlException);
    }
}

TEST_CASE_METHOD(SqlMigrationTestFixture, "Transaction Rollback", "[SqlMigration]")
{
    using namespace SqlColumnTypeDefinitions;

    auto badMigration = SqlMigration::Migration(
        SqlMigration::MigrationTimestamp { 202412102214 }, "bad migration", [](SqlMigrationQueryBuilder& plan) {
            plan.CreateTable("should_not_exist").PrimaryKey("id", Integer());

            plan.RawSql("THIS_IS_INVALID_SQL_TO_FORCE_FAILURE");
        });

    auto& migrationManager = SqlMigration::MigrationManager::GetInstance();
    migrationManager.CreateMigrationHistory();

    // Apply should fail
    {
        auto const _ = ScopedSqlNullLogger {}; // suppress the error message, we are testing for it
        CHECK_THROWS(migrationManager.ApplyPendingMigrations());
    }

    // Verify Not Applied
    auto appliedIds = migrationManager.GetAppliedMigrationIds();
    CHECK(appliedIds.empty());

    // Verify Side Effect Rolled Back
    {
        auto conn = SqlConnection {};
        auto stmt = SqlStatement { conn };
        auto const _ = ScopedSqlNullLogger {};
        // Should throw because table should have been rolled back
        CHECK_THROWS_AS(stmt.ExecuteDirect("SELECT count(*) FROM should_not_exist"), SqlException);
    }
}

TEST_CASE_METHOD(SqlMigrationTestFixture, "Raw SQL Migration", "[SqlMigration]")
{
    auto rawSqlMigration = SqlMigration::Migration(
        SqlMigration::MigrationTimestamp { 202412102215 },
        "raw sql migration",
        [](SqlMigrationQueryBuilder& plan) {
            plan.RawSql("CREATE TABLE raw_sql_test (id INT PRIMARY KEY)");
            plan.RawSql("INSERT INTO raw_sql_test (id) VALUES (42)");
        },
        [](SqlMigrationQueryBuilder& plan) { plan.RawSql("DROP TABLE raw_sql_test"); });

    auto& migrationManager = SqlMigration::MigrationManager::GetInstance();
    migrationManager.CreateMigrationHistory();

    // Apply
    CHECK(migrationManager.ApplyPendingMigrations() == 1);

    // Check
    {
        auto conn = SqlConnection {};
        auto stmt = SqlStatement { conn };
        // Verify using QueryBuilder to ensure portability
        stmt.ExecuteDirect("SELECT id FROM raw_sql_test WHERE id = 42");
        CHECK(stmt.FetchRow());
        CHECK(stmt.GetColumn<int>(1) == 42);
    }

    // Revert
    migrationManager.RevertSingleMigration(rawSqlMigration);

    // Check Revert
    {
        auto conn = SqlConnection {};
        auto stmt = SqlStatement { conn };
        {
            auto const _ = ScopedSqlNullLogger {}; // suppress the error message, we are testing for it
            CHECK_THROWS_AS(stmt.ExecuteDirect("SELECT * FROM raw_sql_test"), SqlException);
        }
    }
}

TEST_CASE_METHOD(SqlMigrationTestFixture, "Duplicate Timestamp Prevention", "[SqlMigration]")
{
    // First migration should succeed
    auto migration1 = SqlMigration::Migration(SqlMigration::MigrationTimestamp { 202501230001 },
                                              "First migration",
                                              [](SqlMigrationQueryBuilder& plan) { plan.RawSql("SELECT 1"); });

    // Second migration with same timestamp should throw
    CHECK_THROWS_AS(SqlMigration::Migration(SqlMigration::MigrationTimestamp { 202501230001 },
                                            "Duplicate migration",
                                            [](SqlMigrationQueryBuilder& plan) { plan.RawSql("SELECT 2"); }),
                    std::runtime_error);
}

TEST_CASE_METHOD(SqlMigrationTestFixture, "Dry Run Migration", "[SqlMigration]")
{
    using namespace SqlColumnTypeDefinitions;

    auto migration = SqlMigration::Migration(
        SqlMigration::MigrationTimestamp { 202501230002 }, "Dry run test", [](SqlMigrationQueryBuilder& plan) {
            plan.CreateTable("dry_run_test").PrimaryKey("id", Integer());
        });

    auto& manager = SqlMigration::MigrationManager::GetInstance();
    manager.CreateMigrationHistory();

    // Preview should return SQL statements without executing
    auto statements = manager.PreviewPendingMigrations();
    CHECK(!statements.empty());

    // Table should NOT exist (dry run only - we only previewed)
    {
        auto conn = SqlConnection {};
        auto stmt = SqlStatement { conn };
        auto const _ = ScopedSqlNullLogger {};
        CHECK_THROWS_AS(stmt.ExecuteDirect("SELECT * FROM dry_run_test"), SqlException);
    }
}

TEST_CASE_METHOD(SqlMigrationTestFixture, "Checksum Computation", "[SqlMigration]")
{
    using namespace SqlColumnTypeDefinitions;

    auto migration = SqlMigration::Migration(
        SqlMigration::MigrationTimestamp { 202501230003 }, "Checksum test", [](SqlMigrationQueryBuilder& plan) {
            plan.CreateTable("checksum_test").PrimaryKey("id", Integer());
        });

    auto& dm = SqlMigration::MigrationManager::GetInstance().GetDataMapper();
    auto checksum1 = migration.ComputeChecksum(dm.Connection().QueryFormatter());
    auto checksum2 = migration.ComputeChecksum(dm.Connection().QueryFormatter());

    // Checksum should be deterministic
    CHECK(checksum1 == checksum2);

    // SHA-256 hex should be 64 characters
    CHECK(checksum1.length() == 64);

    // Checksum should be alphanumeric hex
    CHECK(std::ranges::all_of(checksum1, [](char c) { return std::isxdigit(c); }));
}

TEST_CASE_METHOD(SqlMigrationTestFixture, "Checksum Stored on Migration", "[SqlMigration]")
{
    using namespace SqlColumnTypeDefinitions;

    // Drop old schema_migrations table to ensure we have the new schema with checksum column
    {
        auto conn = SqlConnection {};
        auto stmt = SqlStatement { conn };
        auto const _ = ScopedSqlNullLogger {};
        try
        {
            stmt.ExecuteDirect("DROP TABLE schema_migrations");
        }
        catch (SqlException const&)
        {
            // Table may not exist, ignore
        }
    }

    auto migration = SqlMigration::Migration(
        SqlMigration::MigrationTimestamp { 202501230004 }, "Checksum storage test", [](SqlMigrationQueryBuilder& plan) {
            plan.CreateTable("checksum_storage_test").PrimaryKey("id", Integer());
        });

    auto& manager = SqlMigration::MigrationManager::GetInstance();
    manager.CreateMigrationHistory();

    // Apply the migration
    CHECK(manager.ApplyPendingMigrations() == 1);

    // Verify checksums - should return empty since all should match
    auto mismatches = manager.VerifyChecksums();
    CHECK(mismatches.empty());
}

TEST_CASE_METHOD(SqlMigrationTestFixture, "HasDownImplementation", "[SqlMigration]")
{
    using namespace SqlColumnTypeDefinitions;

    // Migration with Down() should return true
    auto migrationWithDown = SqlMigration::Migration(
        SqlMigration::MigrationTimestamp { 202501240001 },
        "with down",
        [](SqlMigrationQueryBuilder& plan) { plan.CreateTable("with_down_test").PrimaryKey("id", Integer()); },
        [](SqlMigrationQueryBuilder& plan) { plan.DropTable("with_down_test"); });

    CHECK(migrationWithDown.HasDownImplementation() == true);

    // Migration without Down() should return false
    auto migrationWithoutDown = SqlMigration::Migration(
        SqlMigration::MigrationTimestamp { 202501240002 }, "without down", [](SqlMigrationQueryBuilder& plan) {
            plan.CreateTable("without_down_test").PrimaryKey("id", Integer());
        });

    CHECK(migrationWithoutDown.HasDownImplementation() == false);
}

TEST_CASE_METHOD(SqlMigrationTestFixture, "RevertSingleMigration throws without Down", "[SqlMigration]")
{
    using namespace SqlColumnTypeDefinitions;

    auto migrationWithoutDown = SqlMigration::Migration(
        SqlMigration::MigrationTimestamp { 202501240003 }, "no down migration", [](SqlMigrationQueryBuilder& plan) {
            plan.CreateTable("no_down_test").PrimaryKey("id", Integer());
        });

    auto& manager = SqlMigration::MigrationManager::GetInstance();
    manager.CreateMigrationHistory();

    // Apply the migration first
    CHECK(manager.ApplyPendingMigrations() == 1);

    // Attempt to revert should throw with descriptive message
    {
        auto const _ = ScopedSqlNullLogger {};
        CHECK_THROWS_AS(manager.RevertSingleMigration(migrationWithoutDown), std::runtime_error);
    }

    // Migration should still be in applied list
    auto appliedIds = manager.GetAppliedMigrationIds();
    CHECK(appliedIds.size() == 1);
}

TEST_CASE_METHOD(SqlMigrationTestFixture, "MarkMigrationAsApplied", "[SqlMigration]")
{
    using namespace SqlColumnTypeDefinitions;

    auto migration = SqlMigration::Migration(
        SqlMigration::MigrationTimestamp { 202501240004 }, "mark applied test", [](SqlMigrationQueryBuilder& plan) {
            plan.CreateTable("mark_applied_test").PrimaryKey("id", Integer());
        });

    auto& manager = SqlMigration::MigrationManager::GetInstance();
    manager.CreateMigrationHistory();

    // Mark as applied without executing
    manager.MarkMigrationAsApplied(migration);

    // Should appear in applied list
    auto appliedIds = manager.GetAppliedMigrationIds();
    CHECK(appliedIds.size() == 1);
    CHECK(appliedIds[0].value == 202501240004);

    // Table should NOT exist (we didn't execute, just marked)
    {
        auto conn = SqlConnection {};
        auto stmt = SqlStatement { conn };
        auto const _ = ScopedSqlNullLogger {};
        CHECK_THROWS_AS(stmt.ExecuteDirect("SELECT * FROM mark_applied_test"), SqlException);
    }
}

TEST_CASE_METHOD(SqlMigrationTestFixture, "MarkMigrationAsApplied duplicate throws", "[SqlMigration]")
{
    using namespace SqlColumnTypeDefinitions;

    auto migration = SqlMigration::Migration(
        SqlMigration::MigrationTimestamp { 202501240005 },
        "duplicate mark test",
        [](SqlMigrationQueryBuilder& plan) { plan.CreateTable("dup_mark_test").PrimaryKey("id", Integer()); },
        [](SqlMigrationQueryBuilder& plan) { plan.DropTable("dup_mark_test"); });

    auto& manager = SqlMigration::MigrationManager::GetInstance();
    manager.CreateMigrationHistory();

    // Actually apply the migration
    CHECK(manager.ApplyPendingMigrations() == 1);

    // Attempting to mark as applied again should throw
    CHECK_THROWS_AS(manager.MarkMigrationAsApplied(migration), std::runtime_error);
}

TEST_CASE_METHOD(SqlMigrationTestFixture, "RevertToMigration", "[SqlMigration]")
{
    using namespace SqlColumnTypeDefinitions;

    auto migration1 = SqlMigration::Migration(
        SqlMigration::MigrationTimestamp { 202501240010 },
        "migration 1",
        [](SqlMigrationQueryBuilder& plan) { plan.CreateTable("revert_to_1").PrimaryKey("id", Integer()); },
        [](SqlMigrationQueryBuilder& plan) { plan.DropTable("revert_to_1"); });

    auto migration2 = SqlMigration::Migration(
        SqlMigration::MigrationTimestamp { 202501240011 },
        "migration 2",
        [](SqlMigrationQueryBuilder& plan) { plan.CreateTable("revert_to_2").PrimaryKey("id", Integer()); },
        [](SqlMigrationQueryBuilder& plan) { plan.DropTable("revert_to_2"); });

    auto migration3 = SqlMigration::Migration(
        SqlMigration::MigrationTimestamp { 202501240012 },
        "migration 3",
        [](SqlMigrationQueryBuilder& plan) { plan.CreateTable("revert_to_3").PrimaryKey("id", Integer()); },
        [](SqlMigrationQueryBuilder& plan) { plan.DropTable("revert_to_3"); });

    auto& manager = SqlMigration::MigrationManager::GetInstance();
    manager.CreateMigrationHistory();

    // Apply all 3 migrations
    CHECK(manager.ApplyPendingMigrations() == 3);
    CHECK(manager.GetAppliedMigrationIds().size() == 3);

    // Revert to migration 1 (should revert migrations 2 and 3)
    auto result = manager.RevertToMigration(SqlMigration::MigrationTimestamp { 202501240010 });

    CHECK(result.revertedTimestamps.size() == 2);
    CHECK(!result.failedAt.has_value());
    CHECK(result.errorMessage.empty());

    // Should now have only 1 applied migration
    auto appliedIds = manager.GetAppliedMigrationIds();
    CHECK(appliedIds.size() == 1);
    CHECK(appliedIds[0].value == 202501240010);

    // Tables 2 and 3 should not exist
    {
        auto conn = SqlConnection {};
        auto stmt = SqlStatement { conn };
        auto const _ = ScopedSqlNullLogger {};
        CHECK_THROWS_AS(stmt.ExecuteDirect("SELECT * FROM revert_to_2"), SqlException);
        CHECK_THROWS_AS(stmt.ExecuteDirect("SELECT * FROM revert_to_3"), SqlException);
    }

    // Table 1 should still exist
    {
        auto conn = SqlConnection {};
        auto stmt = SqlStatement { conn };
        stmt.ExecuteDirect("SELECT * FROM revert_to_1");
    }
}

TEST_CASE_METHOD(SqlMigrationTestFixture, "GetMigrationStatus", "[SqlMigration]")
{
    using namespace SqlColumnTypeDefinitions;

    auto migration1 = SqlMigration::Migration(
        SqlMigration::MigrationTimestamp { 202501240020 },
        "status test 1",
        [](SqlMigrationQueryBuilder& plan) { plan.CreateTable("status_test_1").PrimaryKey("id", Integer()); },
        [](SqlMigrationQueryBuilder& plan) { plan.DropTable("status_test_1"); });

    auto migration2 = SqlMigration::Migration(
        SqlMigration::MigrationTimestamp { 202501240021 },
        "status test 2",
        [](SqlMigrationQueryBuilder& plan) { plan.CreateTable("status_test_2").PrimaryKey("id", Integer()); },
        [](SqlMigrationQueryBuilder& plan) { plan.DropTable("status_test_2"); });

    auto& manager = SqlMigration::MigrationManager::GetInstance();
    manager.CreateMigrationHistory();

    // Initial status - all pending
    auto status = manager.GetMigrationStatus();
    CHECK(status.totalRegistered == 2);
    CHECK(status.appliedCount == 0);
    CHECK(status.pendingCount == 2);
    CHECK(status.mismatchCount == 0);
    CHECK(status.unknownAppliedCount == 0);

    // Apply first migration
    manager.ApplySingleMigration(migration1);

    status = manager.GetMigrationStatus();
    CHECK(status.totalRegistered == 2);
    CHECK(status.appliedCount == 1);
    CHECK(status.pendingCount == 1);
    CHECK(status.mismatchCount == 0);

    // Apply second migration
    manager.ApplySingleMigration(migration2);

    status = manager.GetMigrationStatus();
    CHECK(status.totalRegistered == 2);
    CHECK(status.appliedCount == 2);
    CHECK(status.pendingCount == 0);
    CHECK(status.mismatchCount == 0);
}

TEST_CASE_METHOD(SqlMigrationTestFixture, "CreateTableIfNotExists", "[SqlMigration]")
{
    using namespace SqlColumnTypeDefinitions;

    auto migration1 = SqlMigration::Migration(
        SqlMigration::MigrationTimestamp { 202501240030 },
        "create table if not exists",
        [](SqlMigrationQueryBuilder& plan) {
            plan.CreateTableIfNotExists("idempotent_table").PrimaryKey("id", Integer()).RequiredColumn("name", Varchar(50));
        },
        [](SqlMigrationQueryBuilder& plan) { plan.DropTableIfExists("idempotent_table"); });

    auto& manager = SqlMigration::MigrationManager::GetInstance();
    manager.CreateMigrationHistory();

    // Apply first time - should create table
    CHECK(manager.ApplyPendingMigrations() == 1);

    // Manually create a second migration that also tries to create the same table
    auto migration2 = SqlMigration::Migration(
        SqlMigration::MigrationTimestamp { 202501240031 },
        "create same table again",
        [](SqlMigrationQueryBuilder& plan) {
            plan.CreateTableIfNotExists("idempotent_table").PrimaryKey("id", Integer()).RequiredColumn("name", Varchar(50));
        },
        [](SqlMigrationQueryBuilder& plan) { plan.DropTableIfExists("idempotent_table"); });

    // Should not throw - IF NOT EXISTS handles existing table
    CHECK_NOTHROW(manager.ApplyPendingMigrations());

    // Table should exist
    {
        auto conn = SqlConnection {};
        auto stmt = SqlStatement { conn };
        stmt.ExecuteDirect("SELECT * FROM idempotent_table");
    }
}

TEST_CASE_METHOD(SqlMigrationTestFixture, "DropIndexIfExists", "[SqlMigration]")
{
    using namespace SqlColumnTypeDefinitions;

    auto createMigration = SqlMigration::Migration(
        SqlMigration::MigrationTimestamp { 202501240040 },
        "create with index",
        [](SqlMigrationQueryBuilder& plan) {
            plan.CreateTable("drop_index_test").PrimaryKey("id", Integer()).RequiredColumn("name", Varchar(50)).Index();
        },
        [](SqlMigrationQueryBuilder& plan) { plan.DropTable("drop_index_test"); });

    auto& manager = SqlMigration::MigrationManager::GetInstance();
    manager.CreateMigrationHistory();

    // Create table with index
    CHECK(manager.ApplyPendingMigrations() == 1);

    auto dropIndexMigration = SqlMigration::Migration(
        SqlMigration::MigrationTimestamp { 202501240041 },
        "drop index if exists",
        [](SqlMigrationQueryBuilder& plan) { plan.AlterTable("drop_index_test").DropIndexIfExists("name"); },
        [](SqlMigrationQueryBuilder& plan) { plan.AlterTable("drop_index_test").AddIndex("name"); });

    // First drop should succeed
    CHECK(manager.ApplyPendingMigrations() == 1);

    auto dropIndexAgainMigration = SqlMigration::Migration(
        SqlMigration::MigrationTimestamp { 202501240042 }, "drop index if exists again", [](SqlMigrationQueryBuilder& plan) {
            plan.AlterTable("drop_index_test").DropIndexIfExists("name");
        });

    // Second drop should not throw - IF EXISTS handles non-existent index
    CHECK_NOTHROW(manager.ApplyPendingMigrations());
}
