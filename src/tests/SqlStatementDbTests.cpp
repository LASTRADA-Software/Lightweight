// SPDX-License-Identifier: Apache-2.0

#include "Utils.hpp"

#include <Lightweight/Lightweight.hpp>

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <utility>
#include <vector>

using namespace Lightweight;

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
// Prepare() reusing the statement already on the handle
// ================================================================================================

TEST_CASE_METHOD(SqlTestFixture, "Repeated Prepare of the same query keeps executing correctly", "[SqlStatement]")
{
    // Preparing byte-identical SQL again reuses the prepared statement instead of re-issuing
    // SQLPrepareW. What must not change is the observable behaviour: the parameter count, the
    // bindings, and the rows that come back have to be the same on every round.
    auto stmt = SqlStatement {};
    CreateEmployeesTable(stmt);
    FillEmployeesTable(stmt);

    auto const query = R"(SELECT "FirstName" FROM "Employees" WHERE "Salary" > ? ORDER BY "EmployeeID")";

    for (auto const [threshold, expectedRows]: { std::pair { 45'000, 3 }, { 55'000, 2 }, { 65'000, 1 } })
    {
        stmt.Prepare(query);
        auto cursor = stmt.Execute(threshold);

        int rows = 0;
        while (cursor.FetchRow())
        {
            CHECK(!cursor.GetColumn<std::string>(1).empty());
            ++rows;
        }
        CHECK(rows == expectedRows);
    }
}

TEST_CASE_METHOD(SqlTestFixture, "Prepare of the same query survives a schema change underneath", "[SqlStatement]")
{
    // The reuse above is what makes this case interesting: the second Prepare() does not re-issue
    // SQLPrepareW, so the statement the server holds may have been planned against the *old* table.
    // PostgreSQL rejects that plan (SQLSTATE 0A000), which the statement recovers from by preparing
    // once more - the caller must see rows, not an exception.
    auto stmt = SqlStatement {};
    CreateEmployeesTable(stmt);
    FillEmployeesTable(stmt);

    auto const query = R"(SELECT "FirstName" FROM "Employees" ORDER BY "EmployeeID")";

    stmt.Prepare(query);
    {
        auto cursor = stmt.Execute();
        CHECK(cursor.FetchRow());
    }

    // Rebuild the table with an extra column, through a different statement handle.
    auto other = SqlStatement { stmt.Connection() };
    (void) other.ExecuteDirect(R"(DROP TABLE "Employees")");
    CreateEmployeesTable(other);
    (void) other.ExecuteDirect(R"(ALTER TABLE "Employees" ADD "Nickname" VARCHAR(50))");
    FillEmployeesTable(other);

    stmt.Prepare(query);
    auto cursor = stmt.Execute();

    int rows = 0;
    while (cursor.FetchRow())
        ++rows;
    CHECK(rows == 3);
}
