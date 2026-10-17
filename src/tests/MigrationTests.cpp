// SPDX-License-Identifier: Apache-2.0

#include "Utils.hpp"

#include <Lightweight/Lightweight.hpp>
#include <Lightweight/QueryFormatter/SqlServerFormatter.hpp>
#include <Lightweight/SqlBackup/Common.hpp>
#include <Lightweight/SqlBackup/SqlBackup.hpp>
#include <Lightweight/SqlScopedLock.hpp>

#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
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
LIGHTWEIGHT_SQL_MIGRATION(20170816112233, "create users")
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

TEST_CASE_METHOD(SqlMigrationTestFixture,
                 "MigrationManager reconnects its data mapper after the default connection string changed",
                 "[SqlMigration]")
{
    auto& migrationManager = SqlMigration::MigrationManager::GetInstance();
    auto& before = migrationManager.GetDataMapper();
    auto const connectionIdBefore = before.Connection().ConnectionId();
    CHECK(&migrationManager.GetDataMapper() == &before); // unchanged default: same connection
    CHECK(migrationManager.GetDataMapper().Connection().ConnectionId() == connectionIdBefore);

    // Re-setting the default to itself changes its generation without switching the database.
    SqlConnection::SetDefaultConnectionString(SqlConnection::DefaultConnectionString());

    auto& after = migrationManager.GetDataMapper();
    CHECK(&after == &before); // reconnected in place: references handed out earlier stay valid
    CHECK(after.Connection().ConnectionId() != connectionIdBefore);
    CHECK(after.Connection().IsAlive());
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

TEST_CASE_METHOD(SqlMigrationTestFixture,
                 "SQL Server: an error late in a migration batch fails and rolls back the migration",
                 "[SqlMigration]")
{
    using namespace SqlColumnTypeDefinitions;

    auto& manager = SqlMigration::MigrationManager::GetInstance();
    auto& connection = manager.GetDataMapper().Connection();
    if (connection.ServerType() != SqlServerType::MICROSOFT_SQL)
        return; // Only SQL Server reports each row count of a batch as a result of its own.

    auto const createTable = SqlMigration::Migration<202610170101>("create probe", [](SqlMigrationQueryBuilder& plan) {
        plan.CreateTable("BatchProbe").PrimaryKey("Id", Integer()).RequiredColumn("Value", Integer());
        plan.Insert("BatchProbe").Set("Id", 1).Set("Value", 10);
    });
    manager.CreateMigrationHistory();
    manager.ApplySingleMigration(createTable);

    auto const storedValue = [&] {
        auto stmt = SqlStatement { connection };
        return stmt.ExecuteDirectScalar<int>(R"(SELECT "Value" FROM "BatchProbe" WHERE "Id" = 1)").value_or(-1);
    };

    // A guard that counts, changes a row and then refuses; and a batch whose second statement fails.
    auto const batch =
        GENERATE(as<std::string> {},
                 R"(DECLARE @n INT; SELECT @n = COUNT(*) FROM sys.columns; )"
                 R"(UPDATE "BatchProbe" SET "Value" = 43 WHERE "Id" = 1; THROW 50000, N'refused', 1;)",
                 R"(UPDATE "BatchProbe" SET "Value" = 40 WHERE "Id" = 1; INSERT INTO "NoSuchTable" VALUES (1);)");
    // Without the migration lock, at the server default NOCOUNT OFF, and under the lock dbtool takes.
    auto const underLock = GENERATE(false, true);
    CAPTURE(batch, underLock);

    auto const refused = SqlMigration::Migration<202610170102>(
        "refused batch", [&batch](SqlMigrationQueryBuilder& plan) { plan.RawSql(batch); });

    (void) SqlStatement { connection }.ExecuteDirect("SET NOCOUNT OFF");
    auto lock = std::optional<SqlScopedLock> {};
    if (underLock)
        lock.emplace(connection, "lightweight_batch_error_lock");

    {
        auto const _ = ScopedSqlNullLogger {}; // the failure is expected
        CHECK_THROWS_AS(manager.ApplySingleMigration(refused), SqlMigration::MigrationException);
    }
    lock.reset();

    CHECK(storedValue() == 10);
    auto const applied = manager.GetAppliedMigrationIds();
    CHECK(std::ranges::find(applied, SqlMigration::MigrationTimestamp { 202610170102 }) == applied.end());
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
///
/// Constructing one auto-registers it with `MigrationManager::GetInstance()` (a
/// hard-coded side effect of the `MigrationBase` ctor); `~MigrationBase` unregisters it
/// again, so a stub with automatic storage duration leaves nothing behind.
class StubMigration: public Lightweight::SqlMigration::MigrationBase
{
  public:
    explicit StubMigration(uint64_t ts):
        MigrationBase(Lightweight::SqlMigration::MigrationTimestamp { ts }, "stub")
    {
    }

    ~StubMigration() override = default;

    StubMigration(StubMigration const&) = delete;
    StubMigration& operator=(StubMigration const&) = delete;
    StubMigration(StubMigration&&) = delete;
    StubMigration& operator=(StubMigration&&) = delete;

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
        .schemaName = "",
        .tableName = "T",
        .columns = { { "n", Lightweight::SqlVariant(std::string("hellooo")) } }, // 7 > 5
        .keyColumns = {},
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
        .keyColumns = {},
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
        .keyColumns = {},
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
        .keyColumns = {},
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
        .keyColumns = {},
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

TEST_CASE_METHOD(SqlMigrationTestFixture, "MigrationBase: destruction unregisters the migration", "[SqlMigration]")
{
    auto& mgr = Lightweight::SqlMigration::MigrationManager::GetInstance();
    REQUIRE(mgr.GetAllMigrations().empty()); // fixture clears state

    {
        StubMigration const scoped { 20'000'000'000'101ULL };
        CHECK(mgr.GetAllMigrations().size() == 1);
    }

    // The constructor registered it, so the destructor has to deregister it: anything left behind is
    // a pointer to a dead object that `AddMigration`'s duplicate lookup would dereference.
    CHECK(mgr.GetAllMigrations().empty());
}

TEST_CASE_METHOD(SqlMigrationTestFixture,
                 "MigrationBase: a migration reusing a dead one's storage is not a duplicate of itself",
                 "[SqlMigration]")
{
    // Regression test for a CI flake: with the dead migration still listed, `AddMigration` compared
    // the incoming timestamp against a dangling pointer. When the new migration happened to land on
    // the freed storage, the pointer resolved back to the object being constructed and it was
    // rejected as conflicting with itself ("'stub' conflicts with 'stub'").
    constexpr uint64_t Timestamp = 20'000'000'000'102ULL;

    {
        StubMigration const first { Timestamp };
        (void) first;
    }

    CHECK_NOTHROW([&] {
        StubMigration const second { Timestamp };
        (void) second;
    }());
}

TEST_CASE_METHOD(SqlMigrationTestFixture,
                 "MigrationManager::RemoveMigration: removing an unregistered migration is a no-op",
                 "[SqlMigration]")
{
    // The ctor only ever registers with the singleton, so any other manager has never seen `stub`.
    Lightweight::SqlMigration::MigrationManager other;
    StubMigration const stub { 20'000'000'000'103ULL };

    CHECK_NOTHROW(other.RemoveMigration(&stub));
    CHECK(other.GetAllMigrations().empty());
    CHECK(Lightweight::SqlMigration::MigrationManager::GetInstance().GetAllMigrations().size() == 1);
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
    fold_test::FoldStub<20'10'02'00'00'03> m3 { "drop temp",
                                                [](SqlMigrationQueryBuilder& plan) { plan.DropTable("temp_table"); } };

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

namespace
{
/// Installs a SQL Server database DDL trigger that refuses every ALTER TABLE for its lifetime, so the
/// foreign-key drops a cascading DropTable batch runs after its row-counting SELECT fail.
class RefuseAlterTableTrigger
{
  public:
    explicit RefuseAlterTableTrigger(SqlConnection& connection):
        _connection { connection }
    {
        (void) SqlStatement { _connection }.ExecuteDirect(
            "CREATE TRIGGER lightweight_refuse_alter ON DATABASE FOR ALTER_TABLE AS THROW 50000, N'alter refused', 1;");
    }
    ~RefuseAlterTableTrigger()
    {
        try
        {
            (void) SqlStatement { _connection }.ExecuteDirect("DROP TRIGGER lightweight_refuse_alter ON DATABASE");
        }
        catch (...) // NOLINT(bugprone-empty-catch) - a destructor must not throw
        {
        }
    }
    RefuseAlterTableTrigger(RefuseAlterTableTrigger const&) = delete;
    RefuseAlterTableTrigger(RefuseAlterTableTrigger&&) = delete;
    RefuseAlterTableTrigger& operator=(RefuseAlterTableTrigger const&) = delete;
    RefuseAlterTableTrigger& operator=(RefuseAlterTableTrigger&&) = delete;

  private:
    SqlConnection& _connection;
};

/// Records the messages of the progress updates a restore helper reports.
class RecordingProgressManager: public SqlBackup::ErrorTrackingProgressManager
{
  public:
    void Update(SqlBackup::Progress const& progress) override
    {
        ErrorTrackingProgressManager::Update(progress);
        messages.push_back(progress.message);
    }
    void AllDone() override {}

    std::vector<std::string> messages;
};
} // namespace

TEST_CASE_METHOD(SqlMigrationTestFixture,
                 "SQL Server: HardReset reports the error of a refused foreign-key drop",
                 "[SqlMigration][HardReset]")
{
    using namespace Lightweight::SqlColumnTypeDefinitions;
    auto& mgr = SqlMigration::MigrationManager::GetInstance();
    auto& connection = mgr.GetDataMapper().Connection();
    if (connection.ServerType() != SqlServerType::MICROSOFT_SQL)
        return; // DDL triggers and multi-result batches are SQL Server specific here.

    fold_test::FoldStub<20'10'17'00'00'01> m1 { "create parent", [](SqlMigrationQueryBuilder& plan) {
                                                   plan.CreateTable("reset_parent").PrimaryKey("id", Integer());
                                               } };
    mgr.CreateMigrationHistory();
    REQUIRE(mgr.ApplyPendingMigrations() == 1);
    // A user table that HardReset keeps, so dropping the parent must drop its foreign key first.
    (void) SqlStatement { connection }.ExecuteDirect(
        R"(CREATE TABLE "reset_user" ("id" INT PRIMARY KEY, "parent_id" INT REFERENCES "reset_parent" ("id")))");

    auto const message = [&] {
        auto const trigger = RefuseAlterTableTrigger { connection };
        auto const _ = ScopedSqlNullLogger {}; // the failure is expected
        try
        {
            (void) mgr.HardReset(/*dryRun=*/false);
        }
        catch (SqlException const& error)
        {
            return std::string { error.what() };
        }
        return std::string {};
    }();
    // The refusal itself, not the later DROP TABLE that fails because the foreign key survived.
    CHECK(message.contains("alter refused"));
}

TEST_CASE_METHOD(SqlMigrationTestFixture,
                 "SQL Server: a restore's table drop reports the error of a refused foreign-key drop",
                 "[SqlBackup]")
{
    auto& connection = SqlMigration::MigrationManager::GetInstance().GetDataMapper().Connection();
    if (connection.ServerType() != SqlServerType::MICROSOFT_SQL)
        return; // DDL triggers and multi-result batches are SQL Server specific here.

    auto stmt = SqlStatement { connection };
    (void) stmt.ExecuteDirect(R"(CREATE TABLE "restore_parent" ("id" INT PRIMARY KEY))");
    (void) stmt.ExecuteDirect(
        R"(CREATE TABLE "restore_child" ("id" INT PRIMARY KEY, "parent_id" INT REFERENCES "restore_parent" ("id")))");

    auto progress = RecordingProgressManager {};
    {
        auto const trigger = RefuseAlterTableTrigger { connection };
        auto const _ = ScopedSqlNullLogger {};
        CHECK_FALSE(SqlBackup::detail::DropTableIfExists(connection, std::string {}, "restore_parent", progress));
    }
    REQUIRE(progress.messages.size() == 1);
    CHECK(progress.messages[0].contains("alter refused"));
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

// ================================================================================================
// Virtual applied migrations overlay
// ================================================================================================

TEST_CASE_METHOD(SqlMigrationTestFixture, "Virtual applied migrations overlay", "[SqlMigration]")
{
    using namespace Lightweight::SqlColumnTypeDefinitions;
    auto& mgr = SqlMigration::MigrationManager::GetInstance();
    mgr.CreateMigrationHistory();

    fold_test::FoldStub<20'11'01'00'00'01> m1 { "one", [](SqlMigrationQueryBuilder& plan) {
                                                   plan.CreateTable("vam_a").PrimaryKey("id", Bigint());
                                               } };
    fold_test::FoldStub<20'11'01'00'00'02> m2 { "two", [](SqlMigrationQueryBuilder& plan) {
                                                   plan.CreateTable("vam_b").PrimaryKey("id", Bigint());
                                               } };

    REQUIRE(mgr.GetPending().size() == 2);

    // Marking the first migration as virtually applied removes it from the pending
    // set and surfaces it through GetAppliedMigrationIds without touching the DB.
    auto const virtualIds = std::array { SqlMigration::MigrationTimestamp { 20'11'01'00'00'01ULL } };
    mgr.AddVirtualAppliedMigrations(virtualIds);

    auto const pending = mgr.GetPending();
    REQUIRE(pending.size() == 1);
    CHECK(pending.front()->GetTimestamp().value == 20'11'01'00'00'02ULL);
    CHECK(std::ranges::contains(mgr.GetAppliedMigrationIds(), virtualIds[0]));

    // Adding the same timestamp again (plus an unsorted duplicate batch) keeps the
    // overlay deduplicated.
    auto const duplicates = std::array {
        SqlMigration::MigrationTimestamp { 20'11'01'00'00'01ULL },
        SqlMigration::MigrationTimestamp { 20'11'01'00'00'01ULL },
    };
    mgr.AddVirtualAppliedMigrations(duplicates);
    CHECK(std::ranges::count(mgr.GetAppliedMigrationIds(), virtualIds[0]) == 1);

    // An empty batch is a no-op.
    mgr.AddVirtualAppliedMigrations({});
    CHECK(mgr.GetPending().size() == 1);

    // Clearing the overlay restores both migrations as pending.
    mgr.ClearVirtualAppliedMigrations();
    CHECK(mgr.GetPending().size() == 2);
}

// ================================================================================================
// Plan folding: rename table, drop table side effects, index folding
// ================================================================================================

TEST_CASE_METHOD(SqlMigrationTestFixture, "Fold: rename table rewrites FK references and indexes", "[SqlMigration][Fold]")
{
    using namespace Lightweight::SqlColumnTypeDefinitions;
    auto& mgr = SqlMigration::MigrationManager::GetInstance();

    fold_test::FoldStub<20'11'02'00'00'01> m1 { "create users + orders", [](SqlMigrationQueryBuilder& plan) {
                                                   plan.CreateTable("users").PrimaryKey("id", Bigint());
                                                   plan.CreateTable("orders")
                                                       .PrimaryKey("id", Bigint())
                                                       .RequiredForeignKey("user_id",
                                                                           Bigint(),
                                                                           SqlForeignKeyReferenceDefinition {
                                                                               .tableName = "users", .columnName = "id" });
                                               } };
    fold_test::FoldStub<20'11'02'00'00'02> m2 { "index users.id", [](SqlMigrationQueryBuilder& plan) {
                                                   plan.AlterTable("users").AddIndex("id");
                                               } };
    fold_test::FoldStub<20'11'02'00'00'03> m3 { "rename users", [](SqlMigrationQueryBuilder& plan) {
                                                   plan.AlterTable("users").RenameTo("accounts");
                                               } };

    auto const fold = mgr.FoldRegisteredMigrations(SqlQueryFormatter::Sqlite());

    auto const accountsKey = SqlSchema::FullyQualifiedTableName { .catalog = {}, .schema = {}, .table = "accounts" };
    auto const usersKey = SqlSchema::FullyQualifiedTableName { .catalog = {}, .schema = {}, .table = "users" };
    REQUIRE(fold.tables.contains(accountsKey));
    CHECK_FALSE(fold.tables.contains(usersKey));
    CHECK(std::ranges::contains(fold.creationOrder, accountsKey));

    // The FK in orders must now reference the renamed table.
    auto const ordersIt =
        fold.tables.find(SqlSchema::FullyQualifiedTableName { .catalog = {}, .schema = {}, .table = "orders" });
    REQUIRE(ordersIt != fold.tables.end());
    auto const userIdColumn =
        std::ranges::find_if(ordersIt->second.columns, [](SqlColumnDeclaration const& c) { return c.name == "user_id"; });
    REQUIRE(userIdColumn != ordersIt->second.columns.end());
    REQUIRE(userIdColumn->foreignKey.has_value());
    CHECK(userIdColumn->foreignKey.value_or(SqlForeignKeyReferenceDefinition {}).tableName == "accounts");

    // The index hosted on the renamed table follows it.
    REQUIRE(fold.indexes.size() == 1);
    CHECK(fold.indexes.front().tableName == "accounts");
}

TEST_CASE_METHOD(SqlMigrationTestFixture, "Fold: drop table resets inbound FKs and data steps", "[SqlMigration][Fold]")
{
    using namespace Lightweight::SqlColumnTypeDefinitions;
    auto& mgr = SqlMigration::MigrationManager::GetInstance();

    fold_test::FoldStub<20'11'03'00'00'01> m1 {
        "create users + orders + seed",
        [](SqlMigrationQueryBuilder& plan) {
            plan.CreateTable("users").PrimaryKey("id", Bigint()).Column("name", Varchar(50));
            plan.CreateTable("orders")
                .PrimaryKey("id", Bigint())
                .RequiredForeignKey(
                    "user_id", Bigint(), SqlForeignKeyReferenceDefinition { .tableName = "users", .columnName = "id" });
            plan.Insert("users").Set("name", "seed"sv);
        }
    };
    fold_test::FoldStub<20'11'03'00'00'02> m2 { "drop users",
                                                [](SqlMigrationQueryBuilder& plan) { plan.DropTable("users"); } };

    auto const fold = mgr.FoldRegisteredMigrations(SqlQueryFormatter::Sqlite());

    CHECK_FALSE(fold.tables.contains(SqlSchema::FullyQualifiedTableName { .catalog = {}, .schema = {}, .table = "users" }));

    // The inline FK declaration in orders is reset, and the seed insert targeting
    // the dropped table is erased from the folded data steps.
    auto const ordersIt =
        fold.tables.find(SqlSchema::FullyQualifiedTableName { .catalog = {}, .schema = {}, .table = "orders" });
    REQUIRE(ordersIt != fold.tables.end());
    auto const userIdColumn =
        std::ranges::find_if(ordersIt->second.columns, [](SqlColumnDeclaration const& c) { return c.name == "user_id"; });
    REQUIRE(userIdColumn != ordersIt->second.columns.end());
    CHECK_FALSE(userIdColumn->foreignKey.has_value());
    CHECK(fold.dataSteps.empty());
}

TEST_CASE_METHOD(SqlMigrationTestFixture, "Fold: conditional column and index commands", "[SqlMigration][Fold]")
{
    using namespace Lightweight::SqlColumnTypeDefinitions;
    auto& mgr = SqlMigration::MigrationManager::GetInstance();

    fold_test::FoldStub<20'11'04'00'00'01> m1 { "create t", [](SqlMigrationQueryBuilder& plan) {
                                                   plan.CreateTable("cond_t").PrimaryKey("id", Bigint());
                                               } };
    fold_test::FoldStub<20'11'04'00'00'02> m2 { "conditional churn", [](SqlMigrationQueryBuilder& plan) {
                                                   // Second AddColumnIfNotExists for the same column must be a fold no-op.
                                                   plan.AlterTable("cond_t").AddColumnIfNotExists("extra", Varchar(50));
                                                   plan.AlterTable("cond_t").AddColumnIfNotExists("extra", Varchar(50));
                                                   plan.AlterTable("cond_t").AddIndex("extra");
                                                   plan.AlterTable("cond_t").DropIndex("extra");
                                                   plan.AlterTable("cond_t").DropColumnIfExists("extra");
                                               } };

    auto const fold = mgr.FoldRegisteredMigrations(SqlQueryFormatter::Sqlite());

    auto const it = fold.tables.find(SqlSchema::FullyQualifiedTableName { .catalog = {}, .schema = {}, .table = "cond_t" });
    REQUIRE(it != fold.tables.end());
    REQUIRE(it->second.columns.size() == 1);
    CHECK(it->second.columns[0].name == "id");
    CHECK(fold.indexes.empty());
}

// ================================================================================================
// Default-schema post-connect hook, revert failure diagnostics, checksum rewriting
// ================================================================================================

TEST_CASE_METHOD(SqlMigrationTestFixture, "SetDefaultSchema post-connect hook runs on new connections", "[SqlMigration]")
{
    auto& mgr = SqlMigration::MigrationManager::GetInstance();

    // Installing a default schema registers a post-connect hook; opening a fresh
    // connection runs it. On DBMSes without a session default-schema statement the
    // hook is a no-op, which is exactly the path this test pins down.
    mgr.SetDefaultSchema("main");
    {
        auto conn = SqlConnection {};
        CHECK(conn.IsAlive());
    }

    // Clearing the schema uninstalls the hook again.
    mgr.SetDefaultSchema("");
    {
        auto conn = SqlConnection {};
        CHECK(conn.IsAlive());
    }
}

TEST_CASE_METHOD(SqlMigrationTestFixture, "MigrationException carries structured context on revert", "[SqlMigration]")
{
    auto badRevert = SqlMigration::Migration<202412102221>(
        "bad revert",
        [](SqlMigrationQueryBuilder& plan) { plan.RawSql("CREATE TABLE bad_revert_t (id INT PRIMARY KEY)"); },
        [](SqlMigrationQueryBuilder& plan) { plan.RawSql("THIS_IS_INVALID_REVERT_SQL"); });

    auto& migrationManager = SqlMigration::MigrationManager::GetInstance();
    migrationManager.CreateMigrationHistory();
    migrationManager.ApplySingleMigration(badRevert);

    ScopedSqlNullLogger const nullLogger; // suppress the expected error message
    (void) nullLogger;
    try
    {
        migrationManager.RevertSingleMigration(badRevert);
        FAIL("expected MigrationException");
    }
    catch (SqlMigration::MigrationException const& ex)
    {
        CHECK(ex.GetOperation() == SqlMigration::MigrationException::Operation::Revert);
        CHECK(ex.GetMigrationTimestamp().value == 202412102221);
        CHECK(ex.GetFailedSql().contains("THIS_IS_INVALID_REVERT_SQL"));
    }
}

TEST_CASE_METHOD(SqlMigrationTestFixture, "RewriteChecksums reports and repairs drifted checksums", "[SqlMigration]")
{
    using namespace SqlColumnTypeDefinitions;

    auto migration = SqlMigration::Migration<202412102222>("checksummed", [](SqlMigrationQueryBuilder& plan) {
        plan.CreateTable("checksummed_t").PrimaryKey("id", Integer());
    });

    auto& mgr = SqlMigration::MigrationManager::GetInstance();
    mgr.CreateMigrationHistory();
    mgr.ApplySingleMigration(migration);

    // Freshly applied migrations carry the current checksum: nothing to rewrite.
    auto clean = mgr.RewriteChecksums(/*dryRun=*/true);
    CHECK(clean.wasDryRun);
    CHECK(clean.entries.empty());

    // Simulate checksum drift (e.g. a migration edited after being applied).
    {
        auto stmt = SqlStatement {};
        (void) stmt.ExecuteDirect("UPDATE schema_migrations SET checksum = 'drifted' WHERE version = 202412102222");
    }

    auto dryRun = mgr.RewriteChecksums(/*dryRun=*/true);
    REQUIRE(dryRun.entries.size() == 1);
    CHECK(dryRun.entries.front().timestamp.value == 202412102222);
    CHECK(dryRun.entries.front().oldChecksum == "drifted");
    CHECK_FALSE(dryRun.entries.front().newChecksum.empty());

    // The dry run must not have fixed anything; the real run does.
    auto repair = mgr.RewriteChecksums(/*dryRun=*/false);
    REQUIRE(repair.entries.size() == 1);
    CHECK(mgr.RewriteChecksums(/*dryRun=*/true).entries.empty());
}

TEST_CASE("ToSql: lup-truncate clips wide-string and SqlText variants, also on UPDATE", "[SqlMigration][compat]")
{
    using namespace Lightweight::SqlColumnTypeDefinitions;
    using Lightweight::MigrationRenderContext;
    using Lightweight::SqlColumnDeclaration;
    using Lightweight::SqlCreateTablePlan;
    using Lightweight::SqlInsertDataPlan;
    using Lightweight::SqlMigrationPlanElement;
    using Lightweight::SqlUpdateDataPlan;

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

    CapturingWarningLogger capture;

    auto const renderInsert = [&](Lightweight::SqlVariant value) {
        SqlInsertDataPlan const insert {
            .schemaName = "",
            .tableName = "T",
            .columns = { { "n", std::move(value) } },
            .keyColumns = {},
        };
        auto const sql = Lightweight::ToSql(formatter, SqlMigrationPlanElement { insert }, context);
        REQUIRE(sql.size() == 1);
        return sql[0];
    };

    SECTION("u16string value")
    {
        auto const sql = renderInsert(Lightweight::SqlVariant { std::u16string { u"hellooo" } });
        CHECK(sql.contains("hello"));
        CHECK(!sql.contains("hellooo"));
    }

    SECTION("u16string_view value")
    {
        auto const sql = renderInsert(Lightweight::SqlVariant { std::u16string_view { u"hellooo" } });
        CHECK(sql.contains("hello"));
        CHECK(!sql.contains("hellooo"));
    }

    SECTION("string_view value")
    {
        auto const sql = renderInsert(Lightweight::SqlVariant { std::string_view { "hellooo" } });
        CHECK(sql.contains("hello"));
        CHECK(!sql.contains("hellooo"));
    }

    SECTION("SqlText value")
    {
        auto const sql = renderInsert(Lightweight::SqlVariant { Lightweight::SqlText { "hellooo" } });
        CHECK(sql.contains("hello"));
        CHECK(!sql.contains("hellooo"));
    }

    SECTION("UPDATE set-columns are clipped too")
    {
        SqlUpdateDataPlan const update {
            .schemaName = "",
            .tableName = "T",
            .setColumns = { { "n", Lightweight::SqlVariant { std::string { "hellooo" } } } },
            .setExpressions = {},
            .whereColumn = {},
            .whereOp = {},
            .whereValue = {},
            .whereExpression = {},
        };
        auto const sql = Lightweight::ToSql(formatter, SqlMigrationPlanElement { update }, context);
        REQUIRE(sql.size() == 1);
        CHECK(sql[0].contains("hello"));
        CHECK(!sql[0].contains("hellooo"));
    }
}

// ================================================================================================
// Foreign-key alterations through the migration executor (SQLite guard → table rebuild)
// ================================================================================================

TEST_CASE_METHOD(SqlMigrationTestFixture, "AddForeignKey and DropForeignKey run through the executor", "[SqlMigration]")
{
    using namespace SqlColumnTypeDefinitions;

    auto createPair = SqlMigration::Migration<202412102230>("create fk pair", [](SqlMigrationQueryBuilder& plan) {
        plan.CreateTable("fk_parent").PrimaryKey("id", Bigint());
        plan.CreateTable("fk_child")
            .PrimaryKey("id", Bigint())
            .RequiredForeignKey(
                "parent_id", Bigint(), SqlForeignKeyReferenceDefinition { .tableName = "fk_parent", .columnName = "id" });
        plan.Insert("fk_parent").Set("id", 1);
        plan.Insert("fk_child").Set("id", 10).Set("parent_id", 1);
    });
    // On SQLite both commands trigger the in-place table rebuild recipe; on the
    // other DBMSes they render as ALTER TABLE DROP/ADD CONSTRAINT.
    auto dropFk = SqlMigration::Migration<202412102231>(
        "drop fk", [](SqlMigrationQueryBuilder& plan) { plan.AlterTable("fk_child").DropForeignKey("parent_id"); });
    auto addFk = SqlMigration::Migration<202412102232>("re-add fk", [](SqlMigrationQueryBuilder& plan) {
        plan.AlterTable("fk_child")
            .AddForeignKey("parent_id", SqlForeignKeyReferenceDefinition { .tableName = "fk_parent", .columnName = "id" });
    });

    auto& mgr = SqlMigration::MigrationManager::GetInstance();
    mgr.CreateMigrationHistory();
    REQUIRE(mgr.ApplyPendingMigrations() == 3);

    // The rebuild must preserve the seeded rows.
    auto stmt = SqlStatement { mgr.GetDataMapper().Connection() };
    CHECK(stmt.ExecuteDirectScalar<long long>(R"(SELECT COUNT(*) FROM "fk_child")").value_or(0) == 1);
    CHECK(stmt.ExecuteDirectScalar<long long>(R"(SELECT "parent_id" FROM "fk_child")").value_or(0) == 1);
}

// ================================================================================================
// Plan folding: foreign-key commands
// ================================================================================================

TEST_CASE_METHOD(SqlMigrationTestFixture, "Fold: alter-table foreign-key commands", "[SqlMigration][Fold]")
{
    using namespace Lightweight::SqlColumnTypeDefinitions;
    auto& mgr = SqlMigration::MigrationManager::GetInstance();

    fold_test::FoldStub<20'11'05'00'00'01> m1 { "create pair", [](SqlMigrationQueryBuilder& plan) {
                                                   plan.CreateTable("ffk_parent").PrimaryKey("id", Bigint());
                                                   plan.CreateTable("ffk_child")
                                                       .PrimaryKey("id", Bigint())
                                                       .Column("parent_id", Bigint())
                                                       .Column("region_a", Bigint())
                                                       .Column("region_b", Bigint());
                                               } };
    fold_test::FoldStub<20'11'05'00'00'02> m2 { "fk churn", [](SqlMigrationQueryBuilder& plan) {
                                                   plan.AlterTable("ffk_child")
                                                       .AddForeignKey("parent_id",
                                                                      SqlForeignKeyReferenceDefinition {
                                                                          .tableName = "ffk_parent", .columnName = "id" })
                                                       .AddCompositeForeignKey(
                                                           { "region_a", "region_b" }, "ffk_parent", { "id", "id" });
                                                   plan.AlterTable("ffk_child").DropForeignKey("parent_id");
                                               } };

    auto const fold = mgr.FoldRegisteredMigrations(SqlQueryFormatter::Sqlite());

    auto const it =
        fold.tables.find(SqlSchema::FullyQualifiedTableName { .catalog = {}, .schema = {}, .table = "ffk_child" });
    REQUIRE(it != fold.tables.end());
    // The single-column FK was added then dropped; only the composite FK survives the fold.
    REQUIRE(it->second.compositeForeignKeys.size() == 1);
    CHECK(it->second.compositeForeignKeys.front().columns == std::vector<std::string> { "region_a", "region_b" });
    CHECK(it->second.compositeForeignKeys.front().referencedTableName == "ffk_parent");
}

// ================================================================================================
// lup-truncate width lookup against the live INFORMATION_SCHEMA
// ================================================================================================

TEST_CASE_METHOD(SqlMigrationTestFixture,
                 "lup-truncate resolves unknown widths from the live schema",
                 "[SqlMigration][compat]")
{
    using namespace SqlColumnTypeDefinitions;
    auto& mgr = SqlMigration::MigrationManager::GetInstance();
    mgr.CreateMigrationHistory();

    mgr.SetCompatPolicy(
        [](SqlMigration::MigrationBase const&) { return std::set<std::string> { std::string(CompatFlagLupTruncateName) }; });

    // First apply run: create the narrow column. The render context dies with it.
    auto create = SqlMigration::Migration<202412102240>("create narrow", [](SqlMigrationQueryBuilder& plan) {
        plan.CreateTable("lup_live").PrimaryKey("id", Bigint()).Column("n", NVarchar(5));
    });
    REQUIRE(mgr.ApplyPendingMigrations() == 1);

    // Second apply run: the INSERT's column width is unknown to the fresh context,
    // so the width lookup queries INFORMATION_SCHEMA.COLUMNS on the live database.
    // On DBMSes without INFORMATION_SCHEMA (SQLite) the lookup failure is swallowed
    // and the value passes through unclipped, which SQLite accepts.
    auto insert = SqlMigration::Migration<202412102241>("oversize insert", [](SqlMigrationQueryBuilder& plan) {
        plan.Insert("lup_live").Set("id", 1).Set("n", "hellooo"sv);
    });
    REQUIRE(mgr.ApplyPendingMigrations() == 1);

    auto stmt = SqlStatement { mgr.GetDataMapper().Connection() };
    auto const stored = stmt.ExecuteDirectScalar<std::string>(R"(SELECT "n" FROM "lup_live")").value_or("");
    if (stmt.Connection().ServerType() == SqlServerType::SQLITE)
        CHECK(stored == "hellooo"); // no width metadata -> unclipped, SQLite tolerates overlength
    else
        CHECK(stored == "hello"); // clipped to the live column's declared width

    mgr.SetCompatPolicy({});
}

// ================================================================================================
// RevertToMigration failure reporting
// ================================================================================================

TEST_CASE_METHOD(SqlMigrationTestFixture, "RevertToMigration reports structured failure details", "[SqlMigration]")
{
    auto badRevert = SqlMigration::Migration<202412102250>(
        "bad revert to",
        [](SqlMigrationQueryBuilder& plan) { plan.RawSql("CREATE TABLE bad_revert_to_t (id INT PRIMARY KEY)"); },
        [](SqlMigrationQueryBuilder& plan) { plan.RawSql("THIS_IS_INVALID_REVERT_TO_SQL"); });

    auto& mgr = SqlMigration::MigrationManager::GetInstance();
    mgr.CreateMigrationHistory();
    REQUIRE(mgr.ApplyPendingMigrations() == 1);

    ScopedSqlNullLogger const nullLogger;
    (void) nullLogger;
    auto const result = mgr.RevertToMigration(SqlMigration::MigrationTimestamp { 0 });

    REQUIRE(result.failedAt.has_value());
    CHECK(result.failedAt.value_or(SqlMigration::MigrationTimestamp {}).value == 202412102250);
    CHECK(result.failedTitle == "bad revert to");
    CHECK(result.failedSql.contains("THIS_IS_INVALID_REVERT_TO_SQL"));
    CHECK_FALSE(result.errorMessage.empty());
    CHECK_FALSE(result.sqlState.empty());
}

TEST_CASE_METHOD(SqlMigrationTestFixture, "RevertToMigration fails on applied-but-unregistered migration", "[SqlMigration]")
{
    auto migration = SqlMigration::Migration<202412102251>(
        "vanishing",
        [](SqlMigrationQueryBuilder& plan) { plan.RawSql("CREATE TABLE vanish_t (id INT PRIMARY KEY)"); },
        [](SqlMigrationQueryBuilder& plan) { plan.RawSql("DROP TABLE vanish_t"); });

    auto& mgr = SqlMigration::MigrationManager::GetInstance();
    mgr.CreateMigrationHistory();
    REQUIRE(mgr.ApplyPendingMigrations() == 1);

    // Simulate a stale binary: the migration is recorded as applied but no longer registered.
    mgr.RemoveAllMigrations();

    auto const result = mgr.RevertToMigration(SqlMigration::MigrationTimestamp { 0 });
    REQUIRE(result.failedAt.has_value());
    CHECK(result.failedAt.value_or(SqlMigration::MigrationTimestamp {}).value == 202412102251);
    CHECK(result.errorMessage.contains("not found"));
}

TEST_CASE_METHOD(SqlMigrationTestFixture, "Virtual applied migrations persist to history on write", "[SqlMigration]")
{
    using namespace Lightweight::SqlColumnTypeDefinitions;
    auto& mgr = SqlMigration::MigrationManager::GetInstance();
    mgr.CreateMigrationHistory();

    fold_test::FoldStub<20'11'06'00'00'01> m1 { "virtually applied", [](SqlMigrationQueryBuilder& plan) {
                                                   plan.CreateTable("vp_a").PrimaryKey("id", Bigint());
                                               } };
    fold_test::FoldStub<20'11'06'00'00'02> m2 { "really applied", [](SqlMigrationQueryBuilder& plan) {
                                                   plan.CreateTable("vp_b").PrimaryKey("id", Bigint());
                                               } };

    auto const virtualIds = std::array { SqlMigration::MigrationTimestamp { 20'11'06'00'00'01ULL } };
    mgr.AddVirtualAppliedMigrations(virtualIds);

    // Applying the remaining pending migration also drains the overlay into the
    // history table, so the virtual timestamp survives a cleared overlay.
    REQUIRE(mgr.ApplyPendingMigrations() == 1);
    mgr.ClearVirtualAppliedMigrations();

    auto const applied = mgr.GetAppliedMigrationIds();
    CHECK(std::ranges::contains(applied, SqlMigration::MigrationTimestamp { 20'11'06'00'00'01ULL }));
    CHECK(std::ranges::contains(applied, SqlMigration::MigrationTimestamp { 20'11'06'00'00'02ULL }));

    // The virtually-applied migration must never have been executed: its table
    // does not exist, while the really-applied one does.
    auto stmt = SqlStatement { mgr.GetDataMapper().Connection() };
    CHECK(stmt.ExecuteDirectScalar<long long>(R"(SELECT COUNT(*) FROM "vp_b")").value_or(-1) == 0);
    CHECK_THROWS(stmt.ExecuteDirectScalar<long long>(R"(SELECT COUNT(*) FROM "vp_a")"));
}

TEST_CASE_METHOD(SqlMigrationTestFixture, "Fold: not-required column and FK-column variants", "[SqlMigration][Fold]")
{
    using namespace Lightweight::SqlColumnTypeDefinitions;
    auto& mgr = SqlMigration::MigrationManager::GetInstance();

    fold_test::FoldStub<20'11'07'00'00'01> m1 {
        "builder variants",
        [](SqlMigrationQueryBuilder& plan) {
            plan.CreateTable("bv_parent").PrimaryKey("id", Bigint());
            plan.CreateTable("bv_t").PrimaryKey("id", Bigint());
            plan.AlterTable("bv_t")
                .AddNotRequiredColumnIfNotExists("opt_note", Varchar(40))
                .AddForeignKeyColumn(
                    "parent_id", Bigint(), SqlForeignKeyReferenceDefinition { .tableName = "bv_parent", .columnName = "id" })
                .AddNotRequiredForeignKeyColumn(
                    "opt_parent_id",
                    Bigint(),
                    SqlForeignKeyReferenceDefinition { .tableName = "bv_parent", .columnName = "id" });
        }
    };

    auto const fold = mgr.FoldRegisteredMigrations(SqlQueryFormatter::Sqlite());
    auto const it = fold.tables.find(SqlSchema::FullyQualifiedTableName { .catalog = {}, .schema = {}, .table = "bv_t" });
    REQUIRE(it != fold.tables.end());

    auto const names = [&] {
        auto v = std::vector<std::string> {};
        for (auto const& c: it->second.columns)
            v.push_back(c.name);
        return v;
    }();
    CHECK(std::ranges::contains(names, "opt_note"));
    CHECK(std::ranges::contains(names, "parent_id"));
    CHECK(std::ranges::contains(names, "opt_parent_id"));
}

TEST_CASE_METHOD(SqlMigrationTestFixture,
                 "UnicodeUpgradeTables: upgrades drifted varchar columns cross-backend",
                 "[SqlMigration][UnicodeUpgrade]")
{
    using namespace Lightweight::SqlColumnTypeDefinitions;
    auto& mgr = SqlMigration::MigrationManager::GetInstance();
    mgr.CreateMigrationHistory();

    // Live table deliberately created with a narrow (non-Unicode) column...
    auto stmt = SqlStatement { mgr.GetDataMapper().Connection() };
    stmt.MigrateDirect([](SqlMigrationQueryBuilder& m) {
        m.CreateTable("uu_generic").PrimaryKey("id", Bigint()).Column("note", Varchar(40));
    });

    // ...while the registered migration declares the wide type: that's the drift
    // UnicodeUpgradeTables exists to repair.
    fold_test::FoldStub<20'11'08'00'00'01> decl {
        "declare wide",
        [](SqlMigrationQueryBuilder& plan) {
            plan.CreateTable("uu_generic").PrimaryKey("id", Bigint()).Column("note", NVarchar(40));
        }
    };

    auto const dry = mgr.UnicodeUpgradeTables(/*dryRun=*/true);
    CHECK(dry.wasDryRun);

    if (stmt.Connection().ServerType() == SqlServerType::POSTGRESQL)
    {
        // psqlODBC (Unicode driver) reports VARCHAR columns as wide types and
        // PostgreSQL stores all strings as UTF-8, so there is no narrow/wide
        // drift to repair — the correct result is "nothing to do".
        CHECK(dry.columns.empty());
    }
    else if (stmt.Connection().ServerType() == SqlServerType::SQLITE && dry.columns.empty())
    {
        // The SQLite ODBC driver's Unicode build (the stock Windows DLL) reports
        // every text column as a wide type, so — as with psqlODBC — no narrow/wide
        // drift is observable and "nothing to do" is the correct result. The ANSI
        // build (typical on Linux) reports the declared narrow type and takes the
        // upgrade path below.
        SUCCEED();
    }
    else
    {
        REQUIRE_FALSE(dry.columns.empty());

        auto const result = mgr.UnicodeUpgradeTables(/*dryRun=*/false);
        CHECK_FALSE(result.wasDryRun);
        REQUIRE_FALSE(result.columns.empty());

        // After the real run the drift is gone — except on SQLite, whose ODBC
        // driver cannot distinguish VARCHAR from NVARCHAR in live metadata (see
        // the roundtrip test above), so the reader keeps reporting the narrow type.
        if (stmt.Connection().ServerType() != SqlServerType::SQLITE)
            CHECK(mgr.UnicodeUpgradeTables(/*dryRun=*/true).columns.empty());
    }

    // The upgraded table must stay usable.
    (void) stmt.ExecuteDirect(R"(INSERT INTO "uu_generic" ("id", "note") VALUES (1, 'ok'))");
    CHECK(stmt.ExecuteDirectScalar<long long>(R"(SELECT COUNT(*) FROM "uu_generic")").value_or(0) == 1);
}

// ================================================================================================
// fk-match-referenced-text: FK columns follow the referenced column's narrow/wide text kind (#643)
// ================================================================================================

namespace
{

/// Renders every step of `build`'s plan through the context-aware `ToSql`.
std::string RenderWithContext(SqlQueryFormatter const& formatter,
                              MigrationRenderContext& context,
                              std::function<void(SqlMigrationQueryBuilder&)> const& build)
{
    auto builder = SqlQueryBuilder(formatter).Migration();
    build(builder);
    auto const plan = std::move(builder).GetPlan();
    auto sql = std::string {};
    for (auto const& step: plan.steps)
        for (auto const& statement: ToSql(formatter, step, context))
            sql += statement + "\n";
    return sql;
}

/// A render context with the flag on and `fk_users.NAME` known as `referencedType`.
MigrationRenderContext FkTextContext(SqlColumnTypeDefinition referencedType)
{
    auto context = MigrationRenderContext {};
    context.fkMatchReferencedText = true;
    context.textColumnTypes[MigrationRenderContext::ColumnKey { .schema = "", .table = "fk_users", .column = "NAME" }] =
        referencedType;
    // Mark the referenced table as already looked up so no live query is attempted.
    context.lookupAttempted.insert(MigrationRenderContext::TableKey { .schema = "", .table = "fk_users" });
    return context;
}

/// The migration from issue #643: a wide FK column referencing `fk_users.NAME`.
void CreateOrdersWithCompositeFk(SqlMigrationQueryBuilder& plan)
{
    using namespace SqlColumnTypeDefinitions;
    plan.CreateTable("fk_orders")
        .PrimaryKey("NR", Integer())
        .RequiredColumn("USER_NAME", NChar(30))
        .ForeignKey({ "USER_NAME" }, "fk_users", { "NAME" });
}

} // namespace

TEST_CASE("fk-match-referenced-text: SQL Server renders the referenced column's narrow variant",
          "[SqlMigration][compat][fk-text]")
{
    using namespace SqlColumnTypeDefinitions;
    auto context = FkTextContext(Char { 30 });
    auto const sql = RenderWithContext(SqlQueryFormatter::SqlServer(), context, CreateOrdersWithCompositeFk);
    CAPTURE(sql);
    CHECK(sql.contains("CHAR(30)"));
    CHECK_FALSE(sql.contains("NCHAR(30)"));
}

TEST_CASE("fk-match-referenced-text: single-column and ALTER TABLE foreign keys are adapted too",
          "[SqlMigration][compat][fk-text]")
{
    using namespace SqlColumnTypeDefinitions;
    {
        auto context = FkTextContext(Varchar { 40 });
        auto const sql = RenderWithContext(SqlQueryFormatter::SqlServer(), context, [](SqlMigrationQueryBuilder& plan) {
            plan.CreateTable("fk_orders")
                .PrimaryKey("NR", Integer())
                .RequiredForeignKey("USER_NAME",
                                    NVarchar(40),
                                    SqlForeignKeyReferenceDefinition { .tableName = "fk_users", .columnName = "NAME" });
        });
        CAPTURE(sql);
        CHECK(sql.contains("VARCHAR(40)"));
        CHECK_FALSE(sql.contains("NVARCHAR(40)"));
    }
    {
        auto context = FkTextContext(Varchar { 40 });
        auto const sql = RenderWithContext(SqlQueryFormatter::SqlServer(), context, [](SqlMigrationQueryBuilder& plan) {
            plan.AlterTable("fk_orders")
                .AddForeignKeyColumn("USER_NAME",
                                     NVarchar(40),
                                     SqlForeignKeyReferenceDefinition { .tableName = "fk_users", .columnName = "NAME" });
        });
        CAPTURE(sql);
        CHECK(sql.contains("VARCHAR(40)"));
        CHECK_FALSE(sql.contains("NVARCHAR(40)"));
    }
}

TEST_CASE("fk-match-referenced-text: nothing changes without the flag, on other backends, or on a length mismatch",
          "[SqlMigration][compat][fk-text]")
{
    using namespace SqlColumnTypeDefinitions;
    auto strict = MigrationRenderContext {};
    auto const declared = RenderWithContext(SqlQueryFormatter::SqlServer(), strict, CreateOrdersWithCompositeFk);
    REQUIRE(declared.contains("NCHAR(30)"));

    auto flagOff = FkTextContext(Char { 30 });
    flagOff.fkMatchReferencedText = false;
    CHECK(RenderWithContext(SqlQueryFormatter::SqlServer(), flagOff, CreateOrdersWithCompositeFk) == declared);

    auto mismatch = FkTextContext(Char { 20 });
    CHECK(RenderWithContext(SqlQueryFormatter::SqlServer(), mismatch, CreateOrdersWithCompositeFk) == declared);

    auto alreadyWide = FkTextContext(NChar { 30 });
    CHECK(RenderWithContext(SqlQueryFormatter::SqlServer(), alreadyWide, CreateOrdersWithCompositeFk) == declared);

    auto sqliteStrict = MigrationRenderContext {};
    auto sqliteFlag = FkTextContext(Char { 30 });
    CHECK(RenderWithContext(SqlQueryFormatter::Sqlite(), sqliteFlag, CreateOrdersWithCompositeFk)
          == RenderWithContext(SqlQueryFormatter::Sqlite(), sqliteStrict, CreateOrdersWithCompositeFk));
}

TEST_CASE_METHOD(SqlMigrationTestFixture,
                 "fk-match-referenced-text: a wide FK applies against a narrow legacy column",
                 "[SqlMigration][compat][fk-text]")
{
    auto& mgr = SqlMigration::MigrationManager::GetInstance();
    mgr.CreateMigrationHistory();
    auto stmt = SqlStatement { mgr.GetDataMapper().Connection() };
    // Created outside the migration set, as legacy tooling did.
    (void) stmt.ExecuteDirect(R"(CREATE TABLE "fk_users" ("NAME" CHAR(30) NOT NULL PRIMARY KEY))");

    mgr.SetCompatPolicy([](SqlMigration::MigrationBase const&) {
        return std::set<std::string> { std::string(CompatFlagFkMatchReferencedTextName) };
    });
    auto orders = SqlMigration::Migration<202610020001>("create orders", CreateOrdersWithCompositeFk);
    REQUIRE(mgr.ApplyPendingMigrations() == 1);
    mgr.SetCompatPolicy({});

    (void) stmt.ExecuteDirect(R"(INSERT INTO "fk_users" ("NAME") VALUES ('alice'))");
    (void) stmt.ExecuteDirect(R"(INSERT INTO "fk_orders" ("NR", "USER_NAME") VALUES (1, 'alice'))");
    CHECK(stmt.ExecuteDirectScalar<long long>(R"(SELECT COUNT(*) FROM "fk_orders")").value_or(0) == 1);

    // Checksums hash the plan as declared, so the adapted SQL must not show up as drift.
    CHECK(mgr.VerifyChecksums().empty());
}

TEST_CASE_METHOD(SqlMigrationTestFixture,
                 "fk-match-referenced-text: without the flag SQL Server still rejects the mismatch",
                 "[SqlMigration][compat][fk-text]")
{
    auto& mgr = SqlMigration::MigrationManager::GetInstance();
    auto stmt = SqlStatement { mgr.GetDataMapper().Connection() };
    if (stmt.Connection().ServerType() != SqlServerType::MICROSOFT_SQL)
        SKIP("only SQL Server distinguishes narrow and wide text in foreign keys");
    mgr.CreateMigrationHistory();
    (void) stmt.ExecuteDirect(R"(CREATE TABLE "fk_users" ("NAME" CHAR(30) NOT NULL PRIMARY KEY))");

    auto orders = SqlMigration::Migration<202610020002>("create orders", CreateOrdersWithCompositeFk);
    CHECK_THROWS(mgr.ApplyPendingMigrations());
}

// ================================================================================================
// unicode-upgrade-tables: key columns and their dependent objects on SQL Server (#643)
// ================================================================================================

namespace
{

/// Runs a scalar catalog query and returns its single string result ("" when NULL / no row).
std::string CatalogScalar(SqlStatement& stmt, std::string const& sql)
{
    return stmt.ExecuteDirectScalar<std::string>(sql).value_or(std::string {});
}

/// INFORMATION_SCHEMA data type of `table.column`.
std::string LiveDataType(SqlStatement& stmt, std::string_view table, std::string_view column)
{
    return CatalogScalar(stmt,
                         std::format("SELECT DATA_TYPE FROM INFORMATION_SCHEMA.COLUMNS WHERE TABLE_NAME = '{}' AND "
                                     "COLUMN_NAME = '{}'",
                                     table,
                                     column));
}

} // namespace

TEST_CASE_METHOD(SqlMigrationTestFixture,
                 "UnicodeUpgradeTables: SQL Server rebuilds keys, constraints and indexes on upgraded columns",
                 "[SqlMigration][UnicodeUpgrade]")
{
    using namespace Lightweight::SqlColumnTypeDefinitions;
    auto& mgr = SqlMigration::MigrationManager::GetInstance();
    auto stmt = SqlStatement { mgr.GetDataMapper().Connection() };
    if (stmt.Connection().ServerType() != SqlServerType::MICROSOFT_SQL)
        SKIP("only SQL Server needs dependent objects dropped to change a column between narrow and wide text");
    mgr.CreateMigrationHistory();

    // A legacy schema created outside the migration set: narrow key columns carrying every kind
    // of dependent object, with names the migrations would never generate.
    // Filtered indexes can only be created with ANSI_WARNINGS ON; the library's connections
    // default to OFF, which is also what the upgrade itself runs under (restored below).
    (void) stmt.ExecuteDirect("SET ANSI_WARNINGS ON");
    for (auto const* sql: {
             "CREATE TABLE uu_users (NAME CHAR(30) NOT NULL CONSTRAINT PK_legacy_users PRIMARY KEY, "
             "NICK VARCHAR(20) NULL CONSTRAINT UQ_legacy_nick UNIQUE, "
             "NOTE VARCHAR(50) NOT NULL CONSTRAINT DF_legacy_note DEFAULT 'none', "
             "CONSTRAINT CK_legacy_note CHECK (NOTE <> ''))",
             "CREATE NONCLUSTERED INDEX IX_legacy_note ON uu_users (NOTE DESC) INCLUDE (NICK) WHERE NICK IS NOT NULL",
             "CREATE TABLE uu_orders (NR INT NOT NULL CONSTRAINT PK_legacy_orders PRIMARY KEY, "
             "USER_NAME CHAR(30) NOT NULL CONSTRAINT FK_legacy_orders_user FOREIGN KEY REFERENCES uu_users (NAME) "
             "ON DELETE CASCADE)",
             "CREATE TABLE uu_lines (USER_NAME CHAR(30) NOT NULL, LINE INT NOT NULL, "
             "CONSTRAINT PK_legacy_lines PRIMARY KEY (USER_NAME, LINE))",
             "CREATE TABLE uu_line_refs (ID INT NOT NULL PRIMARY KEY, USER_NAME CHAR(30) NOT NULL, LINE INT NOT NULL, "
             "CONSTRAINT FK_legacy_line_refs FOREIGN KEY (USER_NAME, LINE) REFERENCES uu_lines (USER_NAME, LINE))",
             "INSERT INTO uu_users (NAME, NICK, NOTE) VALUES ('alice', 'al', 'first')",
             "INSERT INTO uu_orders (NR, USER_NAME) VALUES (1, 'alice')",
             "INSERT INTO uu_lines (USER_NAME, LINE) VALUES ('alice', 1)",
             "INSERT INTO uu_line_refs (ID, USER_NAME, LINE) VALUES (1, 'alice', 1)",
         })
        (void) stmt.ExecuteDirect(sql);
    (void) stmt.ExecuteDirect("SET ANSI_WARNINGS OFF");

    // The registered migrations declare the same tables with wide text.
    fold_test::FoldStub<20'26'10'02'00'01> decl {
        "declare wide",
        [](SqlMigrationQueryBuilder& plan) {
            plan.CreateTable("uu_users")
                .PrimaryKey("NAME", NChar(30))
                .Column("NICK", NVarchar(20))
                .RequiredColumn("NOTE", NVarchar(50));
            plan.CreateTable("uu_orders").PrimaryKey("NR", Integer()).RequiredColumn("USER_NAME", NChar(30));
            plan.CreateTable("uu_lines").RequiredColumn("USER_NAME", NChar(30)).RequiredColumn("LINE", Integer());
            plan.CreateTable("uu_line_refs")
                .PrimaryKey("ID", Integer())
                .RequiredColumn("USER_NAME", NChar(30))
                .RequiredColumn("LINE", Integer());
        }
    };

    // A dry run reports what would be dropped and recreated, and changes nothing.
    auto const preview = mgr.UnicodeUpgradeTables(/*dryRun=*/true);
    CHECK(std::ranges::contains(preview.rebuiltObjects, std::string { "PRIMARY KEY PK_legacy_users on dbo.uu_users" }));
    CHECK(std::ranges::contains(preview.rebuiltObjects,
                                std::string { "FOREIGN KEY FK_legacy_line_refs on dbo.uu_line_refs" }));
    CHECK(LiveDataType(stmt, "uu_users", "NAME") == "char");

    auto const result = mgr.UnicodeUpgradeTables(/*dryRun=*/false);
    REQUIRE(result.columns.size() == 6);

    CHECK(LiveDataType(stmt, "uu_users", "NAME") == "nchar");
    CHECK(LiveDataType(stmt, "uu_users", "NICK") == "nvarchar");
    CHECK(LiveDataType(stmt, "uu_users", "NOTE") == "nvarchar");
    CHECK(LiveDataType(stmt, "uu_orders", "USER_NAME") == "nchar");
    CHECK(LiveDataType(stmt, "uu_lines", "USER_NAME") == "nchar");
    CHECK(LiveDataType(stmt, "uu_line_refs", "USER_NAME") == "nchar");

    // Every dependent object is back, under its original name and with its definition.
    CHECK(CatalogScalar(stmt, "SELECT type FROM sys.key_constraints WHERE name = 'PK_legacy_users'") == "PK");
    CHECK(CatalogScalar(stmt, "SELECT type FROM sys.key_constraints WHERE name = 'UQ_legacy_nick'") == "UQ");
    CHECK(CatalogScalar(stmt, "SELECT type FROM sys.key_constraints WHERE name = 'PK_legacy_lines'") == "PK");
    CHECK(CatalogScalar(stmt, "SELECT definition FROM sys.default_constraints WHERE name = 'DF_legacy_note'") == "('none')");
    CHECK(
        CatalogScalar(stmt, "SELECT definition FROM sys.check_constraints WHERE name = 'CK_legacy_note'").contains("<>''"));
    CHECK(CatalogScalar(stmt,
                        "SELECT delete_referential_action_desc FROM sys.foreign_keys WHERE name = "
                        "'FK_legacy_orders_user'")
          == "CASCADE");
    CHECK(CatalogScalar(stmt, "SELECT name FROM sys.foreign_keys WHERE name = 'FK_legacy_line_refs'")
          == "FK_legacy_line_refs");
    CHECK(CatalogScalar(stmt,
                        "SELECT CAST(i.has_filter AS VARCHAR(1)) + CAST(ic.is_descending_key AS VARCHAR(1)) "
                        "FROM sys.indexes i JOIN sys.index_columns ic ON ic.object_id = i.object_id AND "
                        "ic.index_id = i.index_id AND ic.key_ordinal = 1 WHERE i.name = 'IX_legacy_note'")
          == "11");
    CHECK(CatalogScalar(stmt,
                        "SELECT COL_NAME(ic.object_id, ic.column_id) FROM sys.indexes i JOIN sys.index_columns ic ON "
                        "ic.object_id = i.object_id AND ic.index_id = i.index_id AND ic.is_included_column = 1 WHERE "
                        "i.name = 'IX_legacy_note'")
          == "NICK");

    // Data and constraint enforcement survive: the cascade still fires, the check still bites.
    // (Writes to a table with a filtered index need ANSI_WARNINGS ON as well.)
    (void) stmt.ExecuteDirect("SET ANSI_WARNINGS ON");
    CHECK(stmt.ExecuteDirectScalar<long long>("SELECT COUNT(*) FROM uu_line_refs").value_or(0) == 1);
    CHECK_THROWS(stmt.ExecuteDirect("INSERT INTO uu_users (NAME, NOTE) VALUES ('bob', '')"));
    (void) stmt.ExecuteDirect("DELETE FROM uu_users WHERE NAME = 'alice'");
    CHECK(stmt.ExecuteDirectScalar<long long>("SELECT COUNT(*) FROM uu_orders").value_or(-1) == 0);
    (void) stmt.ExecuteDirect("SET ANSI_WARNINGS OFF");

    CHECK(mgr.UnicodeUpgradeTables(/*dryRun=*/true).columns.empty());
}

TEST_CASE_METHOD(SqlMigrationTestFixture,
                 "UnicodeUpgradeTables: SQL Server refuses columns used by schema-bound views and changes nothing",
                 "[SqlMigration][UnicodeUpgrade]")
{
    using namespace Lightweight::SqlColumnTypeDefinitions;
    auto& mgr = SqlMigration::MigrationManager::GetInstance();
    auto stmt = SqlStatement { mgr.GetDataMapper().Connection() };
    if (stmt.Connection().ServerType() != SqlServerType::MICROSOFT_SQL)
        SKIP("schema-bound views are a SQL Server concept here");
    mgr.CreateMigrationHistory();
    (void) stmt.ExecuteDirect("DROP VIEW IF EXISTS uu_bound_view");
    (void) stmt.ExecuteDirect("CREATE TABLE uu_bound (ID INT NOT NULL PRIMARY KEY, CODE VARCHAR(10) NOT NULL)");
    (void) stmt.ExecuteDirect("CREATE VIEW uu_bound_view WITH SCHEMABINDING AS SELECT CODE FROM dbo.uu_bound");

    fold_test::FoldStub<20'26'10'02'00'02> decl {
        "declare wide",
        [](SqlMigrationQueryBuilder& plan) {
            plan.CreateTable("uu_bound").PrimaryKey("ID", Integer()).RequiredColumn("CODE", NVarchar(10));
        }
    };

    try
    {
        std::ignore = mgr.UnicodeUpgradeTables(/*dryRun=*/false);
        FAIL("expected the schema-bound view to block the upgrade");
    }
    catch (std::runtime_error const& e)
    {
        // Our up-front refusal (not the server's own error 5074 halfway through): it names the
        // blocking object and says what to do, before a single statement has run.
        CHECK(std::string_view { e.what() }.contains("uu_bound_view"));
        CHECK(std::string_view { e.what() }.contains("drop them first"));
    }
    CHECK(LiveDataType(stmt, "uu_bound", "CODE") == "varchar");
    CHECK(CatalogScalar(stmt, "SELECT type FROM sys.key_constraints WHERE parent_object_id = OBJECT_ID('uu_bound')")
          == "PK");
    (void) stmt.ExecuteDirect("DROP VIEW uu_bound_view");
}

TEST_CASE_METHOD(SqlMigrationTestFixture,
                 "UnicodeUpgradeTables: SQL Server widens undeclared columns that reference an upgraded key",
                 "[SqlMigration][UnicodeUpgrade]")
{
    using namespace Lightweight::SqlColumnTypeDefinitions;
    auto& mgr = SqlMigration::MigrationManager::GetInstance();
    auto stmt = SqlStatement { mgr.GetDataMapper().Connection() };
    if (stmt.Connection().ServerType() != SqlServerType::MICROSOFT_SQL)
        SKIP("only SQL Server rejects a foreign key between narrow and wide text");
    mgr.CreateMigrationHistory();

    // A legacy shape the migrations no longer describe: uu_fk_child.REF was renamed since, and
    // uu_fk_log is not declared at all. Both reference the key the migrations widen.
    for (auto const* sql: {
             "CREATE TABLE uu_fk_parent (NAME CHAR(30) NOT NULL CONSTRAINT PK_uu_fk_parent PRIMARY KEY)",
             "CREATE TABLE uu_fk_child (ID INT NOT NULL CONSTRAINT PK_uu_fk_child PRIMARY KEY, "
             "REF CHAR(30) NOT NULL CONSTRAINT FK_uu_fk_child_ref REFERENCES uu_fk_parent (NAME))",
             "CREATE UNIQUE INDEX IX_uu_fk_child_ref ON uu_fk_child (REF, ID)",
             "CREATE TABLE uu_fk_log (ID INT NOT NULL PRIMARY KEY, "
             "PARENT_NAME CHAR(30) NULL CONSTRAINT FK_uu_fk_log_parent REFERENCES uu_fk_parent (NAME))",
             "INSERT INTO uu_fk_parent (NAME) VALUES ('alice')",
             "INSERT INTO uu_fk_child (ID, REF) VALUES (1, 'alice')",
             "INSERT INTO uu_fk_log (ID, PARENT_NAME) VALUES (1, 'alice')",
         })
        (void) stmt.ExecuteDirect(sql);

    fold_test::FoldStub<20'26'10'16'00'01> decl {
        "declare wide",
        [](SqlMigrationQueryBuilder& plan) {
            plan.CreateTable("uu_fk_parent").PrimaryKey("NAME", NChar(30));
            plan.CreateTable("uu_fk_child").PrimaryKey("ID", Integer()).RequiredColumn("PARENT_NAME", NChar(30));
        }
    };

    auto const upgraded = [](SqlMigration::MigrationManager::UnicodeUpgradeResult const& result) {
        auto names = std::vector<std::string> {};
        for (auto const& c: result.columns)
            names.push_back(std::format("{}.{} {}", c.table.table, c.column, c.joinedByForeignKeyTo));
        std::ranges::sort(names);
        return names;
    };
    auto const expected = std::vector<std::string> {
        "uu_fk_child.REF uu_fk_parent.NAME",
        "uu_fk_log.PARENT_NAME uu_fk_parent.NAME",
        "uu_fk_parent.NAME ",
    };

    // The dry run lists the referencing columns next to the declared one, and changes nothing.
    auto const preview = mgr.UnicodeUpgradeTables(/*dryRun=*/true);
    CHECK(upgraded(preview) == expected);
    CHECK(std::ranges::contains(preview.rebuiltObjects, std::string { "INDEX IX_uu_fk_child_ref on dbo.uu_fk_child" }));
    CHECK(std::ranges::contains(preview.rebuiltObjects, std::string { "FOREIGN KEY FK_uu_fk_log_parent on dbo.uu_fk_log" }));
    CHECK(LiveDataType(stmt, "uu_fk_child", "REF") == "char");

    auto const result = mgr.UnicodeUpgradeTables(/*dryRun=*/false);
    CHECK(upgraded(result) == expected);
    CHECK(LiveDataType(stmt, "uu_fk_parent", "NAME") == "nchar");
    CHECK(LiveDataType(stmt, "uu_fk_child", "REF") == "nchar");
    CHECK(LiveDataType(stmt, "uu_fk_log", "PARENT_NAME") == "nchar");

    // The foreign keys and the index are back under their original names, on the same columns.
    CHECK(CatalogScalar(stmt,
                        "SELECT OBJECT_NAME(referenced_object_id) FROM sys.foreign_keys WHERE name = "
                        "'FK_uu_fk_child_ref'")
          == "uu_fk_parent");
    CHECK(CatalogScalar(stmt,
                        "SELECT OBJECT_NAME(referenced_object_id) FROM sys.foreign_keys WHERE name = "
                        "'FK_uu_fk_log_parent'")
          == "uu_fk_parent");
    CHECK(CatalogScalar(stmt,
                        "SELECT CAST(i.is_unique AS VARCHAR(1)) + STRING_AGG(COL_NAME(ic.object_id, ic.column_id), ',') "
                        "WITHIN GROUP (ORDER BY ic.key_ordinal) FROM sys.indexes i JOIN sys.index_columns ic ON "
                        "ic.object_id = i.object_id AND ic.index_id = i.index_id WHERE i.name = 'IX_uu_fk_child_ref' "
                        "GROUP BY i.is_unique")
          == "1REF,ID");

    // The data survives and the keys still bite.
    CHECK(stmt.ExecuteDirectScalar<long long>("SELECT COUNT(*) FROM uu_fk_child WHERE REF = N'alice'").value_or(0) == 1);
    CHECK_THROWS(stmt.ExecuteDirect("INSERT INTO uu_fk_log (ID, PARENT_NAME) VALUES (2, N'nobody')"));

    CHECK(mgr.UnicodeUpgradeTables(/*dryRun=*/true).columns.empty());
}

TEST_CASE_METHOD(SqlMigrationTestFixture,
                 "UnicodeUpgradeTables: SQL Server widens the primary key of an undeclared table that references an "
                 "upgraded key, and what references that key in turn",
                 "[SqlMigration][UnicodeUpgrade]")
{
    using namespace Lightweight::SqlColumnTypeDefinitions;
    auto& mgr = SqlMigration::MigrationManager::GetInstance();
    auto stmt = SqlStatement { mgr.GetDataMapper().Connection() };
    if (stmt.Connection().ServerType() != SqlServerType::MICROSOFT_SQL)
        SKIP("only SQL Server rejects a foreign key between narrow and wide text");
    mgr.CreateMigrationHistory();

    // uu_fk_xref is declared by no migration, and its referencing column is also its primary key, which
    // uu_fk_xref_note references in turn: a second hop.
    for (auto const* sql: {
             "CREATE TABLE uu_fk_product (PRODUCT CHAR(30) NOT NULL CONSTRAINT PK_uu_fk_product PRIMARY KEY)",
             "CREATE TABLE uu_fk_xref (PRODUCT_CODE CHAR(30) NOT NULL CONSTRAINT PK_uu_fk_xref PRIMARY KEY, "
             "CONSTRAINT FK_uu_fk_xref_product_code FOREIGN KEY (PRODUCT_CODE) REFERENCES uu_fk_product (PRODUCT))",
             "CREATE TABLE uu_fk_xref_note (ID INT NOT NULL PRIMARY KEY, PRODUCT_CODE CHAR(30) NOT NULL "
             "CONSTRAINT FK_uu_fk_xref_note REFERENCES uu_fk_xref (PRODUCT_CODE))",
             "INSERT INTO uu_fk_product (PRODUCT) VALUES ('sand')",
             "INSERT INTO uu_fk_xref (PRODUCT_CODE) VALUES ('sand')",
             "INSERT INTO uu_fk_xref_note (ID, PRODUCT_CODE) VALUES (1, 'sand')",
         })
        (void) stmt.ExecuteDirect(sql);

    fold_test::FoldStub<20'26'10'16'00'05> decl { "declare wide", [](SqlMigrationQueryBuilder& plan) {
                                                     plan.CreateTable("uu_fk_product").PrimaryKey("PRODUCT", NChar(30));
                                                 } };

    auto const preview = mgr.UnicodeUpgradeTables(/*dryRun=*/true);
    CHECK(std::ranges::contains(preview.rebuiltObjects, std::string { "PRIMARY KEY PK_uu_fk_xref on dbo.uu_fk_xref" }));

    auto const result = mgr.UnicodeUpgradeTables(/*dryRun=*/false);
    auto const upgraded = [&] {
        auto names = std::vector<std::string> {};
        for (auto const& c: result.columns)
            names.push_back(std::format("{}.{} {}", c.table.table, c.column, c.joinedByForeignKeyTo));
        std::ranges::sort(names);
        return names;
    }();
    CHECK(upgraded
          == std::vector<std::string> {
              "uu_fk_product.PRODUCT ",
              "uu_fk_xref.PRODUCT_CODE uu_fk_product.PRODUCT",
              "uu_fk_xref_note.PRODUCT_CODE uu_fk_xref.PRODUCT_CODE",
          });
    // The second-hop foreign key is reported among the rebuilt ones too.
    CHECK(std::ranges::any_of(result.rebuiltForeignKeys,
                              [](auto const& fk) { return fk.referencedTableName == "uu_fk_xref"; }));
    CHECK(LiveDataType(stmt, "uu_fk_product", "PRODUCT") == "nchar");
    CHECK(LiveDataType(stmt, "uu_fk_xref", "PRODUCT_CODE") == "nchar");
    CHECK(LiveDataType(stmt, "uu_fk_xref_note", "PRODUCT_CODE") == "nchar");
    CHECK(CatalogScalar(stmt,
                        "SELECT OBJECT_NAME(referenced_object_id) FROM sys.foreign_keys WHERE name = "
                        "'FK_uu_fk_xref_note'")
          == "uu_fk_xref");
    CHECK(CatalogScalar(stmt, "SELECT type FROM sys.key_constraints WHERE name = 'PK_uu_fk_xref'") == "PK");
    CHECK(CatalogScalar(stmt,
                        "SELECT OBJECT_NAME(referenced_object_id) FROM sys.foreign_keys WHERE name = "
                        "'FK_uu_fk_xref_product_code'")
          == "uu_fk_product");
    CHECK(stmt.ExecuteDirectScalar<long long>("SELECT COUNT(*) FROM uu_fk_xref").value_or(0) == 1);
    CHECK(stmt.ExecuteDirectScalar<long long>("SELECT COUNT(*) FROM uu_fk_xref_note").value_or(0) == 1);
}

TEST_CASE_METHOD(SqlMigrationTestFixture,
                 "UnicodeUpgradeTables: SQL Server pairs the columns of a composite foreign key by position",
                 "[SqlMigration][UnicodeUpgrade]")
{
    using namespace Lightweight::SqlColumnTypeDefinitions;
    auto& mgr = SqlMigration::MigrationManager::GetInstance();
    auto stmt = SqlStatement { mgr.GetDataMapper().Connection() };
    if (stmt.Connection().ServerType() != SqlServerType::MICROSOFT_SQL)
        SKIP("only SQL Server rejects a foreign key between narrow and wide text");
    mgr.CreateMigrationHistory();

    // The child lists the key columns in another order than its own column order.
    for (auto const* sql: {
             "CREATE TABLE uu_fk_pair (ID INT NOT NULL, CODE CHAR(10) NOT NULL, NOTE CHAR(10) NULL, "
             "CONSTRAINT PK_uu_fk_pair PRIMARY KEY (ID, CODE))",
             "CREATE TABLE uu_fk_pair_ref (CODE CHAR(10) NOT NULL, LABEL CHAR(10) NULL, ID INT NOT NULL, "
             "CONSTRAINT FK_uu_fk_pair_ref FOREIGN KEY (ID, CODE) REFERENCES uu_fk_pair (ID, CODE))",
             "INSERT INTO uu_fk_pair (ID, CODE) VALUES (1, 'a')",
             "INSERT INTO uu_fk_pair_ref (CODE, ID) VALUES ('a', 1)",
         })
        (void) stmt.ExecuteDirect(sql);

    fold_test::FoldStub<20'26'10'16'00'06> decl { "declare wide", [](SqlMigrationQueryBuilder& plan) {
                                                     plan.CreateTable("uu_fk_pair")
                                                         .RequiredColumn("ID", Integer())
                                                         .RequiredColumn("CODE", NChar(10))
                                                         .Column("NOTE", Char(10));
                                                 } };

    auto const result = mgr.UnicodeUpgradeTables(/*dryRun=*/false);
    REQUIRE(result.columns.size() == 2);
    CHECK(result.columns.back().table.table == "uu_fk_pair_ref");
    CHECK(result.columns.back().column == "CODE");
    CHECK(result.columns.back().joinedByForeignKeyTo == "uu_fk_pair.CODE");
    CHECK(LiveDataType(stmt, "uu_fk_pair_ref", "CODE") == "nchar");
    CHECK(LiveDataType(stmt, "uu_fk_pair_ref", "ID") == "int");
    CHECK(LiveDataType(stmt, "uu_fk_pair_ref", "LABEL") == "char");
    CHECK(CatalogScalar(stmt,
                        "SELECT STRING_AGG(COL_NAME(parent_object_id, parent_column_id) + '>' + "
                        "COL_NAME(referenced_object_id, referenced_column_id), ',') WITHIN GROUP "
                        "(ORDER BY constraint_column_id) FROM sys.foreign_key_columns WHERE constraint_object_id = "
                        "OBJECT_ID('FK_uu_fk_pair_ref')")
          == "ID>ID,CODE>CODE");
}

TEST_CASE_METHOD(SqlMigrationTestFixture,
                 "UnicodeUpgradeTables: SQL Server leaves a same-named table in another schema alone",
                 "[SqlMigration][UnicodeUpgrade]")
{
    using namespace Lightweight::SqlColumnTypeDefinitions;
    auto& mgr = SqlMigration::MigrationManager::GetInstance();
    auto stmt = SqlStatement { mgr.GetDataMapper().Connection() };
    if (stmt.Connection().ServerType() != SqlServerType::MICROSOFT_SQL)
        SKIP("only SQL Server rejects a foreign key between narrow and wide text");
    mgr.CreateMigrationHistory();

    // Another schema, sorting before dbo, holds a table named like the undeclared referencing one.
    (void) stmt.ExecuteDirect("DROP TABLE IF EXISTS aa_uu_other.uu_fk_twin");
    (void) stmt.ExecuteDirect("DROP SCHEMA IF EXISTS aa_uu_other");
    (void) stmt.ExecuteDirect("CREATE SCHEMA aa_uu_other");
    for (auto const* sql: {
             "CREATE TABLE aa_uu_other.uu_fk_twin (REF CHAR(30) NOT NULL)",
             "CREATE TABLE uu_fk_origin (NAME CHAR(30) NOT NULL PRIMARY KEY)",
             "CREATE TABLE uu_fk_twin (REF CHAR(30) NOT NULL CONSTRAINT FK_uu_fk_twin REFERENCES uu_fk_origin (NAME))",
         })
        (void) stmt.ExecuteDirect(sql);

    fold_test::FoldStub<20'26'10'16'00'07> decl { "declare wide", [](SqlMigrationQueryBuilder& plan) {
                                                     plan.CreateTable("uu_fk_origin").PrimaryKey("NAME", NChar(30));
                                                 } };

    auto const twinType = [&](std::string_view schema) {
        return CatalogScalar(stmt,
                             std::format("SELECT DATA_TYPE FROM INFORMATION_SCHEMA.COLUMNS WHERE TABLE_SCHEMA = '{}' "
                                         "AND TABLE_NAME = 'uu_fk_twin' AND COLUMN_NAME = 'REF'",
                                         schema));
    };
    auto const result = mgr.UnicodeUpgradeTables(/*dryRun=*/false);
    CHECK(result.columns.size() == 2);
    CHECK(twinType("dbo") == "nchar");
    CHECK(twinType("aa_uu_other") == "char");

    (void) stmt.ExecuteDirect("DROP TABLE aa_uu_other.uu_fk_twin");
    (void) stmt.ExecuteDirect("DROP SCHEMA aa_uu_other");
}

TEST_CASE_METHOD(SqlMigrationTestFixture,
                 "UnicodeUpgradeTables: SQL Server widens an undeclared key that an upgraded column references",
                 "[SqlMigration][UnicodeUpgrade]")
{
    using namespace Lightweight::SqlColumnTypeDefinitions;
    auto& mgr = SqlMigration::MigrationManager::GetInstance();
    auto stmt = SqlStatement { mgr.GetDataMapper().Connection() };
    if (stmt.Connection().ServerType() != SqlServerType::MICROSOFT_SQL)
        SKIP("only SQL Server rejects a foreign key between narrow and wide text");
    mgr.CreateMigrationHistory();

    for (auto const* sql: {
             "CREATE TABLE uu_fk_codes (CODE VARCHAR(10) NOT NULL CONSTRAINT PK_uu_fk_codes PRIMARY KEY)",
             "CREATE TABLE uu_fk_items (ID INT NOT NULL PRIMARY KEY, "
             "CODE VARCHAR(10) NOT NULL CONSTRAINT FK_uu_fk_items_code REFERENCES uu_fk_codes (CODE))",
             "INSERT INTO uu_fk_codes (CODE) VALUES ('a')",
             "INSERT INTO uu_fk_items (ID, CODE) VALUES (1, 'a')",
         })
        (void) stmt.ExecuteDirect(sql);

    fold_test::FoldStub<20'26'10'16'00'02> decl {
        "declare wide",
        [](SqlMigrationQueryBuilder& plan) {
            plan.CreateTable("uu_fk_items").PrimaryKey("ID", Integer()).RequiredColumn("CODE", NVarchar(10));
        }
    };

    auto const result = mgr.UnicodeUpgradeTables(/*dryRun=*/false);
    REQUIRE(result.columns.size() == 2);
    CHECK(result.columns.back().table.table == "uu_fk_codes");
    CHECK(result.columns.back().joinedByForeignKeyTo == "uu_fk_items.CODE");
    CHECK(LiveDataType(stmt, "uu_fk_items", "CODE") == "nvarchar");
    CHECK(LiveDataType(stmt, "uu_fk_codes", "CODE") == "nvarchar");
    CHECK(CatalogScalar(stmt, "SELECT name FROM sys.foreign_keys WHERE name = 'FK_uu_fk_items_code'")
          == "FK_uu_fk_items_code");
    CHECK(CatalogScalar(stmt, "SELECT type FROM sys.key_constraints WHERE name = 'PK_uu_fk_codes'") == "PK");
}

TEST_CASE_METHOD(SqlMigrationTestFixture,
                 "UnicodeUpgradeTables: SQL Server refuses a referencing column declared with another type",
                 "[SqlMigration][UnicodeUpgrade]")
{
    using namespace Lightweight::SqlColumnTypeDefinitions;
    auto& mgr = SqlMigration::MigrationManager::GetInstance();
    auto stmt = SqlStatement { mgr.GetDataMapper().Connection() };
    if (stmt.Connection().ServerType() != SqlServerType::MICROSOFT_SQL)
        SKIP("only SQL Server rejects a foreign key between narrow and wide text");
    mgr.CreateMigrationHistory();

    for (auto const* sql: {
             "CREATE TABLE uu_fk_owner (NAME CHAR(30) NOT NULL PRIMARY KEY)",
             "CREATE TABLE uu_fk_pinned (ID INT NOT NULL PRIMARY KEY, "
             "OWNER CHAR(30) NOT NULL REFERENCES uu_fk_owner (NAME))",
         })
        (void) stmt.ExecuteDirect(sql);

    // The migrations widen the key but keep the referencing column narrow: no upgrade can satisfy both.
    fold_test::FoldStub<20'26'10'16'00'03> decl {
        "declare a contradiction",
        [](SqlMigrationQueryBuilder& plan) {
            plan.CreateTable("uu_fk_owner").PrimaryKey("NAME", NChar(30));
            plan.CreateTable("uu_fk_pinned").PrimaryKey("ID", Integer()).RequiredColumn("OWNER", Char(30));
        }
    };

    for (auto const dryRun: { true, false })
    {
        CAPTURE(dryRun);
        try
        {
            std::ignore = mgr.UnicodeUpgradeTables(dryRun);
            FAIL("expected the narrow declaration to block the upgrade");
        }
        catch (std::runtime_error const& e)
        {
            CHECK(std::string_view { e.what() }.contains("uu_fk_pinned.OWNER"));
            CHECK(std::string_view { e.what() }.contains("uu_fk_owner.NAME"));
        }
    }
    CHECK(LiveDataType(stmt, "uu_fk_owner", "NAME") == "char");
}

TEST_CASE_METHOD(SqlMigrationTestFixture,
                 "UnicodeUpgradeTables: matches declared names to the live schema without regard to case",
                 "[SqlMigration][UnicodeUpgrade]")
{
    using namespace Lightweight::SqlColumnTypeDefinitions;
    auto& mgr = SqlMigration::MigrationManager::GetInstance();
    mgr.CreateMigrationHistory();
    auto stmt = SqlStatement { mgr.GetDataMapper().Connection() };
    (void) stmt.ExecuteDirect(R"(CREATE TABLE "uu_Case_Stamp" ("Item_Name" CHAR(254) NOT NULL))");

    fold_test::FoldStub<20'26'10'16'00'04> decl {
        "declare upper case",
        [](SqlMigrationQueryBuilder& plan) { plan.CreateTable("UU_CASE_STAMP").RequiredColumn("ITEM_NAME", NChar(254)); }
    };

    auto const dry = mgr.UnicodeUpgradeTables(/*dryRun=*/true);
    if (stmt.Connection().ServerType() != SqlServerType::MICROSOFT_SQL)
    {
        // PostgreSQL: quoted identifiers are case-sensitive, so the declared table is another one.
        // SQLite: the Unicode ODBC driver reports CHAR as wide, so there is no drift to see.
        if (stmt.Connection().ServerType() == SqlServerType::POSTGRESQL)
            CHECK(dry.columns.empty());
        return;
    }

    REQUIRE(dry.columns.size() == 1);
    CHECK(dry.columns.front().table.table == "uu_Case_Stamp");
    CHECK(dry.columns.front().column == "Item_Name");

    std::ignore = mgr.UnicodeUpgradeTables(/*dryRun=*/false);
    CHECK(LiveDataType(stmt, "uu_Case_Stamp", "Item_Name") == "nchar");
    CHECK(mgr.UnicodeUpgradeTables(/*dryRun=*/true).columns.empty());
}

TEST_CASE_METHOD(SqlMigrationTestFixture,
                 "CreateIndex with a predicate enforces uniqueness only on matching rows",
                 "[SqlMigration]")
{
    using namespace SqlColumnTypeDefinitions;

    auto& manager = SqlMigration::MigrationManager::GetInstance();
    auto& conn = manager.GetDataMapper().Connection();

    // SQL Server creates a filtered index, and writes to its table, only with ANSI_WARNINGS ON, which
    // is what the Error mode selects; the default Truncate mode turns it off. Inert elsewhere.
    auto const previousMode = conn.StringTruncationMode();
    conn.SetStringTruncationMode(SqlStringTruncationMode::Error);
    auto const restoreMode = detail::Finally([&] { conn.SetStringTruncationMode(previousMode); });

    // Deduplication on an optional key: rows without a key ('') may repeat, rows with one may not.
    auto const migration = SqlMigration::Migration<202610050001>(
        "create queue with partial unique index",
        [](SqlMigrationQueryBuilder& plan) {
            plan.CreateTable("partial_queue").PrimaryKey("id", Integer()).RequiredColumn("dedup_key", Varchar(64));
            plan.CreateIndex("idx_partial_queue_key",
                             "partial_queue",
                             { "dedup_key" },
                             { .type = IndexType::Unique, .whereExpression = R"("dedup_key" <> '')" });
        },
        [](SqlMigrationQueryBuilder& plan) { plan.DropTable("partial_queue"); });

    // Folding the registered migrations keeps the predicate on the surviving index.
    auto const fold = manager.FoldRegisteredMigrations(conn.QueryFormatter());
    REQUIRE(fold.indexes.size() == 1);
    CHECK(fold.indexes.front().unique);
    CHECK(fold.indexes.front().whereExpression == R"("dedup_key" <> '')");

    manager.CreateMigrationHistory();
    REQUIRE(manager.ApplyPendingMigrations() == 1);

    auto stmt = SqlStatement { conn };
    auto const insert = [&](int id, std::string_view key) {
        (void) stmt.ExecuteDirect(
            std::format(R"(INSERT INTO "partial_queue" ("id", "dedup_key") VALUES ({}, '{}'))", id, key));
    };

    // Rows outside the predicate are not indexed, so an empty key may repeat...
    CHECK_NOTHROW(insert(1, ""));
    CHECK_NOTHROW(insert(2, ""));
    // ...while a non-empty key is unique.
    CHECK_NOTHROW(insert(3, "job-42"));
    CHECK_THROWS(insert(4, "job-42"));
    CHECK_NOTHROW(insert(5, "job-43"));

    auto cursor = stmt.ExecuteDirect(R"(SELECT COUNT(*) FROM "partial_queue")");
    REQUIRE(cursor.FetchRow());
    CHECK(cursor.GetColumn<int64_t>(1) == 4);
}

// ================================================================================================
// unicode-upgrade-tables in a database whose tables span several schemas
// ================================================================================================

namespace
{

/// Data type of @p schema.@p table.@p column, from INFORMATION_SCHEMA.
std::string LiveDataTypeIn(SqlStatement& stmt, std::string_view schema, std::string_view table, std::string_view column)
{
    return CatalogScalar(stmt,
                         std::format("SELECT DATA_TYPE FROM INFORMATION_SCHEMA.COLUMNS WHERE TABLE_SCHEMA = '{}' AND "
                                     "TABLE_NAME = '{}' AND COLUMN_NAME = '{}'",
                                     schema,
                                     table,
                                     column));
}

/// Drops what the SQL Server multi-schema upgrade tests create, ending any impersonation first.
void DropSqlServerMultiSchemaFixture(SqlStatement& stmt)
{
    for (auto const* sql: {
             "IF USER_NAME() = N'uu_ms_sales_user' REVERT",
             "IF OBJECT_ID(N'sales.uu_ms_orders') IS NOT NULL DROP TABLE sales.uu_ms_orders",
             "IF OBJECT_ID(N'dbo.uu_ms_orders') IS NOT NULL DROP TABLE dbo.uu_ms_orders",
             "IF OBJECT_ID(N'dbo.uu_ms_customers') IS NOT NULL DROP TABLE dbo.uu_ms_customers",
             "IF OBJECT_ID(N'sales.uu_ms_customers') IS NOT NULL DROP TABLE sales.uu_ms_customers",
             "IF OBJECT_ID(N'uu_ms_other.uu_ms_customers') IS NOT NULL DROP TABLE uu_ms_other.uu_ms_customers",
             "IF OBJECT_ID(N'uu_ms_third.uu_ms_customers') IS NOT NULL DROP TABLE uu_ms_third.uu_ms_customers",
             "IF SCHEMA_ID(N'uu_ms_other') IS NOT NULL DROP SCHEMA uu_ms_other",
             "IF SCHEMA_ID(N'uu_ms_third') IS NOT NULL DROP SCHEMA uu_ms_third",
             "IF USER_ID(N'uu_ms_sales_user') IS NOT NULL DROP USER uu_ms_sales_user",
         })
        (void) stmt.ExecuteDirect(sql);
    // A `sales` schema that still holds other objects stays.
    constexpr auto dropEmptySchema = "IF SCHEMA_ID(N'sales') IS NOT NULL AND NOT EXISTS (SELECT 1 FROM sys.objects "
                                     "WHERE schema_id = SCHEMA_ID(N'sales')) DROP SCHEMA sales";
    (void) stmt.ExecuteDirect(dropEmptySchema);
}

/// Creates `sales` and a user whose default schema it is, for `EXECUTE AS USER`.
void CreateSalesUser(SqlStatement& stmt)
{
    for (auto const* sql: {
             "CREATE SCHEMA sales",
             "CREATE USER uu_ms_sales_user WITHOUT LOGIN WITH DEFAULT_SCHEMA = sales",
             "ALTER ROLE db_owner ADD MEMBER uu_ms_sales_user",
         })
        (void) stmt.ExecuteDirect(sql);
}

/// Registers (for its lifetime) a migration that declares both test tables, unqualified and wide.
std::unique_ptr<SqlMigration::MigrationBase> DeclareMultiSchemaUpgradeTables()
{
    using namespace Lightweight::SqlColumnTypeDefinitions;
    return std::make_unique<fold_test::FoldStub<20'26'10'17'00'01>>("declare wide", [](SqlMigrationQueryBuilder& plan) {
        plan.CreateTable("uu_ms_customers").PrimaryKey("CODE", NChar(10)).Column("NAME", NVarchar(30));
        plan.CreateTable("uu_ms_orders").PrimaryKey("ID", Integer()).RequiredColumn("CUSTOMER_CODE", NChar(10));
    });
}

/// Runs a real upgrade and returns the message it fails with ("" when it succeeds).
std::string UpgradeError(SqlMigration::MigrationManager& manager, bool dryRun)
{
    try
    {
        std::ignore = manager.UnicodeUpgradeTables(dryRun);
        return {};
    }
    catch (std::runtime_error const& error)
    {
        return error.what();
    }
}

} // namespace

TEST_CASE_METHOD(SqlMigrationTestFixture,
                 "UnicodeUpgradeTables: SQL Server widens a key in dbo while the default schema is another one",
                 "[SqlMigration][UnicodeUpgrade][MultiSchema]")
{
    auto& mgr = SqlMigration::MigrationManager::GetInstance();
    auto stmt = SqlStatement { mgr.GetDataMapper().Connection() };
    if (stmt.Connection().ServerType() != SqlServerType::MICROSOFT_SQL)
        SKIP("only SQL Server keeps narrow and wide text apart, so only there a key blocks the upgrade");
    mgr.CreateMigrationHistory();

    DropSqlServerMultiSchemaFixture(stmt);
    auto const cleanup = detail::Finally([&] { DropSqlServerMultiSchemaFixture(stmt); });
    // A CHAR key in dbo, referenced from the default schema `sales`: unqualified names resolve
    // to sales first and then to dbo, as for a login whose default schema is not dbo.
    CreateSalesUser(stmt);
    for (auto const* sql: {
             "CREATE TABLE dbo.uu_ms_customers (CODE CHAR(10) NOT NULL CONSTRAINT PK_uu_ms_customers PRIMARY KEY, "
             "NAME VARCHAR(30) NULL)",
             "CREATE TABLE sales.uu_ms_orders (ID INT NOT NULL CONSTRAINT PK_uu_ms_orders PRIMARY KEY, "
             "CUSTOMER_CODE CHAR(10) NOT NULL CONSTRAINT FK_uu_ms_orders_customer FOREIGN KEY "
             "REFERENCES dbo.uu_ms_customers (CODE))",
             "INSERT INTO dbo.uu_ms_customers (CODE, NAME) VALUES ('c1', 'first')",
             "INSERT INTO sales.uu_ms_orders (ID, CUSTOMER_CODE) VALUES (1, 'c1')",
         })
        (void) stmt.ExecuteDirect(sql);
    auto const declared = DeclareMultiSchemaUpgradeTables();
    (void) stmt.ExecuteDirect("EXECUTE AS USER = N'uu_ms_sales_user'");

    auto const preview = mgr.UnicodeUpgradeTables(/*dryRun=*/true);
    CHECK(std::ranges::contains(preview.rebuiltObjects,
                                std::string { "PRIMARY KEY PK_uu_ms_customers on dbo.uu_ms_customers" }));
    CHECK(std::ranges::contains(preview.rebuiltObjects,
                                std::string { "FOREIGN KEY FK_uu_ms_orders_customer on sales.uu_ms_orders" }));

    auto const result = mgr.UnicodeUpgradeTables(/*dryRun=*/false);
    CHECK(result.columns.size() == 3);
    (void) stmt.ExecuteDirect("REVERT");

    CHECK(LiveDataTypeIn(stmt, "dbo", "uu_ms_customers", "CODE") == "nchar");
    CHECK(LiveDataTypeIn(stmt, "dbo", "uu_ms_customers", "NAME") == "nvarchar");
    CHECK(LiveDataTypeIn(stmt, "sales", "uu_ms_orders", "CUSTOMER_CODE") == "nchar");
    CHECK(CatalogScalar(stmt, "SELECT type FROM sys.key_constraints WHERE name = 'PK_uu_ms_customers'") == "PK");
    CHECK(CatalogScalar(stmt, "SELECT name FROM sys.foreign_keys WHERE name = 'FK_uu_ms_orders_customer'")
          == "FK_uu_ms_orders_customer");
    CHECK_THROWS(stmt.ExecuteDirect("INSERT INTO sales.uu_ms_orders (ID, CUSTOMER_CODE) VALUES (2, 'none')"));
}

TEST_CASE_METHOD(SqlMigrationTestFixture,
                 "UnicodeUpgradeTables: SQL Server widens a key in a schema other than the default one",
                 "[SqlMigration][UnicodeUpgrade][MultiSchema]")
{
    auto& mgr = SqlMigration::MigrationManager::GetInstance();
    auto stmt = SqlStatement { mgr.GetDataMapper().Connection() };
    if (stmt.Connection().ServerType() != SqlServerType::MICROSOFT_SQL)
        SKIP("only SQL Server keeps narrow and wide text apart, so only there a key blocks the upgrade");
    mgr.CreateMigrationHistory();

    DropSqlServerMultiSchemaFixture(stmt);
    auto const cleanup = detail::Finally([&] { DropSqlServerMultiSchemaFixture(stmt); });
    // A CHAR key in `sales`, its only holder, referenced from the default schema dbo.
    for (auto const* sql: {
             "CREATE SCHEMA sales",
             "CREATE TABLE sales.uu_ms_customers (CODE CHAR(10) NOT NULL CONSTRAINT PK_uu_ms_customers PRIMARY KEY, "
             "NAME VARCHAR(30) NULL)",
             "CREATE TABLE dbo.uu_ms_orders (ID INT NOT NULL CONSTRAINT PK_uu_ms_orders PRIMARY KEY, "
             "CUSTOMER_CODE CHAR(10) NOT NULL CONSTRAINT FK_uu_ms_orders_customer FOREIGN KEY "
             "REFERENCES sales.uu_ms_customers (CODE))",
             "INSERT INTO sales.uu_ms_customers (CODE, NAME) VALUES ('c1', 'first')",
         })
        (void) stmt.ExecuteDirect(sql);
    auto const declared = DeclareMultiSchemaUpgradeTables();

    auto const result = mgr.UnicodeUpgradeTables(/*dryRun=*/false);
    CHECK(result.columns.size() == 3);
    CHECK(std::ranges::contains(result.rebuiltObjects,
                                std::string { "PRIMARY KEY PK_uu_ms_customers on sales.uu_ms_customers" }));
    CHECK(LiveDataTypeIn(stmt, "sales", "uu_ms_customers", "CODE") == "nchar");
    CHECK(LiveDataTypeIn(stmt, "dbo", "uu_ms_orders", "CUSTOMER_CODE") == "nchar");
    CHECK(CatalogScalar(stmt, "SELECT type FROM sys.key_constraints WHERE name = 'PK_uu_ms_customers'") == "PK");
    CHECK(CatalogScalar(stmt, "SELECT name FROM sys.foreign_keys WHERE name = 'FK_uu_ms_orders_customer'")
          == "FK_uu_ms_orders_customer");
    CHECK_THROWS(stmt.ExecuteDirect("INSERT INTO dbo.uu_ms_orders (ID, CUSTOMER_CODE) VALUES (1, 'none')"));
}

TEST_CASE_METHOD(SqlMigrationTestFixture,
                 "UnicodeUpgradeTables: SQL Server refuses a declared name that several off-path schemas hold",
                 "[SqlMigration][UnicodeUpgrade][MultiSchema]")
{
    auto& mgr = SqlMigration::MigrationManager::GetInstance();
    auto stmt = SqlStatement { mgr.GetDataMapper().Connection() };
    if (stmt.Connection().ServerType() != SqlServerType::MICROSOFT_SQL)
        SKIP("the resolution order default schema, then dbo, is SQL Server's");
    mgr.CreateMigrationHistory();

    DropSqlServerMultiSchemaFixture(stmt);
    auto const cleanup = detail::Finally([&] { DropSqlServerMultiSchemaFixture(stmt); });
    // The declared name in two schemas, neither the default schema `sales` nor dbo.
    CreateSalesUser(stmt);
    for (auto const* sql: {
             "CREATE SCHEMA uu_ms_other",
             "CREATE SCHEMA uu_ms_third",
             "CREATE TABLE uu_ms_other.uu_ms_customers (CODE CHAR(10) NOT NULL PRIMARY KEY)",
             "CREATE TABLE uu_ms_third.uu_ms_customers (CODE NCHAR(10) NOT NULL PRIMARY KEY)",
         })
        (void) stmt.ExecuteDirect(sql);
    auto const declared = DeclareMultiSchemaUpgradeTables();
    (void) stmt.ExecuteDirect("EXECUTE AS USER = N'uu_ms_sales_user'");

    for (auto const dryRun: { true, false })
    {
        auto const error = UpgradeError(mgr, dryRun);
        CHECK(error.contains("the schemas uu_ms_other, uu_ms_third each hold one"));
        CHECK(error.contains("resolve through sales, dbo only"));
    }
    (void) stmt.ExecuteDirect("REVERT");
    CHECK(LiveDataTypeIn(stmt, "uu_ms_other", "uu_ms_customers", "CODE") == "char");
    CHECK(LiveDataTypeIn(stmt, "uu_ms_third", "uu_ms_customers", "CODE") == "nchar");

    // Once neither holder has a column to widen, the ambiguity no longer matters.
    (void) stmt.ExecuteDirect("DROP TABLE uu_ms_other.uu_ms_customers");
    (void) stmt.ExecuteDirect("CREATE TABLE uu_ms_other.uu_ms_customers (CODE NCHAR(10) NOT NULL PRIMARY KEY)");
    (void) stmt.ExecuteDirect("EXECUTE AS USER = N'uu_ms_sales_user'");
    CHECK(UpgradeError(mgr, /*dryRun=*/false).empty());
}

TEST_CASE_METHOD(SqlMigrationTestFixture,
                 "UnicodeUpgradeTables: PostgreSQL resolves a declared name through the whole search path",
                 "[SqlMigration][UnicodeUpgrade][MultiSchema]")
{
    using namespace Lightweight::SqlColumnTypeDefinitions;
    auto& mgr = SqlMigration::MigrationManager::GetInstance();
    auto stmt = SqlStatement { mgr.GetDataMapper().Connection() };
    if (stmt.Connection().ServerType() != SqlServerType::POSTGRESQL)
        SKIP("the search path is PostgreSQL's");
    mgr.CreateMigrationHistory();

    auto const dropFixture = [&] {
        (void) stmt.ExecuteDirect("RESET search_path");
        (void) stmt.ExecuteDirect(R"(DROP TABLE IF EXISTS "public"."uu_ms_customers")");
        for (auto const* schema: { "uu_ms_first", "uu_ms_other", "uu_ms_third" })
            (void) stmt.ExecuteDirect(std::format(R"(DROP SCHEMA IF EXISTS "{}" CASCADE)", schema));
    };
    dropFixture();
    auto const cleanup = detail::Finally(dropFixture);
    for (auto const* sql: {
             R"(CREATE SCHEMA "uu_ms_first")",
             R"(CREATE SCHEMA "uu_ms_other")",
             R"(CREATE SCHEMA "uu_ms_third")",
             R"(CREATE TABLE "public"."uu_ms_customers" ("CODE" CHAR(10) NOT NULL PRIMARY KEY))",
             R"(CREATE TABLE "uu_ms_other"."uu_ms_customers" ("CODE" CHAR(10) NOT NULL PRIMARY KEY))",
         })
        (void) stmt.ExecuteDirect(sql);
    auto const declared = DeclareMultiSchemaUpgradeTables();

    // The Unicode ODBC driver reports CHAR as wide text, so nothing qualifies and nothing is refused;
    // the ANSI driver reports it as narrow, so the resolution decides what is widened.
    auto const liveTables = SqlSchema::ReadAllTables(stmt, std::string {}, std::string {});
    auto const liveCustomers = std::ranges::find_if(liveTables, [](SqlSchema::Table const& table) {
        return table.schema == "public" && table.name == "uu_ms_customers";
    });
    REQUIRE(liveCustomers != liveTables.end());
    REQUIRE(liveCustomers->columns.size() == 1);
    auto const narrowText = std::holds_alternative<Char>(liveCustomers->columns.front().type);

    // public is the second search path entry; the first one holds no such table.
    (void) stmt.ExecuteDirect(R"(SET search_path TO "uu_ms_first", "public")");
    auto const preview = mgr.UnicodeUpgradeTables(/*dryRun=*/true);
    if (narrowText)
    {
        REQUIRE(preview.columns.size() == 1);
        CHECK(preview.columns.front().table.schema == "public");
        CHECK(preview.columns.front().table.table == "uu_ms_customers");
    }
    else
        CHECK(preview.columns.empty());

    // Two holders off the search path, and one has a column to widen: refused, nothing changes.
    (void) stmt.ExecuteDirect(R"(DROP TABLE "public"."uu_ms_customers")");
    (void) stmt.ExecuteDirect(R"(CREATE TABLE "uu_ms_third"."uu_ms_customers" ("CODE" CHAR(10) NOT NULL PRIMARY KEY))");
    for (auto const dryRun: { true, false })
    {
        auto const error = UpgradeError(mgr, dryRun);
        if (narrowText)
        {
            CHECK(error.contains("the schemas uu_ms_other, uu_ms_third each hold one"));
            CHECK(error.contains("resolve through uu_ms_first, public only"));
        }
        else
            CHECK(error.empty());
    }
    CHECK(LiveDataTypeIn(stmt, "uu_ms_other", "uu_ms_customers", "CODE") == "character");
}

TEST_CASE("Checksum of a plain Insert is unchanged by IfNotExists support", "[SqlMigration]")
{
    // SHA-256 of `INSERT INTO "users" ("name", "age") VALUES ('John', 30)` plus its newline,
    // the text a plain insert has always rendered to on SQLite.
    auto const migration = SqlMigration::Migration<202610170001>(
        "plain insert", [](SqlMigrationQueryBuilder& plan) { plan.Insert("users").Set("name", "John").Set("age", 30); });
    CHECK(migration.ComputeChecksum(SqlQueryFormatter::Sqlite())
          == "15702bf904b0d121320cfa4af0ef18a9d66de7b2ffdc0af0e0f81c58488d5b9e");

    auto const conditional = SqlMigration::Migration<202610170002>("conditional insert", [](SqlMigrationQueryBuilder& plan) {
        plan.Insert("users").Set("name", "John").Set("age", 30).IfNotExists({ "name" });
    });
    CHECK(conditional.ComputeChecksum(SqlQueryFormatter::Sqlite())
          != migration.ComputeChecksum(SqlQueryFormatter::Sqlite()));
}

TEST_CASE("ToBoundSql binds the values of an IfNotExists insert", "[SqlMigration]")
{
    auto builder = SqlQueryBuilder(SqlQueryFormatter::Sqlite()).Migration();
    builder.WithSchema("sales");
    builder.Insert("Categories").Set("Id", 7).Set("Name", "Grüße").Set("Note", SqlNullValue).IfNotExists({ "Id" });
    builder.Insert("Categories").Set("Id", 8);
    auto const plan = std::move(builder).GetPlan();

    auto const bound = ToBoundSql(plan.steps.at(0)).value_or(SqlBoundStatement {});
    CHECK(bound.sql
          == R"(INSERT INTO "sales"."Categories" ("Id", "Name", "Note") SELECT ?, ?, ? )"
             R"(WHERE NOT EXISTS (SELECT 1 FROM "sales"."Categories" WHERE "Id" = ?))");
    REQUIRE(bound.parameters.size() == 4);
    CHECK(bound.parameters[0] == SqlVariant { 7 });
    // UTF-8 text is bound as UTF-16, so the driver never reads it in the client code page.
    REQUIRE(std::holds_alternative<std::u16string>(bound.parameters[1].value));
    CHECK(std::get<std::u16string>(bound.parameters[1].value) == u"Grüße");
    CHECK(bound.parameters[2].IsNull());
    CHECK(bound.parameters[3] == SqlVariant { 7 });

    // A plain insert has no bound form; it keeps running as its literal rendering.
    CHECK_FALSE(ToBoundSql(plan.steps.at(1)).has_value());
}

TEST_CASE_METHOD(SqlMigrationTestFixture, "Insert IfNotExists skips an existing row", "[SqlMigration]")
{
    using namespace SqlColumnTypeDefinitions;

    auto const createTable = SqlMigration::Migration<202610170003>("create categories", [](SqlMigrationQueryBuilder& plan) {
        plan.CreateTable("Categories").PrimaryKey("Id", Integer()).RequiredColumn("Name", NVarchar(50));
    });
    // Down inserts the same key with a different name, so a revert exercises the skip as well.
    auto const seedRow = SqlMigration::Migration<202610170004>(
        "seed category",
        [](SqlMigrationQueryBuilder& plan) {
            plan.Insert("Categories").Set("Id", 200).Set("Name", "Grüße").IfNotExists({ "Id" });
        },
        [](SqlMigrationQueryBuilder& plan) {
            plan.Insert("Categories").Set("Id", 200).Set("Name", "different").IfNotExists({ "Id" });
        });

    auto& manager = SqlMigration::MigrationManager::GetInstance();
    auto logged = std::vector<std::string> {};
    manager.SetLogSink([&logged](std::string_view message) { logged.emplace_back(message); });
    auto const restoreSink = ::Lightweight::detail::Finally([&manager] { manager.SetLogSink({}); });

    auto const rowCount = [] {
        auto stmt = SqlStatement {};
        return stmt.ExecuteDirectScalar<long long>(R"(SELECT COUNT(*) FROM "Categories")").value_or(-1);
    };
    auto const storedName = [] {
        auto stmt = SqlStatement {};
        auto cursor = stmt.ExecuteDirect(R"(SELECT "Name" FROM "Categories" WHERE "Id" = 200)");
        REQUIRE(cursor.FetchRow());
        auto value = SqlVariant {};
        REQUIRE(cursor.GetColumn(1, &value));
        return value.ToString();
    };

    manager.CreateMigrationHistory();
    auto const skippedAtStart = manager.SkippedExistingRowCount();

    // First run inserts the row, with its non-ASCII value intact.
    REQUIRE(manager.ApplyPendingMigrations() == 2);
    CHECK(manager.SkippedExistingRowCount() == skippedAtStart);
    CHECK(logged.empty());
    CHECK(rowCount() == 1);
    CHECK(storedName() == "Grüße");

    // Reverting runs Down's insert of the same key: skipped, and the existing values stay.
    manager.RevertSingleMigration(seedRow);
    CHECK(manager.SkippedExistingRowCount() == skippedAtStart + 1);
    REQUIRE(logged.size() == 1);
    CHECK(logged[0] == "Skipped existing row: 202610170004 Categories (Id=200)");
    CHECK(rowCount() == 1);
    CHECK(storedName() == "Grüße");

    // Applying again finds the row in place: no error, no second row, reported again.
    CHECK_NOTHROW(manager.ApplySingleMigration(seedRow));
    CHECK(manager.SkippedExistingRowCount() == skippedAtStart + 2);
    REQUIRE(logged.size() == 2);
    CHECK(logged[1] == "Skipped existing row: 202610170004 Categories (Id=200)");
    CHECK(rowCount() == 1);
    CHECK(storedName() == "Grüße");
    CHECK(manager.GetPending().empty());
}

TEST_CASE_METHOD(SqlMigrationTestFixture, "Insert IfNotExists stores bound values like literal ones", "[SqlMigration]")
{
    using namespace SqlColumnTypeDefinitions;

    // Row 1 goes through the plain insert's literal path, row 2 through the bound one.
    auto const migration = SqlMigration::Migration<202610170006>("bound values", [](SqlMigrationQueryBuilder& plan) {
        plan.CreateTable("Categories")
            .PrimaryKey("Id", Integer())
            .Column("Name", NVarchar(50))
            .Column("Label", Varchar(50))
            .Column("Note", Varchar(50));
        plan.Insert("Categories").Set("Id", 1).Set("Name", "Grüße").Set("Label", "Café crème").Set("Note", SqlNullValue);
        plan.Insert("Categories")
            .Set("Id", 2)
            .Set("Name", "Grüße")
            .Set("Label", "Café crème")
            .Set("Note", SqlNullValue)
            .IfNotExists({ "Id" });
    });

    auto& manager = SqlMigration::MigrationManager::GetInstance();
    manager.CreateMigrationHistory();
    REQUIRE(manager.ApplyPendingMigrations() == 1);

    auto const readRow = [](int id) {
        auto stmt = SqlStatement {};
        auto cursor =
            stmt.ExecuteDirect(std::format(R"(SELECT "Name", "Label", "Note" FROM "Categories" WHERE "Id" = {})", id));
        REQUIRE(cursor.FetchRow());
        // Fetched as UTF-16, so the driver decodes a narrow column from its code page.
        auto values = std::vector<std::optional<std::u16string>> {};
        for (auto const column: std::views::iota(SQLUSMALLINT { 1 }, SQLUSMALLINT { 4 }))
            values.push_back(cursor.GetNullableColumn<std::u16string>(column));
        return values;
    };

    auto const literal = readRow(1);
    auto const bound = readRow(2);
    CHECK(literal[0] == u"Grüße");
    CHECK(literal[1] == u"Café crème");
    CHECK_FALSE(literal[2].has_value());
    CHECK(bound == literal);

    // The server itself finds both rows' text equal, the narrow column included.
    auto stmt = SqlStatement {};
    CHECK(stmt.ExecuteDirectScalar<long long>(
                  R"(SELECT COUNT(*) FROM "Categories" a, "Categories" b WHERE a."Id" = 1 AND b."Id" = 2 )"
                  R"(AND a."Label" = b."Label" AND a."Name" = b."Name")")
              .value_or(-1)
          == 1);
}

TEST_CASE_METHOD(SqlMigrationTestFixture, "Insert IfNotExists reports a composite string key quoted", "[SqlMigration]")
{
    using namespace SqlColumnTypeDefinitions;

    auto const migration = SqlMigration::Migration<202610170005>("composite seed", [](SqlMigrationQueryBuilder& plan) {
        plan.CreateTable("Codes").RequiredColumn("Kind", Varchar(20)).RequiredColumn("Id", Integer());
        plan.Insert("Codes").Set("Kind", "O'Brien").Set("Id", 7);
        plan.Insert("Codes").Set("Kind", "O'Brien").Set("Id", 7).IfNotExists({ "Kind", "Id" });
        plan.Insert("Codes").Set("Kind", "O'Brien").Set("Id", 8).IfNotExists({ "Kind", "Id" });
    });

    auto& manager = SqlMigration::MigrationManager::GetInstance();
    auto logged = std::vector<std::string> {};
    manager.SetLogSink([&logged](std::string_view message) { logged.emplace_back(message); });
    auto const restoreSink = ::Lightweight::detail::Finally([&manager] { manager.SetLogSink({}); });

    manager.CreateMigrationHistory();
    REQUIRE(manager.ApplyPendingMigrations() == 1);

    REQUIRE(logged.size() == 1);
    CHECK(logged[0] == "Skipped existing row: 202610170005 Codes (Kind='O''Brien', Id=7)");
    auto stmt = SqlStatement {};
    CHECK(stmt.ExecuteDirectScalar<long long>(R"(SELECT COUNT(*) FROM "Codes")").value_or(-1) == 2);
}

namespace
{
/// Captures the migration manager's log lines for the lifetime of the object.
class CapturedMigrationLog
{
  public:
    CapturedMigrationLog()
    {
        SqlMigration::MigrationManager::GetInstance().SetLogSink(
            [this](std::string_view message) { _lines.emplace_back(message); });
    }
    ~CapturedMigrationLog()
    {
        SqlMigration::MigrationManager::GetInstance().SetLogSink({});
    }
    CapturedMigrationLog(CapturedMigrationLog const&) = delete;
    CapturedMigrationLog(CapturedMigrationLog&&) = delete;
    CapturedMigrationLog& operator=(CapturedMigrationLog const&) = delete;
    CapturedMigrationLog& operator=(CapturedMigrationLog&&) = delete;

    [[nodiscard]] std::vector<std::string> const& Lines() const noexcept
    {
        return _lines;
    }

  private:
    std::vector<std::string> _lines;
};
} // namespace

TEST_CASE_METHOD(SqlMigrationTestFixture,
                 "Insert IfNotExists counts inserted rows under the migration lock",
                 "[SqlMigration]")
{
    using namespace SqlColumnTypeDefinitions;

    auto const createTable = SqlMigration::Migration<202610170007>("create categories", [](SqlMigrationQueryBuilder& plan) {
        plan.CreateTable("Categories").PrimaryKey("Id", Integer()).RequiredColumn("Name", NVarchar(50));
        plan.Insert("Categories").Set("Id", 300).Set("Name", "existing");
    });
    // The first IfNotExists statement inserts a new row; the second finds its row in place.
    auto const seedRows = SqlMigration::Migration<202610170008>("seed categories", [](SqlMigrationQueryBuilder& plan) {
        plan.Insert("Categories").Set("Id", 100).Set("Name", "Books").IfNotExists({ "Id" });
        plan.Insert("Categories").Set("Id", 300).Set("Name", "other").IfNotExists({ "Id" });
    });

    auto& manager = SqlMigration::MigrationManager::GetInstance();
    auto const log = CapturedMigrationLog {};
    auto& connection = manager.GetDataMapper().Connection();
    manager.CreateMigrationHistory();
    auto const skippedAtStart = manager.SkippedExistingRowCount();

    {
        // dbtool holds this lock while it migrates; it must not leave the session without row counts.
        auto const lock = SqlScopedLock { connection, "lightweight_if_not_exists_lock" };
        REQUIRE(manager.ApplyPendingMigrations() == 2);
    }

    REQUIRE(log.Lines().size() == 1);
    CHECK(log.Lines()[0] == "Skipped existing row: 202610170008 Categories (Id=300)");
    CHECK(manager.SkippedExistingRowCount() == skippedAtStart + 1);
    auto stmt = SqlStatement { connection };
    CHECK(stmt.ExecuteDirectScalar<long long>(R"(SELECT COUNT(*) FROM "Categories")").value_or(-1) == 2);
}

TEST_CASE_METHOD(SqlMigrationTestFixture, "SQL Server: the migration lock restores the session's NOCOUNT", "[SqlScopedLock]")
{
    auto& connection = SqlMigration::MigrationManager::GetInstance().GetDataMapper().Connection();
    if (connection.ServerType() != SqlServerType::MICROSOFT_SQL)
        return; // NOCOUNT is a SQL Server session option.

    auto const noCountIsOn = [&] {
        auto stmt = SqlStatement { connection };
        return stmt.ExecuteDirectScalar<int>("SELECT @@OPTIONS & 512").value_or(-1) != 0;
    };

    REQUIRE_FALSE(noCountIsOn());
    {
        auto const lock = SqlScopedLock { connection, "lightweight_nocount_lock" };
        CHECK_FALSE(noCountIsOn());
    }
    CHECK_FALSE(noCountIsOn());

    // A session that had NOCOUNT on keeps it.
    (void) SqlStatement { connection }.ExecuteDirect("SET NOCOUNT ON");
    {
        auto const lock = SqlScopedLock { connection, "lightweight_nocount_lock" };
        CHECK(noCountIsOn());
    }
    CHECK(noCountIsOn());
    (void) SqlStatement { connection }.ExecuteDirect("SET NOCOUNT OFF");
}

TEST_CASE_METHOD(SqlMigrationTestFixture,
                 "SQL Server: Insert IfNotExists counts inserted rows under NOCOUNT ON",
                 "[SqlMigration]")
{
    using namespace SqlColumnTypeDefinitions;

    auto& manager = SqlMigration::MigrationManager::GetInstance();
    auto& connection = manager.GetDataMapper().Connection();
    if (connection.ServerType() != SqlServerType::MICROSOFT_SQL)
        return; // NOCOUNT is a SQL Server session option.

    auto const createTable = SqlMigration::Migration<202610170009>("create categories", [](SqlMigrationQueryBuilder& plan) {
        plan.CreateTable("Categories").PrimaryKey("Id", Integer()).RequiredColumn("Name", NVarchar(50));
    });
    auto const seedRow = SqlMigration::Migration<202610170016>("seed under nocount", [](SqlMigrationQueryBuilder& plan) {
        plan.Insert("Categories").Set("Id", 100).Set("Name", "Books").IfNotExists({ "Id" });
    });

    auto const log = CapturedMigrationLog {};
    manager.CreateMigrationHistory();
    auto const skippedAtStart = manager.SkippedExistingRowCount();

    // Whatever left the session without row counts, the skip must not be inferred from it.
    manager.ApplySingleMigration(createTable);
    (void) SqlStatement { connection }.ExecuteDirect("SET NOCOUNT ON");
    manager.ApplySingleMigration(seedRow);

    CHECK(log.Lines().empty());
    CHECK(manager.SkippedExistingRowCount() == skippedAtStart);
    auto stmt = SqlStatement { connection };
    CHECK(stmt.ExecuteDirectScalar<long long>(R"(SELECT COUNT(*) FROM "Categories")").value_or(-1) == 1);
}

TEST_CASE_METHOD(SqlMigrationTestFixture,
                 "Insert IfNotExists reports no skips of a migration that rolls back",
                 "[SqlMigration]")
{
    using namespace SqlColumnTypeDefinitions;

    auto const createTable = SqlMigration::Migration<202610170010>("create categories", [](SqlMigrationQueryBuilder& plan) {
        plan.CreateTable("Categories").PrimaryKey("Id", Integer()).RequiredColumn("Name", NVarchar(50));
        plan.Insert("Categories").Set("Id", 300).Set("Name", "existing");
    });
    auto const failing = SqlMigration::Migration<202610170011>("skip then fail", [](SqlMigrationQueryBuilder& plan) {
        plan.Insert("Categories").Set("Id", 300).Set("Name", "other").IfNotExists({ "Id" });
        plan.RawSql("THIS_IS_INVALID_SQL_TO_FORCE_FAILURE");
    });

    auto& manager = SqlMigration::MigrationManager::GetInstance();
    auto const log = CapturedMigrationLog {};
    manager.CreateMigrationHistory();
    auto const skippedAtStart = manager.SkippedExistingRowCount();

    {
        auto const _ = ScopedSqlNullLogger {}; // the failure is expected
        CHECK_THROWS(manager.ApplyPendingMigrations());
    }

    CHECK(log.Lines().empty());
    CHECK(manager.SkippedExistingRowCount() == skippedAtStart);
    CHECK(manager.GetAppliedMigrationIds().size() == 1);
}

TEST_CASE_METHOD(SqlMigrationTestFixture, "Insert IfNotExists names the schema in a skip line", "[SqlMigration]")
{
    using namespace SqlColumnTypeDefinitions;

    auto& manager = SqlMigration::MigrationManager::GetInstance();
    // A schema every backend already has.
    static auto const schema = [&]() -> std::string {
        switch (manager.GetDataMapper().Connection().ServerType())
        {
            case SqlServerType::MICROSOFT_SQL:
                return "dbo";
            case SqlServerType::POSTGRESQL:
                return "public";
            default:
                return "main";
        }
    }();

    auto const migration = SqlMigration::Migration<202610170012>("schema seed", [](SqlMigrationQueryBuilder& plan) {
        plan.WithSchema(schema);
        plan.CreateTable("Codes").PrimaryKey("Id", Integer());
        plan.Insert("Codes").Set("Id", 7);
        plan.Insert("Codes").Set("Id", 7).IfNotExists({ "Id" });
    });

    auto const log = CapturedMigrationLog {};
    manager.CreateMigrationHistory();
    REQUIRE(manager.ApplyPendingMigrations() == 1);

    REQUIRE(log.Lines().size() == 1);
    CHECK(log.Lines()[0] == std::format("Skipped existing row: 202610170012 {}.Codes (Id=7)", schema));
}

TEST_CASE_METHOD(SqlMigrationTestFixture, "Insert IfNotExists stores bound doubles like literal ones", "[SqlMigration]")
{
    using namespace SqlColumnTypeDefinitions;

    // Rows 1-3 go through the plain insert's literal path, rows 11-13 through the bound one.
    auto const migration = SqlMigration::Migration<202610170013>("bound doubles", [](SqlMigrationQueryBuilder& plan) {
        plan.CreateTable("Measures")
            .PrimaryKey("Id", Integer())
            .Column("Wide", Real { .precision = 53 })
            .Column("Narrow", Real {})
            .Column("Fixed", Decimal { .precision = 10, .scale = 4 });
        for (auto const& [id, value]: std::array { std::pair { 1, 40.3399 }, std::pair { 2, 0.5 }, std::pair { 3, -12.25 } })
        {
            plan.Insert("Measures").Set("Id", id).Set("Wide", value).Set("Narrow", value).Set("Fixed", value);
            plan.Insert("Measures")
                .Set("Id", id + 10)
                .Set("Wide", value)
                .Set("Narrow", value)
                .Set("Fixed", value)
                .IfNotExists({ "Id" });
        }
    });

    auto& manager = SqlMigration::MigrationManager::GetInstance();
    manager.CreateMigrationHistory();
    REQUIRE(manager.ApplyPendingMigrations() == 1);

    // Compared by the server, value for value.
    auto stmt = SqlStatement {};
    CHECK(stmt.ExecuteDirectScalar<long long>(
                  R"(SELECT COUNT(*) FROM "Measures" a, "Measures" b WHERE b."Id" = a."Id" + 10 )"
                  R"(AND a."Wide" = b."Wide" AND a."Narrow" = b."Narrow" AND a."Fixed" = b."Fixed")")
              .value_or(-1)
          == 3);
    CHECK(stmt.ExecuteDirectScalar<double>(R"(SELECT "Wide" FROM "Measures" WHERE "Id" = 11)").value_or(0.0) == 40.3399);
}

TEST_CASE_METHOD(SqlMigrationTestFixture, "Insert IfNotExists binds NULL into any column type", "[SqlMigration]")
{
    using namespace SqlColumnTypeDefinitions;

    auto const migration = SqlMigration::Migration<202610170014>("bound nulls", [](SqlMigrationQueryBuilder& plan) {
        plan.CreateTable("Nullables")
            .PrimaryKey("Id", Integer())
            .Column("Number", Integer())
            .Column("BigNumber", Bigint())
            .Column("Narrow", Real {})
            .Column("Wide", Real { .precision = 53 })
            .Column("Fixed", Decimal { .precision = 10, .scale = 4 })
            .Column("Flag", Bool())
            .Column("Bytes", VarBinary { .size = 16 });
        // NULL in the first parameter slot, then NULLs after the key.
        plan.Insert("Nullables")
            .Set("Number", SqlNullValue)
            .Set("BigNumber", SqlNullValue)
            .Set("Narrow", SqlNullValue)
            .Set("Wide", SqlNullValue)
            .Set("Fixed", SqlNullValue)
            .Set("Flag", SqlNullValue)
            .Set("Bytes", SqlNullValue)
            .Set("Id", 1)
            .IfNotExists({ "Id" });
        plan.Insert("Nullables")
            .Set("Id", 2)
            .Set("Bytes", SqlNullValue)
            .Set("Flag", SqlNullValue)
            .Set("Fixed", SqlNullValue)
            .Set("Wide", SqlNullValue)
            .Set("Narrow", SqlNullValue)
            .Set("BigNumber", SqlNullValue)
            .Set("Number", SqlNullValue)
            .IfNotExists({ "Id" });
    });

    auto& manager = SqlMigration::MigrationManager::GetInstance();
    manager.CreateMigrationHistory();
    REQUIRE(manager.ApplyPendingMigrations() == 1);

    auto stmt = SqlStatement {};
    CHECK(stmt.ExecuteDirectScalar<long long>(
                  R"(SELECT COUNT(*) FROM "Nullables" WHERE "Number" IS NULL AND "BigNumber" IS NULL )"
                  R"(AND "Narrow" IS NULL AND "Wide" IS NULL AND "Fixed" IS NULL AND "Flag" IS NULL AND "Bytes" IS NULL)")
              .value_or(-1)
          == 2);
}

TEST_CASE_METHOD(SqlMigrationTestFixture, "Insert IfNotExists names the migration in a validation error", "[SqlMigration]")
{
    using namespace SqlColumnTypeDefinitions;

    auto const migration = SqlMigration::Migration<202610170015>("bad key", [](SqlMigrationQueryBuilder& plan) {
        plan.CreateTable("Codes").PrimaryKey("Id", Integer());
        plan.Insert("Codes").Set("Id", 7).IfNotExists({ "Code" });
    });

    auto& manager = SqlMigration::MigrationManager::GetInstance();
    manager.CreateMigrationHistory();
    auto const message = [&] {
        try
        {
            (void) manager.ApplyPendingMigrations();
        }
        catch (std::invalid_argument const& error)
        {
            return std::string { error.what() };
        }
        return std::string {};
    }();
    CHECK(message.starts_with("Migration 202610170015 'bad key': "));
    CHECK(message.contains(R"(key column "Code")"));
}

TEST_CASE_METHOD(SqlMigrationTestFixture,
                 "UnicodeUpgradeTables: SQL Server names the schema of a joined column when the plan spans several",
                 "[SqlMigration][UnicodeUpgrade][MultiSchema]")
{
    using namespace Lightweight::SqlColumnTypeDefinitions;
    auto& mgr = SqlMigration::MigrationManager::GetInstance();
    auto stmt = SqlStatement { mgr.GetDataMapper().Connection() };
    if (stmt.Connection().ServerType() != SqlServerType::MICROSOFT_SQL)
        SKIP("only SQL Server widens columns that a foreign key joins");
    mgr.CreateMigrationHistory();

    DropSqlServerMultiSchemaFixture(stmt);
    auto const cleanup = detail::Finally([&] { DropSqlServerMultiSchemaFixture(stmt); });
    for (auto const* sql: {
             "CREATE SCHEMA sales",
             "CREATE TABLE sales.uu_ms_customers (CODE CHAR(10) NOT NULL CONSTRAINT PK_uu_ms_customers PRIMARY KEY)",
             "CREATE TABLE dbo.uu_ms_orders (ID INT NOT NULL PRIMARY KEY, CUSTOMER_CODE CHAR(10) NOT NULL "
             "CONSTRAINT FK_uu_ms_orders_customer FOREIGN KEY REFERENCES sales.uu_ms_customers (CODE))",
         })
        (void) stmt.ExecuteDirect(sql);
    // Only the key is declared; the referencing column is widened because the foreign key joins it.
    fold_test::FoldStub<20'26'10'17'00'04> decl { "declare wide", [](SqlMigrationQueryBuilder& plan) {
                                                     plan.CreateTable("uu_ms_customers").PrimaryKey("CODE", NChar(10));
                                                 } };

    auto const preview = mgr.UnicodeUpgradeTables(/*dryRun=*/true);
    REQUIRE(preview.columns.size() == 2);
    CHECK(preview.columns[1].table.schema == "dbo");
    CHECK(preview.columns[1].column == "CUSTOMER_CODE");
    CHECK(preview.columns[1].joinedByForeignKeyTo == "sales.uu_ms_customers.CODE");
}

// ================================================================================================
// unicode-upgrade-tables commits unit by unit
// ================================================================================================

namespace
{

/// The tables of @p unit as `schema.table` (or `table`), in order.
std::vector<std::string> UnitTables(std::vector<SqlSchema::FullyQualifiedTableName> const& unit)
{
    auto tables = std::vector<std::string> {};
    for (auto const& table: unit)
        tables.push_back(table.schema.empty() ? table.table : std::format("{}.{}", table.schema, table.table));
    return tables;
}

} // namespace

TEST_CASE_METHOD(SqlMigrationTestFixture,
                 "UnicodeUpgradeTables: SQL Server commits each unit on its own and a rerun continues",
                 "[SqlMigration][UnicodeUpgrade][Units]")
{
    using namespace Lightweight::SqlColumnTypeDefinitions;
    auto& mgr = SqlMigration::MigrationManager::GetInstance();
    auto stmt = SqlStatement { mgr.GetDataMapper().Connection() };
    if (stmt.Connection().ServerType() != SqlServerType::MICROSOFT_SQL)
        SKIP("a database DDL trigger injects the failure, which is SQL Server's");
    mgr.CreateMigrationHistory();

    auto const dropFixture = [&] {
        for (auto const* sql: {
                 "IF EXISTS (SELECT 1 FROM sys.triggers WHERE name = N'uu_unit_block' AND parent_class = 0) "
                 "DROP TRIGGER uu_unit_block ON DATABASE",
                 "IF OBJECT_ID(N'dbo.uu_unit_c') IS NOT NULL DROP TABLE dbo.uu_unit_c",
                 "IF OBJECT_ID(N'dbo.uu_unit_a') IS NOT NULL DROP TABLE dbo.uu_unit_a",
                 "IF OBJECT_ID(N'dbo.uu_unit_b') IS NOT NULL DROP TABLE dbo.uu_unit_b",
             })
            (void) stmt.ExecuteDirect(sql);
    };
    dropFixture();
    auto const cleanup = detail::Finally(dropFixture);
    // uu_unit_c joins uu_unit_a by a foreign key, so they form one unit; uu_unit_b is a unit of its own.
    for (auto const* sql: {
             "CREATE TABLE dbo.uu_unit_a (CODE CHAR(10) NOT NULL CONSTRAINT PK_uu_unit_a PRIMARY KEY, "
             "NOTE VARCHAR(20) NULL CONSTRAINT UQ_uu_unit_a_note UNIQUE)",
             "CREATE TABLE dbo.uu_unit_c (ID INT NOT NULL CONSTRAINT PK_uu_unit_c PRIMARY KEY, "
             "A_CODE CHAR(10) NOT NULL CONSTRAINT FK_uu_unit_c_a FOREIGN KEY REFERENCES dbo.uu_unit_a (CODE))",
             "CREATE TABLE dbo.uu_unit_b (CODE CHAR(10) NOT NULL CONSTRAINT PK_uu_unit_b PRIMARY KEY)",
             "INSERT INTO dbo.uu_unit_a (CODE, NOTE) VALUES ('a1', 'first')",
             "INSERT INTO dbo.uu_unit_c (ID, A_CODE) VALUES (1, 'a1')",
             "INSERT INTO dbo.uu_unit_b (CODE) VALUES ('b1')",
         })
        (void) stmt.ExecuteDirect(sql);
    fold_test::FoldStub<20'26'10'17'00'02> decl {
        "declare wide",
        [](SqlMigrationQueryBuilder& plan) {
            plan.CreateTable("uu_unit_a").PrimaryKey("CODE", NChar(10)).Column("NOTE", NVarchar(20));
            plan.CreateTable("uu_unit_b").PrimaryKey("CODE", NChar(10));
        }
    };

    auto const preview = mgr.UnicodeUpgradeTables(/*dryRun=*/true);
    REQUIRE(preview.units.size() == 2);
    CHECK(UnitTables(preview.units[0]) == std::vector<std::string> { "dbo.uu_unit_a", "dbo.uu_unit_c" });
    CHECK(UnitTables(preview.units[1]) == std::vector<std::string> { "dbo.uu_unit_b" });
    // One schema: the joined column names the column it follows without a schema.
    auto const joined = std::ranges::find(
        preview.columns, std::string { "A_CODE" }, &SqlMigration::MigrationManager::ColumnUpgradeEntry::column);
    REQUIRE(joined != preview.columns.end());
    CHECK(joined->joinedByForeignKeyTo == "uu_unit_a.CODE");

    // Every ALTER TABLE on uu_unit_b fails, so the second unit does (XML methods need ANSI_WARNINGS).
    (void) stmt.ExecuteDirect("CREATE TRIGGER uu_unit_block ON DATABASE FOR ALTER_TABLE AS SET ANSI_WARNINGS ON; "
                              "IF EVENTDATA().value('(/EVENT_INSTANCE/ObjectName)[1]', 'nvarchar(128)') = N'uu_unit_b' "
                              "BEGIN RAISERROR (N'blocked by the test', 16, 1); ROLLBACK; END");
    auto committedUnits = std::vector<std::string> {};
    mgr.SetLogSink([&](std::string_view message) {
        if (message.starts_with("committed unit "))
            committedUnits.emplace_back(message);
    });
    auto const resetSink = detail::Finally([&] { mgr.SetLogSink({}); });
    auto error = std::string {};
    try
    {
        std::ignore = mgr.UnicodeUpgradeTables(/*dryRun=*/false);
    }
    catch (SqlMigration::UnicodeUpgradeUnitError const& failure)
    {
        // A SqlException that keeps the cause's diagnostics.
        error = failure.info().message;
        CHECK(failure.UnitNumber() == 2);
        CHECK(failure.UnitCount() == 2);
        CHECK(failure.info().nativeErrorCode != 0);
        CHECK(failure.info().sqlState != "     ");
    }
    INFO(error);
    CHECK(error.contains("unit 2 of 2 (dbo.uu_unit_b) failed and was rolled back"));
    CHECK(error.contains("the 1 unit(s) before it are committed"));
    CHECK(error.contains("blocked by the test"));
    CHECK(committedUnits == std::vector<std::string> { "committed unit 1 of 2: dbo.uu_unit_a, dbo.uu_unit_c" });

    // The first unit stays committed with all its objects; the second is untouched.
    CHECK(LiveDataTypeIn(stmt, "dbo", "uu_unit_a", "CODE") == "nchar");
    CHECK(LiveDataTypeIn(stmt, "dbo", "uu_unit_a", "NOTE") == "nvarchar");
    CHECK(LiveDataTypeIn(stmt, "dbo", "uu_unit_c", "A_CODE") == "nchar");
    CHECK(CatalogScalar(stmt, "SELECT type FROM sys.key_constraints WHERE name = 'PK_uu_unit_a'") == "PK");
    CHECK(CatalogScalar(stmt, "SELECT type FROM sys.key_constraints WHERE name = 'UQ_uu_unit_a_note'") == "UQ");
    CHECK(CatalogScalar(stmt, "SELECT name FROM sys.foreign_keys WHERE name = 'FK_uu_unit_c_a'") == "FK_uu_unit_c_a");
    CHECK(LiveDataTypeIn(stmt, "dbo", "uu_unit_b", "CODE") == "char");
    CHECK(CatalogScalar(stmt, "SELECT type FROM sys.key_constraints WHERE name = 'PK_uu_unit_b'") == "PK");
    CHECK(stmt.ExecuteDirectScalar<long long>("SELECT COUNT(*) FROM dbo.uu_unit_b").value_or(0) == 1);

    // A rerun plans only what is left and completes.
    (void) stmt.ExecuteDirect("DROP TRIGGER uu_unit_block ON DATABASE");
    auto const rerun = mgr.UnicodeUpgradeTables(/*dryRun=*/false);
    REQUIRE(rerun.columns.size() == 1);
    CHECK(rerun.columns.front().table.table == "uu_unit_b");
    CHECK(rerun.units.size() == 1);
    CHECK(LiveDataTypeIn(stmt, "dbo", "uu_unit_b", "CODE") == "nchar");
    CHECK(CatalogScalar(stmt, "SELECT type FROM sys.key_constraints WHERE name = 'PK_uu_unit_b'") == "PK");
    CHECK(mgr.UnicodeUpgradeTables(/*dryRun=*/true).columns.empty());
}

TEST_CASE_METHOD(SqlMigrationTestFixture,
                 "UnicodeUpgradeTables: tables joined by a foreign key share a unit on every backend",
                 "[SqlMigration][UnicodeUpgrade][Units]")
{
    using namespace Lightweight::SqlColumnTypeDefinitions;
    auto& mgr = SqlMigration::MigrationManager::GetInstance();
    mgr.CreateMigrationHistory();

    auto stmt = SqlStatement { mgr.GetDataMapper().Connection() };
    stmt.MigrateDirect([](SqlMigrationQueryBuilder& m) {
        m.CreateTable("uu_unit_a").PrimaryKey("CODE", Varchar(10)).Column("NOTE", Varchar(20));
        m.CreateTable("uu_unit_b").PrimaryKey("CODE", Varchar(10));
        m.CreateTable("uu_unit_c")
            .PrimaryKey("ID", Integer())
            .ForeignKey(
                "A_CODE", Varchar(10), SqlForeignKeyReferenceDefinition { .tableName = "uu_unit_a", .columnName = "CODE" });
    });
    fold_test::FoldStub<20'26'10'17'00'03> decl {
        "declare wide",
        [](SqlMigrationQueryBuilder& plan) {
            plan.CreateTable("uu_unit_a").PrimaryKey("CODE", NVarchar(10)).Column("NOTE", NVarchar(20));
            plan.CreateTable("uu_unit_b").PrimaryKey("CODE", NVarchar(10));
            plan.CreateTable("uu_unit_c").PrimaryKey("ID", Integer()).Column("A_CODE", NVarchar(10));
        }
    };

    auto const preview = mgr.UnicodeUpgradeTables(/*dryRun=*/true);
    if (preview.columns.empty())
    {
        // PostgreSQL's Unicode driver (and some SQLite builds) report the narrow columns as wide.
        CHECK(preview.units.empty());
        return;
    }
    REQUIRE(preview.units.size() == 2);
    auto const tablesOf = [](std::vector<SqlSchema::FullyQualifiedTableName> const& unit) {
        auto names = std::vector<std::string> {};
        for (auto const& table: unit)
            names.push_back(table.table);
        return names;
    };
    CHECK(tablesOf(preview.units[0]) == std::vector<std::string> { "uu_unit_a", "uu_unit_c" });
    CHECK(tablesOf(preview.units[1]) == std::vector<std::string> { "uu_unit_b" });

    auto const result = mgr.UnicodeUpgradeTables(/*dryRun=*/false);
    CHECK(result.units.size() == 2);
    auto const liveTables = SqlSchema::ReadAllTables(stmt, std::string {}, std::string {});
    CHECK(std::ranges::count_if(liveTables, [](SqlSchema::Table const& table) { return table.name.starts_with("uu_unit_"); })
          == 3);
}

TEST_CASE_METHOD(SqlMigrationTestFixture,
                 "UnicodeUpgradeTables: SQL Server fails a unit on an error a later result of its batch reports",
                 "[SqlMigration][UnicodeUpgrade][Units]")
{
    using namespace Lightweight::SqlColumnTypeDefinitions;
    auto& mgr = SqlMigration::MigrationManager::GetInstance();
    auto stmt = SqlStatement { mgr.GetDataMapper().Connection() };
    if (stmt.Connection().ServerType() != SqlServerType::MICROSOFT_SQL)
        SKIP("a database DDL trigger injects the failure, which is SQL Server's");
    mgr.CreateMigrationHistory();

    auto const dropFixture = [&] {
        for (auto const* sql: {
                 "IF EXISTS (SELECT 1 FROM sys.triggers WHERE name = N'uu_unit_late' AND parent_class = 0) "
                 "DROP TRIGGER uu_unit_late ON DATABASE",
                 "IF OBJECT_ID(N'dbo.uu_unit_b') IS NOT NULL DROP TABLE dbo.uu_unit_b",
                 "IF OBJECT_ID(N'dbo.uu_unit_log') IS NOT NULL DROP TABLE dbo.uu_unit_log",
             })
            (void) stmt.ExecuteDirect(sql);
    };
    dropFixture();
    auto const cleanup = detail::Finally(dropFixture);
    for (auto const* sql: {
             "CREATE TABLE dbo.uu_unit_b (CODE CHAR(10) NOT NULL CONSTRAINT PK_uu_unit_b PRIMARY KEY)",
             "CREATE TABLE dbo.uu_unit_log (N INT NOT NULL)",
             "INSERT INTO dbo.uu_unit_log (N) VALUES (0)",
         })
        (void) stmt.ExecuteDirect(sql);
    fold_test::FoldStub<20'26'10'17'00'05> decl { "declare wide", [](SqlMigrationQueryBuilder& plan) {
                                                     plan.CreateTable("uu_unit_b").PrimaryKey("CODE", NChar(10));
                                                 } };

    // The row count of the UPDATE is the batch's first result, so the error only arrives with
    // a later one; the ALTER TABLE itself succeeds.
    (void) stmt.ExecuteDirect("CREATE TRIGGER uu_unit_late ON DATABASE FOR ALTER_TABLE AS SET NOCOUNT OFF; "
                              "UPDATE dbo.uu_unit_log SET N = N + 1; RAISERROR (N'late failure', 16, 1)");
    auto const error = UpgradeError(mgr, /*dryRun=*/false);
    INFO(error);
    CHECK(error.contains("unit 1 of 1 (dbo.uu_unit_b) failed and was rolled back; no unit was committed"));
    CHECK(error.contains("late failure"));
    CHECK(LiveDataTypeIn(stmt, "dbo", "uu_unit_b", "CODE") == "char");
    CHECK(CatalogScalar(stmt, "SELECT type FROM sys.key_constraints WHERE name = 'PK_uu_unit_b'") == "PK");
    CHECK(stmt.ExecuteDirectScalar<long long>("SELECT N FROM dbo.uu_unit_log").value_or(-1) == 0);
}

TEST_CASE_METHOD(SqlMigrationTestFixture,
                 "UnicodeUpgradeTables: SQL Server keeps tables in one unit whatever order the walk records keys in",
                 "[SqlMigration][UnicodeUpgrade][Units]")
{
    using namespace Lightweight::SqlColumnTypeDefinitions;
    auto& mgr = SqlMigration::MigrationManager::GetInstance();
    auto stmt = SqlStatement { mgr.GetDataMapper().Connection() };
    if (stmt.Connection().ServerType() != SqlServerType::MICROSOFT_SQL)
        SKIP("only SQL Server widens columns that a foreign key joins");
    mgr.CreateMigrationHistory();

    auto const dropFixture = [&] {
        for (auto const* sql: {
                 "IF OBJECT_ID(N'dbo.uu_join_y') IS NOT NULL DROP TABLE dbo.uu_join_y",
                 "IF OBJECT_ID(N'dbo.uu_join_x') IS NOT NULL DROP TABLE dbo.uu_join_x",
                 "IF OBJECT_ID(N'dbo.uu_join_r1') IS NOT NULL DROP TABLE dbo.uu_join_r1",
                 "IF OBJECT_ID(N'dbo.uu_join_r2') IS NOT NULL DROP TABLE dbo.uu_join_r2",
             })
            (void) stmt.ExecuteDirect(sql);
    };
    dropFixture();
    auto const cleanup = detail::Finally(dropFixture);
    // Two declared keys, each reached by an undeclared column; those two columns are joined by a
    // foreign key of their own, which a walk that visits both before either may never record.
    for (auto const* sql: {
             "CREATE TABLE dbo.uu_join_r1 (CODE CHAR(10) NOT NULL CONSTRAINT PK_uu_join_r1 PRIMARY KEY)",
             "CREATE TABLE dbo.uu_join_r2 (CODE CHAR(10) NOT NULL CONSTRAINT PK_uu_join_r2 PRIMARY KEY)",
             "CREATE TABLE dbo.uu_join_x (K CHAR(10) NOT NULL CONSTRAINT PK_uu_join_x PRIMARY KEY "
             "CONSTRAINT FK_uu_join_x_r1 FOREIGN KEY REFERENCES dbo.uu_join_r1 (CODE))",
             "CREATE TABLE dbo.uu_join_y (ID INT NOT NULL PRIMARY KEY, K CHAR(10) NOT NULL, "
             "CONSTRAINT FK_uu_join_y_r2 FOREIGN KEY (K) REFERENCES dbo.uu_join_r2 (CODE), "
             "CONSTRAINT FK_uu_join_y_x FOREIGN KEY (K) REFERENCES dbo.uu_join_x (K))",
         })
        (void) stmt.ExecuteDirect(sql);
    fold_test::FoldStub<20'26'10'17'00'06> decl { "declare wide", [](SqlMigrationQueryBuilder& plan) {
                                                     plan.CreateTable("uu_join_r1").PrimaryKey("CODE", NChar(10));
                                                     plan.CreateTable("uu_join_r2").PrimaryKey("CODE", NChar(10));
                                                 } };

    auto const preview = mgr.UnicodeUpgradeTables(/*dryRun=*/true);
    REQUIRE(preview.units.size() == 1);
    auto tables = UnitTables(preview.units.front());
    std::ranges::sort(tables);
    CHECK(tables == std::vector<std::string> { "dbo.uu_join_r1", "dbo.uu_join_r2", "dbo.uu_join_x", "dbo.uu_join_y" });

    auto const result = mgr.UnicodeUpgradeTables(/*dryRun=*/false);
    CHECK(result.columns.size() == 4);
    for (auto const* table: { "uu_join_r1", "uu_join_r2" })
        CHECK(LiveDataTypeIn(stmt, "dbo", table, "CODE") == "nchar");
    CHECK(LiveDataTypeIn(stmt, "dbo", "uu_join_x", "K") == "nchar");
    CHECK(LiveDataTypeIn(stmt, "dbo", "uu_join_y", "K") == "nchar");
    for (auto const* name: { "FK_uu_join_x_r1", "FK_uu_join_y_r2", "FK_uu_join_y_x" })
        CHECK(CatalogScalar(stmt, std::format("SELECT name FROM sys.foreign_keys WHERE name = '{}'", name)) == name);
}

namespace
{
/// A column whose values render as quoted text, binary or a keyword rather than as a plain number.
struct TypedSeedColumn
{
    std::string name;
    SqlColumnTypeDefinition type;
    SqlVariant value;
    /// How SQLite stores the literal: `typeof(column) || ':' || quote(column)`.
    std::string sqliteRepresentation;
};

std::vector<TypedSeedColumn> TypedSeedColumns()
{
    using namespace SqlColumnTypeDefinitions;
    using namespace std::chrono_literals;
    using std::chrono::February;

    // Sub-millisecond fractions, which the literal renderings of time and timestamp handle differently.
    return {
        { .name = "Ref",
          .type = Guid {},
          .value = SqlVariant { SqlGuid::UnsafeParse("0f8fad5b-d9cb-469f-a165-70867728950e") },
          .sqliteRepresentation = "text:'0F8FAD5B-D9CB-469F-A165-70867728950E'" },
        { .name = "Day",
          .type = Date {},
          .value = SqlVariant { SqlDate { 2024y, February, 29d } },
          .sqliteRepresentation = "text:'2024-02-29'" },
        { .name = "Moment",
          .type = Time {},
          .value = SqlVariant { SqlTime { 13h, 45min, 30s, 123456us } },
          .sqliteRepresentation = "text:'13:45:30.123456'" },
        { .name = "Stamp",
          .type = DateTime {},
          .value = SqlVariant { SqlDateTime { 2024y, February, 29d, 13h, 45min, 30s, 123456700ns } },
          .sqliteRepresentation = "text:'2024-02-29T13:45:30.123'" },
        { .name = "Price",
          .type = Decimal { .precision = 10, .scale = 4 },
          .value = SqlVariant { SqlDynamicNumeric { .unscaledValue = -403399, .precision = 10, .scale = 4 } },
          .sqliteRepresentation = "real:-40.3399" },
        { .name = "Amount",
          .type = Decimal { .precision = 28, .scale = 12 },
          .value = SqlVariant { SqlDynamicNumeric { .unscaledValue = 123456789012000000, .precision = 28, .scale = 12 } },
          .sqliteRepresentation = "real:123456.789012" },
        { .name = "Bytes",
          .type = VarBinary { .size = 16 },
          .value = SqlVariant { SqlBinary { 0x00, 0xFF, 0x10, 0x27 } },
          .sqliteRepresentation = "blob:X'00FF1027'" },
        { .name = "Flag", .type = Bool {}, .value = SqlVariant { true }, .sqliteRepresentation = "integer:1" },
    };
}

/// Creates @p tableName with an integer `Id` and one nullable column per typed seed column.
void CreateTypedSeedTable(SqlMigrationQueryBuilder& plan, std::string const& tableName)
{
    auto table = plan.CreateTable(tableName);
    table.RequiredColumn("Id", SqlColumnTypeDefinitions::Integer());
    for (auto const& column: TypedSeedColumns())
        table.Column(column.name, column.type);
}

/// Inserts a row of @p id and every typed seed column's value, skipped by @p keyColumns when given.
void InsertTypedSeedRow(SqlMigrationQueryBuilder& plan,
                        std::string const& tableName,
                        int id,
                        std::vector<std::string> keyColumns = {})
{
    auto insert = plan.Insert(tableName);
    insert.Set("Id", id);
    for (auto const& column: TypedSeedColumns())
        insert.Set(column.name, column.value);
    if (!keyColumns.empty())
        insert.IfNotExists(std::move(keyColumns));
}
} // namespace

TEST_CASE_METHOD(SqlMigrationTestFixture,
                 "Insert IfNotExists stores bound values of every type like literal ones",
                 "[SqlMigration]")
{
    // Row 1 goes through the plain insert's literal path, row 2 through the bound one.
    auto const migration = SqlMigration::Migration<202610170017>("bound typed values", [](SqlMigrationQueryBuilder& plan) {
        CreateTypedSeedTable(plan, "Typed");
        InsertTypedSeedRow(plan, "Typed", 1);
        InsertTypedSeedRow(plan, "Typed", 2, { "Id" });
    });

    auto& manager = SqlMigration::MigrationManager::GetInstance();
    manager.CreateMigrationHistory();
    REQUIRE(manager.ApplyPendingMigrations() == 1);

    auto const isSqlite = manager.GetDataMapper().Connection().ServerType() == SqlServerType::SQLITE;
    auto const readValue = [](std::string const& expression, int id) {
        auto stmt = SqlStatement {};
        auto cursor = stmt.ExecuteDirect(std::format(R"(SELECT {} FROM "Typed" WHERE "Id" = {})", expression, id));
        REQUIRE(cursor.FetchRow());
        auto value = SqlVariant {};
        REQUIRE(cursor.GetColumn(1, &value));
        return value.ToString();
    };

    for (auto const& column: TypedSeedColumns())
    {
        INFO("column " << column.name);
        auto const quoted = std::format(R"("{}")", column.name);
        auto const literal = readValue(quoted, 1);
        auto const bound = readValue(quoted, 2);
        UNSCOPED_INFO("literal " << literal << ", bound " << bound);
        CHECK(bound == literal);

        // The server itself finds both rows' values equal.
        auto stmt = SqlStatement {};
        CHECK(stmt
                  .ExecuteDirectScalar<long long>(std::format(
                      R"(SELECT COUNT(*) FROM "Typed" a, "Typed" b WHERE a."Id" = 1 AND b."Id" = 2 AND a.{0} = b.{0})",
                      quoted))
                  .value_or(-1)
              == 1);

        // SQLite keeps whatever storage class and text it was given, so compare those, too.
        if (isSqlite)
        {
            auto const representation = std::format("typeof({0}) || ':' || quote({0})", quoted);
            CHECK(readValue(representation, 1) == column.sqliteRepresentation);
            CHECK(readValue(representation, 2) == column.sqliteRepresentation);
        }
    }

    // The timestamp literal carries milliseconds, so the bound timestamp must not keep more.
    if (!isSqlite)
    {
        auto stmt = SqlStatement {};
        auto cursor = stmt.ExecuteDirect(R"(SELECT "Stamp" FROM "Typed" WHERE "Id" = 2)");
        REQUIRE(cursor.FetchRow());
        CHECK(cursor.GetColumn<SqlDateTime>(1).sqlValue.fraction == 123'000'000);
    }
}

TEST_CASE_METHOD(SqlMigrationTestFixture,
                 "Insert IfNotExists finds a literal-stored row by a key of every type",
                 "[SqlMigration]")
{
    auto const seedRow = SqlMigration::Migration<202610170018>("seed typed row", [](SqlMigrationQueryBuilder& plan) {
        CreateTypedSeedTable(plan, "Seeded");
        InsertTypedSeedRow(plan, "Seeded", 1);
    });
    // Each insert is keyed on one typed column alone and must find the literal-stored row.
    auto const seedAgain = SqlMigration::Migration<202610170019>("seed typed row again", [](SqlMigrationQueryBuilder& plan) {
        auto id = 100;
        for (auto const& column: TypedSeedColumns())
            InsertTypedSeedRow(plan, "Seeded", id++, { column.name });
    });

    auto& manager = SqlMigration::MigrationManager::GetInstance();
    auto const log = CapturedMigrationLog {};
    manager.CreateMigrationHistory();
    auto const skippedAtStart = manager.SkippedExistingRowCount();
    REQUIRE(manager.ApplyPendingMigrations() == 2);

    auto const columnCount = TypedSeedColumns().size();
    CHECK(manager.SkippedExistingRowCount() == skippedAtStart + columnCount);
    REQUIRE(log.Lines().size() == columnCount);
    for (auto const& line: log.Lines())
        UNSCOPED_INFO(line);
    CHECK(log.Lines()[0] == "Skipped existing row: 202610170019 Seeded (Ref='0F8FAD5B-D9CB-469F-A165-70867728950E')");
    auto stmt = SqlStatement {};
    CHECK(stmt.ExecuteDirectScalar<long long>(R"(SELECT COUNT(*) FROM "Seeded")").value_or(-1) == 1);
}

TEST_CASE("ToBoundSql binds a GUID, date, time and timestamp as their literal text", "[SqlMigration]")
{
    auto builder = SqlQueryBuilder(SqlQueryFormatter::Sqlite()).Migration();
    auto insert = builder.Insert("Typed");
    for (auto const& column: TypedSeedColumns())
        insert.Set(column.name, column.value);
    insert.IfNotExists({ "Ref" });
    auto const plan = std::move(builder).GetPlan();

    auto const bound = ToBoundSql(plan.steps.at(0)).value_or(SqlBoundStatement {});
    REQUIRE(bound.parameters.size() == TypedSeedColumns().size() + 1);
    auto const boundText = [&](std::size_t index) {
        auto const* text = std::get_if<std::u16string>(&bound.parameters[index].value);
        return text ? *text : std::u16string {};
    };
    CHECK(boundText(0) == u"0F8FAD5B-D9CB-469F-A165-70867728950E");
    CHECK(boundText(1) == u"2024-02-29");
    CHECK(boundText(2) == u"13:45:30.123456");
    CHECK(boundText(3) == u"2024-02-29T13:45:30.123");
    CHECK(boundText(8) == u"0F8FAD5B-D9CB-469F-A165-70867728950E");

    // Exact decimals, binary values and bool keep their native binding.
    CHECK(std::holds_alternative<SqlDynamicNumeric>(bound.parameters[4].value));
    CHECK(std::holds_alternative<SqlBinary>(bound.parameters[6].value));
    CHECK(std::holds_alternative<bool>(bound.parameters[7].value));
}
