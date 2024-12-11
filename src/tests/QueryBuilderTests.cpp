// SPDX-License-Identifier: Apache-2.0

#include "Utils.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>

#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <functional>
#include <ranges>
#include <set>
#include <source_location>

struct QueryExpectations
{
    std::string_view sqlite;
    std::string_view postgres;
    std::string_view sqlServer;
    std::string_view oracle;

    static QueryExpectations All(std::string_view query)
    {
        // NOLINTNEXTLINE(modernize-use-designated-initializers)
        return { query, query, query, query };
    }
};

auto EraseLinefeeds(std::string str) noexcept -> std::string
{
    // Remove all LFs from str:
    // str.erase(std::remove(str.begin(), str.end(), '\n'), str.end());
    str.erase(std::ranges::begin(std::ranges::remove(str, '\n')), std::end(str));
    return str;
}

[[nodiscard]] std::string NormalizeText(std::string_view const& text)
{
    auto result = std::string(text);

    // Remove any newlines and reduce all whitespace to a single space
    result.erase(
        std::unique(result.begin(), result.end(), [](char a, char b) { return std::isspace(a) && std::isspace(b); }),
        result.end());

    // trim lading and trailing whitespace
    while (!result.empty() && std::isspace(result.front()))
        result.erase(result.begin());

    while (!result.empty() && std::isspace(result.back()))
        result.pop_back();

    return result;
}

template <typename TheSqlQuery>
    requires(std::is_invocable_v<TheSqlQuery, SqlQueryBuilder&>)
void checkSqlQueryBuilder(TheSqlQuery const& sqlQueryBuilder,
                          QueryExpectations const& expectations,
                          std::function<void()> const& postCheck = {},
                          std::source_location const& location = std::source_location::current())
{
    INFO(std::format("Test source location: {}:{}", location.file_name(), location.line()));

    auto const checkOne = [&](SqlQueryFormatter const& formatter, std::string_view name, std::string_view query) {
        INFO("Testing " << name);
        auto sqliteQueryBuilder = SqlQueryBuilder(formatter);
        auto const sqlQuery = sqlQueryBuilder(sqliteQueryBuilder);
        auto const actual = NormalizeText(sqlQuery.ToSql());
        auto const expected = NormalizeText(query);
        REQUIRE(actual == expected);
        if (postCheck)
            postCheck();
    };

    checkOne(SqlQueryFormatter::Sqlite(), "SQLite", expectations.sqlite);
    checkOne(SqlQueryFormatter::PostgrSQL(), "Postgres", expectations.postgres);
    checkOne(SqlQueryFormatter::SqlServer(), "SQL Server", expectations.sqlServer);
    // TODO: checkOne(SqlQueryFormatter::OracleSQL(), "Oracle", expectations.oracle);
}

TEST_CASE_METHOD(SqlTestFixture, "SqlQueryBuilder.Select.Count", "[SqlQueryBuilder]")
{
    checkSqlQueryBuilder([](SqlQueryBuilder& q) { return q.FromTable("Table").Select().Count(); },
                         QueryExpectations::All("SELECT COUNT(*) FROM \"Table\""));
}

TEST_CASE_METHOD(SqlTestFixture, "SqlQueryBuilder.Select.All", "[SqlQueryBuilder]")
{
    checkSqlQueryBuilder(
        [](SqlQueryBuilder& q) {
            return q.FromTable("That").Select().Fields("a", "b").Field("c").GroupBy("a").OrderBy("b").All();
        },
        QueryExpectations::All(R"(
                               SELECT "a", "b", "c" FROM "That"
                               GROUP BY "a"
                               ORDER BY "b" ASC)"));
}

TEST_CASE_METHOD(SqlTestFixture, "SqlQueryBuilder.Select.Distinct.All", "[SqlQueryBuilder]")
{
    checkSqlQueryBuilder(
        [](SqlQueryBuilder& q) {
            return q.FromTable("That").Select().Distinct().Fields("a", "b").Field("c").GroupBy("a").OrderBy("b").All();
        },
        QueryExpectations::All(R"(
                               SELECT DISTINCT "a", "b", "c" FROM "That"
                               GROUP BY "a"
                               ORDER BY "b" ASC)"));
}

TEST_CASE_METHOD(SqlTestFixture, "SqlQueryBuilder.Select.OrderBy fully qualified", "[SqlQueryBuilder]")
{
    checkSqlQueryBuilder(
        [](SqlQueryBuilder& q) {
            return q.FromTable("That")
                .Select()
                .Fields("a", "b")
                .Field("c")
                .OrderBy(SqlQualifiedTableColumnName { .tableName = "That", .columnName = "b" },
                         SqlResultOrdering::DESCENDING)
                .All();
        },
        QueryExpectations::All(R"(
                               SELECT "a", "b", "c" FROM "That"
                               ORDER BY "That"."b" DESC)"));
}

TEST_CASE_METHOD(SqlTestFixture, "SqlQueryBuilder.Select.First", "[SqlQueryBuilder]")
{
    checkSqlQueryBuilder(
        [](SqlQueryBuilder& q) { return q.FromTable("That").Select().Field("field1").OrderBy("id").First(); },
        QueryExpectations {
            .sqlite = R"(SELECT "field1" FROM "That"
                         ORDER BY "id" ASC LIMIT 1)",
            .postgres = R"(SELECT "field1" FROM "That"
                           ORDER BY "id" ASC LIMIT 1)",
            .sqlServer = R"(SELECT TOP 1 "field1" FROM "That"
                            ORDER BY "id" ASC)",
            .oracle = R"(SELECT "field1" FROM "That"
                         ORDER BY "id" ASC FETCH FIRST 1 ROWS ONLY)",
        });
}

TEST_CASE_METHOD(SqlTestFixture, "SqlQueryBuilder.Select.Range", "[SqlQueryBuilder]")
{
    checkSqlQueryBuilder(
        [](SqlQueryBuilder& q) {
            return q.FromTable("That").Select().Fields("foo", "bar").OrderBy("id").Range(200, 50);
        },
        QueryExpectations {
            .sqlite = R"(SELECT "foo", "bar" FROM "That"
                         ORDER BY "id" ASC LIMIT 50 OFFSET 200)",
            .postgres = R"(SELECT "foo", "bar" FROM "That"
                           ORDER BY "id" ASC LIMIT 50 OFFSET 200)",
            .sqlServer = R"(SELECT "foo", "bar" FROM "That"
                            ORDER BY "id" ASC OFFSET 200 ROWS FETCH NEXT 50 ROWS ONLY)",
            .oracle = R"(SELECT "foo", "bar" FROM "That"
                         ORDER BY "id" ASC OFFSET 200 ROWS FETCH NEXT 50 ROWS ONLY)",
        });
}

struct Users
{
    std::string name;
    std::string address;
};

TEST_CASE_METHOD(SqlTestFixture, "SqlQueryBuilder.Fields", "[SqlQueryBuilder]")
{
    checkSqlQueryBuilder([](SqlQueryBuilder& q) { return q.FromTable("Users").Select().Fields<Users>().First(); },
                         QueryExpectations {
                             .sqlite = R"(SELECT "name", "address" FROM "Users" LIMIT 1)",
                             .postgres = R"(SELECT "name", "address" FROM "Users" LIMIT 1)",
                             .sqlServer = R"(SELECT TOP 1 "name", "address" FROM "Users")",
                             .oracle = R"(SELECT "name", "address" FROM "Users" FETCH FIRST 1 ROWS ONLY)",
                         });
}

struct UsersFields
{
    Field<std::string> name;
    Field<std::optional<std::string>> address;
};

TEST_CASE_METHOD(SqlTestFixture, "SqlQueryBuilder.FieldsForFieldMembers", "[SqlQueryBuilder]")
{
    checkSqlQueryBuilder([](SqlQueryBuilder& q) { return q.FromTable("Users").Select().Fields<UsersFields>().First(); },
                         QueryExpectations {
                             .sqlite = R"(SELECT "name", "address" FROM "Users" LIMIT 1)",
                             .postgres = R"(SELECT "name", "address" FROM "Users" LIMIT 1)",
                             .sqlServer = R"(SELECT TOP 1 "name", "address" FROM "Users")",
                             .oracle = R"(SELECT "name", "address" FROM "Users" FETCH FIRST 1 ROWS ONLY)",
                         });
}

struct QueryBuilderTestEmail
{
    Field<std::string> email;
    BelongsTo<&UsersFields::name> user;
};

TEST_CASE_METHOD(SqlTestFixture, "SqlQueryBuilder.FieldsWithBelongsTo", "[SqlQueryBuilder]")
{
    checkSqlQueryBuilder(
        [](SqlQueryBuilder& q) {
            return q.FromTable("QueryBuilderTestEmail").Select().Fields<QueryBuilderTestEmail>().First();
        },
        QueryExpectations {
            .sqlite = R"(SELECT "email", "user" FROM "QueryBuilderTestEmail" LIMIT 1)",
            .postgres = R"(SELECT "email", "user" FROM "QueryBuilderTestEmail" LIMIT 1)",
            .sqlServer = R"(SELECT TOP 1 "email", "user" FROM "QueryBuilderTestEmail")",
            .oracle = R"(SELECT "email", "user" FROM "QueryBuilderTestEmail" FETCH FIRST 1 ROWS ONLY)",
        });
}

TEST_CASE_METHOD(SqlTestFixture, "SqlQueryBuilder.ComplexOR", "[SqlQueryBuilder]")
{
    checkSqlQueryBuilder(
        [](SqlQueryBuilder& q) {
            using namespace std::string_view_literals;
            return q.FromTable("Table1")
                .Select()
                .LeftOuterJoin("Table2"sv, "id"sv, "id"sv)
                .RightOuterJoin("Table3"sv,
                                [](SqlJoinConditionBuilder q) {
                                    // clang-format off
                                    return q.On("id", { .tableName = "Table1", .columnName = "column1" })
                                            .OrOn("id", { .tableName = "Table1", .columnName = "column2" })
                                            .OrOn("id", { .tableName = "Table1", .columnName = "column3" })
                                            .OrOn("id", { .tableName = "Table1", .columnName = "column4" });
                                    // clang-format on
                                })
                .Fields({ "id"sv, "name"sv }, "Table1")
                .All();
        },
        QueryExpectations::All(R"(SELECT "Table1"."id", "Table1"."name" FROM "Table1"
                                 LEFT OUTER JOIN "Table2" ON "Table2"."id" = "Table1"."id"
                                 RIGHT OUTER JOIN "Table3" ON "Table3"."id" = "Table1"."column1" OR "Table3"."id" = "Table1"."column2" OR "Table3"."id" = "Table1"."column3" OR "Table3"."id" = "Table1"."column4")"));
}

TEST_CASE_METHOD(SqlTestFixture, "SqlQueryBuilder.Where.Junctors", "[SqlQueryBuilder]")
{
    checkSqlQueryBuilder(
        [](SqlQueryBuilder& q) {
            // clang-format off
            return q.FromTable("Table")
                .Select()
                .WhereRaw("a")
                .And().WhereRaw("b")
                .Or().WhereRaw("c")
                .And().WhereRaw("d")
                .And().Not().WhereRaw("e")
                .Count();
            // clang-format on
        },
        QueryExpectations::All(R"SQL(SELECT COUNT(*) FROM "Table"
                                     WHERE a AND b OR c AND d AND NOT e)SQL"));
}

TEST_CASE_METHOD(SqlTestFixture, "SqlQueryBuilder.WhereIn", "[SqlQueryBuilder]")
{
    // Check functionality of container overloads for IN
    checkSqlQueryBuilder(
        [](SqlQueryBuilder& q) { return q.FromTable("That").Delete().WhereIn("foo", std::vector { 1, 2, 3 }); },
        QueryExpectations::All(R"(DELETE FROM "That"
                                  WHERE "foo" IN (1, 2, 3))"));

    // Check functionality of an lvalue input range
    auto const values = std::set { 1, 2, 3 };
    checkSqlQueryBuilder([&](SqlQueryBuilder& q) { return q.FromTable("That").Delete().WhereIn("foo", values); },
                         QueryExpectations::All(R"(DELETE FROM "That"
                                                   WHERE "foo" IN (1, 2, 3))"));

    // Check functionality of the initializer_list overload for IN
    checkSqlQueryBuilder([](SqlQueryBuilder& q) { return q.FromTable("That").Delete().WhereIn("foo", { 1, 2, 3 }); },
                         QueryExpectations::All(R"(DELETE FROM "That"
                                                   WHERE "foo" IN (1, 2, 3))"));
}

TEST_CASE_METHOD(SqlTestFixture, "SqlQueryBuilder.Join", "[SqlQueryBuilder]")
{
    checkSqlQueryBuilder(
        [](SqlQueryBuilder& q) {
            return q.FromTable("That").Select().Fields("foo", "bar").InnerJoin("Other", "id", "that_id").All();
        },
        QueryExpectations::All(
            R"(SELECT "foo", "bar" FROM "That"
               INNER JOIN "Other" ON "Other"."id" = "That"."that_id")"));

    checkSqlQueryBuilder(
        [](SqlQueryBuilder& q) {
            return q.FromTable("That").Select().Fields("foo", "bar").LeftOuterJoin("Other", "id", "that_id").All();
        },
        QueryExpectations::All(
            R"(SELECT "foo", "bar" FROM "That"
               LEFT OUTER JOIN "Other" ON "Other"."id" = "That"."that_id")"));

    checkSqlQueryBuilder(
        [](SqlQueryBuilder& q) {
            using namespace std::string_view_literals;
            return q.FromTable("Table_A")
                .Select()
                .Fields({ "foo"sv, "bar"sv }, "Table_A")
                .Fields({ "that_foo"sv, "that_id"sv }, "Table_B")
                .LeftOuterJoin("Table_B", "id", "that_id")
                .Where(SqlQualifiedTableColumnName { .tableName = "Table_A", .columnName = "foo" }, 42)
                .All();
        },
        QueryExpectations::All("SELECT \"Table_A\".\"foo\", \"Table_A\".\"bar\","
                               " \"Table_B\".\"that_foo\", \"Table_B\".\"that_id\""
                               " FROM \"Table_A\"\n"
                               " LEFT OUTER JOIN \"Table_B\" ON \"Table_B\".\"id\" = \"Table_A\".\"that_id\"\n"
                               " WHERE \"Table_A\".\"foo\" = 42"));

    checkSqlQueryBuilder(
        [](SqlQueryBuilder& q) {
            using namespace std::string_view_literals;
            return q.FromTable("Table_A")
                .Select()
                .Fields({ "foo"sv, "bar"sv }, "Table_A")
                .Fields({ "that_foo"sv, "that_id"sv }, "Table_B")
                .InnerJoin("Table_B",
                           [](SqlJoinConditionBuilder q) {
                               // clang-format off
                               return q.On("id", { .tableName = "Table_A", .columnName = "that_id" })
                                       .On("that_foo", { .tableName = "Table_A", .columnName = "foo" });
                               // clang-format on
                           })
                .Where(SqlQualifiedTableColumnName { .tableName = "Table_A", .columnName = "foo" }, 42)
                .All();
        },
        QueryExpectations::All(
            R"(SELECT "Table_A"."foo", "Table_A"."bar", "Table_B"."that_foo", "Table_B"."that_id" FROM "Table_A"
               INNER JOIN "Table_B" ON "Table_B"."id" = "Table_A"."that_id" AND "Table_B"."that_foo" = "Table_A"."foo"
               WHERE "Table_A"."foo" = 42)"));

    checkSqlQueryBuilder(
        [](SqlQueryBuilder& q) {
            using namespace std::string_view_literals;
            return q.FromTable("Table_A")
                .Select()
                .Fields({ "foo"sv, "bar"sv }, "Table_A")
                .Fields({ "that_foo"sv, "that_id"sv }, "Table_B")
                .LeftOuterJoin("Table_B",
                               [](SqlJoinConditionBuilder q) {
                                   // clang-format off
                               return q.On("id", { .tableName = "Table_A", .columnName = "that_id" })
                                       .On("that_foo", { .tableName = "Table_A", .columnName = "foo" });
                                   // clang-format on
                               })
                .Where(SqlQualifiedTableColumnName { .tableName = "Table_A", .columnName = "foo" }, 42)
                .All();
        },
        QueryExpectations::All(
            R"(SELECT "Table_A"."foo", "Table_A"."bar", "Table_B"."that_foo", "Table_B"."that_id" FROM "Table_A"
               LEFT OUTER JOIN "Table_B" ON "Table_B"."id" = "Table_A"."that_id" AND "Table_B"."that_foo" = "Table_A"."foo"
               WHERE "Table_A"."foo" = 42)"));
}

TEST_CASE_METHOD(SqlTestFixture, "SqlQueryBuilder.SelectAs", "[SqlQueryBuilder]")
{
    checkSqlQueryBuilder(
        [](SqlQueryBuilder& q) { return q.FromTable("That").Select().FieldAs("foo", "F").FieldAs("bar", "B").All(); },
        QueryExpectations::All(R"(SELECT "foo" AS "F", "bar" AS "B" FROM "That")"));
}

TEST_CASE_METHOD(SqlTestFixture, "SqlQueryBuilder.FromTableAs", "[SqlQueryBuilder]")
{
    checkSqlQueryBuilder(
        [](SqlQueryBuilder& q) {
            return q.FromTableAs("Other", "O")
                .Select()
                .Field(SqlQualifiedTableColumnName { "O", "foo" })
                .Field(SqlQualifiedTableColumnName { "O", "bar" })
                .All();
        },
        QueryExpectations::All(R"(SELECT "O"."foo", "O"."bar" FROM "Other" AS "O")"));
}

TEST_CASE_METHOD(SqlTestFixture, "SqlQueryBuilder.Insert", "[SqlQueryBuilder]")
{
    std::vector<SqlVariant> boundValues;
    checkSqlQueryBuilder(
        [&](SqlQueryBuilder& q) {
            return q.FromTableAs("Other", "O")
                .Insert(&boundValues)
                .Set("foo", 42)
                .Set("bar", "baz")
                .Set("baz", SqlNullValue);
        },
        QueryExpectations::All(R"(INSERT INTO "Other" ("foo", "bar", "baz") VALUES (?, ?, NULL))"),
        [&]() {
            CHECK(boundValues.size() == 2);
            CHECK(std::get<int>(boundValues[0].value) == 42);
            CHECK(std::get<std::string_view>(boundValues[1].value) == "baz");
            boundValues.clear();
        });
}

TEST_CASE_METHOD(SqlTestFixture, "SqlQueryBuilder.Update", "[SqlQueryBuilder]")
{
    std::vector<SqlVariant> boundValues;
    checkSqlQueryBuilder(
        [&](SqlQueryBuilder& q) {
            return q.FromTableAs("Other", "O").Update(&boundValues).Set("foo", 42).Set("bar", "baz").Where("id", 123);
        },
        QueryExpectations::All(R"(UPDATE "Other" AS "O" SET "foo" = ?, "bar" = ?
                                  WHERE "id" = ?)"),
        [&]() {
            CHECK(boundValues.size() == 3);
            CHECK(std::get<int>(boundValues[0].value) == 42);
            CHECK(std::get<std::string_view>(boundValues[1].value) == "baz");
            CHECK(std::get<int>(boundValues[2].value) == 123);
            boundValues.clear();
        });
}

TEST_CASE_METHOD(SqlTestFixture, "SqlQueryBuilder.Where.Lambda", "[SqlQueryBuilder]")
{
    checkSqlQueryBuilder(
        [](SqlQueryBuilder& q) {
            return q.FromTable("That")
                .Select()
                .Field("foo")
                .Where("a", 1)
                .OrWhere([](auto& q) { return q.Where("b", 2).Where("c", 3); })
                .All();
        },
        QueryExpectations::All(R"(SELECT "foo" FROM "That"
                                  WHERE "a" = 1 OR ("b" = 2 AND "c" = 3))"));
}

TEST_CASE_METHOD(SqlTestFixture, "SqlQueryBuilder.WhereColumn", "[SqlQueryBuilder]")
{
    checkSqlQueryBuilder(
        [](SqlQueryBuilder& q) {
            return q.FromTable("That").Select().Field("foo").WhereColumn("left", "=", "right").All();
        },
        QueryExpectations::All(R"(SELECT "foo" FROM "That"
                                  WHERE "left" = "right")"));
}

TEST_CASE_METHOD(SqlTestFixture,
                 "Where: SqlQualifiedTableColumnName OP SqlQualifiedTableColumnName",
                 "[SqlQueryBuilder]")
{
    checkSqlQueryBuilder(
        [](SqlQueryBuilder& q) {
            return q.FromTable("That")
                .Select()
                .Field("foo")
                .Where(SqlQualifiedTableColumnName { .tableName = "That", .columnName = "left" },
                       "=",
                       SqlQualifiedTableColumnName { .tableName = "That", .columnName = "right" })
                .All();
        },
        QueryExpectations::All(R"(SELECT "foo" FROM "That"
                                  WHERE "That"."left" = "That"."right")"));
}

TEST_CASE_METHOD(SqlTestFixture, "Where: left IS NULL", "[SqlQueryBuilder]")
{
    checkSqlQueryBuilder(
        [](SqlQueryBuilder& q) {
            return q.FromTable("That")
                .Select()
                .Field("foo")
                .Where("Left1", SqlNullValue)
                .Where("Left2", std::nullopt)
                .All();
        },
        QueryExpectations::All(R"(SELECT "foo" FROM "That"
                                  WHERE "Left1" IS NULL AND "Left2" IS NULL)"));

    checkSqlQueryBuilder(
        [](SqlQueryBuilder& q) {
            // clang-format off
            return q.FromTable("That")
                .Select()
                .Field("foo")
                .WhereNotEqual("Left1", SqlNullValue)
                .Or().WhereNotEqual("Left2", std::nullopt)
                .All();
            // clang-format on
        },
        QueryExpectations::All(R"(SELECT "foo" FROM "That"
                                  WHERE "Left1" IS NOT NULL OR "Left2" IS NOT NULL)"));
}

TEST_CASE_METHOD(SqlTestFixture, "Varying: multiple varying final query types", "[SqlQueryBuilder]")
{
    auto const& sqliteFormatter = SqlQueryFormatter::Sqlite();

    auto queryBuilder = SqlQueryBuilder { sqliteFormatter }
                            .FromTable("Table")
                            .Select()
                            .Varying()
                            .Fields({ "foo", "bar", "baz" })
                            .Where("condition", 42);

    auto const countQuery = EraseLinefeeds(queryBuilder.Count().ToSql());
    auto const allQuery = EraseLinefeeds(queryBuilder.All().ToSql());
    auto const firstQuery = EraseLinefeeds(queryBuilder.First().ToSql());

    CHECK(countQuery == R"(SELECT COUNT(*) FROM "Table" WHERE "condition" = 42)");
    CHECK(allQuery == R"(SELECT "foo", "bar", "baz" FROM "Table" WHERE "condition" = 42)");
    CHECK(firstQuery == R"(SELECT "foo", "bar", "baz" FROM "Table" WHERE "condition" = 42 LIMIT 1)");
}

TEST_CASE_METHOD(SqlTestFixture, "Use SqlQueryBuilder for SqlStatement.ExecuteDirct", "[SqlQueryBuilder]")
{
    auto stmt = SqlStatement {};

    CreateEmployeesTable(stmt);
    FillEmployeesTable(stmt);

    stmt.ExecuteDirect(stmt.Connection().Query("Employees").Select().Fields("FirstName", "LastName").All());

    REQUIRE(stmt.FetchRow());
    CHECK(stmt.GetColumn<std::string>(1) == "Alice");
}

TEST_CASE_METHOD(SqlTestFixture, "Use SqlQueryBuilder for SqlStatement.Prepare", "[SqlQueryBuilder]")
{
    auto stmt = SqlStatement {};

    CreateEmployeesTable(stmt);
    FillEmployeesTable(stmt);

    std::vector<SqlVariant> inputBindings;

    auto const sqlQuery =
        stmt.Connection().Query("Employees").Update(&inputBindings).Set("Salary", 55'000).Where("Salary", 50'000);

    REQUIRE(inputBindings.size() == 2);
    CHECK(std::get<int>(inputBindings[0].value) == 55'000);
    CHECK(std::get<int>(inputBindings[1].value) == 50'000);

    stmt.Prepare(sqlQuery);
    stmt.ExecuteWithVariants(inputBindings);

    stmt.ExecuteDirect(R"(SELECT "FirstName", "LastName", "Salary" FROM "Employees" WHERE "Salary" = 55000)");
    REQUIRE(stmt.FetchRow());
    CHECK(stmt.GetColumn<std::string>(1) == "Alice");
    CHECK(stmt.GetColumn<std::string>(2) == "Smith");
    CHECK(stmt.GetColumn<int>(3) == 55'000);
}

TEST_CASE_METHOD(SqlTestFixture, "Use SqlQueryBuilder for SqlStatement.Prepare: iterative", "[SqlQueryBuilder]")
{
    auto stmt = SqlStatement {};

    CreateLargeTable(stmt);

    // Prepare INSERT query
    auto insertQuery = stmt.Connection().Query("LargeTable").Insert(nullptr /* no auto-fill */);
    for (char c = 'A'; c <= 'Z'; ++c)
    {
        auto const columnName = std::string(1, c);
        insertQuery.Set(columnName, SqlWildcard);
    }
    stmt.Prepare(insertQuery);

    // Execute the same query 10 times

    for (int i = 0; i < 10; ++i)
    {
        // Prepare data (fill all columns naively)
        std::vector<SqlVariant> inputBindings;
        for (char c = 'A'; c <= 'Z'; ++c)
            inputBindings.emplace_back(std::string(1, c) + std::to_string(i));

        // Execute the query with the prepared data
        stmt.ExecuteWithVariants(inputBindings);
    }
}

TEST_CASE_METHOD(SqlTestFixture, "SqlQueryBuilder: sub select with Where", "[SqlQueryBuilder]")
{
    auto sharedConnection = SqlConnection {};
    auto stmt = SqlStatement { sharedConnection };

    stmt.ExecuteDirect(R"SQL(DROP TABLE IF EXISTS "Test")SQL");
    stmt.ExecuteDirect(R"SQL(
        CREATE TABLE "Test" (
            "name" VARCHAR(20) NULL,
            "secret" INT NULL
        )
    )SQL");

    stmt.Prepare(R"SQL(INSERT INTO "Test" ("name", "secret") VALUES (?, ?))SQL");
    auto const names = std::vector<SqlFixedString<20>> { "Alice", "Bob", "Charlie", "David" };
    auto const secrets = std::vector<int> { 42, 43, 44, 45 };
    stmt.ExecuteBatchSoft(names, secrets);

    auto const totalRecords = stmt.ExecuteDirectScalar<int>(R"SQL(SELECT COUNT(*) FROM "Test")SQL");
    REQUIRE(totalRecords.value_or(0) == 4);

    // clang-format off
    auto const subSelect = stmt.Query("Test")
                              .Select()
                              .Field("secret")
                              .Where("name", "Alice")
                              .All();
    auto const selectQuery = stmt.Query("Test")
                                 .Select()
                                 .Fields({ "name", "secret" })
                                 .Where("secret", subSelect)
                                 .All();
    // clang-format on
    stmt.Prepare(selectQuery);
    stmt.Execute();

    REQUIRE(stmt.FetchRow());
    CHECK(stmt.GetColumn<std::string>(1) == "Alice");
    CHECK(stmt.GetColumn<int>(2) == 42);

    REQUIRE(!stmt.FetchRow());
}

TEST_CASE_METHOD(SqlTestFixture, "SqlQueryBuilder: sub select with WhereIn", "[SqlQueryBuilder]")
{
    auto stmt = SqlStatement {};

    stmt.ExecuteDirect(R"SQL(DROP TABLE IF EXISTS "Test")SQL");
    stmt.ExecuteDirect(R"SQL(
        CREATE TABLE "Test" (
            "name" VARCHAR(20) NULL,
            "secret" INT NULL
        )
    )SQL");

    stmt.Prepare(R"SQL(INSERT INTO "Test" ("name", "secret") VALUES (?, ?))SQL");
    auto const names = std::vector<SqlFixedString<20>> { "Alice", "Bob", "Charlie", "David" };
    auto const secrets = std::vector<int> { 42, 43, 44, 45 };
    stmt.ExecuteBatchSoft(names, secrets);

    auto const totalRecords = stmt.ExecuteDirectScalar<int>("SELECT COUNT(*) FROM \"Test\"");
    REQUIRE(totalRecords.value_or(0) == 4);

    // clang-format off
    auto const subSelect = stmt.Query("Test")
                              .Select()
                              .Field("secret")
                              .Where("name", "Alice")
                              .OrWhere("name", "Bob").All();
    auto const selectQuery = stmt.Query("Test")
                                 .Select()
                                 .Fields({ "name", "secret" })
                                 .WhereIn("secret", subSelect)
                                 .All();
    // clang-format on
    stmt.Prepare(selectQuery);
    stmt.Execute();

    REQUIRE(stmt.FetchRow());
    CHECK(stmt.GetColumn<std::string>(1) == "Alice");
    CHECK(stmt.GetColumn<int>(2) == 42);

    REQUIRE(stmt.FetchRow());
    CHECK(stmt.GetColumn<std::string>(1) == "Bob");
    CHECK(stmt.GetColumn<int>(2) == 43);

    REQUIRE(!stmt.FetchRow());
}

TEST_CASE_METHOD(SqlTestFixture, "DropTable", "[SqlQueryBuilder][Migration]")
{
    checkSqlQueryBuilder(
        [](SqlQueryBuilder& q) {
            auto migration = q.Migration();
            migration.DropTable("Table");
            return migration.GetPlan();
        },
        QueryExpectations::All(R"sql(
                                   DROP TABLE "Table";
                               )sql"));
}

TEST_CASE_METHOD(SqlTestFixture, "CreateTable with Column", "[SqlQueryBuilder][Migration]")
{
    using namespace SqlColumnTypeDefinitions;
    checkSqlQueryBuilder(
        [](SqlQueryBuilder& q) {
            auto migration = q.Migration();
            migration.CreateTable("Test").Column("column", Varchar { 255 });
            return migration.GetPlan();
        },
        QueryExpectations::All(R"sql(CREATE TABLE "Test" (
                                        "column" VARCHAR(255)
                                    );
                               )sql"));
}

TEST_CASE_METHOD(SqlTestFixture, "CreateTable with RequiredColumn", "[SqlQueryBuilder][Migration]")
{
    using namespace SqlColumnTypeDefinitions;
    checkSqlQueryBuilder(
        [](SqlQueryBuilder& q) {
            auto migration = q.Migration();
            migration.CreateTable("Test").RequiredColumn("column", Varchar { 255 });
            return migration.GetPlan();
        },
        QueryExpectations::All(R"sql(CREATE TABLE "Test" (
                                        "column" VARCHAR(255) NOT NULL
                                     );
                               )sql"));
}

TEST_CASE_METHOD(SqlTestFixture, "CreateTable with Column: Guid", "[SqlQueryBuilder][Migration]")
{
    using namespace SqlColumnTypeDefinitions;
    checkSqlQueryBuilder(
        [](SqlQueryBuilder& q) {
            auto migration = q.Migration();
            migration.CreateTable("Test").RequiredColumn("column", Guid {});
            return migration.GetPlan();
        },
        QueryExpectations {
            .sqlite = R"sql(CREATE TABLE "Test" (
                                "column" GUID NOT NULL
                            );
            )sql",
            .postgres = R"sql(CREATE TABLE "Test" (
                                "column" UUID NOT NULL
                            );
            )sql",
            .sqlServer = R"sql(CREATE TABLE "Test" (
                                "column" UNIQUEIDENTIFIER NOT NULL
                            );
            )sql",
            .oracle = R"sql(CREATE TABLE "Test" (
                                "column" RAW(16) NOT NULL
                            );
            )sql",
        });
}

TEST_CASE_METHOD(SqlTestFixture, "CreateTable with PrimaryKey", "[SqlQueryBuilder][Migration]")
{
    using namespace SqlColumnTypeDefinitions;
    checkSqlQueryBuilder(
        [](SqlQueryBuilder& q) {
            auto migration = q.Migration();
            migration.CreateTable("Test").PrimaryKey("pk", Integer {});
            return migration.GetPlan();
        },
        QueryExpectations::All(R"sql(CREATE TABLE "Test" (
                                        "pk" INTEGER NOT NULL,
                                        PRIMARY KEY ("pk")
                                     );
                               )sql"));
}

TEST_CASE_METHOD(SqlTestFixture, "CreateTable with PrimaryKeyWithAutoIncrement", "[SqlQueryBuilder][Migration]")
{
    using namespace SqlColumnTypeDefinitions;
    checkSqlQueryBuilder(
        [](SqlQueryBuilder& q) {
            auto migration = q.Migration();
            migration.CreateTable("Test").PrimaryKeyWithAutoIncrement("pk");
            return migration.GetPlan();
        },
        QueryExpectations {
            .sqlite = R"sql(CREATE TABLE "Test" (
                                "pk" INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT
                            );
                           )sql",
            .postgres = R"sql(CREATE TABLE "Test" (
                                "pk" SERIAL NOT NULL PRIMARY KEY
                            );
                           )sql",
            .sqlServer = R"sql(CREATE TABLE "Test" (
                                "pk" BIGINT NOT NULL IDENTITY(1,1) PRIMARY KEY
                            );
                           )sql",
            .oracle = R"sql(CREATE TABLE "Test" (
                                "pk" NUMBER(19,0) NOT NULL PRIMARY KEY
                            );
                            )sql",
        });
}

TEST_CASE_METHOD(SqlTestFixture, "CreateTable with Index", "[SqlQueryBuilder][Migration]")
{
    using namespace SqlColumnTypeDefinitions;
    checkSqlQueryBuilder(
        [](SqlQueryBuilder& q) {
            auto migration = q.Migration();
            migration.CreateTable("Table").RequiredColumn("column", Integer {}).Index();
            return migration.GetPlan();
        },
        QueryExpectations::All(R"sql(CREATE TABLE "Table" (
                                        "column" INTEGER NOT NULL
                                     );
                                     CREATE INDEX "Table_column_index" ON "Table"("column");
                               )sql"));
}

TEST_CASE_METHOD(SqlTestFixture, "CreateTable with Foreign key", "[SqlQueryBuilder][Migration]")
{
    using namespace SqlColumnTypeDefinitions;
    checkSqlQueryBuilder(
        [](SqlQueryBuilder& q) {
            auto migration = q.Migration();
            migration.CreateTable("Table").ForeignKey("other_id",
                                                      Integer {},
                                                      SqlForeignKeyReferenceDefinition {
                                                          .tableName = "OtherTable",
                                                          .columnName = "id",
                                                      });
            return migration.GetPlan();
        },
        QueryExpectations {
            .sqlite = R"sql(CREATE TABLE "Table" (
                                   FOREIGN KEY ("other_id") REFERENCES "OtherTable"("id")
                                     );)sql",
            .postgres = R"sql(CREATE TABLE "Table" (
                                   FOREIGN KEY ("other_id") REFERENCES "OtherTable"("id")
                                     );)sql",
            .sqlServer = R"sql(CREATE TABLE "Table" (
                                   CONSTRAINT FK_other_id FOREIGN KEY ("other_id") REFERENCES "OtherTable"("id")
                                     );)sql",
            .oracle = R"sql(CREATE TABLE "Table" (
                                   CONSTRAINT FK_other_id FOREIGN KEY ("other_id") REFERENCES "OtherTable"("id")
                                     );)sql",
        });
}

TEST_CASE_METHOD(SqlTestFixture, "CreateTable complex demo", "[SqlQueryBuilder][Migration]")
{
    using namespace SqlColumnTypeDefinitions;
    checkSqlQueryBuilder(
        [](SqlQueryBuilder& q) {
            // clang-format off
            auto migration = q.Migration();
            migration.CreateTable("Test")
                .PrimaryKeyWithAutoIncrement("a", Bigint {})
                .RequiredColumn("b", Varchar { 32 }).Unique()
                .Column("c", DateTime {}).Index()
                .Column("d", Varchar { 255 }).UniqueIndex();;
            return migration.GetPlan();
            // clang-format on
        },
        QueryExpectations {
            .sqlite = R"sql(
                    CREATE TABLE "Test" (
                        "a" INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
                        "b" VARCHAR(32) NOT NULL UNIQUE,
                        "c" DATETIME,
                        "d" VARCHAR(255)
                    );
                    CREATE INDEX "Test_c_index" ON "Test"("c");
                    CREATE UNIQUE INDEX "Test_d_index" ON "Test"("d");
                )sql",
            .postgres = R"sql(
                    CREATE TABLE "Test" (
                        "a" SERIAL NOT NULL PRIMARY KEY,
                        "b" VARCHAR(32) NOT NULL UNIQUE,
                        "c" TIMESTAMP,
                        "d" VARCHAR(255)
                    );
                    CREATE INDEX "Test_c_index" ON "Test"("c");
                    CREATE UNIQUE INDEX "Test_d_index" ON "Test"("d");
                )sql",
            .sqlServer = R"sql(
                    CREATE TABLE "Test" (
                        "a" BIGINT NOT NULL IDENTITY(1,1) PRIMARY KEY,
                        "b" VARCHAR(32) NOT NULL UNIQUE,
                        "c" DATETIME,
                        "d" VARCHAR(255)
                    );
                    CREATE INDEX "Test_c_index" ON "Test"("c");
                    CREATE UNIQUE INDEX "Test_d_index" ON "Test"("d");
                )sql",
            .oracle = R"sql(
                    CREATE TABLE "Test" (
                        "a" NUMBER GENERATED BY DEFAULT ON NULL AS IDENTITY PRIMARY KEY
                        "b" VARCHAR2(32 CHAR) NOT NULL UNIQUE,
                        "c" DATETIME,
                        "d" VARCHAR2(255 CHAR)
                    );
                    CREATE INDEX "Test_c_index" ON "Test"("c");
                    CREATE UNIQUE INDEX "Test_d_index" ON "Test"("d");
                )sql",
        });
}

TEST_CASE_METHOD(SqlTestFixture, "AlterTable AddColumn", "[SqlQueryBuilder][Migration]")
{
    using namespace SqlColumnTypeDefinitions;
    checkSqlQueryBuilder(
        [](SqlQueryBuilder& q) {
            auto migration = q.Migration();
            migration.AlterTable("Table").AddColumn("column", Integer {});
            return migration.GetPlan();
        },
        QueryExpectations::All(R"sql(ALTER TABLE "Table" ADD COLUMN "column" INTEGER;
                               )sql"));
}

TEST_CASE_METHOD(SqlTestFixture, "AlterTable multiple AddColumn calls", "[SqlQueryBuilder][Migration]")
{
    using namespace SqlColumnTypeDefinitions;
    checkSqlQueryBuilder(
        [](SqlQueryBuilder& q) {
            auto migration = q.Migration();
            migration.AlterTable("Table").AddColumn("column", Integer {}).AddColumn("column2", Varchar { 255 });
            return migration.GetPlan();
        },
        QueryExpectations::All(R"sql(ALTER TABLE "Table" ADD COLUMN "column" INTEGER;
                                     ALTER TABLE "Table" ADD COLUMN "column2" VARCHAR(255);
                               )sql"));
}

TEST_CASE_METHOD(SqlTestFixture, "AlterTable RenameColumn", "[SqlQueryBuilder][Migration]")
{
    checkSqlQueryBuilder(
        [](SqlQueryBuilder& q) {
            auto migration = q.Migration();
            migration.AlterTable("Table").RenameColumn("old", "new");
            return migration.GetPlan();
        },
        QueryExpectations::All(R"sql(ALTER TABLE "Table" RENAME COLUMN "old" TO "new";
                               )sql"));
}

TEST_CASE_METHOD(SqlTestFixture, "AlterTable RenameTo", "[SqlQueryBuilder][Migration]")
{
    checkSqlQueryBuilder(
        [](SqlQueryBuilder& q) {
            auto migration = q.Migration();
            migration.AlterTable("Table").RenameTo("NewTable");
            return migration.GetPlan();
        },
        QueryExpectations::All(R"sql(ALTER TABLE "Table" RENAME TO "NewTable";
                               )sql"));
}

TEST_CASE_METHOD(SqlTestFixture, "AlterTable AddIndex", "[SqlQueryBuilder][Migration]")
{
    checkSqlQueryBuilder(
        [](SqlQueryBuilder& q) {
            auto migration = q.Migration();
            migration.AlterTable("Table").AddIndex("column");
            return migration.GetPlan();
        },
        QueryExpectations::All(R"sql(CREATE INDEX "Table_column_index" ON "Table"("column");
                               )sql"));
}

TEST_CASE_METHOD(SqlTestFixture, "AlterTable AddUniqueIndex", "[SqlQueryBuilder][Migration]")
{
    checkSqlQueryBuilder(
        [](SqlQueryBuilder& q) {
            auto migration = q.Migration();
            migration.AlterTable("Table").AddUniqueIndex("column");
            return migration.GetPlan();
        },
        QueryExpectations::All(R"sql(CREATE UNIQUE INDEX "Table_column_index" ON "Table"("column");
                               )sql"));
}

TEST_CASE_METHOD(SqlTestFixture, "AlterTable DropIndex", "[SqlQueryBuilder][Migration]")
{
    checkSqlQueryBuilder(
        [](SqlQueryBuilder& q) {
            auto migration = q.Migration();
            migration.AlterTable("Table").DropIndex("column");
            return migration.GetPlan();
        },
        QueryExpectations::All(R"sql(DROP INDEX "Table_column_index";)sql"));
}
