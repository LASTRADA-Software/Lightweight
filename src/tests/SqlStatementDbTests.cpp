// SPDX-License-Identifier: Apache-2.0

#include "Utils.hpp"

#include <Lightweight/Lightweight.hpp>

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <tuple>
#include <vector>

using namespace Lightweight;
using namespace std::string_view_literals;

// ================================================================================================
// SqlResultCursor::TryFetchRow — std::expected-based fetch surface
// ================================================================================================

TEST_CASE_METHOD(SqlTestFixture, "SqlResultCursor::TryFetchRow returns true for a row, false at EOF", "[SqlStatement]")
{
    auto stmt = SqlStatement {};
    CreateEmployeesTable(stmt);
    FillEmployeesTable(stmt);

    stmt.Prepare(R"(SELECT "FirstName" FROM "Employees" ORDER BY "EmployeeID")");
    auto cursor = stmt.Execute();

    int rows = 0;
    while (true)
    {
        auto const result = cursor.TryFetchRow();
        REQUIRE(result.has_value());
        if (!result.value())
            break;
        ++rows;
    }
    CHECK(rows == 3);
}

// ================================================================================================
// SqlStatement::ExecuteWithVariants — happy path and bad-argument-count path
// ================================================================================================

TEST_CASE_METHOD(SqlTestFixture, "SqlStatement::ExecuteWithVariants binds positional parameters", "[SqlStatement]")
{
    auto stmt = SqlStatement {};
    CreateEmployeesTable(stmt);

    stmt.Prepare(R"(INSERT INTO "Employees" ("FirstName", "LastName", "Salary") VALUES (?, ?, ?))");
    std::vector<SqlVariant> args;
    args.emplace_back(std::string { "Vanessa" });
    args.emplace_back(std::string { "Variant" });
    args.emplace_back(123);
    (void) stmt.ExecuteWithVariants(args);

    auto const count = stmt.ExecuteDirectScalar<int>(R"(SELECT COUNT(*) FROM "Employees")");
    REQUIRE(count.has_value());
    if (count.has_value())
        CHECK(*count == 1);
}

TEST_CASE_METHOD(SqlTestFixture, "SqlStatement::ExecuteWithVariants throws on parameter-count mismatch", "[SqlStatement]")
{
    auto stmt = SqlStatement {};
    CreateEmployeesTable(stmt);

    stmt.Prepare(R"(INSERT INTO "Employees" ("FirstName", "LastName", "Salary") VALUES (?, ?, ?))");

    auto const _ = ScopedSqlNullLogger {};
    std::vector<SqlVariant> tooFew;
    tooFew.emplace_back(std::string { "Solo" });
    CHECK_THROWS_AS(stmt.ExecuteWithVariants(tooFew), std::invalid_argument);
}

// ================================================================================================
// SqlResultCursor::NumRowsAffected — UPDATE / DELETE return counts
// ================================================================================================

TEST_CASE_METHOD(SqlTestFixture, "SqlResultCursor::NumRowsAffected reports UPDATE / DELETE row counts", "[SqlStatement]")
{
    auto stmt = SqlStatement {};
    CreateEmployeesTable(stmt);
    FillEmployeesTable(stmt);

    {
        auto cursor = stmt.ExecuteDirect(R"(UPDATE "Employees" SET "Salary" = "Salary" + 1)");
        CHECK(cursor.NumRowsAffected() == 3);
    }

    {
        // After UPDATE, salaries are 50001, 60001, 70001 — DELETE the two highest.
        auto cursor = stmt.ExecuteDirect(R"(DELETE FROM "Employees" WHERE "Salary" > 50001)");
        CHECK(cursor.NumRowsAffected() == 2);
    }
}

// ================================================================================================
// UPDATE ... RETURNING — driver support varies (#545)
// ================================================================================================

TEST_CASE_METHOD(SqlTestFixture, "UPDATE ... RETURNING reports affected rows/columns", "[SqlStatement]")
{
    // SQL Server has no RETURNING clause (it uses OUTPUT instead); this is SQLite/PostgreSQL syntax.
    auto stmt = SqlStatement {};
    UNSUPPORTED_DATABASE(stmt, SqlServerType::MICROSOFT_SQL);

    std::ignore = stmt.ExecuteDirect(R"(CREATE TABLE ReturningProbe (id INTEGER PRIMARY KEY, n INTEGER NOT NULL))");
    std::ignore = stmt.ExecuteDirect(R"(INSERT INTO ReturningProbe (id, n) VALUES (1, 41))");

    stmt.Prepare(R"(UPDATE ReturningProbe SET n = n + 1 WHERE id = ? RETURNING n)");
    auto cursor = stmt.Execute(1);

    // Reliable regardless of driver: the statement executed and the row genuinely changed,
    // independent of whether the driver surfaces the RETURNING result set as a fetchable cursor.
    CHECK(cursor.NumColumnsAffected() == 1);
    CHECK(cursor.NumRowsAffected() == 1);
}

TEST_CASE_METHOD(SqlTestFixture, "UPDATE ... RETURNING fetches the returned row", "[SqlStatement]")
{
    // SQL Server has no RETURNING clause (it uses OUTPUT instead); this is SQLite/PostgreSQL syntax.
    auto stmt = SqlStatement {};
    UNSUPPORTED_DATABASE(stmt, SqlServerType::MICROSOFT_SQL);

    std::ignore = stmt.ExecuteDirect(R"(CREATE TABLE ReturningProbe (id INTEGER PRIMARY KEY, n INTEGER NOT NULL))");
    std::ignore = stmt.ExecuteDirect(R"(INSERT INTO ReturningProbe (id, n) VALUES (1, 41))");

    stmt.Prepare(R"(UPDATE ReturningProbe SET n = n + 1 WHERE id = ? RETURNING n)");
    auto cursor = stmt.Execute(1);

    // Confirmed via this project's own CI matrix (not just the original bug report): unixODBC's
    // sqliteodbc driver — both Ubuntu's official libsqliteodbc apt package and Homebrew's sqliteodbc
    // on macOS — never opens a fetchable cursor over a RETURNING result set; FetchRow() throws
    // 24000 "Invalid cursor state" there every time. The Windows-native "SQLite3 ODBC Driver" does
    // not have this problem. This is a real, reproducible platform split, not a flaky driver
    // quirk, so gate on it explicitly rather than tolerating any failure blanket (which would also
    // hide a genuine regression on Windows, where this is verified to work).
#if defined(_WIN32) || defined(_WIN64)
    REQUIRE(cursor.FetchRow());
    CHECK(cursor.GetColumn<int>(1) == 42);
    CHECK_FALSE(cursor.FetchRow()); // exactly one row is returned
#else
    auto const fetchResult = cursor.TryFetchRow();
    if (fetchResult.has_value())
    {
        CHECK(fetchResult.value());
        CHECK(cursor.GetColumn<int>(1) == 42);
        CHECK_FALSE(cursor.FetchRow());
    }
    else
    {
        WARN("unixODBC's sqliteodbc driver did not open a fetchable cursor over the RETURNING "
             "result set (sqlState="
             << fetchResult.error().sqlState << "); known limitation, see #545. "
             << "The row/column-count contract (tested separately) is unaffected.");
    }
#endif
}

// ================================================================================================
// SqlStatement(nullopt) — constructed without a connection should not be alive
// ================================================================================================

TEST_CASE_METHOD(SqlTestFixture, "SqlStatement(std::nullopt) yields a not-alive statement", "[SqlStatement]")
{
    auto stmt = SqlStatement { std::nullopt };
    CHECK_FALSE(stmt.IsAlive());
}

// ================================================================================================
// LastInsertId returns 0 when no rows have been inserted yet
// ================================================================================================

TEST_CASE_METHOD(SqlTestFixture, "SqlStatement::LastInsertId returns the most recent identity", "[SqlStatement]")
{
    auto stmt = SqlStatement {};
    CreateEmployeesTable(stmt);
    FillEmployeesTable(stmt);

    auto const lastId = stmt.LastInsertId("Employees");
    CHECK(lastId >= 1);
    // FillEmployeesTable inserts 3 rows starting at 1, so the last row's id should be 3.
    CHECK(lastId == 3);
}

// ================================================================================================
// Cursor BindOutputColumn / BindOutputColumns
// ================================================================================================

TEST_CASE_METHOD(SqlTestFixture, "SqlResultCursor::BindOutputColumns reads multiple columns into locals", "[SqlStatement]")
{
    auto stmt = SqlStatement {};
    CreateEmployeesTable(stmt);
    FillEmployeesTable(stmt);

    stmt.Prepare(R"(SELECT "FirstName", "LastName", "Salary" FROM "Employees" ORDER BY "EmployeeID")");
    auto cursor = stmt.Execute();

    std::string firstName(20, '\0');
    std::string lastName(20, '\0');
    int salary {};
    cursor.BindOutputColumns(&firstName, &lastName, &salary);

    REQUIRE(cursor.FetchRow());
    CHECK(firstName == "Alice");
    CHECK(lastName == "Smith");
    CHECK(salary == 50'000);
}

// ================================================================================================
// Named column access — reading result columns by the name spelled in the query builder (#341)
// ================================================================================================

TEST_CASE_METHOD(SqlTestFixture, "SqlResultCursor::GetColumn by name reads bare column names", "[SqlStatement]")
{
    auto stmt = SqlStatement {};
    CreateEmployeesTable(stmt);
    FillEmployeesTable(stmt);

    auto cursor = stmt.ExecuteDirect(
        stmt.Query("Employees").Select().Fields({ "FirstName"sv, "Salary"sv }).OrderBy("EmployeeID"sv).All());

    REQUIRE(cursor.FetchRow());
    CHECK(cursor.GetColumn<std::string>("FirstName") == "Alice");
    CHECK(cursor.GetColumn<int>("Salary") == 50'000);
}

TEST_CASE_METHOD(SqlTestFixture,
                 "SqlResultCursor::GetColumn by name reads names given to variadic Fields()",
                 "[SqlStatement]")
{
    auto stmt = SqlStatement {};
    CreateEmployeesTable(stmt);
    FillEmployeesTable(stmt);

    // The tests around this one project through the container overloads of Fields(). The variadic
    // Fields(first, more...) registers the projected names along a separate code path, and its
    // single-argument form is a separate instantiation again - one in which the fold over the
    // remaining fields is discarded entirely, leaving the first field as the only registration.
    SECTION("a single field")
    {
        auto cursor = stmt.ExecuteDirect(stmt.Query("Employees").Select().Fields("FirstName").OrderBy("EmployeeID"sv).All());

        REQUIRE(cursor.FetchRow());
        CHECK(cursor.GetColumn<std::string>("FirstName") == "Alice");
    }

    SECTION("several fields")
    {
        auto cursor =
            stmt.ExecuteDirect(stmt.Query("Employees").Select().Fields("FirstName", "Salary").OrderBy("EmployeeID"sv).All());

        REQUIRE(cursor.FetchRow());
        CHECK(cursor.GetColumn<std::string>("FirstName") == "Alice");
        CHECK(cursor.GetColumn<int>("Salary") == 50'000);
    }
}

TEST_CASE_METHOD(SqlTestFixture, "SqlResultCursor::GetColumn by name reads qualified names", "[SqlStatement]")
{
    auto stmt = SqlStatement {};
    CreateEmployeesTable(stmt);
    FillEmployeesTable(stmt);

    auto cursor =
        stmt.ExecuteDirect(stmt.Query("Employees")
                               .Select()
                               .Field(SqlQualifiedTableColumnName { .tableName = "Employees", .columnName = "FirstName" })
                               .Field(SqlQualifiedTableColumnName { .tableName = "Employees", .columnName = "Salary" })
                               .OrderBy(SqlQualifiedTableColumnName { .tableName = "Employees", .columnName = "EmployeeID" })
                               .All());

    REQUIRE(cursor.FetchRow());
    CHECK(cursor.GetColumn<std::string>("Employees.FirstName") == "Alice");
    CHECK(cursor.GetColumn<int>("Employees.Salary") == 50'000);
}

TEST_CASE_METHOD(SqlTestFixture, "SqlResultCursor::GetColumn by name reads an alias", "[SqlStatement]")
{
    auto stmt = SqlStatement {};
    CreateEmployeesTable(stmt);
    FillEmployeesTable(stmt);

    auto cursor =
        stmt.ExecuteDirect(stmt.Query("Employees")
                               .Select()
                               .Field("FirstName"sv)
                               .Field(SqlQualifiedTableColumnName { .tableName = "Employees", .columnName = "Salary" })
                               .As("MonthlyPay"sv)
                               .OrderBy("EmployeeID"sv)
                               .All());

    REQUIRE(cursor.FetchRow());
    CHECK(cursor.GetColumn<std::string>("FirstName") == "Alice");
    CHECK(cursor.GetColumn<int>("MonthlyPay") == 50'000);
}

// FieldAs() is deprecated in favour of Field(...).As(...), but it still ships and still has to keep
// the projected-name table correct — a deprecated overload that silently stopped registering its
// alias would break named access for every caller who has not migrated yet. Calling it is therefore
// the point of this test, and the deprecation warning is suppressed only around it.
#if defined(_MSC_VER)
    #pragma warning(push)
    #pragma warning(disable : 4996)
#else
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

TEST_CASE_METHOD(SqlTestFixture, "SqlResultCursor::GetColumn by name reads a FieldAs alias", "[SqlStatement]")
{
    auto stmt = SqlStatement {};
    CreateEmployeesTable(stmt);
    FillEmployeesTable(stmt);

    // FieldAs() names the column in the same call that projects it, where Field().As() renames a
    // projection that was already recorded — two different paths into the name table, so both need
    // to end up with the alias as the lookup key.
    auto cursor = stmt.ExecuteDirect(
        stmt.Query("Employees")
            .Select()
            .FieldAs("FirstName"sv, "GivenName"sv)
            .FieldAs(SqlQualifiedTableColumnName { .tableName = "Employees", .columnName = "Salary" }, "MonthlyPay"sv)
            .OrderBy("EmployeeID"sv)
            .All());

    REQUIRE(cursor.FetchRow());
    CHECK(cursor.GetColumn<std::string>("GivenName") == "Alice");
    CHECK(cursor.GetColumn<int>("MonthlyPay") == 50'000);

    // The aliased-away name is not a lookup key: the query does not project a column by that name.
    CHECK_THROWS_AS(std::ignore = cursor.GetColumn<std::string>("FirstName"), std::invalid_argument);
    CHECK_THROWS_AS(std::ignore = cursor.GetColumn<int>("Employees.Salary"), std::invalid_argument);
}

#if defined(_MSC_VER)
    #pragma warning(pop)
#else
    #pragma GCC diagnostic pop
#endif

TEST_CASE_METHOD(SqlTestFixture, "SqlResultCursor named access survives Prepare and Execute", "[SqlStatement]")
{
    auto stmt = SqlStatement {};
    CreateEmployeesTable(stmt);
    FillEmployeesTable(stmt);

    stmt.Prepare(stmt.Query("Employees").Select().Fields({ "FirstName"sv, "Salary"sv }).OrderBy("EmployeeID"sv).All());
    auto cursor = stmt.Execute();

    REQUIRE(cursor.FetchRow());
    CHECK(cursor.GetColumn<std::string>("FirstName") == "Alice");
    CHECK(cursor.GetColumn<int>("Salary") == 50'000);
}

TEST_CASE_METHOD(SqlTestFixture, "SqlResultCursor::GetNullableColumn and GetColumnOr by name", "[SqlStatement]")
{
    auto stmt = SqlStatement {};
    CreateEmployeesTable(stmt);

    stmt.Prepare(stmt.Query("Employees")
                     .Insert()
                     .Set("FirstName", SqlWildcard)
                     .Set("LastName", SqlWildcard)
                     .Set("Salary", SqlWildcard));
    (void) stmt.Execute("Dana", SqlNullValue, 42'000);

    auto cursor = stmt.ExecuteDirect(stmt.Query("Employees").Select().Fields({ "LastName"sv, "Salary"sv }).All());

    REQUIRE(cursor.FetchRow());
    CHECK(cursor.GetNullableColumn<std::string>("LastName") == std::nullopt);
    CHECK(cursor.GetColumnOr<int>("Salary", 0) == 42'000);
}

TEST_CASE_METHOD(SqlTestFixture, "SqlResultCursor named access rejects unusable names", "[SqlStatement]")
{
    auto stmt = SqlStatement {};
    CreateEmployeesTable(stmt);
    FillEmployeesTable(stmt);

    SECTION("an unknown name throws")
    {
        auto cursor = stmt.ExecuteDirect(stmt.Query("Employees").Select().Fields({ "FirstName"sv }).All());
        REQUIRE(cursor.FetchRow());
        CHECK_THROWS_AS(cursor.GetColumn<std::string>("NoSuchColumn"), std::invalid_argument);
    }

    SECTION("a name projected twice is ambiguous")
    {
        auto cursor = stmt.ExecuteDirect(stmt.Query("Employees").Select().Field("FirstName"sv).Field("FirstName"sv).All());
        REQUIRE(cursor.FetchRow());
        CHECK_THROWS_AS(cursor.GetColumn<std::string>("FirstName"), std::invalid_argument);
    }

    SECTION("a wildcard projection has no mapping")
    {
        auto cursor = stmt.ExecuteDirect(stmt.Query("Employees").Select().Field("*"sv).All());
        REQUIRE(cursor.FetchRow());
        CHECK_THROWS_AS(cursor.GetColumn<std::string>("FirstName"), std::invalid_argument);
    }

    SECTION("raw SQL has no mapping")
    {
        stmt.Prepare(R"(SELECT "FirstName" FROM "Employees")");
        auto cursor = stmt.Execute();
        REQUIRE(cursor.FetchRow());
        CHECK_THROWS_AS(cursor.GetColumn<std::string>("FirstName"), std::invalid_argument);
    }

    SECTION("raw SQL does not inherit the previous query's mapping")
    {
        {
            auto cursor = stmt.ExecuteDirect(stmt.Query("Employees").Select().Fields({ "FirstName"sv }).All());
            REQUIRE(cursor.FetchRow());
            CHECK(cursor.GetColumn<std::string>("FirstName") == "Alice");
        }

        stmt.Prepare(R"(SELECT "Salary" FROM "Employees")");
        auto cursor = stmt.Execute();
        REQUIRE(cursor.FetchRow());
        CHECK_THROWS_AS(cursor.GetColumn<std::string>("FirstName"), std::invalid_argument);
    }
}

TEST_CASE_METHOD(SqlTestFixture, "SqlResultCursor named access rejects an empty name", "[SqlStatement]")
{
    auto stmt = SqlStatement {};
    CreateEmployeesTable(stmt);
    FillEmployeesTable(stmt);

    // An un-aliased aggregate occupies an unnamed slot; an empty lookup name must not resolve to it.
    // Both projections are aggregates so the query needs no GROUP BY on any supported database.
    auto cursor = stmt.ExecuteDirect(
        stmt.Query("Employees").Select().Field(Aggregate::Count("EmployeeID"sv)).Field(Aggregate::Max("Salary"sv)).All());

    REQUIRE(cursor.FetchRow());
    CHECK_THROWS_AS(cursor.GetColumn<int>(""), std::invalid_argument);
}
