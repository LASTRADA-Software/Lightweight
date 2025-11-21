// SPDX-License-Identifier: Apache-2.0

#include "Utils.hpp"

#include <Lightweight/Lightweight.hpp>

#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cstdlib>

using namespace std::string_view_literals;

namespace std
{
std::ostream& operator<<(std::ostream& os, Lightweight::SqlMigration::MigrationTimestamp const& timestamp)
{
    return os << "MigrationTimestamp(" << timestamp.value << ")";
}
} // namespace std

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

#if 0 // TODO: Disabled temporarily, because I need to re-check it against valgrind again (separately)

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
    migration.Execute(builder);
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
    BelongsTo<&Person::id> person;
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
        "description here",
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
        "description here",
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
#endif
