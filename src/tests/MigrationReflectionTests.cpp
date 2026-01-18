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
