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
        CHECK(ex.GetFailedSql().find("THIS_IS_INVALID_SQL_FOR_DIAG") != std::string::npos);
        CHECK_FALSE(ex.GetDriverMessage().empty());
        // Base-class accessors keep working for catch(SqlException) callers.
        CHECK(ex.info().message.find("failing migration") != std::string::npos);
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
