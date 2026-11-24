// SPDX-License-Identifier: Apache-2.0

#include "Utils.hpp"

#include <Lightweight/DataBinder/SqlRawColumn.hpp>
#include <Lightweight/Lightweight.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
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

    auto const* const query = R"(SELECT "FirstName" FROM "Employees" WHERE "Salary" > ? ORDER BY "EmployeeID")";

    for (auto const& [threshold, expectedRows]: { std::pair { 45'000, 3 }, { 55'000, 2 }, { 65'000, 1 } })
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

    auto const* const query = R"(SELECT "FirstName" FROM "Employees" ORDER BY "EmployeeID")";

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

TEST_CASE_METHOD(SqlTestFixture, "A failed Prepare does not leave its query marked as prepared", "[SqlStatement]")
{
    // Prepare() used to record the query text as "prepared" before SQLPrepareW ran. When that call
    // failed - SQLite prepares eagerly, so a table that does not exist yet fails right there - the
    // next Prepare() of the same text took the reuse fast path above and skipped SQLPrepareW, keeping
    // the parameter count and the handle of the query prepared before it. Depending on whether the
    // counts matched, that surfaced as "Invalid argument count" or ran the previous query with the
    // new arguments. A long-lived statement (DataMapper's, reused by every lazy relation load on a
    // thread) stayed poisoned for that query text from then on.
    auto stmt = SqlStatement {};
    CreateEmployeesTable(stmt);
    FillEmployeesTable(stmt);

    // Something with one parameter is prepared on the handle first.
    stmt.Prepare(R"(SELECT "FirstName" FROM "Employees" WHERE "Salary" > ?)");
    std::ignore = stmt.Execute(45'000);

    auto const createLater = [&] {
        auto other = SqlStatement { stmt.Connection() };
        (void) other.ExecuteDirect(R"(CREATE TABLE "LaterCreated" ("Id" INTEGER, "Value" INTEGER))");
        (void) other.ExecuteDirect(R"(INSERT INTO "LaterCreated" ("Id", "Value") VALUES (1, 42))");
    };

    SECTION("same parameter count as the previous query")
    {
        auto const* const query = R"(SELECT "Value" FROM "LaterCreated" WHERE "Id" = ?)";
        // Fails in Prepare() on SQLite, in Execute() on drivers that defer the prepare.
        CHECK_THROWS_AS((stmt.Prepare(query), std::ignore = stmt.Execute(1)), SqlException);

        createLater();
        stmt.Prepare(query);
        auto cursor = stmt.Execute(1);
        REQUIRE(cursor.FetchRow());
        CHECK(cursor.GetColumn<int>(1) == 42);
    }

    SECTION("different parameter count than the previous query")
    {
        auto const* const query = R"(SELECT "Value" FROM "LaterCreated" WHERE "Id" = ? AND "Value" = ?)";
        CHECK_THROWS_AS((stmt.Prepare(query), std::ignore = stmt.Execute(1, 42)), SqlException);

        createLater();
        stmt.Prepare(query);
        auto cursor = stmt.Execute(1, 42);
        REQUIRE(cursor.FetchRow());
        CHECK(cursor.GetColumn<int>(1) == 42);
    }
}

TEST_CASE_METHOD(SqlTestFixture, "Prepare reuse returns correct rows after the table is recreated", "[SqlStatement]")
{
    using namespace Lightweight::SqlColumnTypeDefinitions;

    auto connection = SqlConnection {};
    auto stmt = SqlStatement { connection };

    auto const createTable = [](SqlMigrationQueryBuilder& migration) {
        migration.DropTableIfExists("stale_plan");
        migration.CreateTable("stale_plan").PrimaryKey("id", Integer {}).RequiredColumn("value", Integer {});
    };

    stmt.MigrateDirect(createTable);
    stmt.Prepare(R"(INSERT INTO "stale_plan" ("id", "value") VALUES (?, ?))");
    std::ignore = stmt.Execute(1, 10);

    auto const* const selectQuery = R"(SELECT "value" FROM "stale_plan" WHERE "id" = ?)";
    stmt.Prepare(selectQuery);
    {
        auto cursor = stmt.Execute(1);
        REQUIRE(cursor.FetchRow());
        CHECK(cursor.GetColumn<int>(1) == 10);
    }

    // Recreate the table behind the prepared handle, through a second statement so that `stmt` keeps
    // its prepared query and takes the reuse path below. MS SQL Server compiles a plan against the
    // object *ids* it saw, so dropping and recreating the table invalidates the plan while the SQL
    // text still resolves — 42S02; PostgreSQL reports the same condition as 0A000 / 26000.
    {
        auto ddl = SqlStatement { connection };
        ddl.MigrateDirect(createTable);
        ddl.Prepare(R"(INSERT INTO "stale_plan" ("id", "value") VALUES (?, ?))");
        std::ignore = ddl.Execute(1, 99);
    }

    // Byte-identical text, so Prepare() reuses the handle rather than re-issuing SQLPrepare, and the
    // execute must return the *new* table's rows.
    //
    // This is the scenario RetryStalePreparedStatement() exists for, but none of the three backends
    // in the test matrix actually rejects the reused handle here (verified: the "Re-preparing
    // statement" warning never fires) — MS SQL Server's Driver 18 defers preparation to execution
    // time, so there is no server-side plan to go stale. The recovery arm therefore stays uncovered;
    // reaching it needs a fault-injection seam rather than a real driver.
    stmt.Prepare(selectQuery);
    auto cursor = stmt.Execute(1);
    REQUIRE(cursor.FetchRow());
    CHECK(cursor.GetColumn<int>(1) == 99);
}

// ================================================================================================
// Prepare reuse: recovering a handle the server no longer knows about
// ================================================================================================

TEST_CASE_METHOD(SqlTestFixture, "Prepare reuse recovers when the server forgot the statement", "[SqlStatement]")
{
    using namespace Lightweight::SqlColumnTypeDefinitions;

    auto connection = SqlConnection {};
    auto stmt = SqlStatement { connection };

    // `DISCARD ALL` is the one lever in the test matrix that reliably invalidates a *reused* prepared
    // handle: it drops every server-side prepared statement of the session, so the next execute of a
    // handle Prepare() reused reports 26000 and RetryStalePreparedStatement has to re-prepare and run
    // it again. Recreating the table behind the handle does not achieve this on any of the three
    // backends (see the recreated-table test above), and `DISCARD ALL` is PostgreSQL-specific SQL
    // besides - so this is genuinely a one-backend test rather than a dodged failure.
    UNSUPPORTED_DATABASE(stmt, SqlServerType::SQLITE);
    UNSUPPORTED_DATABASE(stmt, SqlServerType::MICROSOFT_SQL);
    UNSUPPORTED_DATABASE(stmt, SqlServerType::MYSQL);
    UNSUPPORTED_DATABASE(stmt, SqlServerType::UNKNOWN);

    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.DropTableIfExists("forgotten_plan");
        migration.CreateTable("forgotten_plan").PrimaryKey("id", Integer {}).RequiredColumn("value", Integer {});
    });
    std::ignore = stmt.ExecuteDirect(R"(INSERT INTO "forgotten_plan" ("id", "value") VALUES (1, 10))");

    auto const* const selectQuery = R"(SELECT "value" FROM "forgotten_plan" WHERE "id" = ?)";

    auto const discardServerSideStatements = [&connection] {
        auto other = SqlStatement { connection };
        std::ignore = other.ExecuteDirect("DISCARD ALL");
    };

    // Both execute paths carry their own retry call site, so both are driven through the recovery.
    SECTION("typed Execute(Args...)")
    {
        stmt.Prepare(selectQuery);
        {
            auto cursor = stmt.Execute(1);
            REQUIRE(cursor.FetchRow());
            CHECK(cursor.GetColumn<int>(1) == 10);
        }

        discardServerSideStatements();

        // Byte-identical text, so Prepare() reuses the handle instead of re-issuing SQLPrepare - which
        // is what leaves the execute below facing a statement the server has forgotten.
        stmt.Prepare(selectQuery);
        auto cursor = stmt.Execute(1);
        REQUIRE(cursor.FetchRow());
        CHECK(cursor.GetColumn<int>(1) == 10);
    }

    SECTION("ExecuteWithVariants")
    {
        stmt.Prepare(selectQuery);
        {
            auto cursor = stmt.ExecuteWithVariants({ SqlVariant { 1 } });
            REQUIRE(cursor.FetchRow());
            CHECK(cursor.GetColumn<int>(1) == 10);
        }

        discardServerSideStatements();

        stmt.Prepare(selectQuery);
        auto cursor = stmt.ExecuteWithVariants({ SqlVariant { 1 } });
        REQUIRE(cursor.FetchRow());
        CHECK(cursor.GetColumn<int>(1) == 10);
    }
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

TEST_CASE_METHOD(SqlTestFixture, "SqlResultCursor::ColumnSqlType reports the described column types", "[SqlStatement]")
{
    auto stmt = SqlStatement {};
    CreateEmployeesTable(stmt);
    FillEmployeesTable(stmt);

    auto cursor = stmt.ExecuteDirect(R"(SELECT "FirstName", "Salary" FROM "Employees")");

    auto const firstName = cursor.ColumnSqlType(1);
    REQUIRE(firstName.has_value());
    constexpr auto TextTypes = std::array<SQLSMALLINT, 4> { SQL_VARCHAR, SQL_WVARCHAR, SQL_LONGVARCHAR, SQL_WLONGVARCHAR };
    CHECK(std::ranges::contains(TextTypes, *firstName));
    auto const salary = cursor.ColumnSqlType(2);
    REQUIRE(salary.has_value());
    CHECK((*salary == SQL_INTEGER || *salary == SQL_BIGINT));
    // A column the result set does not have is reported as an error, not a type.
    CHECK_FALSE(cursor.ColumnSqlType(3).has_value());
}

TEST_CASE_METHOD(SqlTestFixture, "SqlResultCursor::ColumnSqlType tells a native GUID column", "[SqlStatement]")
{
    auto stmt = SqlStatement {};
    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.CreateTable("ColumnSqlTypeGuid").PrimaryKey("ID", SqlColumnTypeDefinitions::Guid {});
    });

    // The backup archives a GUID column verbatim exactly where the driver does not describe it as SQL_GUID.
    auto cursor = stmt.ExecuteDirect(R"(SELECT "ID" FROM "ColumnSqlTypeGuid")");
    auto const sqlType = cursor.ColumnSqlType(1);
    REQUIRE(sqlType.has_value());
    CHECK((*sqlType == SQL_GUID) == stmt.Connection().QueryFormatter().HasNativeGuidType());
}

TEST_CASE_METHOD(SqlTestFixture, "SqlStatement::ConsumePendingResults", "[SqlStatement]")
{
    auto stmt = SqlStatement {};
    stmt.MigrateDirect([](auto& migration) {
        migration.CreateTable("PendingProbe").PrimaryKey("Id", SqlColumnTypeDefinitions::Integer());
    });

    // Nothing pending, also after a statement that matched no rows.
    CHECK_NOTHROW(stmt.ExecuteDirect(R"(INSERT INTO "PendingProbe" ("Id") VALUES (1))").ConsumePendingResults());
    CHECK_NOTHROW(stmt.ExecuteDirect(R"(DELETE FROM "PendingProbe" WHERE "Id" = 2)").ConsumePendingResults());
    CHECK(stmt.ExecuteDirectScalar<long long>(R"(SELECT COUNT(*) FROM "PendingProbe")").value_or(-1) == 1);

    if (stmt.Connection().ServerType() == SqlServerType::MICROSOFT_SQL)
    {
        // The error of a later statement in a batch surfaces only here.
        (void) stmt.ExecuteDirect("SET NOCOUNT OFF");
        auto const _ = ScopedSqlNullLogger {};
        CHECK_THROWS_AS(stmt.ExecuteDirect(R"(DELETE FROM "PendingProbe" WHERE "Id" = 1; THROW 50000, N'refused', 1;)")
                            .ConsumePendingResults(),
                        SqlException);
    }
}

TEST_CASE_METHOD(SqlTestFixture, "Query text carries non-ASCII literals to the database intact", "[SqlStatement]")
{
    auto stmt = SqlStatement {};
    auto const literal = stmt.Connection().QueryFormatter().StringLiteral("Grüße");
    (void) stmt.ExecuteDirect(R"(CREATE TABLE "LiteralText" ("Id" INTEGER, "Value" VARCHAR(30)))");
    (void) stmt.ExecuteDirect(std::format(R"(INSERT INTO "LiteralText" ("Id", "Value") VALUES (1, {}))", literal));

    // Fetched as UTF-16, so the driver decodes the column itself.
    auto cursor = stmt.ExecuteDirect(R"(SELECT "Value" FROM "LiteralText" WHERE "Id" = 1)");
    REQUIRE(cursor.FetchRow());
    CHECK(cursor.GetNullableColumn<std::u16string>(1) == std::optional<std::u16string> { u"Grüße" });
    cursor.ConsumePendingResults();

    // A prepared query's text takes the same way to the driver.
    stmt.Prepare(std::format(R"(SELECT COUNT(*) FROM "LiteralText" WHERE "Value" = {} AND "Id" = ?)", literal));
    auto counted = stmt.Execute(1);
    REQUIRE(counted.FetchRow());
    CHECK(counted.GetColumn<int>(1) == 1);
}

TEST_CASE_METHOD(SqlTestFixture,
                 "A driver diagnostic quoting a non-ASCII identifier reaches the caller intact",
                 "[SqlStatement]")
{
    auto stmt = SqlStatement {};
    auto message = std::string {};
    try
    {
        (void) stmt.ExecuteDirect(R"(SELECT * FROM "Café_absent")");
    }
    catch (SqlException const& failure)
    {
        message = failure.info().message;
    }
    CHECK(message.contains("Café_absent"));
}

// ================================================================================================
// SqlStatement::ExecuteBatch (raw columns) with a parameter status array
// ================================================================================================

namespace
{

/// A non-nullable INTEGER parameter column over @p values, with @p indicators sized alike.
SqlRawColumn RawIntegerColumn(std::span<int32_t const> values, std::span<SQLLEN const> indicators)
{
    return SqlRawColumn {
        .metadata = { .cType = SQL_C_SLONG,
                      .sqlType = SQL_INTEGER,
                      .size = 0,
                      .decimalDigits = 0,
                      .nullable = SQL_NO_NULLS,
                      .bufferLength = sizeof(int32_t) },
        .data = std::as_bytes(values),
        .indicators = indicators,
    };
}

/// Renders a parameter status array for diagnostics, e.g. "0 0 5 7 7".
std::string DescribeParamStatus(std::span<SQLUSMALLINT const> status)
{
    auto text = std::string {};
    for (auto const value: status)
        text += std::format("{} ", value);
    return text;
}

} // namespace

TEST_CASE_METHOD(SqlTestFixture, "raw ExecuteBatch reports per-row status", "[SqlStatement]")
{
    using namespace SqlColumnTypeDefinitions;

    auto stmt = SqlStatement {};
    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.CreateTable("raw_batch_status").PrimaryKey("ID", Integer {}).Column("V", Integer {});
    });
    stmt.Prepare(R"(INSERT INTO "raw_batch_status" ("ID", "V") VALUES (?, ?))");

    {
        // Row 3 duplicates row 1's primary key.
        auto const ids = std::array<int32_t, 5> { 1, 2, 1, 4, 5 };
        auto const values = std::array<int32_t, 5> { 10, 20, 30, 40, 50 };
        auto const indicators = std::array<SQLLEN, 5> { 4, 4, 4, 4, 4 };
        auto const columns = std::array { RawIntegerColumn(ids, indicators), RawIntegerColumn(values, indicators) };

        // Scoped, so that a status pointer the statement kept would dangle (and trip ASan) below.
        auto status = std::array<SQLUSMALLINT, 5> {};
        auto processed = SQLULEN { 0 };
        auto threw = false;
        try
        {
            (void) stmt.ExecuteBatch(columns, ids.size(), status, processed);
        }
        catch (SqlException const&)
        {
            threw = true;
        }
        INFO("param status: " << DescribeParamStatus(status) << "processed: " << processed);
        CHECK((threw || status[2] == SQL_PARAM_ERROR));
        // The duplicate is never reported as inserted, whatever the driver did with later rows.
        CHECK(status[2] != SQL_PARAM_SUCCESS);
        CHECK(status[2] != SQL_PARAM_SUCCESS_WITH_INFO);
    }

    // The status and processed pointers are reset: a plain batch on the same statement works.
    auto const ids = std::array<int32_t, 2> { 10, 11 };
    auto const values = std::array<int32_t, 2> { 100, 110 };
    auto const indicators = std::array<SQLLEN, 2> { 4, 4 };
    auto const columns = std::array { RawIntegerColumn(ids, indicators), RawIntegerColumn(values, indicators) };
    CHECK_NOTHROW((void) stmt.ExecuteBatch(columns, ids.size()));
    CHECK(SqlStatement {}.ExecuteDirectScalar<int64_t>(R"(SELECT COUNT(*) FROM "raw_batch_status" WHERE "ID" >= 10)")
          == std::optional<int64_t> { 2 });
}

TEST_CASE_METHOD(SqlTestFixture,
                 "ExecuteBatchNative column-wise leaves the statement ready for a single Execute",
                 "[SqlStatement]")
{
    auto stmt = SqlStatement {};
    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.CreateTable("native_batch_then_single")
            .PrimaryKey("Id", SqlColumnTypeDefinitions::Integer {})
            .RequiredColumn("Value", SqlColumnTypeDefinitions::Integer {});
    });
    stmt.Prepare(R"(INSERT INTO "native_batch_then_single" ("Id", "Value") VALUES (?, ?))");

    auto const ids = std::array { 1, 2, 3, 4, 5, 6, 7, 8 };
    auto const values = std::array { 10, 20, 30, 40, 50, 60, 70, 80 };
    (void) stmt.ExecuteBatchNative(ids, values);

    // Same prepared statement, no re-Prepare: one parameter set, read from the values bound now.
    (void) stmt.Execute(100, 1000);

    CHECK(SqlStatement {}.ExecuteDirectScalar<int64_t>(R"(SELECT COUNT(*) FROM "native_batch_then_single")")
          == std::optional<int64_t> { 9 });
    CHECK(SqlStatement {}.ExecuteDirectScalar<int64_t>(R"(SELECT "Value" FROM "native_batch_then_single" WHERE "Id" = 100)")
          == std::optional<int64_t> { 1000 });
}

TEST_CASE_METHOD(SqlTestFixture, "ExecuteBatch column-wise of SqlTime round-trips every row", "[SqlStatement]")
{
    using namespace std::chrono_literals;

    auto stmt = SqlStatement {};
    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.CreateTable("batch_time")
            .PrimaryKey("Id", SqlColumnTypeDefinitions::Integer {})
            .RequiredColumn("Value", SqlColumnTypeDefinitions::Time {});
    });
    stmt.Prepare(R"(INSERT INTO "batch_time" ("Id", "Value") VALUES (?, ?))");

    auto const ids = std::array { 1, 2, 3, 4, 5, 6, 7, 8 };
    auto const times = std::array { SqlTime { 0h, 0min, 1s },    SqlTime { 1h, 2min, 3s },    SqlTime { 4h, 5min, 6s },
                                    SqlTime { 7h, 8min, 9s },    SqlTime { 10h, 11min, 12s }, SqlTime { 13h, 14min, 15s },
                                    SqlTime { 16h, 17min, 18s }, SqlTime { 23h, 59min, 59s } };
    (void) stmt.ExecuteBatch(ids, times);

    auto cursor = stmt.ExecuteDirect(R"(SELECT "Value" FROM "batch_time" ORDER BY "Id")"sv);
    auto actual = std::vector<SqlTime> {};
    while (cursor.FetchRow())
        actual.push_back(cursor.GetColumn<SqlTime>(1));
    CHECK(actual == std::vector<SqlTime>(times.begin(), times.end()));
}

namespace
{

/// A row of the by-value accessor batch test.
struct NamedBatchRow
{
    std::int64_t id {};
    std::string name;
};

/// Text that a batch accessor returns by value, bound in place from a buffer that outlives it, which it
/// overwrites when it is destroyed: a row stores its text only if the value lives until the row has executed.
struct OverwrittenWhenDestroyed
{
    /// The text bound, which outlives this value.
    std::string* text {};

    /// Points at @p text, which must outlive this value.
    explicit OverwrittenWhenDestroyed(std::string* text) noexcept:
        text { text }
    {
    }
    OverwrittenWhenDestroyed(OverwrittenWhenDestroyed const&) = default;
    OverwrittenWhenDestroyed(OverwrittenWhenDestroyed&&) = default;
    OverwrittenWhenDestroyed& operator=(OverwrittenWhenDestroyed const&) = default;
    OverwrittenWhenDestroyed& operator=(OverwrittenWhenDestroyed&&) = default;
    ~OverwrittenWhenDestroyed()
    {
        if (text != nullptr)
            std::ranges::fill(*text, '#');
    }
};

} // namespace

namespace Lightweight
{

/// Binds an OverwrittenWhenDestroyed as the text it points at.
template <>
struct SqlDataBinder<OverwrittenWhenDestroyed>
{
    /// Binds the text @p value points at, in place.
    /// @return What the @c std::string binder returns.
    static SQLRETURN InputParameter(SQLHSTMT stmt,
                                    SQLUSMALLINT column,
                                    OverwrittenWhenDestroyed const& value,
                                    SqlDataBinderCallback& cb) noexcept
    {
        return SqlDataBinder<std::string>::InputParameter(stmt, column, *value.text, cb);
    }
};

} // namespace Lightweight

TEST_CASE_METHOD(SqlTestFixture,
                 "ExecuteBatch of rows keeps a value an accessor returns by value alive until its row has executed",
                 "[SqlStatement]")
{
    auto stmt = SqlStatement {};
    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.CreateTable("batch_by_value")
            .PrimaryKey("Id", SqlColumnTypeDefinitions::Bigint {})
            .RequiredColumn("Name", SqlColumnTypeDefinitions::Varchar { .size = 200 });
    });
    stmt.Prepare(R"(INSERT INTO "batch_by_value" ("Id", "Name") VALUES (?, ?))");

    // A by-value accessor takes the row-by-row path: each row binds the accessor's result before executing.
    auto const rowCount = std::int64_t { 50 };
    auto rows = std::vector<NamedBatchRow> {};
    for (auto const id: std::views::iota(std::int64_t { 1 }, rowCount + 1))
        rows.push_back(NamedBatchRow {
            .id = id, .name = std::format("{:0>3} {}", id, std::string(100, static_cast<char>('a' + (id % 26)))) });
    auto const idOf = [](NamedBatchRow const& row) -> std::int64_t const& {
        return row.id;
    };
    (void) stmt.ExecuteBatch(std::span { rows }, idOf, [](NamedBatchRow const& row) -> std::string { return row.name; });

    // A value whose destruction is visible: the text it binds must be stored before it is overwritten.
    auto texts = std::vector<std::string> {};
    for (auto const id: std::views::iota(rowCount + 1, (2 * rowCount) + 1))
        texts.push_back(std::format("{:0>3} {}", id, std::string(100, static_cast<char>('A' + (id % 26)))));
    auto const expectedTexts = texts;
    auto pointers = std::vector<NamedBatchRow> {};
    for (auto const id: std::views::iota(rowCount + 1, (2 * rowCount) + 1))
        pointers.push_back(NamedBatchRow { .id = id, .name = {} });
    (void) stmt.ExecuteBatch(std::span { pointers }, idOf, [&](NamedBatchRow const& row) {
        return OverwrittenWhenDestroyed { &texts[static_cast<std::size_t>(row.id - rowCount - 1)] };
    });

    auto expected = std::vector<std::string> {};
    for (auto const& row: rows)
        expected.push_back(row.name);
    expected.insert(expected.end(), expectedTexts.begin(), expectedTexts.end());
    auto actual = std::vector<std::string> {};
    auto cursor = stmt.ExecuteDirect(R"(SELECT "Name" FROM "batch_by_value" ORDER BY "Id")"sv);
    while (cursor.FetchRow())
        actual.push_back(cursor.GetColumn<std::string>(1));
    CHECK(actual == expected);
}

TEST_CASE_METHOD(SqlTestFixture, "ExecuteBatch column-wise of a UTF-16 fixed string round-trips", "[SqlStatement]")
{
    auto stmt = SqlStatement {};
    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.CreateTable("batch_wide_fixed")
            .PrimaryKey("Id", SqlColumnTypeDefinitions::Integer {})
            .RequiredColumn("Value", SqlColumnTypeDefinitions::NVarchar { .size = 16 });
    });
    stmt.Prepare(R"(INSERT INTO "batch_wide_fixed" ("Id", "Value") VALUES (?, ?))");

    using WideFixed = SqlFixedString<16, char16_t>;
    auto const ids = std::array { 1, 2, 3 };
    auto const values = std::array { WideFixed { u"one" }, WideFixed { u"Grüße" }, WideFixed { u"three" } };
    (void) stmt.ExecuteBatch(ids, values);

    auto cursor = stmt.ExecuteDirect(R"(SELECT "Value" FROM "batch_wide_fixed" ORDER BY "Id")"sv);
    auto actual = std::vector<std::u16string> {};
    while (cursor.FetchRow())
        actual.push_back(cursor.GetColumn<std::u16string>(1));
    CHECK(actual == std::vector<std::u16string> { u"one", u"Grüße", u"three" });
}

TEST_CASE_METHOD(SqlTestFixture, "ExecuteDirectScalar of an empty result set", "[SqlStatement]")
{
    auto stmt = SqlStatement {};
    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.CreateTable("scalar_empty")
            .PrimaryKey("Id", SqlColumnTypeDefinitions::Integer {})
            .RequiredColumn("Value", SqlColumnTypeDefinitions::Integer {});
    });
    auto const query = R"(SELECT "Value" FROM "scalar_empty")"sv;

    // No row means no scalar: the optional overload says so ...
    CHECK(stmt.ExecuteDirectScalar<int>(query) == std::nullopt);
    // ... and the SqlVariant overload, which reports a NULL value as SqlNullValue, reports it as an error.
    CHECK_THROWS_AS(stmt.ExecuteDirectScalar<SqlVariant>(query), SqlException);

    // The statement stays usable, and non-empty results are unaffected.
    CHECK(stmt.ExecuteDirectScalar<int64_t>(R"(SELECT COUNT(*) FROM "scalar_empty")") == std::optional<int64_t> { 0 });
}

TEST_CASE_METHOD(SqlTestFixture, "Narrow string output at the bound buffer's size", "[SqlStatement]")
{
    // A TEXT column has no declared length short enough to size the buffer by, so a std::string is bound
    // with the default length plus the NUL the driver appends. A value exactly as long as that buffer fills
    // it but for its last character: it must be recognised as truncated and read in full. The lengths
    // straddle that boundary.
    constexpr auto BoundSize = detail::DefaultBoundColumnSize + 1;
    auto const lengths = std::array { BoundSize, BoundSize - 1, BoundSize + 1, BoundSize, BoundSize - 2 };
    auto const textOfLength = [](std::size_t length) {
        auto text = std::string {};
        for (auto const i: std::views::iota(std::size_t { 0 }, length))
            text.push_back(static_cast<char>('a' + (i % 26)));
        return text;
    };

    auto stmt = SqlStatement {};
    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.CreateTable("boundary_text")
            .PrimaryKey("Id", SqlColumnTypeDefinitions::Integer {})
            .RequiredColumn("Value", SqlColumnTypeDefinitions::Text {});
    });
    stmt.Prepare(R"(INSERT INTO "boundary_text" ("Id", "Value") VALUES (?, ?))");
    for (auto const index: std::views::iota(std::size_t { 0 }, lengths.size()))
        (void) stmt.Execute(static_cast<int>(index), textOfLength(lengths.at(index)));

    auto const expected = [&] {
        auto texts = std::vector<std::string> {};
        for (auto const length: lengths)
            texts.push_back(textOfLength(length));
        return texts;
    }();
    auto const query = R"(SELECT "Value" FROM "boundary_text" ORDER BY "Id")"sv;
    {
        // Where the driver cannot read a bound column again, the buffer would be the column's declared
        // length: the boundary above holds there only if the driver reports no usable one for TEXT.
        auto cursor = stmt.ExecuteDirect(query);
        REQUIRE(detail::DeclaredBoundColumnSize(stmt.NativeHandle(), 1, SqlOutputColumnLength::Octets) + 1 == BoundSize);
    }

    // Reading the rest of a truncated bound column needs SQLGetData on a bound column, which the MS SQL
    // Server driver does not support (no SQL_GD_BOUND). There the first value of the bound size or longer
    // fails the fetch with 22001 rather than coming back truncated.
    auto const readsBoundColumnsAgain = stmt.Connection().ReadsBoundColumnsAgain();
    auto const expectedBeforeFailure = [&] {
        auto texts = std::vector<std::string> {};
        for (auto const& text: expected)
        {
            if (!readsBoundColumnsAgain && text.size() >= BoundSize)
                break;
            texts.push_back(text);
        }
        return texts;
    }();
    auto const fetchAll = [&](SqlResultCursor& cursor, auto&& bindAndFetch) {
        auto actual = std::vector<std::string> {};
        auto failure = std::string {};
        try
        {
            while (auto value = bindAndFetch(cursor))
                actual.push_back(std::move(*value));
        }
        catch (SqlException const& error)
        {
            failure = error.info().sqlState;
        }
        CHECK(actual == expectedBeforeFailure);
        CHECK(failure == (readsBoundColumnsAgain ? "" : "22001"));
    };

    SECTION("Bound output column")
    {
        auto value = std::string {};
        auto cursor = stmt.ExecuteDirect(query);
        cursor.BindOutputColumns(&value);
        fetchAll(cursor, [&](SqlResultCursor& c) { return c.FetchRow() ? std::optional { value } : std::nullopt; });
    }

    SECTION("Bound output column, a fresh variable per row")
    {
        auto value = std::string {};
        auto cursor = stmt.ExecuteDirect(query);
        fetchAll(cursor, [&](SqlResultCursor& c) {
            value = std::string {};
            c.BindOutputColumns(&value);
            return c.FetchRow() ? std::optional { value } : std::nullopt;
        });
    }

    SECTION("GetColumn")
    {
        auto actual = std::vector<std::string> {};
        auto cursor = stmt.ExecuteDirect(query);
        while (cursor.FetchRow())
            actual.push_back(cursor.GetColumn<std::string>(1));
        CHECK(actual == expected);
    }
}

// ================================================================================================
// Output columns bound once (BindOutputColumns before the fetch loop) stay bound for every row
// ================================================================================================

namespace
{

constexpr int BoundOnceRowCount = 60;

/// Row @p i's text: lengths vary up and down (0..40 code units), row 3 is empty, every fifth row starts
/// with non-ASCII text.
template <typename Char>
std::basic_string<Char> BoundOnceText(int i)
{
    if (i == 3)
        return {};
    auto text = std::basic_string<Char>(static_cast<std::size_t>((i * 7) % 41), static_cast<Char>('a' + (i % 26)));
    if (i % 5 == 0)
    {
        if constexpr (std::same_as<Char, char16_t>)
            text.insert(0, u"Gr\u00FC\u00DFe-");
        else
        {
            auto const prefix = std::u8string_view { u8"Gr\u00FC\u00DFe-" };
            text.insert(0, reinterpret_cast<Char const*>(prefix.data()), prefix.size());
        }
    }
    return text;
}

/// Row @p i's nullable text: NULL on every fourth row, otherwise the next row's text.
template <typename Char>
std::optional<std::basic_string<Char>> BoundOnceMaybeText(int i)
{
    if (i % 4 == 1)
        return std::nullopt;
    return BoundOnceText<Char>(i + 1);
}

/// Row @p i's fixed-capacity text (1..17 bytes, some non-ASCII).
SqlAnsiString<20> BoundOnceFixed(int i)
{
    auto const prefix = i % 6 == 0 ? std::string { "\xC3\xBC" } : std::string {};
    return SqlAnsiString<20> { std::format("{}f{}", prefix, std::string(static_cast<std::size_t>((i * 3) % 15), 'z')) };
}

/// Row @p i's nullable blob: NULL on every fourth row, otherwise 0..29 bytes.
std::optional<SqlBinary> BoundOnceBlob(int i)
{
    if (i % 4 == 2)
        return std::nullopt;
    auto blob = SqlBinary {};
    for (auto const k: std::views::iota(0, (i * 5) % 30))
        blob.push_back(static_cast<std::uint8_t>((i + k) % 251));
    return blob;
}

void CreateBoundOnceTable(SqlStatement& stmt)
{
    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.CreateTable("BoundOnce")
            .PrimaryKey("Id", SqlColumnTypeDefinitions::Integer {})
            .RequiredColumn("Text", SqlColumnTypeDefinitions::NVarchar { .size = 64 })
            .Column("MaybeText", SqlColumnTypeDefinitions::NVarchar { .size = 64 })
            .RequiredColumn("WideText", SqlColumnTypeDefinitions::NVarchar { .size = 64 })
            .Column("MaybeWideText", SqlColumnTypeDefinitions::NVarchar { .size = 64 })
            .RequiredColumn("Fixed", SqlColumnTypeDefinitions::Varchar { .size = 20 })
            .Column("Blob", SqlColumnTypeDefinitions::VarBinary { .size = 64 });
    });
    // Narrow text is written (and read back) narrow, wide text wide: how narrow input lands in a Unicode
    // column is the driver's business (MS SQL Server takes it as the client codepage), not this test's.
    stmt.Prepare(
        R"(INSERT INTO "BoundOnce" ("Id", "Text", "MaybeText", "WideText", "MaybeWideText", "Fixed", "Blob") VALUES (?, ?, ?, ?, ?, ?, ?))");
    for (auto const i: std::views::iota(0, BoundOnceRowCount))
        (void) stmt.Execute(i,
                            BoundOnceText<char>(i),
                            BoundOnceMaybeText<char>(i),
                            BoundOnceText<char16_t>(i),
                            BoundOnceMaybeText<char16_t>(i),
                            BoundOnceFixed(i),
                            BoundOnceBlob(i));
}

/// The two ways a bound-column loop is served: per-row SQLFetch with the binds issued right away
/// (prefetch off), and with the binds deferred to the first fetch (prefetch on, declined for text).
constexpr auto BoundOncePrefetchDepths = std::array { std::size_t { 1 }, PrefetchDepthDefault };

} // namespace

TEST_CASE_METHOD(SqlTestFixture,
                 "BindOutputColumns once: narrow and fixed and optional and binary columns",
                 "[SqlStatement][BindOnce]")
{
    auto stmt = SqlStatement {};
    CreateBoundOnceTable(stmt);

    for (auto const depth: BoundOncePrefetchDepths)
    {
        INFO("prefetch depth " << depth);
        stmt.Connection().SetDefaultPrefetchDepth(depth);

        auto id = 0;
        auto text = std::string {};
        auto maybeText = std::optional<std::string> {};
        auto fixed = SqlAnsiString<20> {};
        auto blob = std::optional<SqlBinary> {};

        auto cursor =
            stmt.ExecuteDirect(R"(SELECT "Id", "Text", "MaybeText", "Fixed", "Blob" FROM "BoundOnce" ORDER BY "Id")"sv);
        cursor.BindOutputColumns(&id, &text, &maybeText, &fixed, &blob);
        auto rows = 0;
        while (cursor.FetchRow())
        {
            INFO("row " << rows);
            CHECK(id == rows);
            CHECK(text == BoundOnceText<char>(rows));
            CHECK(maybeText == BoundOnceMaybeText<char>(rows));
            CHECK(fixed == BoundOnceFixed(rows));
            CHECK((blob == BoundOnceBlob(rows))); // no printer for SqlBinary
            ++rows;
        }
        CHECK(rows == BoundOnceRowCount);
    }
}

TEST_CASE_METHOD(SqlTestFixture, "BindOutputColumns once: UTF-16 and UTF-8 columns", "[SqlStatement][BindOnce]")
{
    auto stmt = SqlStatement {};
    CreateBoundOnceTable(stmt);

    for (auto const depth: BoundOncePrefetchDepths)
    {
        INFO("prefetch depth " << depth);
        stmt.Connection().SetDefaultPrefetchDepth(depth);

        auto wide = std::u16string {};
        auto maybeWide = std::optional<std::u16string> {};
        auto utf8 = std::u8string {};
        auto maybeUtf8 = std::optional<std::u8string> {};

        auto cursor = stmt.ExecuteDirect(
            R"(SELECT "WideText", "MaybeWideText", "WideText", "MaybeWideText" FROM "BoundOnce" ORDER BY "Id")"sv);
        cursor.BindOutputColumns(&wide, &maybeWide, &utf8, &maybeUtf8);
        auto rows = 0;
        while (cursor.FetchRow())
        {
            INFO("row " << rows);
            CHECK(wide == BoundOnceText<char16_t>(rows));
            CHECK(maybeWide == BoundOnceMaybeText<char16_t>(rows));
            CHECK(utf8 == BoundOnceText<char8_t>(rows));
            CHECK((maybeUtf8 == BoundOnceMaybeText<char8_t>(rows))); // no printer for optional<u8string>
            ++rows;
        }
        CHECK(rows == BoundOnceRowCount);
    }
}

TEST_CASE_METHOD(SqlTestFixture,
                 "BindOutputColumns once: values taken out of the bound variables",
                 "[SqlStatement][BindOnce]")
{
    auto stmt = SqlStatement {};
    CreateBoundOnceTable(stmt);

    for (auto const depth: BoundOncePrefetchDepths)
    {
        INFO("prefetch depth " << depth);
        stmt.Connection().SetDefaultPrefetchDepth(depth);

        // Taking each value out leaves the bound variables with fresh storage the column must follow.
        auto text = std::string {};
        auto wide = std::u16string {};
        auto texts = std::vector<std::string> {};
        auto wides = std::vector<std::u16string> {};

        auto cursor = stmt.ExecuteDirect(R"(SELECT "Text", "WideText" FROM "BoundOnce" ORDER BY "Id")"sv);
        cursor.BindOutputColumns(&text, &wide);
        while (cursor.FetchRow())
        {
            texts.emplace_back(std::exchange(text, std::string {}));
            wides.emplace_back(std::exchange(wide, std::u16string {}));
        }

        REQUIRE(std::cmp_equal(texts.size(), BoundOnceRowCount));
        REQUIRE(std::cmp_equal(wides.size(), BoundOnceRowCount));
        for (auto const i: std::views::iota(0, BoundOnceRowCount))
        {
            INFO("row " << i);
            CHECK(texts[static_cast<std::size_t>(i)] == BoundOnceText<char>(i));
            CHECK(wides[static_cast<std::size_t>(i)] == BoundOnceText<char16_t>(i));
        }
    }
}

TEST_CASE_METHOD(SqlTestFixture, "BindOutputColumns once: a prepared statement executed twice", "[SqlStatement][BindOnce]")
{
    auto stmt = SqlStatement {};
    CreateBoundOnceTable(stmt);

    // The column bindings outlive the cursor: closing it (SQLCloseCursor, at the end of the result set
    // or when the cursor goes) does not unbind, so the second execution fetches into them again.
    auto const fetchBoth = [&](auto&& bind, auto&& checkRow) {
        auto rows = 0;
        {
            auto cursor = stmt.Execute(0, BoundOnceRowCount / 2);
            bind(cursor);
            while (cursor.FetchRow())
            {
                INFO("first execution, row " << rows);
                checkRow(rows);
                ++rows;
            }
        }
        {
            auto cursor = stmt.Execute(BoundOnceRowCount / 2, BoundOnceRowCount);
            while (cursor.FetchRow())
            {
                INFO("second execution, row " << rows);
                checkRow(rows);
                ++rows;
            }
        }
        CHECK(rows == BoundOnceRowCount);
    };

    for (auto const depth: BoundOncePrefetchDepths)
    {
        INFO("prefetch depth " << depth);
        stmt.Connection().SetDefaultPrefetchDepth(depth);

        // Text columns: the per-row path, with UTF-16 staging buffers for the UTF-8 strings.
        stmt.Prepare(
            R"(SELECT "Id", "Text", "WideText", "MaybeWideText" FROM "BoundOnce" WHERE "Id" >= ? AND "Id" < ? ORDER BY "Id")");
        auto id = 0;
        auto text = std::string {};
        auto utf8 = std::u8string {};
        auto maybeUtf8 = std::optional<std::u8string> {};
        fetchBoth([&](SqlResultCursor& cursor) { cursor.BindOutputColumns(&id, &text, &utf8, &maybeUtf8); },
                  [&](int row) {
                      CHECK(id == row);
                      CHECK(text == BoundOnceText<char>(row));
                      CHECK(utf8 == BoundOnceText<char8_t>(row));
                      CHECK((maybeUtf8 == BoundOnceMaybeText<char8_t>(row))); // no printer for optional<u8string>
                  });

        // An integer column only: served from the block buffer where the driver can.
        stmt.Prepare(R"(SELECT "Id" FROM "BoundOnce" WHERE "Id" >= ? AND "Id" < ? ORDER BY "Id")");
        auto onlyId = -1;
        fetchBoth([&](SqlResultCursor& cursor) { cursor.BindOutputColumns(&onlyId); },
                  [&](int row) { CHECK(onlyId == row); });
    }
}

namespace
{

/// An output column type whose binder plans a post-process callback and then fails, as a binder whose
/// SQLBindCol fails after it planned its callbacks does.
struct FailingOutputColumn
{
    int postProcessed = 0;
};

} // namespace

template <>
struct Lightweight::SqlDataBinder<FailingOutputColumn>
{
    static SQLRETURN OutputColumn(SQLHSTMT /*stmt*/,
                                  SQLUSMALLINT /*column*/,
                                  FailingOutputColumn* result,
                                  SQLLEN* /*indicator*/,
                                  SqlDataBinderCallback& cb) noexcept
    {
        cb.PlanPostProcessOutputColumn([result] { ++result->postProcessed; });
        return SQL_ERROR;
    }
};

TEST_CASE_METHOD(SqlTestFixture, "A failed output column binding plans no callbacks", "[SqlStatement][BindOnce]")
{
    auto stmt = SqlStatement {};
    CreateBoundOnceTable(stmt);
    stmt.Connection().SetDefaultPrefetchDepth(1); // binds right away, so the binder's failure throws here

    auto failing = FailingOutputColumn {};
    auto id = -1;
    auto cursor = stmt.ExecuteDirect(R"(SELECT "Id", "Fixed" FROM "BoundOnce" ORDER BY "Id")"sv);
    CHECK_THROWS_AS(cursor.BindOutputColumn(2, &failing), SqlException);
    cursor.BindOutputColumn(1, &id); // column 2 stays unbound
    REQUIRE(cursor.FetchRow());
    CHECK(id == 0);
    CHECK(failing.postProcessed == 0);
}

namespace
{

constexpr std::size_t BoundLongColumnSize = 1000;

/// Text lengths around the 255 units an unsized buffer used to get, and up to the column's declared
/// size, between short ones.
constexpr auto BoundLongLengths =
    std::array<std::size_t, 13> { 5, 255, 3, 300, 0, 254, 256, 600, 10, 255, BoundLongColumnSize, 1, 999 };

std::string BoundLongText(std::size_t row)
{
    auto text = std::string(BoundLongLengths[row], static_cast<char>('a' + (row % 26)));
    return text;
}

std::u16string BoundLongWideText(std::size_t row)
{
    auto text = std::u16string(BoundLongLengths[row], static_cast<char16_t>('A' + (row % 26)));
    if (!text.empty())
        text.front() = u'ü';
    return text;
}

std::u8string BoundLongUtf8Text(std::size_t row)
{
    auto const wide = BoundLongWideText(row);
    return ToUtf8(std::u16string_view { wide });
}

SqlBinary BoundLongBlob(std::size_t row)
{
    auto blob = SqlBinary {};
    for (auto const k: std::views::iota(std::size_t { 0 }, BoundLongLengths[row]))
        blob.push_back(static_cast<std::uint8_t>((row + k) % 251));
    return blob;
}

/// NULL on every third row, otherwise up to the full 40 characters.
std::optional<SqlAnsiString<40>> BoundLongMaybeFixed(std::size_t row)
{
    if (row % 3 == 2)
        return std::nullopt;
    return SqlAnsiString<40> { std::string((row * 11) % 41, 'q') };
}

} // namespace

TEST_CASE_METHOD(SqlTestFixture,
                 "BindOutputColumns once: values longer than 255 between short ones",
                 "[SqlStatement][BindOnce]")
{
    auto stmt = SqlStatement {};
    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.CreateTable("BoundLong")
            .PrimaryKey("Id", SqlColumnTypeDefinitions::Integer {})
            .RequiredColumn("Text", SqlColumnTypeDefinitions::Varchar { .size = BoundLongColumnSize })
            .RequiredColumn("WideText", SqlColumnTypeDefinitions::NVarchar { .size = BoundLongColumnSize })
            .Column("Blob", SqlColumnTypeDefinitions::VarBinary { .size = BoundLongColumnSize })
            .Column("MaybeFixed", SqlColumnTypeDefinitions::Varchar { .size = 40 });
    });
    stmt.Prepare(R"(INSERT INTO "BoundLong" ("Id", "Text", "WideText", "Blob", "MaybeFixed") VALUES (?, ?, ?, ?, ?))");
    for (auto const row: std::views::iota(std::size_t { 0 }, BoundLongLengths.size()))
        (void) stmt.Execute(
            static_cast<int>(row), BoundLongText(row), BoundLongWideText(row), BoundLongBlob(row), BoundLongMaybeFixed(row));

    for (auto const depth: BoundOncePrefetchDepths)
    {
        INFO("prefetch depth " << depth);
        stmt.Connection().SetDefaultPrefetchDepth(depth);

        auto id = -1;
        auto text = std::string {};
        auto wide = std::u16string {};
        auto utf8 = std::u8string {};
        auto blob = std::optional<SqlBinary> {};
        auto maybeFixed = std::optional<SqlAnsiString<40>> {};

        auto cursor = stmt.ExecuteDirect(
            R"(SELECT "Id", "Text", "WideText", "WideText", "Blob", "MaybeFixed" FROM "BoundLong" ORDER BY "Id")"sv);
        cursor.BindOutputColumns(&id, &text, &wide, &utf8, &blob, &maybeFixed);
        auto rows = std::size_t { 0 };
        while (cursor.FetchRow())
        {
            INFO("row " << rows << ", length " << BoundLongLengths[rows]);
            CHECK(std::cmp_equal(id, rows));
            CHECK(text == BoundLongText(rows));
            CHECK(wide == BoundLongWideText(rows));
            CHECK((utf8 == BoundLongUtf8Text(rows))); // no printer for u8string
            // An empty VARBINARY may come back as NULL.
            CHECK((blob.value_or(SqlBinary {}) == BoundLongBlob(rows))); // no printer for SqlBinary
            CHECK((maybeFixed == BoundLongMaybeFixed(rows)));            // no printer for optional<SqlAnsiString<N>>
            ++rows;
        }
        CHECK(rows == BoundLongLengths.size());
    }
}

namespace
{

/// Lengths of 300, 5000 and 70000 characters between short ones; a negative length is a NULL row.
constexpr auto BoundHugeLengths = std::array<int, 11> { 5, 300, -1, 5000, 2, 70000, 0, 300, -1, 70000, 7 };

/// Row @p row's text: "Grüße-" on odd rows (non-ASCII, in every client code page the tests run with), then
/// ASCII letters up to the row's length in code points.
template <typename Char>
std::optional<std::basic_string<Char>> BoundHugeText(std::size_t row)
{
    auto const length = BoundHugeLengths[row];
    if (length < 0)
        return std::nullopt;
    auto text = std::u16string {};
    if (row % 2 == 1)
        text = u"Grüße-";
    text.resize((std::max) (text.size(), static_cast<std::size_t>(length)), static_cast<char16_t>('a' + (row % 26)));
    if constexpr (std::same_as<Char, char16_t>)
        return text;
    else
    {
        auto const utf8 = ToUtf8(std::u16string_view { text });
        return std::basic_string<Char>(reinterpret_cast<Char const*>(utf8.data()), utf8.size());
    }
}

} // namespace

TEST_CASE_METHOD(SqlTestFixture,
                 "BindOutputColumns once: values of 5000 and 70000 characters between short ones",
                 "[SqlStatement][BindOnce]")
{
    auto stmt = SqlStatement {};
    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.CreateTable("BoundHuge")
            .PrimaryKey("Id", SqlColumnTypeDefinitions::Integer {})
            .Column("Text", SqlColumnTypeDefinitions::Text {})
            .Column("WideText", SqlColumnTypeDefinitions::NVarchar { .size = 0 });
    });
    stmt.Prepare(R"(INSERT INTO "BoundHuge" ("Id", "Text", "WideText") VALUES (?, ?, ?))");
    for (auto const row: std::views::iota(std::size_t { 0 }, BoundHugeLengths.size()))
        (void) stmt.Execute(static_cast<int>(row), BoundHugeText<char>(row), BoundHugeText<char16_t>(row));

    // A driver that cannot read a bound column again (MS SQL Server) cannot serve a value longer than
    // the bound buffer, which for a MAX column has the default size: the fetch must fail, never return
    // the value truncated.
    auto const readsBoundColumnsAgain = stmt.Connection().ReadsBoundColumnsAgain();
    auto const firstTooLong = static_cast<std::size_t>(std::ranges::distance(
        BoundHugeLengths.begin(), std::ranges::find_if(BoundHugeLengths, [](int length) { return length > 255; })));

    for (auto const depth: BoundOncePrefetchDepths)
    {
        INFO("prefetch depth " << depth);
        stmt.Connection().SetDefaultPrefetchDepth(depth);

        auto text = std::optional<std::string> {};
        auto wide = std::optional<std::u16string> {};
        auto utf8 = std::optional<std::u8string> {};
        auto cursor = stmt.ExecuteDirect(R"(SELECT "Text", "WideText", "WideText" FROM "BoundHuge" ORDER BY "Id")"sv);
        cursor.BindOutputColumns(&text, &wide, &utf8);
        auto rows = std::size_t { 0 };
        while (readsBoundColumnsAgain || rows < firstTooLong)
        {
            if (!cursor.FetchRow())
                break;
            INFO("row " << rows << ", length " << BoundHugeLengths[rows]);
            CHECK(text == BoundHugeText<char>(rows));
            CHECK(wide == BoundHugeText<char16_t>(rows));
            CHECK((utf8 == BoundHugeText<char8_t>(rows))); // no printer for optional<u8string>
            ++rows;
        }
        if (readsBoundColumnsAgain)
            CHECK(rows == BoundHugeLengths.size());
        else
            CHECK_THROWS_AS(cursor.FetchRow(), SqlException);
    }
}

namespace
{

/// A row for the row-major batch of SqlTime.
struct TimeFractionRow
{
    int id {};
    SqlTime time;
};

} // namespace

TEST_CASE_METHOD(SqlTestFixture, "SqlTime: reading leaves no stale fraction", "[SqlTime]")
{
    using namespace std::chrono_literals;

    auto stmt = SqlStatement {};
    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.CreateTable("time_fraction")
            .PrimaryKey("Id", SqlColumnTypeDefinitions::Integer {})
            .RequiredColumn("Value", SqlColumnTypeDefinitions::Time {});
    });
    auto const written = SqlTime { 13h, 59min, 26s, 123456us };

    // Single execute, column-major batch, row-major batch.
    stmt.Prepare(R"(INSERT INTO "time_fraction" ("Id", "Value") VALUES (?, ?))");
    (void) stmt.Execute(1, written);
    (void) stmt.ExecuteBatch(std::array { 2 }, std::array { written });
    auto const rows = std::array { TimeFractionRow { .id = 3, .time = written } };
    (void) stmt.ExecuteBatch(
        std::span { rows },
        [](TimeFractionRow const& row) -> int const& { return row.id; },
        [](TimeFractionRow const& row) -> SqlTime const& { return row.time; });

    // The binders transfer SQL_C_TYPE_TIME, which carries no fraction of a second: the time of day
    // round-trips, the fraction reads back as 0 rather than as whatever the target held before.
    auto const checkRead = [&](SqlTime const& read) {
        CHECK(read.sqlValue.hour == written.sqlValue.hour);
        CHECK(read.sqlValue.minute == written.sqlValue.minute);
        CHECK(read.sqlValue.second == written.sqlValue.second);
        CHECK(read.sqlValue.fraction == 0);
    };
    constexpr auto StaleFraction = SQLUINTEGER { 999'999 };
    auto const query = R"(SELECT "Id", "Value" FROM "time_fraction" ORDER BY "Id")"sv;
    for (auto const depth: { PrefetchDepthDefault, std::size_t { 1 } })
    {
        INFO("prefetch depth " << depth);
        stmt.Connection().SetDefaultPrefetchDepth(depth);
        {
            // A column is read once per row: drivers other than SQLite refuse a second SQLGetData.
            auto cursor = stmt.ExecuteDirect(query);
            auto rowCount = 0;
            while (cursor.FetchRow())
            {
                INFO("row " << ++rowCount);
                checkRead(cursor.GetColumn<SqlTime>(2));
            }
            CHECK(rowCount == 3);
        }
        {
            auto cursor = stmt.ExecuteDirect(query);
            auto rowCount = 0;
            while (cursor.FetchRow())
            {
                INFO("row " << ++rowCount);
                auto target = SqlTime {};
                target.sqlValue.fraction = StaleFraction;
                CHECK(cursor.GetColumn(2, &target));
                checkRead(target);
            }
            CHECK(rowCount == 3);
        }
        {
            auto id = 0;
            auto bound = SqlTime {};
            bound.sqlValue.fraction = StaleFraction;
            auto cursor = stmt.ExecuteDirect(query);
            cursor.BindOutputColumns(&id, &bound);
            while (cursor.FetchRow())
            {
                INFO("bound row " << id);
                checkRead(bound);
            }
        }
    }
}

TEST_CASE_METHOD(SqlTestFixture,
                 "BindOutputColumns once: a long value into a UTF-16 staged or direct string alone",
                 "[SqlStatement][BindOnce]")
{
    // Each type is bound on its own, so no other column's failure hides its outcome: std::u8string,
    // std::u32string (and std::wstring where it is 32-bit) read through a UTF-16 staging buffer,
    // std::u16string (and a 16-bit std::wstring) straight into the string.
    auto stmt = SqlStatement {};
    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.CreateTable("BoundLongAlone")
            .PrimaryKey("Id", SqlColumnTypeDefinitions::Integer {})
            .Column("Value", SqlColumnTypeDefinitions::NVarchar { .size = 0 });
    });
    auto const lengths = std::array { std::size_t { 300 }, std::size_t { 70000 } };
    auto const textOf = [](std::size_t length) {
        auto text = std::u16string(u"ü-");
        text.resize(length, u'w');
        return text;
    };
    stmt.Prepare(R"(INSERT INTO "BoundLongAlone" ("Id", "Value") VALUES (?, ?))");
    for (auto const index: std::views::iota(std::size_t { 0 }, lengths.size()))
        (void) stmt.Execute(static_cast<int>(index), textOf(lengths.at(index)));

    // Where the driver can read a bound column again the whole value arrives; where it cannot (MS SQL
    // Server) the fetch fails with 22001. Never a truncated value, never an abort.
    auto const readsBoundColumnsAgain = stmt.Connection().ReadsBoundColumnsAgain();
    auto const readAlone = [&]<typename T>(std::size_t index, auto const& convert) {
        INFO(std::format("row {} ({} characters)", index, lengths[index]));
        auto value = T {};
        auto failure = std::string {};
        try
        {
            auto cursor = stmt.ExecuteDirect(std::format(R"(SELECT "Value" FROM "BoundLongAlone" WHERE "Id" = {})", index));
            cursor.BindOutputColumns(&value);
            if (!cursor.FetchRow())
                failure = "no row";
        }
        catch (SqlException const& error)
        {
            failure = error.info().sqlState;
        }
        if (readsBoundColumnsAgain)
        {
            CHECK(failure.empty());
            CHECK((convert(value) == textOf(lengths.at(index)))); // no printer for the string types
        }
        else
            CHECK(failure == "22001");
    };
    auto const fromU8 = [](std::u8string const& text) {
        return ToUtf16(std::u8string_view { text });
    };
    auto const fromU32 = [](std::u32string const& text) {
        return ToUtf16(std::u32string_view { text });
    };
    auto const fromOptionalU8 = [](std::optional<std::u8string> const& text) {
        return text ? ToUtf16(std::u8string_view { *text }) : std::u16string {};
    };
    auto const fromU16 = [](std::u16string const& text) {
        return text;
    };
    auto const fromWide = [](std::wstring const& text) {
        if constexpr (sizeof(wchar_t) == 2)
            return std::u16string(reinterpret_cast<char16_t const*>(text.data()), text.size());
        else
            return ToUtf16(std::u32string_view(reinterpret_cast<char32_t const*>(text.data()), text.size()));
    };
    for (auto const index: std::views::iota(std::size_t { 0 }, lengths.size()))
    {
        readAlone.operator()<std::u8string>(index, fromU8);
        readAlone.operator()<std::optional<std::u8string>>(index, fromOptionalU8);
        readAlone.operator()<std::u16string>(index, fromU16);
        readAlone.operator()<std::u32string>(index, fromU32);
        readAlone.operator()<std::wstring>(index, fromWide);
    }
}

TEST_CASE_METHOD(SqlTestFixture,
                 "Bound column: a value longer than its buffer of unknown length is read in full",
                 "[SqlStatement][BindOnce]")
{
    // A driver may report a value longer than the bound buffer as SQL_NO_TOTAL instead of its length. The
    // binders' post-processing then reads it again in full; this drives that step directly with the
    // indicator such a driver leaves, on a fetched row whose columns are not bound.
    constexpr auto ValueLength = std::size_t { 700 };
    auto stmt = SqlStatement {};
    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.CreateTable("BoundNoTotal")
            .PrimaryKey("Id", SqlColumnTypeDefinitions::Integer {})
            .RequiredColumn("Text", SqlColumnTypeDefinitions::Varchar { .size = 1000 })
            .RequiredColumn("WideText", SqlColumnTypeDefinitions::NVarchar { .size = 1000 })
            .RequiredColumn("Blob", SqlColumnTypeDefinitions::VarBinary { .size = 1000 });
    });
    auto text = std::string(ValueLength, 'x');
    for (auto const index: std::views::iota(std::size_t { 0 }, text.size()))
        text[index] = static_cast<char>('a' + (index % 26));
    auto wide = std::u16string(ValueLength, u'y');
    wide.replace(0, 6, u"Grüße-");
    auto blob = SqlBinary(ValueLength, std::uint8_t { 0 });
    for (auto const index: std::views::iota(std::size_t { 0 }, blob.size()))
        blob[index] = static_cast<std::uint8_t>(index % 251);
    stmt.Prepare(R"(INSERT INTO "BoundNoTotal" ("Id", "Text", "WideText", "Blob") VALUES (?, ?, ?, ?))");
    (void) stmt.Execute(1, text, wide, blob);

    auto cursor = stmt.ExecuteDirect(R"(SELECT "Text", "WideText", "WideText", "Blob" FROM "BoundNoTotal")"sv);
    REQUIRE(cursor.FetchRow());
    auto* const hStmt = stmt.NativeHandle();

    // Each read starts from what a bound buffer of the default size holds; the columns are read in order.
    auto narrow = std::string(detail::DefaultBoundColumnSize, '\0');
    auto indicator = SQLLEN { SQL_NO_TOTAL };
    SqlDataBinder<std::string>::PostProcessOutputColumn(hStmt, 1, &narrow, &indicator);
    CHECK(narrow == text);
    CHECK(std::cmp_equal(indicator, text.size()));

    auto staging = std::u16string(detail::DefaultBoundColumnSize + 1, u'\0');
    indicator = SQL_NO_TOTAL;
    CHECK(detail::ReadBoundUtf16Staging(hStmt, 2, staging, &indicator) == wide);

    auto direct = std::u16string(detail::DefaultBoundColumnSize, u'\0');
    indicator = SQL_NO_TOTAL;
    auto const directLength =
        detail::ReadBoundValueOfUnknownLength<SqlBasicStringOperations<std::u16string>, sizeof(char16_t), 1>(
            hStmt, 3, SQL_C_WCHAR, &direct, &indicator);
    direct.resize(directLength);
    CHECK(direct == wide);

    auto binary = SqlBinary(detail::DefaultBoundColumnSize, std::uint8_t { 0 });
    indicator = SQL_NO_TOTAL;
    binary.resize(detail::ReadBoundValueOfUnknownLength<detail::ContainerBufferOperations, 1, 0>(
        hStmt, 4, SQL_C_BINARY, &binary, &indicator));
    CHECK((binary == blob)); // no printer for SqlBinary

    // A value the driver cannot read again fails the fetch rather than come back truncated.
    auto unreadable = std::string(detail::DefaultBoundColumnSize, '\0');
    indicator = SQL_NO_TOTAL;
    try
    {
        SqlDataBinder<std::string>::PostProcessOutputColumn(hStmt, 9, &unreadable, &indicator);
        FAIL("the read of a column that does not exist succeeded");
    }
    catch (SqlException const& error)
    {
        CHECK(error.info().sqlState == "22001");
    }
}

namespace
{

/// One scripted SQLGetData answer: its return code, the indicator it reports, and whether it fills the target.
struct ScriptedChunk
{
    SQLRETURN returnCode;
    SQLLEN indicator;
};

/// A getData callable for ReadBoundValueOfUnknownLength that answers from @p script, in order, and fills
/// each target it is given with 'x'.
auto ScriptedGetData(std::vector<ScriptedChunk> const& script, std::size_t& calls)
{
    return [&script, &calls](SQLPOINTER target, SQLLEN length, SQLLEN* indicator) -> SQLRETURN {
        auto const& chunk = script.at(std::min(calls, script.size() - 1));
        ++calls;
        std::memset(target, 'x', static_cast<std::size_t>(length));
        *indicator = chunk.indicator;
        return chunk.returnCode;
    };
}

/// The SQLSTATE @p read throws, or "none".
template <typename Read>
std::string SqlStateOf(Read const& read)
{
    try
    {
        read();
        return "none";
    }
    catch (SqlException const& error)
    {
        return error.info().sqlState;
    }
}

} // namespace

TEST_CASE_METHOD(SqlTestFixture,
                 "Bound column: the read of a value of unknown length checks what the driver reports",
                 "[SqlStatement][BindOnce]")
{
    auto stmt = SqlStatement {};
    auto* const hStmt = stmt.NativeHandle();
    using Ops = detail::ContainerBufferOperations;
    auto const read = [&](std::vector<ScriptedChunk> const& script,
                          std::size_t maxBytes = detail::MaxUnknownLengthValueBytes) {
        auto calls = std::size_t { 0 };
        auto buffer = std::vector<std::uint8_t>(detail::DefaultBoundColumnSize);
        auto indicator = SQLLEN { SQL_NO_TOTAL };
        auto const length = detail::ReadBoundValueOfUnknownLength<Ops, 1, 0>(
            hStmt, 1, SQL_C_BINARY, &buffer, &indicator, ScriptedGetData(script, calls), maxBytes);
        CHECK(std::cmp_equal(indicator, length));
        return length;
    };
    constexpr auto Full = SQLLEN { SQL_NO_TOTAL };

    // Exactly one buffer's worth.
    CHECK(read({ { SQL_SUCCESS, 255 } }) == 255);
    // Two full chunks of unknown length (the buffer doubles after each), then the rest.
    CHECK(read({ { SQL_SUCCESS_WITH_INFO, Full }, { SQL_SUCCESS_WITH_INFO, Full }, { SQL_SUCCESS, 10 } }) == 255 + 255 + 10);
    // All of it in full chunks, the end reported by SQL_NO_DATA.
    CHECK(read({ { SQL_SUCCESS_WITH_INFO, Full }, { SQL_NO_DATA, 0 } }) == 255);

    // A failure half-way, nothing to read at all, a negative or an impossible length, endless growth.
    CHECK(SqlStateOf([&] { (void) read({ { SQL_SUCCESS_WITH_INFO, Full }, { SQL_ERROR, 0 } }); }) == "22001");
    CHECK(SqlStateOf([&] { (void) read({ { SQL_NO_DATA, 0 } }); }) == "22001");
    CHECK(SqlStateOf([&] { (void) read({ { SQL_SUCCESS, -5 } }); }) == "HY000");
    CHECK(SqlStateOf([&] { (void) read({ { SQL_SUCCESS, 256 } }); }) == "HY000");
    CHECK(SqlStateOf([&] { (void) read({ { SQL_SUCCESS_WITH_INFO, Full } }, 4096); }) == "22001");

    // A NULL row, as a column unbound after a failed re-bind reads it: no value, and the indicator stays NULL.
    auto calls = std::size_t { 0 };
    auto buffer = std::vector<std::uint8_t>(detail::DefaultBoundColumnSize);
    auto indicator = SQLLEN { SQL_NO_TOTAL };
    auto const nullScript = std::vector<ScriptedChunk> { { .returnCode = SQL_SUCCESS, .indicator = SQL_NULL_DATA } };
    CHECK(detail::ReadBoundValueOfUnknownLength<Ops, 1, 0>(
              hStmt, 1, SQL_C_BINARY, &buffer, &indicator, ScriptedGetData(nullScript, calls))
          == 0);
    CHECK(indicator == SQL_NULL_DATA);
}

TEST_CASE_METHOD(SqlTestFixture,
                 "Bound column: a failed read of a staged value of unknown length leaves the column bound to live storage",
                 "[SqlStatement][BindOnce]")
{
    auto stmt = SqlStatement {};
    auto cursor = stmt.ExecuteDirect(
        R"(SELECT "v" FROM (SELECT 1 AS "k", 'first' AS "v" UNION ALL SELECT 2, 'second') "t" ORDER BY "k")"sv);
    REQUIRE(cursor.FetchRow());
    auto* const hStmt = stmt.NativeHandle();

    // The read grows the staging buffer (moving it), then fails.
    auto const script = std::vector<ScriptedChunk> { { .returnCode = SQL_SUCCESS_WITH_INFO, .indicator = SQL_NO_TOTAL },
                                                     { .returnCode = SQL_ERROR, .indicator = 0 } };
    auto calls = std::size_t { 0 };
    auto staging = std::u16string(detail::DefaultBoundColumnSize + 1, u'\0');
    auto const* const before = staging.data();
    auto indicator = SQLLEN { SQL_NO_TOTAL };
    CHECK(SqlStateOf(
              [&] { (void) detail::ReadBoundUtf16Staging(hStmt, 1, staging, &indicator, ScriptedGetData(script, calls)); })
          == "22001");
    CHECK(staging.data() != before);

    // The next row lands where the staging buffer is now, not in the storage it left.
    REQUIRE(cursor.FetchRow());
    REQUIRE(indicator >= 0);
    CHECK(std::u16string_view { staging.data(), static_cast<std::size_t>(indicator) / sizeof(char16_t) } == u"second");
    REQUIRE(SQL_SUCCEEDED(SQLFreeStmt(hStmt, SQL_UNBIND))); // the staging buffer goes before the statement
}

TEST_CASE_METHOD(SqlTestFixture,
                 "Bound column: a staging buffer the driver refuses to bind again is unbound, and the read fails",
                 "[SqlStatement][BindOnce]")
{
    auto stmt = SqlStatement {};
    auto cursor = stmt.ExecuteDirect(
        R"(SELECT "v" FROM (SELECT 1 AS "k", 'first' AS "v" UNION ALL SELECT 2, 'second') "t" ORDER BY "k")"sv);
    REQUIRE(cursor.FetchRow());
    auto* const hStmt = stmt.NativeHandle();

    // Column 0 is the bookmark column, and bookmarks are off: every driver refuses to bind it, so the read
    // of the value succeeds and binding the staging buffer to that column again fails.
    constexpr auto RefusedColumn = SQLUSMALLINT { 0 };
    auto calls = std::size_t { 0 };
    auto staging = std::u16string(detail::DefaultBoundColumnSize + 1, u'\0');

    SECTION("unknown length")
    {
        auto const script = std::vector<ScriptedChunk> { { .returnCode = SQL_SUCCESS, .indicator = 10 } };
        auto indicator = SQLLEN { SQL_NO_TOTAL };
        CHECK(SqlStateOf([&] {
                  (void) detail::ReadBoundUtf16Staging(
                      hStmt, RefusedColumn, staging, &indicator, ScriptedGetData(script, calls));
              })
              != "none");
        CHECK(indicator == SQL_NO_TOTAL); // the next row is read again, and bound again
    }

    SECTION("known length")
    {
        auto const totalBytes = static_cast<SQLLEN>((staging.size() + 10) * sizeof(char16_t));
        auto const script = std::vector<ScriptedChunk> { { .returnCode = SQL_SUCCESS, .indicator = totalBytes } };
        auto indicator = totalBytes;
        CHECK(SqlStateOf([&] {
                  (void) detail::ReadBoundUtf16Staging(
                      hStmt, RefusedColumn, staging, &indicator, ScriptedGetData(script, calls));
              })
              != "none");
        CHECK(indicator == SQL_NO_TOTAL);
    }

    // The read fails as well: its 22001 is reported, not the refused bind's error, and the column is unbound.
    SECTION("known length, the read fails")
    {
        auto const totalBytes = static_cast<SQLLEN>((staging.size() + 10) * sizeof(char16_t));
        auto const script = std::vector<ScriptedChunk> { { .returnCode = SQL_ERROR, .indicator = 0 } };
        auto indicator = totalBytes;
        CHECK(SqlStateOf([&] {
                  (void) detail::ReadBoundUtf16Staging(
                      hStmt, RefusedColumn, staging, &indicator, ScriptedGetData(script, calls));
              })
              == "22001");
        CHECK(indicator == SQL_NO_TOTAL);
    }

    SECTION("unknown length, the read fails")
    {
        auto const script = std::vector<ScriptedChunk> {
            { .returnCode = SQL_SUCCESS_WITH_INFO, .indicator = SQL_NO_TOTAL },
            { .returnCode = SQL_ERROR, .indicator = 0 },
        };
        auto indicator = SQLLEN { SQL_NO_TOTAL };
        CHECK(SqlStateOf([&] {
                  (void) detail::ReadBoundUtf16Staging(
                      hStmt, RefusedColumn, staging, &indicator, ScriptedGetData(script, calls));
              })
              == "22001");
        CHECK(indicator == SQL_NO_TOTAL);
    }
}

TEST_CASE_METHOD(SqlTestFixture,
                 "Bound column: a re-bind of moved storage that fails is tried again on the next fetch",
                 "[SqlStatement][BindOnce]")
{
    auto stmt = SqlStatement {};
    auto cursor = stmt.ExecuteDirect(R"(SELECT 'first')"sv);
    auto* const hStmt = stmt.NativeHandle();
    using Ops = SqlBasicStringOperations<std::string>;

    // The container's storage moved since the column was bound (boundData is elsewhere): the pre-fetch step
    // binds it again. The first bind fails (column 0, the bookmark column, with bookmarks off).
    auto value = std::string(16, '\0');
    auto indicator = SQLLEN {};
    auto const elsewhere = std::string(16, '\0');
    auto step = SqlOutputColumnPreFetch {
        .run = nullptr,
        .target = &value,
        .indicator = &indicator,
        .boundData = elsewhere.data(),
        .boundSize = value.size(),
        .column = 0,
        .cType = SQL_C_CHAR,
    };
    CHECK_FALSE(SQL_SUCCEEDED((detail::RestoreBoundBuffer<Ops, std::string, 1, 1>(step, hStmt))));
    CHECK(step.boundData == elsewhere.data()); // not recorded as bound

    // The next fetch's step binds it again (here to the real column) instead of trusting the failed bind.
    step.column = 1;
    REQUIRE(SQL_SUCCEEDED((detail::RestoreBoundBuffer<Ops, std::string, 1, 1>(step, hStmt))));
    CHECK(step.boundData == value.data());
    REQUIRE(SQL_SUCCEEDED(SQLFetch(hStmt)));
    REQUIRE(indicator == 5);
    CHECK(std::string_view { value.data(), 5 } == "first");
    REQUIRE(SQL_SUCCEEDED(SQLFreeStmt(hStmt, SQL_UNBIND))); // the value goes before the statement
}

namespace
{
/// The distinct input indicator and staging buffer slots a SlotProbe parameter was handed.
struct ParameterSlotRecorder
{
    std::vector<SQLLEN const*> indicators;
    std::vector<std::byte const*> stagingBuffers;

    void Record(SQLLEN const* indicator, std::byte const* stagingBuffer)
    {
        if (!std::ranges::contains(indicators, indicator))
            indicators.push_back(indicator);
        if (!std::ranges::contains(stagingBuffers, stagingBuffer))
            stagingBuffers.push_back(stagingBuffer);
    }
};

/// An integer parameter whose binder asks the statement for one indicator and one staging slot, and
/// records which ones it got: a statement that hands out new slots per execute shows new addresses.
struct SlotProbe
{
    int value {};
    ParameterSlotRecorder* recorder {};
};
} // namespace

template <>
struct Lightweight::SqlDataBinder<SlotProbe>
{
    static SQLRETURN InputParameter(SQLHSTMT hStmt,
                                    SQLUSMALLINT column,
                                    SlotProbe const& value,
                                    SqlDataBinderCallback& cb) noexcept
    {
        auto const* const indicator = cb.ProvideInputIndicator();
        auto const* const stagingBuffer = cb.ProvideBatchStagingBuffer(sizeof(value.value));
        value.recorder->Record(indicator, stagingBuffer);
        return SqlDataBinder<int>::InputParameter(hStmt, column, value.value, cb);
    }

    static std::string Inspect(SlotProbe const& value) noexcept
    {
        return std::to_string(value.value);
    }
};

TEST_CASE_METHOD(SqlTestFixture,
                 "Repeated Execute of one prepared statement reuses its parameter slots",
                 "[SqlStatement][ParameterSlots]")
{
    using namespace std::chrono_literals;

    auto stmt = SqlStatement {};
    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.CreateTable("ParameterSlots")
            .PrimaryKey("Id", SqlColumnTypeDefinitions::Integer {})
            .RequiredColumn("D", SqlColumnTypeDefinitions::Date {})
            .RequiredColumn("DT", SqlColumnTypeDefinitions::DateTime {})
            .RequiredColumn("G", SqlColumnTypeDefinitions::Guid {})
            .RequiredColumn("N", SqlColumnTypeDefinitions::Decimal { .precision = 18, .scale = 4 })
            .RequiredColumn("S", SqlColumnTypeDefinitions::Varchar { .size = 40 })
            .RequiredColumn("U", SqlColumnTypeDefinitions::NVarchar { .size = 40 })
            .RequiredColumn("P", SqlColumnTypeDefinitions::Integer {});
    });

    struct Row
    {
        int id;
        SqlDate date;
        SqlDateTime dateTime;
        SqlGuid guid;
        SqlDynamicNumeric number;
        std::string text;
        std::u16string wideText;
    };
    // Values that differ from row to row, so a parameter bound to another one's slot shows.
    auto const rowFor = [](int id) {
        auto guid = SqlGuid {};
        std::ranges::fill(guid.data, static_cast<std::uint8_t>(id % 251));
        guid.data[0] = static_cast<std::uint8_t>(id / 251);
        auto const day = std::chrono::year_month_day { std::chrono::sys_days { std::chrono::year { 2026 } / 1 / 1 }
                                                       + std::chrono::days { id % 365 } };
        return Row {
            .id = id,
            .date = SqlDate { day },
            .dateTime =
                SqlDateTime { day,
                              std::chrono::hh_mm_ss<SqlDateTime::duration_type> { std::chrono::seconds { id % 86'400 } } },
            .guid = guid,
            .number = SqlDynamicNumeric { .unscaledValue = (id * 12'345) - 6'000'000, .precision = 18, .scale = 4 },
            .text = std::format("text {}", id),
            .wideText = std::u16string(static_cast<std::size_t>(1 + (id % 30)), u'ä'),
        };
    };

    constexpr auto SingleRowExecutes = 1000;
    constexpr auto SoftBatchRows = 50;
    auto recorder = ParameterSlotRecorder {};

    stmt.Prepare(
        R"(INSERT INTO "ParameterSlots" ("Id", "D", "DT", "G", "N", "S", "U", "P") VALUES (?, ?, ?, ?, ?, ?, ?, ?))");
    for (auto const id: std::views::iota(1, SingleRowExecutes + 1))
    {
        auto const row = rowFor(id);
        (void) stmt.Execute(row.id,
                            row.date,
                            row.dateTime,
                            row.guid,
                            row.number,
                            row.text,
                            row.wideText,
                            SlotProbe { .value = id, .recorder = &recorder });
    }
    // Every execute after the first handed the probe the slots of the one before: one each, not 1000.
    CHECK(recorder.indicators.size() == 1);
    CHECK(recorder.stagingBuffers.size() == 1);

    // The soft batch binds and executes row by row: its rows reuse the slots as well.
    auto batch = std::vector<Row> {};
    auto probes = std::vector<SlotProbe> {};
    for (auto const id: std::views::iota(SingleRowExecutes + 1, SingleRowExecutes + SoftBatchRows + 1))
    {
        batch.push_back(rowFor(id));
        probes.push_back(SlotProbe { .value = id, .recorder = &recorder });
    }
    auto const column = [&](auto member) {
        auto values = std::vector<std::remove_cvref_t<decltype(batch.front().*member)>> {};
        values.reserve(batch.size());
        for (auto const& row: batch)
            values.push_back(row.*member);
        return values;
    };
    (void) stmt.ExecuteBatchSoft(column(&Row::id),
                                 column(&Row::date),
                                 column(&Row::dateTime),
                                 column(&Row::guid),
                                 column(&Row::number),
                                 column(&Row::text),
                                 column(&Row::wideText),
                                 probes);
    CHECK(recorder.indicators.size() == 1);
    CHECK(recorder.stagingBuffers.size() == 1);

    // A query run in between, as DataMapper's Create() runs one for LastInsertId(), keeps the slots, and the
    // Prepare() after it hands them back rather than freeing them: the next round of the insert takes the same ones.
    CHECK(stmt.ExecuteDirectScalar<int>("SELECT 1") == std::optional { 1 });
    stmt.Prepare(
        R"(INSERT INTO "ParameterSlots" ("Id", "D", "DT", "G", "N", "S", "U", "P") VALUES (?, ?, ?, ?, ?, ?, ?, ?))");
    {
        auto const row = rowFor(SingleRowExecutes + SoftBatchRows + 1);
        (void) stmt.Execute(row.id,
                            row.date,
                            row.dateTime,
                            row.guid,
                            row.number,
                            row.text,
                            row.wideText,
                            SlotProbe { .value = row.id, .recorder = &recorder });
    }
    CHECK(recorder.indicators.size() == 1);
    CHECK(recorder.stagingBuffers.size() == 1);

    // Every row round-trips: no parameter read another one's slot. One column read per statement, as
    // drivers other than SQLite refuse to read a column twice or out of order.
    auto cursor =
        stmt.ExecuteDirect(R"(SELECT "Id", "D", "DT", "G", "N", "S", "U", "P" FROM "ParameterSlots" ORDER BY "Id")");
    auto rowCount = 0;
    while (cursor.FetchRow())
    {
        auto const expected = rowFor(++rowCount);
        INFO("row " << rowCount);
        CHECK(cursor.GetColumn<int>(1) == expected.id);
        CHECK(cursor.GetColumn<SqlDate>(2) == expected.date);
        CHECK(std::format("{}", cursor.GetColumn<SqlDateTime>(3)) == std::format("{}", expected.dateTime));
        CHECK(cursor.GetColumn<SqlGuid>(4) == expected.guid);
        CHECK(cursor.GetColumn<SqlDynamicNumeric>(5) == expected.number);
        CHECK(cursor.GetColumn<std::string>(6) == expected.text);
        CHECK((cursor.GetColumn<std::u16string>(7) == expected.wideText));
        CHECK(cursor.GetColumn<int>(8) == expected.id);
    }
    CHECK(rowCount == SingleRowExecutes + SoftBatchRows + 1);
}

namespace
{
/// A row of the hand-bound parameter test's row-wise fetch: a nullable column, so the fetch takes
/// row-strided indicator storage of its own.
struct HandBoundFetchRow
{
    std::optional<std::int64_t> id;
};
} // namespace

TEST_CASE_METHOD(SqlTestFixture,
                 "A row-wise fetch leaves hand-bound parameters' staging alone for the next execute",
                 "[SqlStatement][ParameterSlots]")
{
    auto stmt = SqlStatement {};
    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.CreateTable("HandBoundStaging")
            .PrimaryKey("Id", SqlColumnTypeDefinitions::Integer {})
            .RequiredColumn("G", SqlColumnTypeDefinitions::Guid {})
            .RequiredColumn("N", SqlColumnTypeDefinitions::Decimal { .precision = 18, .scale = 4 });
    });
    auto const guid = SqlGuid::UnsafeParse("1E772AED-3E73-4C72-8684-5DFFAA17330E");
    auto const number = SqlDynamicNumeric { .unscaledValue = 1'234'567, .precision = 18, .scale = 4 };
    stmt.Prepare(R"(INSERT INTO "HandBoundStaging" ("Id", "G", "N") VALUES (?, ?, ?))");
    (void) stmt.Execute(1, guid, number);
    (void) stmt.Execute(2, SqlGuid::UnsafeParse("00000000-0000-0000-0000-000000000001"), number);

    // The GUID and the numeric are staged by their binders; bound by hand, they are read at every Execute().
    stmt.Prepare(R"(SELECT "Id" FROM "HandBoundStaging" WHERE "G" = ? AND "N" = ?)");
    stmt.BindInputParameter(1, guid);
    stmt.BindInputParameter(2, number);
    auto const fetchIds = [&] {
        auto rows = std::vector<HandBoundFetchRow> {};
        auto cursor = stmt.Execute();
        cursor.FetchAllRowWise(rows, 8, [](HandBoundFetchRow& row) -> std::optional<std::int64_t>& { return row.id; });
        auto ids = std::vector<std::int64_t> {};
        for (auto const& row: rows)
            ids.push_back(row.id.value_or(-1));
        return ids;
    };
    CHECK(fetchIds() == std::vector<std::int64_t> { 1 });

    // Memory the fetch may have freed is taken and overwritten, as any later allocation could.
    auto scribbles = std::vector<std::vector<char>> {};
    for (auto const size: std::views::iota(std::size_t { 1 }, std::size_t { 600 }))
        scribbles.emplace_back(size, 'x');

    // The same bindings executed again: the parameters still read the GUID and the numeric.
    CHECK(fetchIds() == std::vector<std::int64_t> { 1 });
}

TEST_CASE_METHOD(SqlTestFixture,
                 "ExecuteDirect keeps the slots of parameters bound by hand, which stay bound",
                 "[SqlStatement]")
{
    // Bound by hand, parameter 1 stays bound across both ExecuteDirect() calls while only parameter 2 is bound
    // again: had the second bind taken parameter 1's slot, parameter 1 would read 8 bytes of a 3-byte value.
    auto stmt = SqlStatement {};
    auto const first = std::string { "abc" };
    auto const second = std::string { "defgh" };
    auto const longer = std::string { "12345678" };
    auto const read = [&] {
        auto cursor = stmt.ExecuteDirect("SELECT ?, ?");
        REQUIRE(cursor.FetchRow());
        auto one = cursor.GetColumn<std::string>(1);
        auto two = cursor.GetColumn<std::string>(2);
        return std::pair { std::move(one), std::move(two) };
    };
    stmt.BindInputParameter(1, first);
    stmt.BindInputParameter(2, second);
    CHECK(read() == std::pair { first, second });
    stmt.BindInputParameter(2, longer);
    CHECK(read() == std::pair { first, longer });
}
