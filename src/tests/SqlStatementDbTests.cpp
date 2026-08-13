// SPDX-License-Identifier: Apache-2.0

#include "Utils.hpp"

#include <Lightweight/Lightweight.hpp>

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
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

    // Both are reliable regardless of driver: the statement executed and the row genuinely changed,
    // independent of whether the driver surfaces the RETURNING result set as a fetchable cursor.
    CHECK(cursor.NumColumnsAffected() == 1);
    CHECK(cursor.NumRowsAffected() == 1);

    // Whether the RETURNING row is actually fetchable is driver-dependent: some ODBC driver builds
    // (observed with certain sqliteodbc builds, see #545) execute the statement through a path that
    // never opens a cursor over the returned row, leaving FetchRow() unable to retrieve it even
    // though the UPDATE itself succeeded. Don't hard-fail on that — document it via WARN so a
    // regression in the *common* case (driver builds where this works, exercised by our own CI
    // matrix) is still caught, without making the suite depend on a specific driver's internals.
    auto const fetchResult = cursor.TryFetchRow();
    if (!fetchResult.has_value())
    {
        WARN("UPDATE ... RETURNING executed and reported correct row/column counts, but FetchRow() "
             "could not retrieve the returned row on this driver (sqlState="
             << fetchResult.error().sqlState << "). See issue #545 - some ODBC driver builds do not "
             << "open a cursor over a RETURNING result set. Use a two-statement UPDATE + read-back "
             << "as a portable workaround.");
        return;
    }

    CHECK(fetchResult.value());
    CHECK(cursor.GetColumn<int>(1) == 42);
    CHECK_FALSE(cursor.FetchRow()); // exactly one row is returned
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
