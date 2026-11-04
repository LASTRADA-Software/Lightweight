// SPDX-License-Identifier: Apache-2.0

#include "Utils.hpp"

#include <Lightweight/DataBinder/SqlDateTime.hpp>
#include <Lightweight/Lightweight.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <format>
#include <optional>
#include <span>

using namespace std::chrono_literals;
using namespace Lightweight;

// Runs on every SQL Server driver; the legacy in-box driver (SQLSRV32.DLL) is the one these cases guard.

namespace
{

/// Creates the table @p tableName with an integer column "id" and a nullable DATETIME column "at".
void CreateDateTimeTable(SqlStatement& stmt, std::string_view tableName)
{
    stmt.MigrateDirect([tableName](auto& migration) {
        migration.CreateTable(tableName)
            .RequiredColumn("id", SqlColumnTypeDefinitions::Integer {})
            .Column("at", SqlColumnTypeDefinitions::DateTime {});
    });
}

/// Returns midnight of the given day in 2026.
SqlDateTime MakeDay2026(unsigned month, unsigned day)
{
    return SqlDateTime { std::chrono::year { 2026 }, std::chrono::month { month }, std::chrono::day { day }, 0h, 0min, 0s };
}

/// Inserts the three rows (1, 2026-01-15), (2, 2026-02-15) and (3, 2026-03-15) into @p tableName.
void InsertThreeMonths(SqlStatement& stmt, std::string_view tableName)
{
    stmt.Prepare(std::format(R"(INSERT INTO "{}" ("id", "at") VALUES (?, ?))", tableName));
    (void) stmt.Execute(1, MakeDay2026(1, 15));
    (void) stmt.Execute(2, MakeDay2026(2, 15));
    (void) stmt.Execute(3, MakeDay2026(3, 15));
}

/// The rows a batch insert writes and reads back.
struct DateTimeRow
{
    int id {};
    SqlDateTime at;
};

/// A batch row whose date-time may be NULL.
struct OptionalDateTimeRow
{
    int id {};
    std::optional<SqlDateTime> at;
};

/// Checks that @p tableName holds exactly @p expectedRows, compared to the second.
void CheckRowsReadBack(SqlStatement& stmt, std::string_view tableName, std::span<DateTimeRow const> expectedRows)
{
    CHECK(stmt.ExecuteDirectScalar<int>(std::format(R"(SELECT COUNT(*) FROM "{}")", tableName)).value_or(-1)
          == static_cast<int>(expectedRows.size()));

    auto cursor = stmt.ExecuteDirect(std::format(R"(SELECT "id", "at" FROM "{}" ORDER BY "id")", tableName));
    for (auto const& expected: expectedRows)
    {
        REQUIRE(cursor.FetchRow());
        CHECK(cursor.GetColumn<int>(1) == expected.id);
        auto const actual = cursor.GetColumn<SqlDateTime>(2);
        CHECK(actual.sqlValue.year == expected.at.sqlValue.year);
        CHECK(actual.sqlValue.month == expected.at.sqlValue.month);
        CHECK(actual.sqlValue.day == expected.at.sqlValue.day);
        CHECK(actual.sqlValue.hour == expected.at.sqlValue.hour);
        CHECK(actual.sqlValue.minute == expected.at.sqlValue.minute);
        CHECK(actual.sqlValue.second == expected.at.sqlValue.second);
    }
    CHECK_FALSE(cursor.FetchRow());
}

} // namespace

TEST_CASE_METHOD(SqlTestFixture, "DateTime parameter after another parameter", "[LegacySqlServerDriver]")
{
    auto stmt = SqlStatement {};
    if (stmt.Connection().ServerType() != SqlServerType::MICROSOFT_SQL)
        return;

    CreateDateTimeTable(stmt, "AfterParameter");
    InsertThreeMonths(stmt, "AfterParameter");

    stmt.Prepare(R"(SELECT COUNT(*) FROM "AfterParameter" WHERE "id" > ? AND "at" >= ?)");
    auto cursor = stmt.Execute(0, MakeDay2026(2, 1));
    REQUIRE(cursor.FetchRow());
    CHECK(cursor.GetColumn<int>(1) == 2);
}

TEST_CASE_METHOD(SqlTestFixture, "DateTime parameter inside a sub-select", "[LegacySqlServerDriver]")
{
    auto stmt = SqlStatement {};
    if (stmt.Connection().ServerType() != SqlServerType::MICROSOFT_SQL)
        return;

    CreateDateTimeTable(stmt, "SubSelect");
    InsertThreeMonths(stmt, "SubSelect");

    stmt.Prepare(R"(SELECT COUNT(*) FROM "SubSelect" WHERE "id" IN )"
                 R"((SELECT "id" FROM "SubSelect" WHERE "id" > ? AND "at" <= ?))");
    auto const endOfFebruary =
        SqlDateTime { std::chrono::year { 2026 }, std::chrono::month { 2 }, std::chrono::day { 28 }, 23h, 59min, 59s };
    auto cursor = stmt.Execute(0, endOfFebruary);
    REQUIRE(cursor.FetchRow());
    CHECK(cursor.GetColumn<int>(1) == 2);
}

TEST_CASE_METHOD(SqlTestFixture, "DateTime with sub-millisecond fraction binds", "[LegacySqlServerDriver]")
{
    auto stmt = SqlStatement {};
    if (stmt.Connection().ServerType() != SqlServerType::MICROSOFT_SQL)
        return;

    CreateDateTimeTable(stmt, "SubMillisecond");

    auto const value = SqlDateTime { std::chrono::year { 2026 },
                                     std::chrono::month { 4 },
                                     std::chrono::day { 1 },
                                     10h,
                                     20min,
                                     30s,
                                     std::chrono::nanoseconds { 123'456'700 } };
    stmt.Prepare(R"(INSERT INTO "SubMillisecond" ("id", "at") VALUES (?, ?))");
    REQUIRE_NOTHROW((void) stmt.Execute(1, value));

    auto const fetched = stmt.ExecuteDirectScalar<SqlDateTime>(R"(SELECT "at" FROM "SubMillisecond")");
    REQUIRE(fetched.has_value());
    if (fetched.has_value())
    {
        CHECK(fetched->sqlValue.year == 2026);
        CHECK(fetched->sqlValue.month == 4);
        CHECK(fetched->sqlValue.day == 1);
        CHECK(fetched->sqlValue.hour == 10);
        CHECK(fetched->sqlValue.minute == 20);
        CHECK(fetched->sqlValue.second == 30);
        CHECK(fetched->sqlValue.fraction / 1'000'000 <= 124);
    }
}

TEST_CASE_METHOD(SqlTestFixture, "DateTime batch insert", "[LegacySqlServerDriver]")
{
    auto stmt = SqlStatement {};
    if (stmt.Connection().ServerType() != SqlServerType::MICROSOFT_SQL)
        return;

    CreateDateTimeTable(stmt, "BatchInsert");
    stmt.Prepare(R"(INSERT INTO "BatchInsert" ("id", "at") VALUES (?, ?))");

    auto const rows = std::array {
        DateTimeRow { .id = 1, .at = MakeDay2026(1, 15) + 10h + 20min + 30s },
        DateTimeRow { .id = 2, .at = MakeDay2026(2, 15) + 11h + 21min + 31s + 123456us },
        DateTimeRow { .id = 3, .at = MakeDay2026(3, 15) + 12h + 22min + 32s },
    };
    auto const ids = std::array { rows[0].id, rows[1].id, rows[2].id };
    auto const dates = std::array { rows[0].at, rows[1].at, rows[2].at };

    SECTION("ExecuteBatchNative")
    {
        (void) stmt.ExecuteBatchNative(ids, dates);
        CheckRowsReadBack(stmt, "BatchInsert", rows);
    }

    SECTION("ExecuteBatchSoft")
    {
        (void) stmt.ExecuteBatchSoft(ids, dates);
        CheckRowsReadBack(stmt, "BatchInsert", rows);
    }

    SECTION("ExecuteBatch (row-wise)")
    {
        (void) stmt.ExecuteBatch(
            std::span { rows },
            [](DateTimeRow const& row) -> int const& { return row.id; },
            [](DateTimeRow const& row) -> SqlDateTime const& { return row.at; });
        CheckRowsReadBack(stmt, "BatchInsert", rows);
    }

    SECTION("ExecuteBatch (row-wise, optional)")
    {
        auto const optionalRows = std::array {
            OptionalDateTimeRow { .id = 1, .at = rows[0].at },
            OptionalDateTimeRow { .id = 2, .at = std::nullopt },
            OptionalDateTimeRow { .id = 3, .at = rows[2].at },
        };
        (void) stmt.ExecuteBatch(
            std::span { optionalRows },
            [](OptionalDateTimeRow const& row) -> int const& { return row.id; },
            [](OptionalDateTimeRow const& row) -> std::optional<SqlDateTime> const& { return row.at; });

        CHECK(stmt.ExecuteDirectScalar<int>(R"(SELECT COUNT(*) FROM "BatchInsert" WHERE "at" IS NULL)").value_or(-1) == 1);
        (void) stmt.ExecuteDirect(R"(DELETE FROM "BatchInsert" WHERE "at" IS NULL)");
        CheckRowsReadBack(stmt, "BatchInsert", std::array { rows[0], rows[2] });
    }
}
