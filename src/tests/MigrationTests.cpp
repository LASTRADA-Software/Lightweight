// SPDX-License-Identifier: Apache-2.0

#include "Utils.hpp"

#include <Lightweight/Lightweight.hpp>
#include <Lightweight/QueryFormatter/SqlServerFormatter.hpp>
#include <Lightweight/SqlScopedLock.hpp>

#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <filesystem>
#include <fstream>
#include <streambuf>

#include <CodeGen/SplitFileWriter.hpp>

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
        Lightweight::SqlMigration::MigrationManager::GetInstance().RemoveAllReleases();
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
    auto createTablesMigration = SqlMigration::Migration<202412102211>(
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
    auto createPersonMigration = SqlMigration::Migration<202412102211>(
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

    auto createOrderMigration = SqlMigration::Migration<202412102212>(
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

    auto reversibleMigration = SqlMigration::Migration<202412102213>(
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
        (void) stmt.ExecuteDirect("SELECT count(*) FROM reversible_table");
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
        CHECK_THROWS_AS((void) stmt.ExecuteDirect("SELECT count(*) FROM reversible_table"), SqlException);
    }
}

TEST_CASE_METHOD(SqlMigrationTestFixture, "Transaction Rollback", "[SqlMigration]")
{
    using namespace SqlColumnTypeDefinitions;

    auto badMigration = SqlMigration::Migration<202412102214>("bad migration", [](SqlMigrationQueryBuilder& plan) {
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
        CHECK_THROWS_AS((void) stmt.ExecuteDirect("SELECT count(*) FROM should_not_exist"), SqlException);
    }
}

TEST_CASE_METHOD(SqlMigrationTestFixture, "MigrationException carries structured context", "[SqlMigration]")
{
    using namespace SqlColumnTypeDefinitions;

    // Apply path: second step is deliberately bogus; the first CreateTable
    // statement succeeds, so the failure surfaces at step 1 with the exact
    // invalid SQL script we supplied.
    auto failingMigration = SqlMigration::Migration<202412102220>("failing migration", [](SqlMigrationQueryBuilder& plan) {
        plan.CreateTable("should_not_exist_ctx").PrimaryKey("id", Integer());
        plan.RawSql("THIS_IS_INVALID_SQL_FOR_DIAG");
    });

    auto& migrationManager = SqlMigration::MigrationManager::GetInstance();
    migrationManager.CreateMigrationHistory();

    ScopedSqlNullLogger const nullLogger; // suppress the expected error message
    (void) nullLogger;
    try
    {
        migrationManager.ApplySingleMigration(failingMigration);
        FAIL("expected MigrationException");
    }
    catch (SqlMigration::MigrationException const& ex)
    {
        CHECK(ex.GetOperation() == SqlMigration::MigrationException::Operation::Apply);
        CHECK(ex.GetMigrationTimestamp().value == 202412102220);
        CHECK(ex.GetMigrationTitle() == "failing migration");
        CHECK(ex.GetStepIndex() == 1);
        CHECK(ex.GetFailedSql().contains("THIS_IS_INVALID_SQL_FOR_DIAG"));
        CHECK_FALSE(ex.GetDriverMessage().empty());
        // Base-class accessors keep working for catch(SqlException) callers.
        CHECK(ex.info().message.contains("failing migration"));
    }
}

TEST_CASE_METHOD(SqlMigrationTestFixture, "Raw SQL Migration", "[SqlMigration]")
{
    auto rawSqlMigration = SqlMigration::Migration<202412102215>(
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
        auto cursor = stmt.ExecuteDirect("SELECT id FROM raw_sql_test WHERE id = 42");
        CHECK(cursor.FetchRow());
        CHECK(cursor.GetColumn<int>(1) == 42);
    }

    // Revert
    migrationManager.RevertSingleMigration(rawSqlMigration);

    // Check Revert
    {
        auto conn = SqlConnection {};
        auto stmt = SqlStatement { conn };
        {
            auto const _ = ScopedSqlNullLogger {}; // suppress the error message, we are testing for it
            CHECK_THROWS_AS((void) stmt.ExecuteDirect("SELECT * FROM raw_sql_test"), SqlException);
        }
    }
}

TEST_CASE_METHOD(SqlMigrationTestFixture, "Duplicate Timestamp Prevention", "[SqlMigration]")
{
    // First migration should succeed
    auto migration1 = SqlMigration::Migration<202501230001>("First migration",
                                                            [](SqlMigrationQueryBuilder& plan) { plan.RawSql("SELECT 1"); });

    // Second migration with same timestamp should throw
    CHECK_THROWS_AS(SqlMigration::Migration<202501230001>("Duplicate migration",
                                                          [](SqlMigrationQueryBuilder& plan) { plan.RawSql("SELECT 2"); }),
                    std::runtime_error);
}

TEST_CASE_METHOD(SqlMigrationTestFixture, "Dry Run Migration", "[SqlMigration]")
{
    using namespace SqlColumnTypeDefinitions;

    auto migration = SqlMigration::Migration<202501230002>("Dry run test", [](SqlMigrationQueryBuilder& plan) {
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
        CHECK_THROWS_AS((void) stmt.ExecuteDirect("SELECT * FROM dry_run_test"), SqlException);
    }
}

TEST_CASE_METHOD(SqlMigrationTestFixture, "Checksum Computation", "[SqlMigration]")
{
    using namespace SqlColumnTypeDefinitions;

    auto migration = SqlMigration::Migration<202501230003>("Checksum test", [](SqlMigrationQueryBuilder& plan) {
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
            (void) stmt.ExecuteDirect("DROP TABLE schema_migrations");
        }
        // NOLINTNEXTLINE(bugprone-empty-catch) - Table may not exist, intentionally ignoring
        catch (SqlException const&)
        {
        }
    }

    auto migration = SqlMigration::Migration<202501230004>("Checksum storage test", [](SqlMigrationQueryBuilder& plan) {
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
    auto migrationWithDown = SqlMigration::Migration<202501240001>(
        "with down",
        [](SqlMigrationQueryBuilder& plan) { plan.CreateTable("with_down_test").PrimaryKey("id", Integer()); },
        [](SqlMigrationQueryBuilder& plan) { plan.DropTable("with_down_test"); });

    CHECK(migrationWithDown.HasDownImplementation() == true);

    // Migration without Down() should return false
    auto migrationWithoutDown = SqlMigration::Migration<202501240002>("without down", [](SqlMigrationQueryBuilder& plan) {
        plan.CreateTable("without_down_test").PrimaryKey("id", Integer());
    });

    CHECK(migrationWithoutDown.HasDownImplementation() == false);
}

TEST_CASE_METHOD(SqlMigrationTestFixture, "RevertSingleMigration throws without Down", "[SqlMigration]")
{
    using namespace SqlColumnTypeDefinitions;

    auto migrationWithoutDown =
        SqlMigration::Migration<202501240003>("no down migration", [](SqlMigrationQueryBuilder& plan) {
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

    auto migration = SqlMigration::Migration<202501240004>("mark applied test", [](SqlMigrationQueryBuilder& plan) {
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
        CHECK_THROWS_AS((void) stmt.ExecuteDirect("SELECT * FROM mark_applied_test"), SqlException);
    }
}

TEST_CASE_METHOD(SqlMigrationTestFixture, "MarkMigrationAsApplied duplicate throws", "[SqlMigration]")
{
    using namespace SqlColumnTypeDefinitions;

    auto migration = SqlMigration::Migration<202501240005>(
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

    auto migration1 = SqlMigration::Migration<202501240010>(
        "migration 1",
        [](SqlMigrationQueryBuilder& plan) { plan.CreateTable("revert_to_1").PrimaryKey("id", Integer()); },
        [](SqlMigrationQueryBuilder& plan) { plan.DropTable("revert_to_1"); });

    auto migration2 = SqlMigration::Migration<202501240011>(
        "migration 2",
        [](SqlMigrationQueryBuilder& plan) { plan.CreateTable("revert_to_2").PrimaryKey("id", Integer()); },
        [](SqlMigrationQueryBuilder& plan) { plan.DropTable("revert_to_2"); });

    auto migration3 = SqlMigration::Migration<202501240012>(
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
        CHECK_THROWS_AS((void) stmt.ExecuteDirect("SELECT * FROM revert_to_2"), SqlException);
        CHECK_THROWS_AS((void) stmt.ExecuteDirect("SELECT * FROM revert_to_3"), SqlException);
    }

    // Table 1 should still exist
    {
        auto conn = SqlConnection {};
        auto stmt = SqlStatement { conn };
        (void) stmt.ExecuteDirect("SELECT * FROM revert_to_1");
    }
}

TEST_CASE_METHOD(SqlMigrationTestFixture, "GetMigrationStatus", "[SqlMigration]")
{
    using namespace SqlColumnTypeDefinitions;

    auto migration1 = SqlMigration::Migration<202501240020>(
        "status test 1",
        [](SqlMigrationQueryBuilder& plan) { plan.CreateTable("status_test_1").PrimaryKey("id", Integer()); },
        [](SqlMigrationQueryBuilder& plan) { plan.DropTable("status_test_1"); });

    auto migration2 = SqlMigration::Migration<202501240021>(
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

    auto migration1 = SqlMigration::Migration<202501240030>(
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
    auto migration2 = SqlMigration::Migration<202501240031>(
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
        (void) stmt.ExecuteDirect("SELECT * FROM idempotent_table");
    }
}

TEST_CASE_METHOD(SqlMigrationTestFixture, "Migration dependencies applied in declared order", "[SqlMigration]")
{
    using namespace SqlColumnTypeDefinitions;

    // migration_b depends on migration_a but is declared with an earlier-sounding description.
    // Despite declaration order, apply should respect the dependency.
    auto migrationA = SqlMigration::Migration<202601010001>(
        "dep base", [](SqlMigrationQueryBuilder& plan) { plan.CreateTable("dep_a").PrimaryKey("id", Integer()); });

    auto migrationB = SqlMigration::Migration<202601010002>(
        "dep dependent",
        SqlMigration::MigrationMetadata { .dependencies = { SqlMigration::TimestampOf<decltype(migrationA)> } },
        [](SqlMigrationQueryBuilder& plan) {
            plan.CreateTable("dep_b")
                .PrimaryKey("id", Integer())
                .ForeignKey(
                    "a_id", Integer(), SqlForeignKeyReferenceDefinition { .tableName = "dep_a", .columnName = "id" });
        });

    auto& manager = SqlMigration::MigrationManager::GetInstance();
    manager.CreateMigrationHistory();

    CHECK_NOTHROW(manager.ApplyPendingMigrations());

    auto const appliedIds = manager.GetAppliedMigrationIds();
    CHECK(appliedIds.size() == 2);
    CHECK(appliedIds[0] == SqlMigration::TimestampOf<decltype(migrationA)>);
    CHECK(appliedIds[1] == SqlMigration::TimestampOf<decltype(migrationB)>);
}

TEST_CASE_METHOD(SqlMigrationTestFixture, "Migration dependency on unknown timestamp throws", "[SqlMigration]")
{
    using namespace SqlColumnTypeDefinitions;

    constexpr auto tsUnknown = SqlMigration::MigrationTimestamp { 999999999999 };

    auto migration = SqlMigration::Migration<202601020001>(
        "orphan dep", SqlMigration::MigrationMetadata { .dependencies = { tsUnknown } }, [](SqlMigrationQueryBuilder& plan) {
            plan.CreateTable("dep_orphan").PrimaryKey("id", Integer());
        });

    auto& manager = SqlMigration::MigrationManager::GetInstance();
    manager.CreateMigrationHistory();

    auto const _ = ScopedSqlNullLogger {};
    CHECK_THROWS_AS(manager.ApplyPendingMigrations(), std::runtime_error);
    CHECK_THROWS_AS(manager.ValidateDependencies(), std::runtime_error);
}

TEST_CASE_METHOD(SqlMigrationTestFixture, "Migration dependency cycle detected", "[SqlMigration]")
{
    using namespace SqlColumnTypeDefinitions;

    // Forward-declare each migration's type so the mutually-referential dependency lists
    // can use SqlMigrationTimestampOf on the opposite migration's type.
    using MigrationA = SqlMigration::Migration<202601030001>;
    using MigrationB = SqlMigration::Migration<202601030002>;

    auto migrationA =
        MigrationA("cycle a",
                   SqlMigration::MigrationMetadata { .dependencies = { SqlMigration::TimestampOf<MigrationB> } },
                   [](SqlMigrationQueryBuilder& plan) { plan.CreateTable("cycle_a").PrimaryKey("id", Integer()); });

    auto migrationB =
        MigrationB("cycle b",
                   SqlMigration::MigrationMetadata { .dependencies = { SqlMigration::TimestampOf<MigrationA> } },
                   [](SqlMigrationQueryBuilder& plan) { plan.CreateTable("cycle_b").PrimaryKey("id", Integer()); });

    auto& manager = SqlMigration::MigrationManager::GetInstance();
    manager.CreateMigrationHistory();

    auto const _ = ScopedSqlNullLogger {};
    CHECK_THROWS_AS(manager.ValidateDependencies(), std::runtime_error);
}

TEST_CASE_METHOD(SqlMigrationTestFixture, "Migration dependency already applied is satisfied", "[SqlMigration]")
{
    using namespace SqlColumnTypeDefinitions;

    auto base = SqlMigration::Migration<202601040001>(
        "base", [](SqlMigrationQueryBuilder& plan) { plan.CreateTable("dep_base2").PrimaryKey("id", Integer()); });

    auto& manager = SqlMigration::MigrationManager::GetInstance();
    manager.CreateMigrationHistory();
    manager.ApplySingleMigration(base);
    CHECK(manager.GetAppliedMigrationIds().size() == 1);

    auto follow = SqlMigration::Migration<202601040002>(
        "depends on base",
        SqlMigration::MigrationMetadata { .dependencies = { SqlMigration::TimestampOf<decltype(base)> } },
        [](SqlMigrationQueryBuilder& plan) { plan.CreateTable("dep_follow").PrimaryKey("id", Integer()); });

    CHECK_NOTHROW(manager.ApplySingleMigration(follow));
    CHECK(manager.GetAppliedMigrationIds().size() == 2);
}

TEST_CASE_METHOD(SqlMigrationTestFixture,
                 "Migration dependency not applied via ApplySingleMigration throws",
                 "[SqlMigration]")
{
    using namespace SqlColumnTypeDefinitions;

    auto base = SqlMigration::Migration<202601050001>("unapplied base", [](SqlMigrationQueryBuilder& plan) {
        plan.CreateTable("dep_b_base").PrimaryKey("id", Integer());
    });

    auto follow = SqlMigration::Migration<202601050002>(
        "follow requires unapplied",
        SqlMigration::MigrationMetadata { .dependencies = { SqlMigration::TimestampOf<decltype(base)> } },
        [](SqlMigrationQueryBuilder& plan) { plan.CreateTable("dep_b_follow").PrimaryKey("id", Integer()); });

    auto& manager = SqlMigration::MigrationManager::GetInstance();
    manager.CreateMigrationHistory();

    auto const _ = ScopedSqlNullLogger {};
    CHECK_THROWS_AS(manager.ApplySingleMigration(follow), std::runtime_error);
}

TEST_CASE_METHOD(SqlMigrationTestFixture, "Audit metadata recorded on apply", "[SqlMigration]")
{
    using namespace SqlColumnTypeDefinitions;

    // Start from a fresh schema_migrations table so the new audit columns exist.
    {
        auto conn = SqlConnection {};
        auto stmt = SqlStatement { conn };
        auto const _ = ScopedSqlNullLogger {};
        try
        {
            (void) stmt.ExecuteDirect("DROP TABLE schema_migrations");
        }
        catch (SqlException const&) // NOLINT(bugprone-empty-catch)
        {
        }
    }

    auto migration = SqlMigration::Migration<202601060001>(
        "audited",
        SqlMigration::MigrationMetadata { .author = "Ada Lovelace", .description = "First documented migration." },
        [](SqlMigrationQueryBuilder& plan) { plan.CreateTable("audited_table").PrimaryKey("id", Integer()); });

    auto& manager = SqlMigration::MigrationManager::GetInstance();
    manager.CreateMigrationHistory();
    manager.ApplySingleMigration(migration);

    auto conn = SqlConnection {};
    auto stmt = SqlStatement { conn };
    auto cursor = stmt.ExecuteDirect(
        "SELECT author, description, execution_duration_ms FROM schema_migrations WHERE version = 202601060001");
    CHECK(cursor.FetchRow());

    auto const author = cursor.GetColumn<std::optional<std::string>>(1);
    auto const description = cursor.GetColumn<std::optional<std::string>>(2);
    auto const duration = cursor.GetColumn<std::optional<int64_t>>(3);

    CHECK(author.has_value());
    CHECK(author.value_or("") == "Ada Lovelace");
    CHECK(description.has_value());
    CHECK(description.value_or("") == "First documented migration.");
    CHECK(duration.has_value());
    CHECK(duration.value_or(-1) >= 0);
}

TEST_CASE_METHOD(SqlMigrationTestFixture, "Audit metadata null when unspecified", "[SqlMigration]")
{
    using namespace SqlColumnTypeDefinitions;

    {
        auto conn = SqlConnection {};
        auto stmt = SqlStatement { conn };
        auto const _ = ScopedSqlNullLogger {};
        try
        {
            (void) stmt.ExecuteDirect("DROP TABLE schema_migrations");
        }
        catch (SqlException const&) // NOLINT(bugprone-empty-catch)
        {
        }
    }

    auto migration = SqlMigration::Migration<202601060002>(
        "plain", [](SqlMigrationQueryBuilder& plan) { plan.CreateTable("plain_table").PrimaryKey("id", Integer()); });

    auto& manager = SqlMigration::MigrationManager::GetInstance();
    manager.CreateMigrationHistory();
    manager.ApplySingleMigration(migration);

    auto conn = SqlConnection {};
    auto stmt = SqlStatement { conn };
    auto cursor = stmt.ExecuteDirect("SELECT author, description FROM schema_migrations WHERE version = 202601060002");
    CHECK(cursor.FetchRow());
    CHECK(!cursor.GetColumn<std::optional<std::string>>(1).has_value());
    CHECK(!cursor.GetColumn<std::optional<std::string>>(2).has_value());
}

TEST_CASE_METHOD(SqlMigrationTestFixture, "Audit metadata duration null when MarkAsApplied", "[SqlMigration]")
{
    using namespace SqlColumnTypeDefinitions;

    {
        auto conn = SqlConnection {};
        auto stmt = SqlStatement { conn };
        auto const _ = ScopedSqlNullLogger {};
        try
        {
            (void) stmt.ExecuteDirect("DROP TABLE schema_migrations");
        }
        catch (SqlException const&) // NOLINT(bugprone-empty-catch)
        {
        }
    }

    auto migration = SqlMigration::Migration<202601060003>(
        "marked", [](SqlMigrationQueryBuilder& plan) { plan.CreateTable("marked_only").PrimaryKey("id", Integer()); });

    auto& manager = SqlMigration::MigrationManager::GetInstance();
    manager.CreateMigrationHistory();
    manager.MarkMigrationAsApplied(migration);

    auto conn = SqlConnection {};
    auto stmt = SqlStatement { conn };
    auto cursor = stmt.ExecuteDirect("SELECT execution_duration_ms FROM schema_migrations WHERE version = 202601060003");
    CHECK(cursor.FetchRow());
    CHECK(!cursor.GetColumn<std::optional<int64_t>>(1).has_value());
}

TEST_CASE_METHOD(SqlMigrationTestFixture, "AddColumnIfNotExists idempotent", "[SqlMigration]")
{
    using namespace SqlColumnTypeDefinitions;

    auto create =
        SqlMigration::Migration<202601070001>("create table for idempotent add", [](SqlMigrationQueryBuilder& plan) {
            plan.CreateTable("idempotent_cols").PrimaryKey("id", Integer()).RequiredColumn("name", Varchar(50));
        });

    auto addOnce = SqlMigration::Migration<202601070002>("add column once", [](SqlMigrationQueryBuilder& plan) {
        plan.AlterTable("idempotent_cols").AddColumnIfNotExists("note", Varchar(100));
    });

    auto addAgain = SqlMigration::Migration<202601070003>("add column again", [](SqlMigrationQueryBuilder& plan) {
        plan.AlterTable("idempotent_cols").AddColumnIfNotExists("note", Varchar(100));
    });

    auto& manager = SqlMigration::MigrationManager::GetInstance();
    manager.CreateMigrationHistory();

    CHECK_NOTHROW(manager.ApplyPendingMigrations());
    CHECK(manager.GetAppliedMigrationIds().size() == 3);

    // Column should be usable.
    auto conn = SqlConnection {};
    auto stmt = SqlStatement { conn };
    (void) stmt.ExecuteDirect(R"(INSERT INTO idempotent_cols (id, name, note) VALUES (1, 'x', 'y'))");
}

TEST_CASE_METHOD(SqlMigrationTestFixture, "DropColumnIfExists idempotent", "[SqlMigration]")
{
    using namespace SqlColumnTypeDefinitions;

    auto create = SqlMigration::Migration<202601080001>("create for idempotent drop", [](SqlMigrationQueryBuilder& plan) {
        plan.CreateTable("idempotent_drop").PrimaryKey("id", Integer()).Column("removeme", Varchar(50));
    });

    auto dropOnce = SqlMigration::Migration<202601080002>("drop column once", [](SqlMigrationQueryBuilder& plan) {
        plan.AlterTable("idempotent_drop").DropColumnIfExists("removeme");
    });

    auto dropAgain = SqlMigration::Migration<202601080003>("drop column again", [](SqlMigrationQueryBuilder& plan) {
        plan.AlterTable("idempotent_drop").DropColumnIfExists("removeme");
    });

    auto& manager = SqlMigration::MigrationManager::GetInstance();
    manager.CreateMigrationHistory();

    CHECK_NOTHROW(manager.ApplyPendingMigrations());
    CHECK(manager.GetAppliedMigrationIds().size() == 3);
}

TEST_CASE_METHOD(SqlMigrationTestFixture, "DropIndexIfExists", "[SqlMigration]")
{
    using namespace SqlColumnTypeDefinitions;

    auto createMigration = SqlMigration::Migration<202501240040>(
        "create with index",
        [](SqlMigrationQueryBuilder& plan) {
            plan.CreateTable("drop_index_test").PrimaryKey("id", Integer()).RequiredColumn("name", Varchar(50)).Index();
        },
        [](SqlMigrationQueryBuilder& plan) { plan.DropTable("drop_index_test"); });

    auto& manager = SqlMigration::MigrationManager::GetInstance();
    manager.CreateMigrationHistory();

    // Create table with index
    CHECK(manager.ApplyPendingMigrations() == 1);

    auto dropIndexMigration = SqlMigration::Migration<202501240041>(
        "drop index if exists",
        [](SqlMigrationQueryBuilder& plan) { plan.AlterTable("drop_index_test").DropIndexIfExists("name"); },
        [](SqlMigrationQueryBuilder& plan) { plan.AlterTable("drop_index_test").AddIndex("name"); });

    // First drop should succeed
    CHECK(manager.ApplyPendingMigrations() == 1);

    auto dropIndexAgainMigration =
        SqlMigration::Migration<202501240042>("drop index if exists again", [](SqlMigrationQueryBuilder& plan) {
            plan.AlterTable("drop_index_test").DropIndexIfExists("name");
        });

    // Second drop should not throw - IF EXISTS handles non-existent index
    CHECK_NOTHROW(manager.ApplyPendingMigrations());
}

TEST_CASE_METHOD(SqlMigrationTestFixture, "AlterTable AddForeignKey rebuilds SQLite table", "[SqlMigration]")
{
    using namespace SqlColumnTypeDefinitions;

    // AddForeignKey via ALTER TABLE is a SQLite-specific code path (formatter emits a
    // sentinel, executor rebuilds the table). Other dialects ALTER in place.
    auto conn = SqlConnection {};
    auto skipStmt = SqlStatement { conn };
    UNSUPPORTED_DATABASE(skipStmt, SqlServerType::MICROSOFT_SQL);
    UNSUPPORTED_DATABASE(skipStmt, SqlServerType::POSTGRESQL);
    UNSUPPORTED_DATABASE(skipStmt, SqlServerType::MYSQL);

    auto createParent = SqlMigration::Migration<202601010001>(
        "create parent",
        [](SqlMigrationQueryBuilder& plan) {
            plan.CreateTable("Parent").PrimaryKey("id", Integer()).RequiredColumn("name", Varchar(50));
        },
        [](SqlMigrationQueryBuilder& plan) { plan.DropTable("Parent"); });

    auto createChild = SqlMigration::Migration<202601010002>(
        "create child",
        [](SqlMigrationQueryBuilder& plan) {
            plan.CreateTable("Child").PrimaryKey("id", Integer()).RequiredColumn("label", Varchar(50));
        },
        [](SqlMigrationQueryBuilder& plan) { plan.DropTable("Child"); });

    auto& manager = SqlMigration::MigrationManager::GetInstance();
    manager.CreateMigrationHistory();
    CHECK(manager.ApplyPendingMigrations() == 2);

    // Seed a row that must survive the table rebuild.
    {
        auto stmt = SqlStatement { conn };
        (void) stmt.ExecuteDirect(R"(INSERT INTO "Parent" ("id", "name") VALUES (1, 'anchor'))");
        (void) stmt.ExecuteDirect(R"(INSERT INTO "Child" ("id", "label") VALUES (10, 'row-a'))");
    }

    auto addFk = SqlMigration::Migration<202601010003>(
        "add fk column + fk",
        [](SqlMigrationQueryBuilder& plan) {
            plan.AlterTable("Child").AddNotRequiredColumn("parent_id", Integer());
            plan.AlterTable("Child").AddForeignKey(
                "parent_id", SqlForeignKeyReferenceDefinition { .tableName = "Parent", .columnName = "id" });
        },
        [](SqlMigrationQueryBuilder&) {});

    CHECK(manager.ApplyPendingMigrations() == 1);

    // Row-preservation: rebuild must not lose data.
    {
        auto stmt = SqlStatement { conn };
        auto cursor = stmt.ExecuteDirect(R"(SELECT "id", "label", "parent_id" FROM "Child")");
        REQUIRE(cursor.FetchRow());
        CHECK(cursor.GetColumn<int64_t>(1) == 10);
        CHECK(cursor.GetColumn<std::string>(2) == "row-a");
        CHECK(cursor.GetColumn<std::optional<int64_t>>(3) == std::nullopt);
        CHECK_FALSE(cursor.FetchRow());
    }

    // FK presence: pragma_foreign_key_list must now report the new constraint.
    {
        auto stmt = SqlStatement { conn };
        auto cursor = stmt.ExecuteDirect(R"(PRAGMA foreign_key_list("Child"))");
        bool found = false;
        while (cursor.FetchRow())
        {
            if (cursor.GetColumn<std::string>(3) == "Parent"       // `table`
                && cursor.GetColumn<std::string>(4) == "parent_id" // `from`
                && cursor.GetColumn<std::string>(5) == "id")       // `to`
            {
                found = true;
            }
        }
        CHECK(found);
    }
}

TEST_CASE_METHOD(SqlMigrationTestFixture, "AlterTable AddCompositeForeignKey rebuilds SQLite table", "[SqlMigration]")
{
    using namespace SqlColumnTypeDefinitions;

    // AddCompositeForeignKey via ALTER TABLE is a SQLite-specific code path (formatter
    // emits an ADD_COMPOSITE_FOREIGN_KEY sentinel, executor rebuilds the table with
    // the multi-column FOREIGN KEY clause appended). Other dialects ALTER in place.
    auto conn = SqlConnection {};
    if (conn.ServerType() != SqlServerType::SQLITE)
        return;

    auto createParent = SqlMigration::Migration<202601020001>(
        "create composite parent",
        [](SqlMigrationQueryBuilder& plan) {
            plan.CreateTable("CPParent")
                .PrimaryKey("a", Integer())
                .PrimaryKey("b", Integer())
                .RequiredColumn("name", Varchar(50));
        },
        [](SqlMigrationQueryBuilder& plan) { plan.DropTable("CPParent"); });

    auto createChild = SqlMigration::Migration<202601020002>(
        "create composite child",
        [](SqlMigrationQueryBuilder& plan) {
            plan.CreateTable("CPChild").PrimaryKey("id", Integer()).Column("pa", Integer()).Column("pb", Integer());
        },
        [](SqlMigrationQueryBuilder& plan) { plan.DropTable("CPChild"); });

    auto& manager = SqlMigration::MigrationManager::GetInstance();
    manager.CreateMigrationHistory();
    CHECK(manager.ApplyPendingMigrations() == 2);

    // Seed a row that must survive the table rebuild.
    {
        auto stmt = SqlStatement { conn };
        (void) stmt.ExecuteDirect(R"(INSERT INTO "CPParent" ("a", "b", "name") VALUES (1, 2, 'anchor'))");
        (void) stmt.ExecuteDirect(R"(INSERT INTO "CPChild" ("id", "pa", "pb") VALUES (100, 1, 2))");
    }

    auto addCompositeFk = SqlMigration::Migration<202601020003>(
        "add composite fk",
        [](SqlMigrationQueryBuilder& plan) {
            plan.AlterTable("CPChild").AddCompositeForeignKey({ "pa", "pb" }, "CPParent", { "a", "b" });
        },
        [](SqlMigrationQueryBuilder&) {});

    CHECK(manager.ApplyPendingMigrations() == 1);

    // Row-preservation: rebuild must not lose data.
    {
        auto stmt = SqlStatement { conn };
        auto cursor = stmt.ExecuteDirect(R"(SELECT "id", "pa", "pb" FROM "CPChild")");
        REQUIRE(cursor.FetchRow());
        CHECK(cursor.GetColumn<int64_t>(1) == 100);
        CHECK(cursor.GetColumn<int64_t>(2) == 1);
        CHECK(cursor.GetColumn<int64_t>(3) == 2);
        CHECK_FALSE(cursor.FetchRow());
    }

    // FK presence: pragma_foreign_key_list must now report both columns of the composite FK.
    {
        auto stmt = SqlStatement { conn };
        auto cursor = stmt.ExecuteDirect(R"(PRAGMA foreign_key_list("CPChild"))");
        bool foundPa = false;
        bool foundPb = false;
        while (cursor.FetchRow())
        {
            if (cursor.GetColumn<std::string>(3) == "CPParent") // `table`
            {
                auto const from = cursor.GetColumn<std::string>(4); // `from`
                auto const to = cursor.GetColumn<std::string>(5);   // `to`
                if (from == "pa" && to == "a")
                    foundPa = true;
                else if (from == "pb" && to == "b")
                    foundPb = true;
            }
        }
        CHECK(foundPa);
        CHECK(foundPb);
    }
}

TEST_CASE_METHOD(SqlMigrationTestFixture, "AlterTable AlterColumn rebuilds SQLite table", "[SqlMigration]")
{
    using namespace SqlColumnTypeDefinitions;

    // ALTER COLUMN is a SQLite-specific code path: SQLite has no `ALTER TABLE … ALTER COLUMN`,
    // so the formatter emits an ALTER_COLUMN sentinel and the executor rebuilds the table.
    // This test drives the change through MigrationManager::ApplyPendingMigrations() so the
    // guard-aware executor (not a bare ExecuteDirect) runs — i.e. it exercises the real
    // rebuild and is verified against live SQLite by CI's --test-env=sqlite3 matrix leg.
    // Other dialects ALTER the column in place and are covered by the formatter-output test.
    auto conn = SqlConnection {};
    auto skipStmt = SqlStatement { conn };
    UNSUPPORTED_DATABASE(skipStmt, SqlServerType::MICROSOFT_SQL);
    UNSUPPORTED_DATABASE(skipStmt, SqlServerType::POSTGRESQL);
    UNSUPPORTED_DATABASE(skipStmt, SqlServerType::MYSQL);

    auto& manager = SqlMigration::MigrationManager::GetInstance();

    SECTION("widen type: rows, other columns, and the primary key all survive")
    {
        auto create = SqlMigration::Migration<202602010001>(
            "create widen",
            [](SqlMigrationQueryBuilder& plan) {
                plan.CreateTable("Widen")
                    .PrimaryKey("id", Integer())
                    .RequiredColumn("descr", Varchar(10))
                    .Column("note", Varchar(20));
            },
            [](SqlMigrationQueryBuilder& plan) { plan.DropTable("Widen"); });

        manager.CreateMigrationHistory();
        REQUIRE(manager.ApplyPendingMigrations() == 1);

        {
            auto stmt = SqlStatement { conn };
            (void) stmt.ExecuteDirect(R"(INSERT INTO "Widen" ("id", "descr", "note") VALUES (1, 'short', 'keep'))");
        }

        auto widen = SqlMigration::Migration<202602010002>(
            "widen descr",
            [](SqlMigrationQueryBuilder& plan) {
                plan.AlterTable("Widen").AlterColumn("descr", Text(), SqlNullable::NotNull);
            },
            [](SqlMigrationQueryBuilder&) {});

        REQUIRE(manager.ApplyPendingMigrations() == 1);

        // Row-preservation: the rebuild must not lose data, including the untouched 'note'.
        {
            auto stmt = SqlStatement { conn };
            auto cursor = stmt.ExecuteDirect(R"(SELECT "id", "descr", "note" FROM "Widen")");
            REQUIRE(cursor.FetchRow());
            CHECK(cursor.GetColumn<int64_t>(1) == 1);
            CHECK(cursor.GetColumn<std::string>(2) == "short");
            CHECK(cursor.GetColumn<std::string>(3) == "keep");
            CHECK_FALSE(cursor.FetchRow());
        }

        // Schema: 'descr' is now TEXT and still NOT NULL; the primary key on 'id' survived.
        {
            auto stmt = SqlStatement { conn };
            auto cursor = stmt.ExecuteDirect(R"(PRAGMA table_info("Widen"))");
            bool checkedDescr = false;
            bool checkedId = false;
            while (cursor.FetchRow())
            {
                auto const name = cursor.GetColumn<std::string>(2); // name
                if (name == "descr")
                {
                    CHECK(cursor.GetColumn<std::string>(3) == "TEXT"); // type
                    CHECK(cursor.GetColumn<int64_t>(4) == 1);          // notnull
                    checkedDescr = true;
                }
                else if (name == "id")
                {
                    CHECK(cursor.GetColumn<int64_t>(6) == 1); // pk
                    checkedId = true;
                }
            }
            CHECK(checkedDescr);
            CHECK(checkedId);
        }
    }

    SECTION("relax NOT NULL to NULL lets a NULL be inserted afterwards")
    {
        auto create = SqlMigration::Migration<202602020001>(
            "create relax",
            [](SqlMigrationQueryBuilder& plan) {
                plan.CreateTable("Relax").PrimaryKey("id", Integer()).RequiredColumn("descr", Varchar(20));
            },
            [](SqlMigrationQueryBuilder& plan) { plan.DropTable("Relax"); });

        manager.CreateMigrationHistory();
        REQUIRE(manager.ApplyPendingMigrations() == 1);

        auto relax = SqlMigration::Migration<202602020002>(
            "relax descr",
            [](SqlMigrationQueryBuilder& plan) {
                plan.AlterTable("Relax").AlterColumn("descr", Varchar(20), SqlNullable::Null);
            },
            [](SqlMigrationQueryBuilder&) {});

        REQUIRE(manager.ApplyPendingMigrations() == 1);

        auto stmt = SqlStatement { conn };
        // A NULL is now accepted where the column previously was NOT NULL.
        CHECK_NOTHROW(stmt.ExecuteDirect(R"(INSERT INTO "Relax" ("id", "descr") VALUES (1, NULL))"));
        auto cursor = stmt.ExecuteDirect(R"(SELECT "descr" FROM "Relax" WHERE "id" = 1)");
        REQUIRE(cursor.FetchRow());
        CHECK(cursor.GetColumn<std::optional<std::string>>(1) == std::nullopt);
    }

    SECTION("tighten NULL to NOT NULL enforces the constraint and keeps existing rows")
    {
        auto create = SqlMigration::Migration<202602030001>(
            "create tighten",
            [](SqlMigrationQueryBuilder& plan) {
                plan.CreateTable("Tighten").PrimaryKey("id", Integer()).Column("descr", Varchar(20));
            },
            [](SqlMigrationQueryBuilder& plan) { plan.DropTable("Tighten"); });

        manager.CreateMigrationHistory();
        REQUIRE(manager.ApplyPendingMigrations() == 1);

        {
            auto stmt = SqlStatement { conn };
            (void) stmt.ExecuteDirect(R"(INSERT INTO "Tighten" ("id", "descr") VALUES (1, 'present'))");
        }

        auto tighten = SqlMigration::Migration<202602030002>(
            "tighten descr",
            [](SqlMigrationQueryBuilder& plan) {
                plan.AlterTable("Tighten").AlterColumn("descr", Varchar(20), SqlNullable::NotNull);
            },
            [](SqlMigrationQueryBuilder&) {});

        REQUIRE(manager.ApplyPendingMigrations() == 1);

        auto stmt = SqlStatement { conn };
        // The existing non-null row survived the rebuild.
        {
            auto cursor = stmt.ExecuteDirect(R"(SELECT "descr" FROM "Tighten" WHERE "id" = 1)");
            REQUIRE(cursor.FetchRow());
            CHECK(cursor.GetColumn<std::string>(1) == "present");
        }
        // NOT NULL is now enforced.
        CHECK_THROWS(stmt.ExecuteDirect(R"(INSERT INTO "Tighten" ("id", "descr") VALUES (2, NULL))"));
    }

    SECTION("two successive AlterColumn calls in one migration fold and apply")
    {
        auto create = SqlMigration::Migration<202602040001>(
            "create multi",
            [](SqlMigrationQueryBuilder& plan) {
                plan.CreateTable("Multi").PrimaryKey("id", Integer()).RequiredColumn("descr", Varchar(10));
            },
            [](SqlMigrationQueryBuilder& plan) { plan.DropTable("Multi"); });

        manager.CreateMigrationHistory();
        REQUIRE(manager.ApplyPendingMigrations() == 1);

        {
            auto stmt = SqlStatement { conn };
            (void) stmt.ExecuteDirect(R"(INSERT INTO "Multi" ("id", "descr") VALUES (1, 'x'))");
        }

        auto alter = SqlMigration::Migration<202602040002>(
            "two alters",
            [](SqlMigrationQueryBuilder& plan) {
                plan.AlterTable("Multi").AlterColumn("descr", Varchar(50), SqlNullable::NotNull);
                plan.AlterTable("Multi").AlterColumn("descr", Text(), SqlNullable::Null);
            },
            [](SqlMigrationQueryBuilder&) {});

        REQUIRE(manager.ApplyPendingMigrations() == 1);

        auto stmt = SqlStatement { conn };
        auto cursor = stmt.ExecuteDirect(R"(SELECT "id", "descr" FROM "Multi")");
        REQUIRE(cursor.FetchRow());
        CHECK(cursor.GetColumn<int64_t>(1) == 1);
        CHECK(cursor.GetColumn<std::string>(2) == "x");
        CHECK_FALSE(cursor.FetchRow());
    }

    SECTION("altering a non-existent column raises and leaves the table intact")
    {
        auto create = SqlMigration::Migration<202602050001>(
            "create missing",
            [](SqlMigrationQueryBuilder& plan) {
                plan.CreateTable("Missing").PrimaryKey("id", Integer()).RequiredColumn("descr", Varchar(10));
            },
            [](SqlMigrationQueryBuilder& plan) { plan.DropTable("Missing"); });

        manager.CreateMigrationHistory();
        REQUIRE(manager.ApplyPendingMigrations() == 1);

        {
            auto stmt = SqlStatement { conn };
            (void) stmt.ExecuteDirect(R"(INSERT INTO "Missing" ("id", "descr") VALUES (1, 'here'))");
        }

        auto bad = SqlMigration::Migration<202602050002>(
            "alter missing column",
            [](SqlMigrationQueryBuilder& plan) {
                plan.AlterTable("Missing").AlterColumn("nonexistent", Text(), SqlNullable::Null);
            },
            [](SqlMigrationQueryBuilder&) {});

        CHECK_THROWS(manager.ApplyPendingMigrations());

        // The transform throws before any DDL runs, so the original table and its row remain.
        auto stmt = SqlStatement { conn };
        auto cursor = stmt.ExecuteDirect(R"(SELECT "id", "descr" FROM "Missing")");
        REQUIRE(cursor.FetchRow());
        CHECK(cursor.GetColumn<int64_t>(1) == 1);
        CHECK(cursor.GetColumn<std::string>(2) == "here");
    }

    SECTION("indexes and uniqueness survive the rebuild")
    {
        // A column with a separately-created UNIQUE INDEX lives in its own sqlite_schema row, which
        // DROP TABLE destroys. The rebuild must re-create it, or altering an unrelated column would
        // silently drop the index and stop enforcing uniqueness.
        auto create = SqlMigration::Migration<202602060001>(
            "create indexed",
            [](SqlMigrationQueryBuilder& plan) {
                plan.CreateTable("Indexed")
                    .PrimaryKey("id", Integer())
                    .RequiredColumn("code", Varchar(10))
                    .Unique()
                    .Index() // emits a separate CREATE UNIQUE INDEX "Indexed_code_index"
                    .RequiredColumn("descr", Varchar(10));
            },
            [](SqlMigrationQueryBuilder& plan) { plan.DropTable("Indexed"); });

        manager.CreateMigrationHistory();
        REQUIRE(manager.ApplyPendingMigrations() == 1);

        {
            auto stmt = SqlStatement { conn };
            (void) stmt.ExecuteDirect(R"(INSERT INTO "Indexed" ("id", "code", "descr") VALUES (1, 'AAA', 'x'))");
        }

        auto alter = SqlMigration::Migration<202602060002>(
            "widen descr keep index",
            [](SqlMigrationQueryBuilder& plan) {
                plan.AlterTable("Indexed").AlterColumn("descr", Text(), SqlNullable::NotNull);
            },
            [](SqlMigrationQueryBuilder&) {});

        REQUIRE(manager.ApplyPendingMigrations() == 1);

        auto stmt = SqlStatement { conn };
        // The explicitly-created index still exists after the rebuild of an unrelated column.
        {
            auto cursor = stmt.ExecuteDirect(
                R"(SELECT COUNT(*) FROM sqlite_schema WHERE type='index' AND tbl_name='Indexed' AND name='Indexed_code_index')");
            REQUIRE(cursor.FetchRow());
            CHECK(cursor.GetColumn<int64_t>(1) == 1);
        }
        // ...and it still enforces uniqueness on 'code'.
        CHECK_THROWS(stmt.ExecuteDirect(R"(INSERT INTO "Indexed" ("id", "code", "descr") VALUES (2, 'AAA', 'y'))"));
    }

    SECTION("altering an AUTOINCREMENT primary key to a non-integer type is rejected")
    {
        auto create = SqlMigration::Migration<202602070001>(
            "create autoinc",
            [](SqlMigrationQueryBuilder& plan) {
                plan.CreateTable("Autoinc")
                    .PrimaryKeyWithAutoIncrement("id", Integer())
                    .RequiredColumn("descr", Varchar(10));
            },
            [](SqlMigrationQueryBuilder& plan) { plan.DropTable("Autoinc"); });

        manager.CreateMigrationHistory();
        REQUIRE(manager.ApplyPendingMigrations() == 1);

        {
            auto stmt = SqlStatement { conn };
            (void) stmt.ExecuteDirect(R"(INSERT INTO "Autoinc" ("descr") VALUES ('x'))");
        }

        auto bad = SqlMigration::Migration<202602070002>(
            "retype autoinc pk",
            [](SqlMigrationQueryBuilder& plan) {
                plan.AlterTable("Autoinc").AlterColumn("id", Text(), SqlNullable::NotNull);
            },
            [](SqlMigrationQueryBuilder&) {});

        // SQLite cannot keep AUTOINCREMENT on a non-INTEGER column; the rebuild refuses up front
        // rather than emitting DDL SQLite rejects mid-rebuild.
        CHECK_THROWS(manager.ApplyPendingMigrations());

        // The table and its row are intact (the rewrite throws before any DDL runs).
        auto stmt = SqlStatement { conn };
        auto cursor = stmt.ExecuteDirect(R"(SELECT COUNT(*) FROM "Autoinc")");
        REQUIRE(cursor.FetchRow());
        CHECK(cursor.GetColumn<int64_t>(1) == 1);
    }

    SECTION("rebuild drops a contradictory DEFAULT NULL and survives literal defaults")
    {
        manager.CreateMigrationHistory();

        // A DEFAULT NULL column and a sibling whose default is a string literal with an unbalanced
        // parenthesis — neither shape is producible via the CreateTable DSL, so build the table
        // out-of-band, then drive the rebuilds through a migration.
        {
            auto stmt = SqlStatement { conn };
            (void) stmt.ExecuteDirect(
                R"(CREATE TABLE "Defs" ("id" INTEGER PRIMARY KEY, "a" INTEGER DEFAULT NULL, "b" TEXT DEFAULT '(x'))");
            (void) stmt.ExecuteDirect(R"(INSERT INTO "Defs" ("id", "a", "b") VALUES (1, 5, 'keep'))");
        }

        auto alter = SqlMigration::Migration<202602080002>(
            "tighten a and retype b",
            [](SqlMigrationQueryBuilder& plan) {
                // Tighten 'a' (DEFAULT NULL) to NOT NULL, then retype 'b' (paren-literal default).
                plan.AlterTable("Defs").AlterColumn("a", Integer(), SqlNullable::NotNull);
                plan.AlterTable("Defs").AlterColumn("b", Text(), SqlNullable::NotNull);
            },
            [](SqlMigrationQueryBuilder&) {});

        REQUIRE(manager.ApplyPendingMigrations() == 1);

        auto stmt = SqlStatement { conn };
        // The row survived; 'b''s literal default with a stray '(' did not corrupt the rebuild.
        {
            auto cursor = stmt.ExecuteDirect(R"(SELECT "id", "a", "b" FROM "Defs")");
            REQUIRE(cursor.FetchRow());
            CHECK(cursor.GetColumn<int64_t>(1) == 1);
            CHECK(cursor.GetColumn<int64_t>(2) == 5);
            CHECK(cursor.GetColumn<std::string>(3) == "keep");
        }
        // 'a' is now NOT NULL with the contradictory NULL default removed: omitting it must fail.
        CHECK_THROWS(stmt.ExecuteDirect(R"(INSERT INTO "Defs" ("id", "b") VALUES (2, 'y'))"));
        {
            auto cursor = stmt.ExecuteDirect(R"(SELECT sql FROM sqlite_schema WHERE type='table' AND name='Defs')");
            REQUIRE(cursor.FetchRow());
            auto const sql = cursor.GetColumn<std::string>(1);
            CHECK(!sql.contains("NOT NULL DEFAULT NULL")); // no contradiction
            CHECK(sql.contains("DEFAULT '(x'"));           // literal default preserved verbatim
        }
    }

    SECTION("the column anchor is not confused by an earlier inline REFERENCES")
    {
        manager.CreateMigrationHistory();

        // Externally-created tables: 'Child' has a first column whose inline REFERENCES names a
        // column ("descr") sharing the name of a later column we then alter. A first-match without
        // the trailing-space anchor would splice into the REFERENCES clause instead of the column.
        {
            auto stmt = SqlStatement { conn };
            (void) stmt.ExecuteDirect(R"(CREATE TABLE "Parent" ("descr" INTEGER PRIMARY KEY))");
            (void) stmt.ExecuteDirect(
                R"(CREATE TABLE "Child" ("ref" INTEGER REFERENCES "Parent"("descr"), "descr" VARCHAR(10) NOT NULL))");
            (void) stmt.ExecuteDirect(R"(INSERT INTO "Parent" ("descr") VALUES (7))");
            (void) stmt.ExecuteDirect(R"(INSERT INTO "Child" ("ref", "descr") VALUES (7, 'hi'))");
        }

        auto alter = SqlMigration::Migration<202602090002>(
            "widen child descr",
            [](SqlMigrationQueryBuilder& plan) {
                plan.AlterTable("Child").AlterColumn("descr", Text(), SqlNullable::NotNull);
            },
            [](SqlMigrationQueryBuilder&) {});

        REQUIRE(manager.ApplyPendingMigrations() == 1);

        auto stmt = SqlStatement { conn };
        {
            auto cursor = stmt.ExecuteDirect(R"(SELECT sql FROM sqlite_schema WHERE type='table' AND name='Child')");
            REQUIRE(cursor.FetchRow());
            auto const sql = cursor.GetColumn<std::string>(1);
            // The FK clause stays intact and the real 'descr' column (not the FK reference) is retyped.
            CHECK(sql.contains(R"(REFERENCES "Parent"("descr"))"));
            CHECK(sql.contains(R"("descr" TEXT)"));
        }

        auto rows = stmt.ExecuteDirect(R"(SELECT "ref", "descr" FROM "Child")");
        REQUIRE(rows.FetchRow());
        CHECK(rows.GetColumn<int64_t>(1) == 7);
        CHECK(rows.GetColumn<std::string>(2) == "hi");
    }

    SECTION("an earlier inline CHECK mentioning the column does not misanchor the rewrite")
    {
        manager.CreateMigrationHistory();

        // The first column's inline CHECK references a *later* column ("descr") by quoted name followed
        // by a space — a first-substring anchor would splice the new type into the CHECK clause. Build
        // out-of-band since the DSL cannot emit such a CHECK.
        {
            auto stmt = SqlStatement { conn };
            (void) stmt.ExecuteDirect(
                R"(CREATE TABLE "Chk" ("a" INTEGER CHECK("descr" <> ''), "descr" VARCHAR(10) NOT NULL))");
            (void) stmt.ExecuteDirect(R"(INSERT INTO "Chk" ("a", "descr") VALUES (1, 'x'))");
        }

        auto alter = SqlMigration::Migration<202602100002>(
            "widen chk descr",
            [](SqlMigrationQueryBuilder& plan) {
                plan.AlterTable("Chk").AlterColumn("descr", Text(), SqlNullable::NotNull);
            },
            [](SqlMigrationQueryBuilder&) {});

        REQUIRE(manager.ApplyPendingMigrations() == 1);

        auto stmt = SqlStatement { conn };
        {
            auto cursor = stmt.ExecuteDirect(R"(SELECT sql FROM sqlite_schema WHERE type='table' AND name='Chk')");
            REQUIRE(cursor.FetchRow());
            auto const sql = cursor.GetColumn<std::string>(1);
            CHECK(sql.contains(R"(CHECK("descr" <> ''))")); // CHECK clause intact
            CHECK(sql.contains(R"("descr" TEXT)"));         // real column retyped
        }
        auto rows = stmt.ExecuteDirect(R"(SELECT "a", "descr" FROM "Chk")");
        REQUIRE(rows.FetchRow());
        CHECK(rows.GetColumn<int64_t>(1) == 1);
        CHECK(rows.GetColumn<std::string>(2) == "x");
    }

    SECTION("a column whose quoted name contains an apostrophe survives the rebuild")
    {
        manager.CreateMigrationHistory();

        // The apostrophe-bearing identifier precedes the altered column, so the rewriter must scan past
        // it; treating the `'` as a string-literal delimiter would corrupt the column list.
        {
            auto stmt = SqlStatement { conn };
            (void) stmt.ExecuteDirect(
                R"(CREATE TABLE "Apos" ("id" INTEGER PRIMARY KEY, "O'Brien" INTEGER, "descr" VARCHAR(10)))");
            (void) stmt.ExecuteDirect(R"(INSERT INTO "Apos" ("id", "O'Brien", "descr") VALUES (1, 7, 'x'))");
        }

        auto alter = SqlMigration::Migration<202602110002>(
            "widen apos descr",
            [](SqlMigrationQueryBuilder& plan) { plan.AlterTable("Apos").AlterColumn("descr", Text(), SqlNullable::Null); },
            [](SqlMigrationQueryBuilder&) {});

        REQUIRE(manager.ApplyPendingMigrations() == 1);

        auto stmt = SqlStatement { conn };
        auto cursor = stmt.ExecuteDirect(R"(SELECT "id", "O'Brien", "descr" FROM "Apos")");
        REQUIRE(cursor.FetchRow());
        CHECK(cursor.GetColumn<int64_t>(1) == 1);
        CHECK(cursor.GetColumn<int64_t>(2) == 7);
        CHECK(cursor.GetColumn<std::string>(3) == "x");
    }

    SECTION("a space-less DEFAULT(NULL) is dropped when tightening to NOT NULL")
    {
        manager.CreateMigrationHistory();

        {
            auto stmt = SqlStatement { conn };
            (void) stmt.ExecuteDirect(R"(CREATE TABLE "DefNs" ("id" INTEGER PRIMARY KEY, "a" INTEGER DEFAULT(NULL)))");
            (void) stmt.ExecuteDirect(R"(INSERT INTO "DefNs" ("id", "a") VALUES (1, 5))");
        }

        auto alter = SqlMigration::Migration<202602120002>(
            "tighten defns a",
            [](SqlMigrationQueryBuilder& plan) {
                plan.AlterTable("DefNs").AlterColumn("a", Integer(), SqlNullable::NotNull);
            },
            [](SqlMigrationQueryBuilder&) {});

        REQUIRE(manager.ApplyPendingMigrations() == 1);

        auto stmt = SqlStatement { conn };
        {
            auto cursor = stmt.ExecuteDirect(R"(SELECT sql FROM sqlite_schema WHERE type='table' AND name='DefNs')");
            REQUIRE(cursor.FetchRow());
            auto const sql = cursor.GetColumn<std::string>(1);
            CHECK(!sql.contains("DEFAULT(NULL)")); // contradictory default removed
        }
        // NOT NULL is now enforced even though the default was a no-space DEFAULT(NULL).
        CHECK_THROWS(stmt.ExecuteDirect(R"(INSERT INTO "DefNs" ("id") VALUES (2))"));
    }

    SECTION("a DEFAULT literal containing the word AUTOINCREMENT does not block retyping")
    {
        manager.CreateMigrationHistory();

        {
            auto stmt = SqlStatement { conn };
            (void) stmt.ExecuteDirect(
                R"(CREATE TABLE "Faux" ("id" INTEGER PRIMARY KEY, "note" VARCHAR(40) DEFAULT 'has AUTOINCREMENT word'))");
            (void) stmt.ExecuteDirect(R"(INSERT INTO "Faux" ("id") VALUES (1))");
        }

        auto alter = SqlMigration::Migration<202602130002>(
            "retype faux note",
            [](SqlMigrationQueryBuilder& plan) { plan.AlterTable("Faux").AlterColumn("note", Text(), SqlNullable::Null); },
            [](SqlMigrationQueryBuilder&) {});

        // The substring 'AUTOINCREMENT' inside the DEFAULT literal must not trip the INTEGER-PK guard.
        REQUIRE(manager.ApplyPendingMigrations() == 1);

        auto stmt = SqlStatement { conn };
        auto cursor = stmt.ExecuteDirect(R"(SELECT "note" FROM "Faux" WHERE "id" = 1)");
        REQUIRE(cursor.FetchRow());
        CHECK(cursor.GetColumn<std::string>(1) == "has AUTOINCREMENT word"); // default applied, literal preserved
    }

    SECTION("a generated column is preserved and excluded from the data copy")
    {
        manager.CreateMigrationHistory();

        // A STORED generated column lives in the column list but cannot be inserted into; the rebuild's
        // INSERT...SELECT must skip it, and its expression (referencing the altered column) must survive.
        {
            auto stmt = SqlStatement { conn };
            (void) stmt.ExecuteDirect(
                R"(CREATE TABLE "Gen" ("id" INTEGER PRIMARY KEY, "a" INTEGER, "b" INTEGER GENERATED ALWAYS AS ("a" * 2) STORED))");
            (void) stmt.ExecuteDirect(R"(INSERT INTO "Gen" ("id", "a") VALUES (1, 5))");
        }

        auto alter = SqlMigration::Migration<202602140002>(
            "tighten gen a",
            [](SqlMigrationQueryBuilder& plan) { plan.AlterTable("Gen").AlterColumn("a", Integer(), SqlNullable::NotNull); },
            [](SqlMigrationQueryBuilder&) {});

        REQUIRE(manager.ApplyPendingMigrations() == 1);

        auto stmt = SqlStatement { conn };
        auto cursor = stmt.ExecuteDirect(R"(SELECT "id", "a", "b" FROM "Gen")");
        REQUIRE(cursor.FetchRow());
        CHECK(cursor.GetColumn<int64_t>(1) == 1);
        CHECK(cursor.GetColumn<int64_t>(2) == 5);
        CHECK(cursor.GetColumn<int64_t>(3) == 10); // generated column still computes a*2
    }

    SECTION("an externally-created table with an unquoted short name rebuilds correctly")
    {
        manager.CreateMigrationHistory();

        // Unquoted table name "T" is a substring of "CREATE"/"TABLE"; a naive substring replace of the
        // name would corrupt the stored DDL. The word-boundary fallback must target the declaration.
        {
            auto stmt = SqlStatement { conn };
            (void) stmt.ExecuteDirect(R"(CREATE TABLE T ("id" INTEGER PRIMARY KEY, "descr" VARCHAR(10)))");
            (void) stmt.ExecuteDirect(R"(INSERT INTO T ("id", "descr") VALUES (1, 'x'))");
        }

        auto alter = SqlMigration::Migration<202602150002>(
            "widen T descr",
            [](SqlMigrationQueryBuilder& plan) { plan.AlterTable("T").AlterColumn("descr", Text(), SqlNullable::Null); },
            [](SqlMigrationQueryBuilder&) {});

        REQUIRE(manager.ApplyPendingMigrations() == 1);

        auto stmt = SqlStatement { conn };
        auto cursor = stmt.ExecuteDirect(R"(SELECT "id", "descr" FROM T)");
        REQUIRE(cursor.FetchRow());
        CHECK(cursor.GetColumn<int64_t>(1) == 1);
        CHECK(cursor.GetColumn<std::string>(2) == "x");
    }
}

TEST_CASE_METHOD(SqlMigrationTestFixture, "AlterColumn via MigrateDirect fails loudly on SQLite", "[SqlMigration]")
{
    using namespace SqlColumnTypeDefinitions;

    // ALTER COLUMN on SQLite needs a table rebuild that only MigrationManager performs. Applying it
    // through the comment-only-sentinel-unaware MigrateDirect must throw rather than silently no-op.
    auto conn = SqlConnection {};
    auto skipStmt = SqlStatement { conn };
    UNSUPPORTED_DATABASE(skipStmt, SqlServerType::MICROSOFT_SQL);
    UNSUPPORTED_DATABASE(skipStmt, SqlServerType::POSTGRESQL);
    UNSUPPORTED_DATABASE(skipStmt, SqlServerType::MYSQL);

    auto stmt = SqlStatement { conn };
    stmt.MigrateDirect([](SqlMigrationQueryBuilder& plan) {
        plan.CreateTable("DirectAlter").PrimaryKey("id", Integer()).RequiredColumn("descr", Varchar(10));
    });

    CHECK_THROWS(stmt.MigrateDirect([](SqlMigrationQueryBuilder& plan) {
        plan.AlterTable("DirectAlter").AlterColumn("descr", Text(), SqlNullable::Null);
    }));
}

TEST_CASE_METHOD(SqlMigrationTestFixture, "RegisterRelease stores and orders releases", "[SqlMigration]")
{
    auto& manager = SqlMigration::MigrationManager::GetInstance();

    // Register out of order; expect ascending-by-timestamp order afterwards.
    manager.RegisterRelease("6.8.0", SqlMigration::MigrationTimestamp { 202606010000 });
    manager.RegisterRelease("6.6.0", SqlMigration::MigrationTimestamp { 202601010000 });
    manager.RegisterRelease("6.7.0", SqlMigration::MigrationTimestamp { 202603010000 });

    auto const& releases = manager.GetAllReleases();
    REQUIRE(releases.size() == 3);
    CHECK(releases[0].version == "6.6.0");
    CHECK(releases[1].version == "6.7.0");
    CHECK(releases[2].version == "6.8.0");
}

TEST_CASE_METHOD(SqlMigrationTestFixture, "RegisterRelease rejects duplicate version", "[SqlMigration]")
{
    auto& manager = SqlMigration::MigrationManager::GetInstance();
    manager.RegisterRelease("7.0.0", SqlMigration::MigrationTimestamp { 202701010000 });

    CHECK_THROWS_AS(manager.RegisterRelease("7.0.0", SqlMigration::MigrationTimestamp { 202701020000 }), std::runtime_error);
}

TEST_CASE_METHOD(SqlMigrationTestFixture, "RegisterRelease rejects duplicate timestamp", "[SqlMigration]")
{
    auto& manager = SqlMigration::MigrationManager::GetInstance();
    manager.RegisterRelease("7.1.0", SqlMigration::MigrationTimestamp { 202702010000 });

    CHECK_THROWS_AS(manager.RegisterRelease("7.1.1", SqlMigration::MigrationTimestamp { 202702010000 }), std::runtime_error);
}

TEST_CASE_METHOD(SqlMigrationTestFixture, "FindReleaseByVersion and FindReleaseForTimestamp", "[SqlMigration]")
{
    auto& manager = SqlMigration::MigrationManager::GetInstance();
    manager.RegisterRelease("8.0.0", SqlMigration::MigrationTimestamp { 202801010000 });
    manager.RegisterRelease("8.1.0", SqlMigration::MigrationTimestamp { 202802010000 });

    auto const* v800 = manager.FindReleaseByVersion("8.0.0");
    REQUIRE(v800 != nullptr);
    CHECK(v800->highestTimestamp.value == 202801010000);

    CHECK(manager.FindReleaseByVersion("does-not-exist") == nullptr);

    // Before any release — still covered by the first release, because lower_bound returns the first
    // release whose highestTimestamp >= ts.
    auto const* early = manager.FindReleaseForTimestamp(SqlMigration::MigrationTimestamp { 202701010000 });
    REQUIRE(early != nullptr);
    CHECK(early->version == "8.0.0");

    // Exactly at the first release's timestamp.
    auto const* atFirst = manager.FindReleaseForTimestamp(SqlMigration::MigrationTimestamp { 202801010000 });
    REQUIRE(atFirst != nullptr);
    CHECK(atFirst->version == "8.0.0");

    // Between first and second.
    auto const* between = manager.FindReleaseForTimestamp(SqlMigration::MigrationTimestamp { 202801150000 });
    REQUIRE(between != nullptr);
    CHECK(between->version == "8.1.0");

    // Past the last release.
    CHECK(manager.FindReleaseForTimestamp(SqlMigration::MigrationTimestamp { 202901010000 }) == nullptr);
}

TEST_CASE_METHOD(SqlMigrationTestFixture, "GetMigrationsForRelease returns range-bounded migrations", "[SqlMigration]")
{
    using namespace SqlColumnTypeDefinitions;

    auto m1 = SqlMigration::Migration<202801010000>(
        "m1", [](SqlMigrationQueryBuilder& plan) { plan.CreateTable("rel_m1").PrimaryKey("id", Integer()); });
    auto m2 = SqlMigration::Migration<202802010000>(
        "m2", [](SqlMigrationQueryBuilder& plan) { plan.CreateTable("rel_m2").PrimaryKey("id", Integer()); });
    auto m3 = SqlMigration::Migration<202803010000>(
        "m3", [](SqlMigrationQueryBuilder& plan) { plan.CreateTable("rel_m3").PrimaryKey("id", Integer()); });
    auto m4 = SqlMigration::Migration<202804010000>(
        "m4", [](SqlMigrationQueryBuilder& plan) { plan.CreateTable("rel_m4").PrimaryKey("id", Integer()); });

    auto& manager = SqlMigration::MigrationManager::GetInstance();
    // Release A covers m1+m2; release B covers m3 only. m4 is post-all-releases.
    manager.RegisterRelease("A", SqlMigration::MigrationTimestamp { 202802010000 });
    manager.RegisterRelease("B", SqlMigration::MigrationTimestamp { 202803010000 });

    auto const inA = manager.GetMigrationsForRelease("A");
    REQUIRE(inA.size() == 2);
    auto itA = inA.begin();
    CHECK((*itA++)->GetTimestamp().value == 202801010000);
    CHECK((*itA)->GetTimestamp().value == 202802010000);

    auto const inB = manager.GetMigrationsForRelease("B");
    REQUIRE(inB.size() == 1);
    CHECK(inB.front()->GetTimestamp().value == 202803010000);

    // Unknown version => empty list, not an error.
    CHECK(manager.GetMigrationsForRelease("does-not-exist").empty());
}

TEST_CASE_METHOD(SqlMigrationTestFixture, "RollbackToRelease semantics via RevertToMigration", "[SqlMigration]")
{
    using namespace SqlColumnTypeDefinitions;

    auto m1 = SqlMigration::Migration<202810010000>(
        "rel r1",
        [](SqlMigrationQueryBuilder& plan) { plan.CreateTable("rel_r1").PrimaryKey("id", Integer()); },
        [](SqlMigrationQueryBuilder& plan) { plan.DropTable("rel_r1"); });
    auto m2 = SqlMigration::Migration<202810020000>(
        "rel r2",
        [](SqlMigrationQueryBuilder& plan) { plan.CreateTable("rel_r2").PrimaryKey("id", Integer()); },
        [](SqlMigrationQueryBuilder& plan) { plan.DropTable("rel_r2"); });
    auto m3 = SqlMigration::Migration<202810030000>(
        "rel r3",
        [](SqlMigrationQueryBuilder& plan) { plan.CreateTable("rel_r3").PrimaryKey("id", Integer()); },
        [](SqlMigrationQueryBuilder& plan) { plan.DropTable("rel_r3"); });

    auto& manager = SqlMigration::MigrationManager::GetInstance();
    manager.RegisterRelease("9.0.0", SqlMigration::MigrationTimestamp { 202810020000 });

    manager.CreateMigrationHistory();
    CHECK(manager.ApplyPendingMigrations() == 3);

    // Semantically: rollback-to-release 9.0.0 reverts everything after 202810020000 — i.e. m3.
    auto const* release = manager.FindReleaseByVersion("9.0.0");
    REQUIRE(release != nullptr);

    auto result = manager.RevertToMigration(release->highestTimestamp);
    CHECK(result.revertedTimestamps.size() == 1);
    CHECK(!result.failedAt.has_value());

    auto const appliedIds = manager.GetAppliedMigrationIds();
    CHECK(appliedIds.size() == 2);
    CHECK(appliedIds.back().value == 202810020000);
}

TEST_CASE_METHOD(SqlMigrationTestFixture, "ApplyPendingMigrationsUpTo applies up to the boundary", "[SqlMigration]")
{
    using namespace SqlColumnTypeDefinitions;

    auto m1 = SqlMigration::Migration<202811010000>(
        "up r1", [](SqlMigrationQueryBuilder& plan) { plan.CreateTable("up_r1").PrimaryKey("id", Integer()); });
    auto m2 = SqlMigration::Migration<202811020000>(
        "up r2", [](SqlMigrationQueryBuilder& plan) { plan.CreateTable("up_r2").PrimaryKey("id", Integer()); });
    auto m3 = SqlMigration::Migration<202811030000>(
        "up r3", [](SqlMigrationQueryBuilder& plan) { plan.CreateTable("up_r3").PrimaryKey("id", Integer()); });

    auto& manager = SqlMigration::MigrationManager::GetInstance();
    manager.RegisterRelease("up-A", SqlMigration::MigrationTimestamp { 202811020000 });

    manager.CreateMigrationHistory();
    auto const* releaseA = manager.FindReleaseByVersion("up-A");
    REQUIRE(releaseA != nullptr);

    auto const applied = manager.ApplyPendingMigrationsUpTo(releaseA->highestTimestamp);
    CHECK(applied == 2);

    auto const ids = manager.GetAppliedMigrationIds();
    REQUIRE(ids.size() == 2);
    CHECK(ids[0].value == 202811010000);
    CHECK(ids[1].value == 202811020000);
}

TEST_CASE_METHOD(SqlMigrationTestFixture, "ApplyPendingMigrationsUpTo with off-boundary target", "[SqlMigration]")
{
    using namespace SqlColumnTypeDefinitions;

    auto m1 = SqlMigration::Migration<202812010000>(
        "off r1", [](SqlMigrationQueryBuilder& plan) { plan.CreateTable("off_r1").PrimaryKey("id", Integer()); });
    auto m2 = SqlMigration::Migration<202812020000>(
        "off r2", [](SqlMigrationQueryBuilder& plan) { plan.CreateTable("off_r2").PrimaryKey("id", Integer()); });
    auto m3 = SqlMigration::Migration<202812030000>(
        "off r3", [](SqlMigrationQueryBuilder& plan) { plan.CreateTable("off_r3").PrimaryKey("id", Integer()); });

    auto& manager = SqlMigration::MigrationManager::GetInstance();
    manager.CreateMigrationHistory();

    // Pick a target strictly between m2 and m3. Expect m1+m2 to be applied; m3 to remain pending.
    auto const target = SqlMigration::MigrationTimestamp { 202812025000 };
    CHECK(manager.ApplyPendingMigrationsUpTo(target) == 2);

    auto const ids = manager.GetAppliedMigrationIds();
    REQUIRE(ids.size() == 2);
    CHECK(ids.back().value == 202812020000);
}

TEST_CASE_METHOD(SqlMigrationTestFixture, "ApplyPendingMigrationsUpTo is a no-op when already at target", "[SqlMigration]")
{
    using namespace SqlColumnTypeDefinitions;

    auto m1 = SqlMigration::Migration<202813010000>(
        "noop r1", [](SqlMigrationQueryBuilder& plan) { plan.CreateTable("noop_r1").PrimaryKey("id", Integer()); });
    auto m2 = SqlMigration::Migration<202813020000>(
        "noop r2", [](SqlMigrationQueryBuilder& plan) { plan.CreateTable("noop_r2").PrimaryKey("id", Integer()); });

    auto& manager = SqlMigration::MigrationManager::GetInstance();
    manager.CreateMigrationHistory();

    auto const target = SqlMigration::MigrationTimestamp { 202813020000 };
    CHECK(manager.ApplyPendingMigrationsUpTo(target) == 2);
    // Second invocation finds nothing pending below the target — engine returns 0.
    CHECK(manager.ApplyPendingMigrationsUpTo(target) == 0);
}

TEST_CASE_METHOD(SqlMigrationTestFixture,
                 "PreviewPendingMigrationsUpTo emits the same SQL the bounded apply would",
                 "[SqlMigration]")
{
    using namespace SqlColumnTypeDefinitions;

    auto m1 = SqlMigration::Migration<202814010000>(
        "prev r1", [](SqlMigrationQueryBuilder& plan) { plan.CreateTable("prev_r1").PrimaryKey("id", Integer()); });
    auto m2 = SqlMigration::Migration<202814020000>(
        "prev r2", [](SqlMigrationQueryBuilder& plan) { plan.CreateTable("prev_r2").PrimaryKey("id", Integer()); });
    auto m3 = SqlMigration::Migration<202814030000>(
        "prev r3", [](SqlMigrationQueryBuilder& plan) { plan.CreateTable("prev_r3").PrimaryKey("id", Integer()); });

    auto& manager = SqlMigration::MigrationManager::GetInstance();
    manager.CreateMigrationHistory();

    auto const target = SqlMigration::MigrationTimestamp { 202814020000 };
    auto const previewed = manager.PreviewPendingMigrationsUpTo(target);
    CHECK_FALSE(previewed.empty());
    // Preview must not move the database.
    CHECK(manager.GetAppliedMigrationIds().empty());

    // Each previewed statement must mention only included tables (prev_r1 / prev_r2),
    // never the excluded m3's table (prev_r3).
    for (auto const& sql: previewed)
    {
        CHECK(!sql.contains("prev_r3"));
    }
    bool sawR1 = false;
    bool sawR2 = false;
    for (auto const& sql: previewed)
    {
        if (sql.contains("prev_r1"))
            sawR1 = true;
        if (sql.contains("prev_r2"))
            sawR2 = true;
    }
    CHECK(sawR1);
    CHECK(sawR2);
}

TEST_CASE_METHOD(SqlMigrationTestFixture, "ApplyPendingMigrationsUpTo refuses dependency-cross-boundary", "[SqlMigration]")
{
    using namespace SqlColumnTypeDefinitions;

    // m_below depends on m_above which is *above* the target — applying m_below alone
    // would apply a migration whose dependency is unmet. Engine must refuse up front.
    using MigrationAbove = SqlMigration::Migration<202815020000>;

    auto mAbove = MigrationAbove(
        "cross above", [](SqlMigrationQueryBuilder& plan) { plan.CreateTable("cross_above").PrimaryKey("id", Integer()); });

    auto mBelow = SqlMigration::Migration<202815010000>(
        "cross below",
        SqlMigration::MigrationMetadata { .dependencies = { SqlMigration::TimestampOf<MigrationAbove> } },
        [](SqlMigrationQueryBuilder& plan) { plan.CreateTable("cross_below").PrimaryKey("id", Integer()); });

    auto& manager = SqlMigration::MigrationManager::GetInstance();
    manager.CreateMigrationHistory();

    auto const target = SqlMigration::MigrationTimestamp { 202815010000 };
    auto const _ = ScopedSqlNullLogger {};
    CHECK_THROWS_AS(manager.ApplyPendingMigrationsUpTo(target), std::runtime_error);
    CHECK(manager.GetAppliedMigrationIds().empty());
}

namespace
{

/// @brief Catches every `OnWarning` invocation made while it is the active logger,
/// so tests can assert truncation diagnostics fired (or didn't).
class CapturingWarningLogger: public Lightweight::SqlLogger::Null
{
  public:
    CapturingWarningLogger():
        _previous { &Lightweight::SqlLogger::GetLogger() }
    {
        Lightweight::SqlLogger::SetLogger(*this);
    }

    ~CapturingWarningLogger() override
    {
        Lightweight::SqlLogger::SetLogger(*_previous);
    }

    CapturingWarningLogger(CapturingWarningLogger const&) = delete;
    CapturingWarningLogger(CapturingWarningLogger&&) = delete;
    CapturingWarningLogger& operator=(CapturingWarningLogger const&) = delete;
    CapturingWarningLogger& operator=(CapturingWarningLogger&&) = delete;

    void OnWarning(std::string_view const& message) override
    {
        _warnings.emplace_back(message);
    }

    [[nodiscard]] std::vector<std::string> const& Warnings() const noexcept
    {
        return _warnings;
    }

  private:
    Lightweight::SqlLogger* _previous;
    std::vector<std::string> _warnings;
};

/// @brief Tiny synthetic stand-in for `MigrationBase` used by policy-composition tests.
/// We only need `GetTimestamp` to be callable from a `CompatPolicy` lambda.
class StubMigration: public Lightweight::SqlMigration::MigrationBase
{
  public:
    explicit StubMigration(uint64_t ts):
        MigrationBase(Lightweight::SqlMigration::MigrationTimestamp { ts }, "stub")
    {
    }

    void Up(Lightweight::SqlMigrationQueryBuilder& /*plan*/) const override {}
};

} // namespace

TEST_CASE("ToSql: lup-truncate off leaves oversize INSERT values intact", "[SqlMigration][compat]")
{
    using namespace Lightweight::SqlColumnTypeDefinitions;
    using Lightweight::MigrationRenderContext;
    using Lightweight::SqlColumnDeclaration;
    using Lightweight::SqlCreateTablePlan;
    using Lightweight::SqlInsertDataPlan;
    using Lightweight::SqlMigrationPlanElement;

    Lightweight::SqlServerQueryFormatter const formatter;
    MigrationRenderContext context; // strict by default

    SqlCreateTablePlan const create {
        .schemaName = "",
        .tableName = "T",
        .columns = { SqlColumnDeclaration { .name = "n", .type = NVarchar { 5 } } },
        .foreignKeys = {},
        .ifNotExists = false,
    };
    (void) Lightweight::ToSql(formatter, SqlMigrationPlanElement { create }, context);

    SqlInsertDataPlan insert {
        .schemaName = "", .tableName = "T", .columns = { { "n", Lightweight::SqlVariant(std::string("hellooo")) } }, // 7 > 5
    };

    CapturingWarningLogger capture;
    auto const sql = Lightweight::ToSql(formatter, SqlMigrationPlanElement { insert }, context);

    REQUIRE(sql.size() == 1);
    // Strict mode: the value lands in the INSERT verbatim.
    CHECK(sql[0].contains("'hellooo'"));
    CHECK(capture.Warnings().empty());
}

TEST_CASE("ToSql: lup-truncate on clips oversize INSERT values and warns", "[SqlMigration][compat]")
{
    using namespace Lightweight::SqlColumnTypeDefinitions;
    using Lightweight::MigrationRenderContext;
    using Lightweight::SqlColumnDeclaration;
    using Lightweight::SqlCreateTablePlan;
    using Lightweight::SqlInsertDataPlan;
    using Lightweight::SqlMigrationPlanElement;

    Lightweight::SqlServerQueryFormatter const formatter;
    MigrationRenderContext context;
    context.lupTruncate = true;
    context.activeMigrationTimestamp = 20'200'101'120'000ULL;
    context.activeMigrationTitle = "widen N column";

    SqlCreateTablePlan const create {
        .schemaName = "",
        .tableName = "T",
        .columns = { SqlColumnDeclaration { .name = "n", .type = NVarchar { 5 } } },
        .foreignKeys = {},
        .ifNotExists = false,
    };
    (void) Lightweight::ToSql(formatter, SqlMigrationPlanElement { create }, context);

    SqlInsertDataPlan const insert {
        .schemaName = "",
        .tableName = "T",
        .columns = { { "n", Lightweight::SqlVariant(std::string("hellooo")) } },
    };

    CapturingWarningLogger capture;
    auto const sql = Lightweight::ToSql(formatter, SqlMigrationPlanElement { insert }, context);

    REQUIRE(sql.size() == 1);
    CHECK(sql[0].contains("'hello'"));    // truncated to 5 chars
    CHECK(!sql[0].contains("'hellooo'")); // original gone
    REQUIRE(capture.Warnings().size() == 1);
    auto const& warning = capture.Warnings()[0];
    CHECK(warning.contains("lup-truncate"));
    CHECK(warning.contains("T.n"));
    CHECK(warning.contains("declared width 5"));
    // Migration identity attribution + the rendered SQL must travel with the warning so
    // operators can answer "which migration?" / "which statement?" without re-running.
    CHECK(warning.contains("20200101120000"));
    CHECK(warning.contains("widen N column"));
    CHECK(warning.contains("statement: "));
    CHECK(warning.contains(sql[0]));
}

TEST_CASE("ToSql: lup-truncate handles UTF-8 multi-byte by character count", "[SqlMigration][compat]")
{
    using namespace Lightweight::SqlColumnTypeDefinitions;
    using Lightweight::MigrationRenderContext;
    using Lightweight::SqlColumnDeclaration;
    using Lightweight::SqlCreateTablePlan;
    using Lightweight::SqlInsertDataPlan;
    using Lightweight::SqlMigrationPlanElement;

    Lightweight::SqlServerQueryFormatter const formatter;
    MigrationRenderContext context;
    context.lupTruncate = true;

    SqlCreateTablePlan const create {
        .schemaName = "",
        .tableName = "T",
        .columns = { SqlColumnDeclaration { .name = "n", .type = NVarchar { 4 } } },
        .foreignKeys = {},
        .ifNotExists = false,
    };
    (void) Lightweight::ToSql(formatter, SqlMigrationPlanElement { create }, context);

    // "Körnung" is 7 characters / 8 UTF-8 bytes. Width 4 → keep first 4 chars ("Körn").
    // The MSSQL formatter then encodes the literal as `N'K' + NCHAR(246) + N'rn'` so
    // arbitrary UTF-8 round-trips correctly under the database's narrow code page.
    SqlInsertDataPlan const insert {
        .schemaName = "",
        .tableName = "T",
        .columns = { { "n", Lightweight::SqlVariant(std::string("Körnung")) } },
    };

    CapturingWarningLogger capture;
    auto const sql = Lightweight::ToSql(formatter, SqlMigrationPlanElement { insert }, context);

    REQUIRE(sql.size() == 1);
    // 'ö' is U+00F6 = 246 decimal. The encoder splits the run on the non-ASCII char.
    CHECK(sql[0].contains("N'K' + NCHAR(246) + N'rn'"));
    CHECK(capture.Warnings().size() == 1);
}

TEST_CASE("ToSql: lup-truncate truncates string_view values from char-array literals", "[SqlMigration][compat]")
{
    using namespace Lightweight::SqlColumnTypeDefinitions;
    using Lightweight::MigrationRenderContext;
    using Lightweight::SqlColumnDeclaration;
    using Lightweight::SqlCreateTablePlan;
    using Lightweight::SqlInsertDataPlan;
    using Lightweight::SqlMigrationPlanElement;

    Lightweight::SqlServerQueryFormatter const formatter;
    MigrationRenderContext context;
    context.lupTruncate = true;

    SqlCreateTablePlan const create {
        .schemaName = "",
        .tableName = "T",
        .columns = { SqlColumnDeclaration { .name = "n", .type = NVarchar { 5 } } },
        .foreignKeys = {},
        .ifNotExists = false,
    };
    (void) Lightweight::ToSql(formatter, SqlMigrationPlanElement { create }, context);

    // Char-array literal forces the SqlVariant string_view ctor — the path that
    // the silent-truncation regression on the LUP plugin exposed.
    SqlInsertDataPlan const insert {
        .schemaName = "",
        .tableName = "T",
        .columns = { { "n", Lightweight::SqlVariant("hellooo") } },
    };

    auto const sql = Lightweight::ToSql(formatter, SqlMigrationPlanElement { insert }, context);

    REQUIRE(sql.size() == 1);
    CHECK(sql[0].contains("'hello'"));
}

TEST_CASE("ToSql: DROP TABLE evicts column widths from the cache", "[SqlMigration][compat]")
{
    using namespace Lightweight::SqlColumnTypeDefinitions;
    using Lightweight::MigrationRenderContext;
    using Lightweight::SqlColumnDeclaration;
    using Lightweight::SqlCreateTablePlan;
    using Lightweight::SqlDropTablePlan;
    using Lightweight::SqlInsertDataPlan;
    using Lightweight::SqlMigrationPlanElement;

    Lightweight::SqlServerQueryFormatter const formatter;
    MigrationRenderContext context;
    context.lupTruncate = true;

    SqlCreateTablePlan const create {
        .schemaName = "",
        .tableName = "T",
        .columns = { SqlColumnDeclaration { .name = "n", .type = NVarchar { 5 } } },
        .foreignKeys = {},
        .ifNotExists = false,
    };
    (void) Lightweight::ToSql(formatter, SqlMigrationPlanElement { create }, context);
    REQUIRE(context.columnWidths.size() == 1);

    SqlDropTablePlan const drop { .schemaName = "", .tableName = "T", .ifExists = false, .cascade = false };
    (void) Lightweight::ToSql(formatter, SqlMigrationPlanElement { drop }, context);

    CHECK(context.columnWidths.empty());

    // After DROP, an oversize INSERT no longer has a known width to clip against,
    // so the value passes through untouched (and unwarned).
    SqlInsertDataPlan const insert {
        .schemaName = "",
        .tableName = "T",
        .columns = { { "n", Lightweight::SqlVariant(std::string("hellooo")) } },
    };
    CapturingWarningLogger capture;
    auto const sql = Lightweight::ToSql(formatter, SqlMigrationPlanElement { insert }, context);
    REQUIRE(sql.size() == 1);
    CHECK(sql[0].contains("'hellooo'"));
    CHECK(capture.Warnings().empty());
}

TEST_CASE_METHOD(SqlMigrationTestFixture,
                 "MigrationManager: ComposeCompatPolicy unions flag sets per migration",
                 "[SqlMigration][compat]")
{
    auto& mgr = Lightweight::SqlMigration::MigrationManager::GetInstance();
    REQUIRE(!mgr.GetCompatPolicy()); // fixture clears state

    mgr.SetCompatPolicy([](Lightweight::SqlMigration::MigrationBase const&) { return std::set<std::string> { "alpha" }; });
    mgr.ComposeCompatPolicy(
        [](Lightweight::SqlMigration::MigrationBase const&) { return std::set<std::string> { "beta" }; });

    StubMigration const m { 20'000'000'000'001ULL };
    auto const flags = mgr.CompatFlagsFor(m);
    CHECK(flags.size() == 2);
    CHECK(flags.contains("alpha"));
    CHECK(flags.contains("beta"));

    // Reset so other tests aren't affected.
    mgr.SetCompatPolicy({});
}

TEST_CASE_METHOD(SqlMigrationTestFixture,
                 "MigrationManager: per-migration policy enables lup-truncate by timestamp",
                 "[SqlMigration][compat]")
{
    auto& mgr = Lightweight::SqlMigration::MigrationManager::GetInstance();
    constexpr uint64_t kCutoff = 20'000'000'060'000ULL;

    mgr.SetCompatPolicy([](Lightweight::SqlMigration::MigrationBase const& m) {
        if (m.GetTimestamp().value < kCutoff)
            return std::set<std::string> { std::string(Lightweight::CompatFlagLupTruncateName) };
        return std::set<std::string> {};
    });

    StubMigration const legacy { kCutoff - 1 };
    StubMigration const modern { kCutoff };

    CHECK(mgr.CompatFlagsFor(legacy).contains("lup-truncate"));
    CHECK(mgr.CompatFlagsFor(modern).empty());

    mgr.SetCompatPolicy({});
}

// ============================================================================
// Tests for FoldRegisteredMigrations / HardReset / UnicodeUpgradeTables
// ============================================================================

namespace fold_test
{

using namespace Lightweight::SqlColumnTypeDefinitions;

/// @brief Test migration that takes a Up-builder closure and a timestamp. Used in
/// fold-only tests to register varied migration shapes in a single test case.
template <uint64_t Ts>
class FoldStub: public SqlMigration::MigrationBase
{
  public:
    using BuildFn = std::function<void(SqlMigrationQueryBuilder&)>;

    FoldStub(std::string_view title, BuildFn build):
        MigrationBase(SqlMigration::MigrationTimestamp { Ts }, title),
        _build(std::move(build))
    {
    }

    void Up(SqlMigrationQueryBuilder& builder) const override
    {
        _build(builder);
    }

  private:
    BuildFn _build;
};

} // namespace fold_test

TEST_CASE_METHOD(SqlMigrationTestFixture, "Fold: create + addcolumn + altercolumn", "[SqlMigration][Fold]")
{
    using namespace Lightweight::SqlColumnTypeDefinitions;
    auto& mgr = SqlMigration::MigrationManager::GetInstance();

    fold_test::FoldStub<20'10'01'00'00'01> m1 {
        "create users",
        [](SqlMigrationQueryBuilder& plan) {
            plan.CreateTable("users").PrimaryKey("id", Bigint()).Column("name", Varchar(100));
        }
    };
    fold_test::FoldStub<20'10'01'00'00'02> m2 { "add email", [](SqlMigrationQueryBuilder& plan) {
                                                   plan.AlterTable("users").AddColumn("email", Varchar(255));
                                               } };
    fold_test::FoldStub<20'10'01'00'00'03> m3 { "widen name", [](SqlMigrationQueryBuilder& plan) {
                                                   plan.AlterTable("users").AlterColumn(
                                                       "name", NVarchar(200), SqlNullable::Null);
                                               } };

    auto const fold = mgr.FoldRegisteredMigrations(SqlQueryFormatter::Sqlite());

    REQUIRE(fold.foldedMigrations.size() == 3);
    REQUIRE(fold.tables.size() == 1);
    REQUIRE(fold.creationOrder.size() == 1);

    auto const it = fold.tables.find(SqlSchema::FullyQualifiedTableName { .catalog = {}, .schema = {}, .table = "users" });
    REQUIRE(it != fold.tables.end());
    auto const& state = it->second;

    REQUIRE(state.columns.size() == 3);
    CHECK(state.columns[0].name == "id");
    CHECK(state.columns[1].name == "name");
    CHECK(state.columns[2].name == "email");

    // The 'name' column was widened from Varchar(100) → NVarchar(200) by m3.
    auto const* nv = std::get_if<NVarchar>(&state.columns[1].type);
    REQUIRE(nv != nullptr);
    CHECK(nv->size == 200);
}

TEST_CASE_METHOD(SqlMigrationTestFixture, "Fold: drop table cleans residual references", "[SqlMigration][Fold]")
{
    using namespace Lightweight::SqlColumnTypeDefinitions;
    auto& mgr = SqlMigration::MigrationManager::GetInstance();

    fold_test::FoldStub<20'10'02'00'00'01> m1 { "create temp", [](SqlMigrationQueryBuilder& plan) {
                                                   plan.CreateTable("temp_table").PrimaryKey("id", Bigint());
                                               } };
    fold_test::FoldStub<20'10'02'00'00'02> m2 { "create idx on temp", [](SqlMigrationQueryBuilder& plan) {
                                                   plan.CreateIndex("idx_temp_id", "temp_table", { "id" });
                                               } };
    fold_test::FoldStub<20'10'02'00'00'03> m3 { "drop temp", [](SqlMigrationQueryBuilder& plan) {
                                                   plan.DropTable("temp_table");
                                               } };

    auto const fold = mgr.FoldRegisteredMigrations(SqlQueryFormatter::Sqlite());

    CHECK(fold.tables.empty());
    CHECK(fold.creationOrder.empty());
    CHECK(fold.indexes.empty());
}

TEST_CASE_METHOD(SqlMigrationTestFixture, "Fold: data steps preserve chronological order", "[SqlMigration][Fold]")
{
    using namespace Lightweight::SqlColumnTypeDefinitions;
    auto& mgr = SqlMigration::MigrationManager::GetInstance();

    fold_test::FoldStub<20'10'03'00'00'01> m1 {
        "create + first insert",
        [](SqlMigrationQueryBuilder& plan) {
            plan.CreateTable("logs").PrimaryKey("id", Bigint()).Column("msg", Varchar(255));
            plan.Insert("logs").Set("msg", "first"sv);
        }
    };
    fold_test::FoldStub<20'10'03'00'00'02> m2 { "second insert", [](SqlMigrationQueryBuilder& plan) {
                                                   plan.Insert("logs").Set("msg", "second"sv);
                                               } };

    auto const fold = mgr.FoldRegisteredMigrations(SqlQueryFormatter::Sqlite());

    REQUIRE(fold.dataSteps.size() == 2);
    CHECK(fold.dataSteps[0].sourceTimestamp.value == 20'10'03'00'00'01ULL);
    CHECK(fold.dataSteps[1].sourceTimestamp.value == 20'10'03'00'00'02ULL);
}

TEST_CASE_METHOD(SqlMigrationTestFixture, "Fold: --up-to truncates correctly", "[SqlMigration][Fold]")
{
    using namespace Lightweight::SqlColumnTypeDefinitions;
    auto& mgr = SqlMigration::MigrationManager::GetInstance();

    fold_test::FoldStub<20'10'04'00'00'01> m1 { "v1", [](SqlMigrationQueryBuilder& plan) {
                                                   plan.CreateTable("a").PrimaryKey("id", Bigint());
                                               } };
    fold_test::FoldStub<20'10'04'00'00'02> m2 { "v2", [](SqlMigrationQueryBuilder& plan) {
                                                   plan.CreateTable("b").PrimaryKey("id", Bigint());
                                               } };
    fold_test::FoldStub<20'10'04'00'00'03> m3 { "v3", [](SqlMigrationQueryBuilder& plan) {
                                                   plan.CreateTable("c").PrimaryKey("id", Bigint());
                                               } };

    auto const cutoff = SqlMigration::MigrationTimestamp { 20'10'04'00'00'02ULL };
    auto const fold = mgr.FoldRegisteredMigrations(SqlQueryFormatter::Sqlite(), cutoff);

    CHECK(fold.foldedMigrations.size() == 2);
    CHECK(fold.tables.size() == 2);
    CHECK(fold.tables.contains(SqlSchema::FullyQualifiedTableName { .catalog = {}, .schema = {}, .table = "a" }));
    CHECK(fold.tables.contains(SqlSchema::FullyQualifiedTableName { .catalog = {}, .schema = {}, .table = "b" }));
    CHECK_FALSE(fold.tables.contains(SqlSchema::FullyQualifiedTableName { .catalog = {}, .schema = {}, .table = "c" }));
}

TEST_CASE_METHOD(SqlMigrationTestFixture, "Fold: RawSql passes through unmodified", "[SqlMigration][Fold]")
{
    using namespace Lightweight::SqlColumnTypeDefinitions;
    auto& mgr = SqlMigration::MigrationManager::GetInstance();

    fold_test::FoldStub<20'10'05'00'00'01> m1 { "raw", [](SqlMigrationQueryBuilder& plan) {
                                                   plan.RawSql("PRAGMA foreign_keys = ON");
                                               } };

    auto const fold = mgr.FoldRegisteredMigrations(SqlQueryFormatter::Sqlite());

    REQUIRE(fold.dataSteps.size() == 1);
    auto const* raw = std::get_if<SqlRawSqlPlan>(&fold.dataSteps[0].element);
    REQUIRE(raw != nullptr);
    CHECK(raw->sql == "PRAGMA foreign_keys = ON");
}

TEST_CASE_METHOD(SqlMigrationTestFixture, "Fold: rename column propagates to FK references", "[SqlMigration][Fold]")
{
    using namespace Lightweight::SqlColumnTypeDefinitions;
    auto& mgr = SqlMigration::MigrationManager::GetInstance();

    fold_test::FoldStub<20'10'06'00'00'01> m1 { "create base", [](SqlMigrationQueryBuilder& plan) {
                                                   plan.CreateTable("users").PrimaryKey("id", Bigint());
                                               } };
    fold_test::FoldStub<20'10'06'00'00'02> m2 { "rename column", [](SqlMigrationQueryBuilder& plan) {
                                                   plan.AlterTable("users").RenameColumn("id", "user_id");
                                               } };

    auto const fold = mgr.FoldRegisteredMigrations(SqlQueryFormatter::Sqlite());
    auto const it = fold.tables.find(SqlSchema::FullyQualifiedTableName { .catalog = {}, .schema = {}, .table = "users" });
    REQUIRE(it != fold.tables.end());
    REQUIRE(it->second.columns.size() == 1);
    CHECK(it->second.columns[0].name == "user_id");
}

TEST_CASE_METHOD(SqlMigrationTestFixture, "Fold: respects releases falling within range", "[SqlMigration][Fold]")
{
    using namespace Lightweight::SqlColumnTypeDefinitions;
    auto& mgr = SqlMigration::MigrationManager::GetInstance();

    fold_test::FoldStub<20'10'07'00'00'01> m1 { "release-1 migration", [](SqlMigrationQueryBuilder& plan) {
                                                   plan.CreateTable("a").PrimaryKey("id", Bigint());
                                               } };
    mgr.RegisterRelease("1.0.0", SqlMigration::MigrationTimestamp { 20'10'07'00'00'01ULL });

    fold_test::FoldStub<20'10'07'00'00'02> m2 { "release-2 migration", [](SqlMigrationQueryBuilder& plan) {
                                                   plan.CreateTable("b").PrimaryKey("id", Bigint());
                                               } };
    mgr.RegisterRelease("2.0.0", SqlMigration::MigrationTimestamp { 20'10'07'00'00'02ULL });

    auto const fold =
        mgr.FoldRegisteredMigrations(SqlQueryFormatter::Sqlite(), SqlMigration::MigrationTimestamp { 20'10'07'00'00'01ULL });

    REQUIRE(fold.releases.size() == 1);
    CHECK(fold.releases[0].version == "1.0.0");
}

// ============================================================================
// SQLite end-to-end tests for HardReset
// ============================================================================

TEST_CASE_METHOD(SqlMigrationTestFixture,
                 "HardReset: drops migrated tables and schema_migrations",
                 "[SqlMigration][HardReset]")
{
    using namespace Lightweight::SqlColumnTypeDefinitions;
    auto& mgr = SqlMigration::MigrationManager::GetInstance();

    fold_test::FoldStub<20'10'08'00'00'01> m1 { "create A", [](SqlMigrationQueryBuilder& plan) {
                                                   plan.CreateTable("table_a").PrimaryKey("id", Bigint());
                                               } };
    fold_test::FoldStub<20'10'08'00'00'02> m2 { "create B", [](SqlMigrationQueryBuilder& plan) {
                                                   plan.CreateTable("table_b").PrimaryKey("id", Bigint());
                                               } };

    mgr.CreateMigrationHistory();
    REQUIRE(mgr.ApplyPendingMigrations() == 2);

    auto const dryRun = mgr.HardReset(/*dryRun=*/true);
    CHECK(dryRun.wasDryRun);
    CHECK(dryRun.droppedTables.size() == 2);
    CHECK_FALSE(dryRun.schemaMigrationsDropped);

    auto const live = mgr.HardReset(/*dryRun=*/false);
    CHECK_FALSE(live.wasDryRun);
    CHECK(live.droppedTables.size() == 2);
    CHECK(live.schemaMigrationsDropped);

    // Re-apply should succeed cleanly.
    mgr.CreateMigrationHistory();
    CHECK(mgr.ApplyPendingMigrations() == 2);
}

TEST_CASE_METHOD(SqlMigrationTestFixture, "HardReset: preserves user tables", "[SqlMigration][HardReset]")
{
    using namespace Lightweight::SqlColumnTypeDefinitions;
    auto& mgr = SqlMigration::MigrationManager::GetInstance();

    fold_test::FoldStub<20'10'09'00'00'01> m1 { "create migrated", [](SqlMigrationQueryBuilder& plan) {
                                                   plan.CreateTable("migrated_t").PrimaryKey("id", Bigint());
                                               } };

    mgr.CreateMigrationHistory();
    REQUIRE(mgr.ApplyPendingMigrations() == 1);

    // Create a user-owned table outside the migration system.
    auto stmt = SqlStatement { mgr.GetDataMapper().Connection() };
    (void) stmt.ExecuteDirect("CREATE TABLE user_t (id INTEGER PRIMARY KEY)");

    auto const result = mgr.HardReset(/*dryRun=*/false);
    REQUIRE(result.preservedTables.size() == 1);
    CHECK(result.preservedTables[0].table == "user_t");

    // Verify user table still exists after the reset. SqlSchema::ReadAllTables is the
    // portable way to introspect the live catalog — direct queries against
    // `sqlite_schema` would only work on SQLite.
    auto verifyStmt = SqlStatement { mgr.GetDataMapper().Connection() };
    auto const liveTables = SqlSchema::ReadAllTables(verifyStmt, std::string {}, std::string {});
    auto const it = std::ranges::find_if(liveTables, [](SqlSchema::Table const& t) { return t.name == "user_t"; });
    REQUIRE(it != liveTables.end());
}

TEST_CASE_METHOD(SqlMigrationTestFixture, "HardReset: dry-run is observationally pure", "[SqlMigration][HardReset]")
{
    using namespace Lightweight::SqlColumnTypeDefinitions;
    auto& mgr = SqlMigration::MigrationManager::GetInstance();

    fold_test::FoldStub<20'10'10'00'00'01> m1 { "create T", [](SqlMigrationQueryBuilder& plan) {
                                                   plan.CreateTable("t1").PrimaryKey("id", Bigint());
                                               } };

    mgr.CreateMigrationHistory();
    REQUIRE(mgr.ApplyPendingMigrations() == 1);

    auto const before = mgr.GetAppliedMigrationIds().size();
    auto const result = mgr.HardReset(/*dryRun=*/true);
    auto const after = mgr.GetAppliedMigrationIds().size();
    CHECK(before == after);
    CHECK_FALSE(result.schemaMigrationsDropped);
}

// ============================================================================
// UnicodeUpgradeTables — SQLite end-to-end
// ============================================================================

TEST_CASE_METHOD(SqlMigrationTestFixture, "UnicodeUpgradeTables: dry-run reports drift", "[SqlMigration][UnicodeUpgrade]")
{
    using namespace Lightweight::SqlColumnTypeDefinitions;
    auto& mgr = SqlMigration::MigrationManager::GetInstance();

    // Apply a migration that creates a Varchar(100) column. The fold's intended type
    // would also be Varchar(100), so no drift expected.
    fold_test::FoldStub<20'10'11'00'00'01> m1 {
        "create t",
        [](SqlMigrationQueryBuilder& plan) {
            plan.CreateTable("widen_me").PrimaryKey("id", Bigint()).Column("name", Varchar(50));
        }
    };

    mgr.CreateMigrationHistory();
    REQUIRE(mgr.ApplyPendingMigrations() == 1);

    auto const result = mgr.UnicodeUpgradeTables(/*dryRun=*/true);
    CHECK(result.wasDryRun);
    // Same intended/live types — no entries.
    CHECK(result.columns.empty());
}

TEST_CASE_METHOD(SqlMigrationTestFixture,
                 "UnicodeUpgradeTables: completes without error on roundtrip",
                 "[SqlMigration][UnicodeUpgrade]")
{
    using namespace Lightweight::SqlColumnTypeDefinitions;
    auto& mgr = SqlMigration::MigrationManager::GetInstance();

    // SQLite is a special case: its ODBC driver doesn't reliably distinguish
    // VARCHAR from NVARCHAR, so SqlSchema::ReadAllTables reports the live column
    // as Varchar even though the migration declared NVarchar. The upgrader will
    // therefore see "drift" and rebuild the table — but the rebuild must succeed
    // and the table must remain queryable afterwards.
    fold_test::FoldStub<20'10'12'00'00'01> m1 {
        "create t",
        [](SqlMigrationQueryBuilder& plan) {
            plan.CreateTable("idem").PrimaryKey("id", Bigint()).Column("note", NVarchar(80));
        }
    };

    mgr.CreateMigrationHistory();
    REQUIRE(mgr.ApplyPendingMigrations() == 1);

    // Should run cleanly without exceptions.
    auto const result = mgr.UnicodeUpgradeTables(/*dryRun=*/false);
    (void) result;

    // After the upgrade the table must still be queryable. SqlSchema::ReadAllTables is
    // the portable way to introspect — `sqlite_schema` is SQLite-only.
    auto stmt = SqlStatement { mgr.GetDataMapper().Connection() };
    auto const liveTables = SqlSchema::ReadAllTables(stmt, std::string {}, std::string {});
    auto const it = std::ranges::find_if(liveTables, [](SqlSchema::Table const& t) { return t.name == "idem"; });
    REQUIRE(it != liveTables.end());
}

// ============================================================================
// SplitFileWriter unit tests
// ============================================================================

TEST_CASE("SplitFileWriter: bin-packs blocks within budget", "[CodeGen][SplitFileWriter]")
{
    using namespace Lightweight::CodeGen;

    std::vector<CodeBlock> blocks {
        { .content = "A\n", .lineCount = 1 },
        { .content = "B\nB\n", .lineCount = 2 },
        { .content = "C\n", .lineCount = 1 },
        { .content = "D\nD\nD\n", .lineCount = 3 },
    };
    auto const chunks = GroupBlocksByLineBudget(blocks, 3);
    REQUIRE(chunks.size() >= 2);
    for (auto const& chunk: chunks)
        CHECK_FALSE(chunk.empty());
}

TEST_CASE("SplitFileWriter: emits single chunk when total fits", "[CodeGen][SplitFileWriter]")
{
    using namespace Lightweight::CodeGen;
    std::vector<CodeBlock> blocks {
        { .content = "A\n", .lineCount = 1 },
        { .content = "B\n", .lineCount = 1 },
    };
    auto const chunks = GroupBlocksByLineBudget(blocks, 100);
    REQUIRE(chunks.size() == 1);
    CHECK(chunks[0].size() == 2);
}

TEST_CASE("SplitFileWriter: maxLinesPerFile=0 keeps everything in one chunk", "[CodeGen][SplitFileWriter]")
{
    using namespace Lightweight::CodeGen;
    std::vector<CodeBlock> blocks {
        { .content = "x\n", .lineCount = 1 },
        { .content = "y\n", .lineCount = 1 },
        { .content = "z\n", .lineCount = 1 },
    };
    auto const chunks = GroupBlocksByLineBudget(blocks, 0);
    REQUIRE(chunks.size() == 1);
    CHECK(chunks[0].size() == 3);
}

TEST_CASE("SplitFileWriter: oversize block lands wholly in its own chunk", "[CodeGen][SplitFileWriter]")
{
    using namespace Lightweight::CodeGen;
    std::vector<CodeBlock> blocks {
        { .content = "small\n", .lineCount = 1 },
        { .content = "huge\nhuge\nhuge\nhuge\nhuge\nhuge\nhuge\nhuge\nhuge\n", .lineCount = 9 },
        { .content = "tail\n", .lineCount = 1 },
    };
    auto const chunks = GroupBlocksByLineBudget(blocks, 3);
    // Since the huge block exceeds the budget but cannot be split, it must occupy
    // its own chunk while small/tail can be elsewhere.
    REQUIRE(chunks.size() >= 2);
    bool foundSoloHuge = false;
    for (auto const& chunk: chunks)
        if (chunk.size() == 1 && chunk[0].lineCount == 9)
            foundSoloHuge = true;
    CHECK(foundSoloHuge);
}

TEST_CASE_METHOD(SqlMigrationTestFixture, "AdvisoryLockOps: returns the dialect-correct handler", "[SqlScopedLock]")
{
    auto& dm = SqlMigration::MigrationManager::GetInstance().GetDataMapper();
    auto const& formatter = dm.Connection().QueryFormatter();

    // Process-singleton handler: same reference every time, so callers can
    // safely cache the pointer.
    auto const& a = formatter.AdvisoryLockOps();
    auto const& b = formatter.AdvisoryLockOps();
    CHECK(&a == &b);
}

TEST_CASE_METHOD(SqlMigrationTestFixture, "SqlScopedLock acquire/release happy path", "[SqlScopedLock]")
{
    auto& dm = SqlMigration::MigrationManager::GetInstance().GetDataMapper();

    // Throwing constructor for ergonomic use.
    {
        SqlScopedLock lock { dm.Connection(), "lightweight_test_lock" };
        CHECK(lock.IsLocked());
        CHECK(lock.Name() == "lightweight_test_lock");
    }

    // The non-throwing factory returns a fully-typed expected so callers
    // can inspect SqlLockError directly.
    auto maybe = SqlScopedLock::TryConstruct(dm.Connection(), "lightweight_test_lock_2", std::chrono::seconds(5));
    REQUIRE(maybe.has_value());
    CHECK(maybe->IsLocked());

    // Explicit release returns void on success and yields the held lock.
    auto release = maybe->Release();
    CHECK(release.has_value());
    CHECK_FALSE(maybe->IsLocked());
}

TEST_CASE_METHOD(SqlMigrationTestFixture, "SqlScopedLock release is idempotent", "[SqlScopedLock]")
{
    auto& dm = SqlMigration::MigrationManager::GetInstance().GetDataMapper();
    SqlScopedLock lock { dm.Connection(), "lightweight_idempotent_release" };

    // First release succeeds; second release on an already-released lock
    // is a no-op success — the contract is "idempotent" so the destructor
    // doesn't have to special-case explicit Release().
    CHECK(lock.Release().has_value());
    CHECK(lock.Release().has_value());
    CHECK_FALSE(lock.IsLocked());
}

TEST_CASE_METHOD(SqlMigrationTestFixture,
                 "HardReset drops bookkeeping tables (lock table) without preserving them",
                 "[SqlMigration][HardReset]")
{
    auto& dm = SqlMigration::MigrationManager::GetInstance().GetDataMapper();
    auto const& formatter = dm.Connection().QueryFormatter();

    // Touch the lock so any backend with a lock-table primitive (SQLite)
    // creates `_lightweight_locks` in the live schema. Server-native
    // backends (SQL Server / PostgreSQL) keep the bookkeeping list empty
    // and the test still validates the not-preserved contract trivially.
    {
        SqlScopedLock lock { dm.Connection(), "lightweight_test_hardreset" };
        CHECK(lock.IsLocked());
    }

    auto& mgr = SqlMigration::MigrationManager::GetInstance();
    mgr.CreateMigrationHistory();

    auto const result = mgr.HardReset(/*dryRun=*/false);

    auto const bookkeeping = formatter.AdvisoryLockOps().BookkeepingTableNames();
    for (auto const& name: bookkeeping)
    {
        // Bookkeeping table must NOT appear in `preservedTables` — the
        // user-data list — even though it has no migration backing it.
        bool wasPreserved =
            std::ranges::any_of(result.preservedTables, [&](auto const& fqtn) { return fqtn.table == name; });
        CAPTURE(name);
        CHECK_FALSE(wasPreserved);
    }
}

TEST_CASE_METHOD(SqlMigrationTestFixture, "SetDefaultSchema rejects unsafe identifiers", "[SqlMigration]")
{
    auto& mgr = SqlMigration::MigrationManager::GetInstance();

    // Always reset hook state at the end so we don't pollute later cases.
    auto resetHook = [] {
        SqlConnection::ResetPostConnectedHook();
    };

    CHECK_THROWS_AS(mgr.SetDefaultSchema("evil; DROP TABLE x;--"), std::invalid_argument);
    CHECK_THROWS_AS(mgr.SetDefaultSchema(R"(quote"injection)"), std::invalid_argument);
    CHECK_THROWS_AS(mgr.SetDefaultSchema("with space"), std::invalid_argument);
    CHECK(mgr.DefaultSchema().empty());

    // Underscore + digits are fine.
    CHECK_NOTHROW(mgr.SetDefaultSchema("lasa_v2"));
    CHECK(mgr.DefaultSchema() == "lasa_v2");

    // Empty clears.
    CHECK_NOTHROW(mgr.SetDefaultSchema(""));
    CHECK(mgr.DefaultSchema().empty());

    resetHook();
}

TEST_CASE_METHOD(SqlMigrationTestFixture, "SetDefaultSchema PostgreSQL search_path applies on connect", "[SqlMigration]")
{
    // Only PostgreSQL has a session-level "default schema" we can flip from
    // a post-connect hook. SQL Server's default schema is login-level and
    // SQLite has no schema concept — both are no-ops here.
    auto& mgr = SqlMigration::MigrationManager::GetInstance();

    auto probeConn = SqlConnection {};
    if (probeConn.ServerType() != SqlServerType::POSTGRESQL)
    {
        WARN(std::format("TODO({}): SetDefaultSchemaStatement is only meaningful on PostgreSQL; skipping live-effect check.",
                         probeConn.ServerType()));
        SqlConnection::ResetPostConnectedHook();
        return;
    }

    // Create the target schema on the live DB. Drop it afterwards so the
    // test is hermetic.
    auto setup = SqlStatement { probeConn };
    (void) setup.ExecuteDirect(R"(CREATE SCHEMA IF NOT EXISTS "lw_test_schema")");
    auto cleanup = std::shared_ptr<void>(nullptr, [&](void*) {
        try
        {
            auto stmt = SqlStatement { probeConn };
            (void) stmt.ExecuteDirect(R"(DROP SCHEMA IF EXISTS "lw_test_schema" CASCADE)");
        }
        catch (std::exception const& ex)
        {
            // Cleanup is best-effort: the connection may already be dead or
            // the test may have aborted before the schema was created. Log
            // so a real failure isn't silently masked.
            WARN("cleanup of lw_test_schema failed: " << ex.what());
        }
        SqlConnection::ResetPostConnectedHook();
    });

    mgr.SetDefaultSchema("lw_test_schema");

    // Force a fresh connection so the post-connect hook actually fires.
    auto freshConn = SqlConnection { SqlConnection::DefaultConnectionString() };
    auto stmt = SqlStatement { freshConn };
    auto cursor = stmt.ExecuteDirect("SHOW search_path");
    REQUIRE(cursor.FetchRow());
    auto const searchPath = cursor.GetColumn<std::string>(1);
    INFO("search_path after connect: " << searchPath);
    // PostgreSQL renders this as: "lw_test_schema", public
    CHECK(searchPath.contains("lw_test_schema"));

    // Clearing the schema removes the hook so subsequent connects don't try
    // to switch to a now-dropped schema.
    mgr.SetDefaultSchema("");
}

// =============================================================================
// Regression tests for the MS SQL Server "Connection is busy with results for
// another command" failure that appeared while applying the Lastrada migration
// sequence through dbtool / dbtool-gui.
//
// Root cause being pinned by these tests:
//   - SQL Server's ODBC driver returns row-count / done-in-proc tokens after
//     every statement in a T-SQL batch, plus a real result set for any embedded
//     SELECT. The application must drain them with SQLMoreResults until
//     SQL_NO_DATA before the *connection* will accept another command.
//   - Lightweight's `SqlStatement::CloseCursor` only calls
//     `SQLFreeStmt(SQL_CLOSE)`, which closes the current cursor but does NOT
//     advance past pending result sets in a batch.
//   - With MARS off (the default on every connection string we ship), this
//     leaves the connection in the "busy" state and the next statement on a
//     *different* `SqlStatement` handle of the same `SqlConnection` fails
//     with HY000.
//
// Both tests are gated on MS SQL Server — only that driver produces the
// pending-result-set state we need to exercise.
// =============================================================================

TEST_CASE_METHOD(SqlMigrationTestFixture,
                 "Regression: repeated multi-statement DDL batches do not leave connection busy",
                 "[SqlStatement][regression][mssql]")
{
    auto& dm = SqlMigration::MigrationManager::GetInstance().GetDataMapper();
    auto& conn = dm.Connection();

    if (conn.ServerType() != SqlServerType::MICROSOFT_SQL)
    {
        SUCCEED("Reproduces only on MS SQL Server (MARS off, NOCOUNT off).");
        return;
    }

    // Mirror the migration-runner's actual pattern: each migration wraps
    // multiple multi-statement DDL batches in a transaction, then inserts a
    // tracking row and commits. We run many such cycles to exercise the same
    // pending-result-set accumulation that surfaces in 356-migration LUP runs.
    constexpr int kCycles = 60;
    constexpr std::string_view probeTable = "_lw_busy_probe";

    auto cleanup = [&] {
        try
        {
            auto stmt = SqlStatement { conn };
            (void) stmt.ExecuteDirect(std::format("DROP TABLE IF EXISTS dbo.{};", probeTable));
        }
        catch (std::exception const& e)
        {
            // Best-effort: cleanup ahead of CREATE TABLE; ignore failures (e.g., the table
            // never existed) but surface the reason so a regression in DROP isn't silent.
            UNSCOPED_INFO(std::format("probe cleanup ignored exception: {}", e.what()));
        }
    };
    cleanup(); // pre-clean in case a prior test crashed mid-cycle

    {
        auto stmt = SqlStatement { conn };
        (void) stmt.ExecuteDirect(std::format("CREATE TABLE dbo.{} (id INT NULL);", probeTable));
    }

    for (auto const cycle: std::views::iota(0, kCycles))
    {
        auto const colName = std::format("col_{}", cycle);
        auto transaction = SqlTransaction { conn, SqlTransactionMode::ROLLBACK };

        // Multi-step "migration": each step is a multi-statement T-SQL batch
        // matching the failing real-world shape:
        //     IF NOT EXISTS (SELECT … FROM sys.columns …) ALTER TABLE … ADD …;
        auto stmtA = SqlStatement { conn };
        (void) stmtA.ExecuteDirect(std::format("IF NOT EXISTS (SELECT 1 FROM sys.columns "
                                               "WHERE object_id = OBJECT_ID('dbo.{}') AND name = '{}') "
                                               "ALTER TABLE dbo.{} ADD {} INT NULL;",
                                               probeTable,
                                               colName,
                                               probeTable,
                                               colName));

        // Second step in the same migration on the SAME statement handle —
        // this is where the migration runner's reuse of `stmt` happens.
        (void) stmtA.ExecuteDirect(std::format("IF EXISTS (SELECT 1 FROM sys.columns "
                                               "WHERE object_id = OBJECT_ID('dbo.{}') AND name = '{}') "
                                               "UPDATE dbo.{} SET {} = NULL WHERE 1 = 0;",
                                               probeTable,
                                               colName,
                                               probeTable,
                                               colName));

        // Now allocate a *new* SqlStatement on the same connection — exactly
        // what `dm.CreateExplicit(SchemaMigration{...})` does after the
        // per-migration steps complete. Before the fix this is where the
        // accumulated pending-result tokens surface as HY000.
        auto stmtB = SqlStatement { conn };
        REQUIRE_NOTHROW((void) stmtB.ExecuteDirect("SELECT 1;"));

        transaction.Commit();
    }

    cleanup();
}

TEST_CASE_METHOD(SqlMigrationTestFixture,
                 "Regression: sp_getapplock acquire does not leave the connection busy",
                 "[SqlScopedLock][regression][mssql]")
{
    auto& dm = SqlMigration::MigrationManager::GetInstance().GetDataMapper();
    auto& conn = dm.Connection();

    if (conn.ServerType() != SqlServerType::MICROSOFT_SQL)
    {
        SUCCEED("Reproduces only on MS SQL Server (MARS off, NOCOUNT off).");
        return;
    }

    // Acquiring the advisory lock runs a multi-statement batch
    //     DECLARE @result INT; EXEC @result = sp_getapplock …; SELECT @result;
    // Before the fix, only the SELECT result is fetched; the EXEC's done
    // token stays pending and poisons the connection for the next caller.
    auto lock = SqlScopedLock { conn, "lightweight_busy_probe" };
    REQUIRE(lock.IsLocked());

    auto stmt = SqlStatement { conn };
    REQUIRE_NOTHROW((void) stmt.ExecuteDirect("SELECT 1;"));
}

// ================================================================================================
// PreviewMigration: returns the SQL that would be executed for a single migration without running it
// ================================================================================================

TEST_CASE_METHOD(SqlMigrationTestFixture, "PreviewMigration returns SQL for a single pending migration", "[SqlMigration]")
{
    using namespace SqlColumnTypeDefinitions;

    auto migration = SqlMigration::Migration<202901010001>("preview single", [](SqlMigrationQueryBuilder& plan) {
        plan.CreateTable("preview_table").PrimaryKey("id", Integer());
    });

    auto& manager = SqlMigration::MigrationManager::GetInstance();
    manager.CreateMigrationHistory();

    auto const sqls = manager.PreviewMigration(migration);
    REQUIRE_FALSE(sqls.empty());
    bool foundCreate = false;
    for (auto const& s: sqls)
    {
        if (s.contains("CREATE") && s.contains("preview_table"))
        {
            foundCreate = true;
            break;
        }
    }
    CHECK(foundCreate);

    // Preview must not have actually executed the migration.
    {
        auto conn = SqlConnection {};
        auto stmt = SqlStatement { conn };
        auto const _ = ScopedSqlNullLogger {};
        CHECK_THROWS_AS((void) stmt.ExecuteDirect("SELECT count(*) FROM preview_table"), SqlException);
    }
}

// ================================================================================================
// ApplyPendingMigrations: invokes the feedback callback once per pending migration
// ================================================================================================

TEST_CASE_METHOD(SqlMigrationTestFixture,
                 "ApplyPendingMigrations invokes the feedback callback per migration",
                 "[SqlMigration]")
{
    using namespace SqlColumnTypeDefinitions;

    auto m1 = SqlMigration::Migration<202901010002>(
        "feedback 1", [](SqlMigrationQueryBuilder& plan) { plan.CreateTable("fb_one").PrimaryKey("id", Integer()); });
    auto m2 = SqlMigration::Migration<202901010003>(
        "feedback 2", [](SqlMigrationQueryBuilder& plan) { plan.CreateTable("fb_two").PrimaryKey("id", Integer()); });

    auto& manager = SqlMigration::MigrationManager::GetInstance();
    manager.CreateMigrationHistory();

    size_t callbackCalls = 0;
    size_t lastTotal = 0;
    auto const applied = manager.ApplyPendingMigrations(
        [&](SqlMigration::MigrationBase const& /*migration*/, size_t /*current*/, size_t total) {
            ++callbackCalls;
            lastTotal = total;
        });

    CHECK(applied == 2);
    CHECK(callbackCalls == 2);
    CHECK(lastTotal == 2);
}

// ================================================================================================
// PreviewPendingMigrations: empty when no pending migrations
// ================================================================================================

TEST_CASE_METHOD(SqlMigrationTestFixture,
                 "PreviewPendingMigrations returns empty when no pending migrations",
                 "[SqlMigration]")
{
    auto& manager = SqlMigration::MigrationManager::GetInstance();
    manager.CreateMigrationHistory();

    auto const sqls = manager.PreviewPendingMigrations();
    CHECK(sqls.empty());
}
