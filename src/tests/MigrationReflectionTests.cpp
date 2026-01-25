// SPDX-License-Identifier: Apache-2.0

#include "Utils.hpp"

#include <Lightweight/Lightweight.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace Lightweight;
using namespace std::string_view_literals;

namespace ReflectionTests
{
struct TestRecord
{
    Field<int, PrimaryKey::AutoAssign> id;
    Field<std::string> name;
    Field<std::optional<int>> age;

    static constexpr auto TableName = "test_record";
};

struct OtherRecord
{
    Field<int, PrimaryKey::AutoAssign> id;
    Field<std::string> title;
};
} // namespace ReflectionTests

TEST_CASE("Migration Reflection CreateTable", "[MigrationReflection]")
{
    auto const* formatter = SqlQueryFormatter::Get(SqlServerType::POSTGRESQL);
    auto builder = SqlMigrationQueryBuilder(*formatter);

    std::ignore = builder.CreateTable<ReflectionTests::TestRecord>();

    auto const& plan = builder.GetPlan();
    REQUIRE(plan.steps.size() == 1);
    REQUIRE(std::holds_alternative<SqlCreateTablePlan>(plan.steps[0]));

    auto const& createTable = std::get<SqlCreateTablePlan>(plan.steps[0]);
    CHECK(createTable.tableName == "test_record");
    REQUIRE(createTable.columns.size() == 3);

    // Verify 'id' column
    auto const& idColumn = createTable.columns[0];
    CHECK(idColumn.name == "id");
    CHECK(std::holds_alternative<SqlColumnTypeDefinitions::Integer>(idColumn.type));
    CHECK(idColumn.primaryKey != SqlPrimaryKeyType::NONE);
    CHECK(idColumn.required);

    // Verify 'name' column
    auto const& nameColumn = createTable.columns[1];
    CHECK(nameColumn.name == "name");
    CHECK(std::holds_alternative<SqlColumnTypeDefinitions::Text>(nameColumn.type));
    CHECK(nameColumn.required);
    CHECK(nameColumn.primaryKey == SqlPrimaryKeyType::NONE);

    // Verify 'age' column
    auto const& ageColumn = createTable.columns[2];
    CHECK(ageColumn.name == "age");
    CHECK(std::holds_alternative<SqlColumnTypeDefinitions::Integer>(ageColumn.type));
    CHECK(!ageColumn.required);
    CHECK(ageColumn.primaryKey == SqlPrimaryKeyType::NONE);
}

TEST_CASE("Migration Reflection AlterTable AddColumn", "[MigrationReflection]")
{
    auto const* formatter = SqlQueryFormatter::Get(SqlServerType::POSTGRESQL);
    auto builder = SqlMigrationQueryBuilder(*formatter);

    builder.AlterTable<ReflectionTests::TestRecord>()
        .AddColumn<Member(ReflectionTests::TestRecord::name)>()
        .AddNotRequiredColumn<Member(ReflectionTests::TestRecord::age)>();

    auto const& plan = builder.GetPlan();
    REQUIRE(plan.steps.size() == 1);
    REQUIRE(std::holds_alternative<SqlAlterTablePlan>(plan.steps[0]));

    auto const& alterTable = std::get<SqlAlterTablePlan>(plan.steps[0]);
    CHECK(alterTable.tableName == "test_record");
    REQUIRE(alterTable.commands.size() == 2);

    // Verify AddColumn "name"
    REQUIRE(std::holds_alternative<SqlAlterTableCommands::AddColumn>(alterTable.commands[0]));
    auto const& addName = std::get<SqlAlterTableCommands::AddColumn>(alterTable.commands[0]);
    CHECK(addName.columnName == "name");
    CHECK(std::holds_alternative<SqlColumnTypeDefinitions::Text>(addName.columnType));
    CHECK(addName.nullable == SqlNullable::NotNull);

    // Verify AddColumn "age"
    REQUIRE(std::holds_alternative<SqlAlterTableCommands::AddColumn>(alterTable.commands[1]));
    auto const& addAge = std::get<SqlAlterTableCommands::AddColumn>(alterTable.commands[1]);
    CHECK(addAge.columnName == "age");
    CHECK(std::holds_alternative<SqlColumnTypeDefinitions::Integer>(addAge.columnType));
    CHECK(addAge.nullable == SqlNullable::Null);
}

TEST_CASE_METHOD(SqlTestFixture,
                 "SqlSchema::MakeCreateTablePlan with single-column foreign key via Migration API",
                 "[SqlSchema][Migration]")
{
    using namespace SqlColumnTypeDefinitions;

    auto conn = SqlConnection {};
    auto stmt = SqlStatement { conn };

    // Create parent table
    auto parentMigration = conn.Migration();
    parentMigration.CreateTable("FkTestParent").PrimaryKey("id", Integer {});
    for (auto const& sql: parentMigration.GetPlan().ToSql())
        stmt.ExecuteDirect(sql);

    // Create child table with single-column foreign key
    auto childMigration = conn.Migration();
    childMigration.CreateTable("FkTestChild")
        .PrimaryKey("id", Integer {})
        .ForeignKey(
            "parent_id", Integer {}, SqlForeignKeyReferenceDefinition { .tableName = "FkTestParent", .columnName = "id" });
    for (auto const& sql: childMigration.GetPlan().ToSql())
        stmt.ExecuteDirect(sql);

    // Read schema back from database
    auto tables = SqlSchema::ReadAllTables(stmt, conn.DatabaseName(), "");

    // Find the child table
    auto const childIt = std::ranges::find_if(tables, [](auto const& t) { return t.name == "FkTestChild"; });
    REQUIRE(childIt != tables.end());

    // Generate plan from the read schema
    auto const plan = SqlSchema::MakeCreateTablePlan(*childIt);

    CHECK(plan.tableName == "FkTestChild");
    REQUIRE(plan.columns.size() == 2);

    // Find 'parent_id' column (order may vary by database)
    auto const parentIdIt = std::ranges::find_if(plan.columns, [](auto const& c) { return c.name == "parent_id"; });
    REQUIRE(parentIdIt != plan.columns.end());

    // Verify 'parent_id' column has per-column foreign key reference
    REQUIRE(parentIdIt->foreignKey.has_value());
    // NOLINTBEGIN(bugprone-unchecked-optional-access) - checked by REQUIRE above
    CHECK(parentIdIt->foreignKey->tableName == "FkTestParent");
    CHECK(parentIdIt->foreignKey->columnName == "id");
    // NOLINTEND(bugprone-unchecked-optional-access)

    // Verify foreign keys are also present at table level
    REQUIRE(plan.foreignKeys.size() == 1);
    CHECK(plan.foreignKeys[0].referencedTableName == "FkTestParent");

    // Cleanup
    stmt.ExecuteDirect("DROP TABLE \"FkTestChild\"");
    stmt.ExecuteDirect("DROP TABLE \"FkTestParent\"");
}

TEST_CASE_METHOD(SqlTestFixture,
                 "SqlSchema::MakeCreateTablePlan with composite foreign key via Migration API",
                 "[SqlSchema][Migration]")
{
    using namespace SqlColumnTypeDefinitions;

    auto conn = SqlConnection {};
    auto stmt = SqlStatement { conn };

    // Create parent table with composite primary key
    auto parentMigration = conn.Migration();
    parentMigration.CreateTable("FkTestParentComposite")
        .Column(SqlColumnDeclaration {
            .name = "key1", .type = Integer {}, .primaryKey = SqlPrimaryKeyType::MANUAL, .required = true })
        .Column(SqlColumnDeclaration {
            .name = "key2", .type = Integer {}, .primaryKey = SqlPrimaryKeyType::MANUAL, .required = true });
    for (auto const& sql: parentMigration.GetPlan().ToSql())
        stmt.ExecuteDirect(sql);

    // Create child table with composite foreign key
    auto childMigration = conn.Migration();
    childMigration.CreateTable("FkTestChildComposite")
        .PrimaryKey("id", Integer {})
        .RequiredColumn("parent_key1", Integer {})
        .RequiredColumn("parent_key2", Integer {})
        .ForeignKey({ "parent_key1", "parent_key2" }, "FkTestParentComposite", { "key1", "key2" });
    for (auto const& sql: childMigration.GetPlan().ToSql())
        stmt.ExecuteDirect(sql);

    // Read schema back from database
    auto tables = SqlSchema::ReadAllTables(stmt, conn.DatabaseName(), "");

    // Find the child table
    auto const childIt = std::ranges::find_if(tables, [](auto const& t) { return t.name == "FkTestChildComposite"; });
    REQUIRE(childIt != tables.end());

    // Generate plan from the read schema
    auto const plan = SqlSchema::MakeCreateTablePlan(*childIt);

    CHECK(plan.tableName == "FkTestChildComposite");
    REQUIRE(plan.columns.size() == 3);

    // Find FK columns
    auto const parentKey1It = std::ranges::find_if(plan.columns, [](auto const& c) { return c.name == "parent_key1"; });
    auto const parentKey2It = std::ranges::find_if(plan.columns, [](auto const& c) { return c.name == "parent_key2"; });
    REQUIRE(parentKey1It != plan.columns.end());
    REQUIRE(parentKey2It != plan.columns.end());

    // Verify composite FK columns do NOT have per-column foreign key
    // (composite FKs should only be at table level)
    CHECK(!parentKey1It->foreignKey.has_value());
    CHECK(!parentKey2It->foreignKey.has_value());

    // Verify composite foreign keys are present at table level
    REQUIRE(plan.foreignKeys.size() == 1);
    CHECK(plan.foreignKeys[0].referencedTableName == "FkTestParentComposite");
    REQUIRE(plan.foreignKeys[0].columns.size() == 2);
    REQUIRE(plan.foreignKeys[0].referencedColumns.size() == 2);

    // Cleanup
    stmt.ExecuteDirect("DROP TABLE \"FkTestChildComposite\"");
    stmt.ExecuteDirect("DROP TABLE \"FkTestParentComposite\"");
}
