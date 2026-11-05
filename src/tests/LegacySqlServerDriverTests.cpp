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
#include <string_view>
#include <tuple>
#include <vector>

using namespace std::chrono_literals;
using namespace Lightweight;

// NOLINTBEGIN(bugprone-unchecked-optional-access) - every access is guarded by a preceding REQUIRE

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

/// Reads the record with the given primary key, failing the test when it is missing.
template <typename Record, typename PrimaryKey>
Record ReadRecord(DataMapper& dm, PrimaryKey const& primaryKey)
{
    auto const found = dm.QuerySingle<Record>(primaryKey);
    REQUIRE(found.has_value());
    return *found;
}

/// Unwraps an optional that the test expects to be engaged.
template <typename Value>
Value const& RequireValue(std::optional<Value> const& optionalValue)
{
    REQUIRE(optionalValue.has_value());
    return *optionalValue;
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

TEST_CASE_METHOD(SqlTestFixture, "A parameter the driver rejects raises instead of executing", "[LegacySqlServerDriver]")
{
    auto stmt = SqlStatement {};
    if (stmt.Connection().ServerType() != SqlServerType::MICROSOFT_SQL || stmt.Connection().DriverName() != "SQLSRV32.DLL")
        return;

    CreateDateTimeTable(stmt, "RejectedParameter");
    InsertThreeMonths(stmt, "RejectedParameter");

    stmt.Prepare(R"(SELECT COUNT(*) FROM "RejectedParameter" WHERE "id" > ? AND ? IS NOT NULL)");
    auto const arguments = std::vector<SqlVariant> { SqlVariant { 0 }, SqlVariant { SqlTime { 12h, 30min, 0s } } };
    try
    {
        std::ignore = stmt.ExecuteWithVariants(arguments);
        FAIL("ExecuteWithVariants must throw when a parameter cannot be bound");
    }
    catch (SqlException const& exception)
    {
        CHECK(std::string_view { exception.what() }.contains("HYC00"));
    }
}

TEST_CASE_METHOD(SqlTestFixture,
                 "A batch parameter the driver rejects raises instead of executing",
                 "[LegacySqlServerDriver]")
{
    auto stmt = SqlStatement {};
    if (stmt.Connection().ServerType() != SqlServerType::MICROSOFT_SQL || stmt.Connection().DriverName() != "SQLSRV32.DLL")
        return;

    CreateDateTimeTable(stmt, "RejectedBatchParameter");

    stmt.Prepare(R"(INSERT INTO "RejectedBatchParameter" ("id") SELECT ? WHERE ? IS NOT NULL)");
    auto const ids = std::array { 1, 2 };
    auto const times = std::array { SqlTime { 12h, 30min, 0s }, SqlTime { 13h, 0min, 0s } };
    try
    {
        std::ignore = stmt.ExecuteBatchSoft(ids, times);
        FAIL("ExecuteBatchSoft must throw when a parameter cannot be bound");
    }
    catch (SqlException const& exception)
    {
        INFO(exception.what());
        CHECK(std::string_view { exception.what() }.contains("HYC00"));
    }
}

// Records stay outside the anonymous namespace: reflection needs them to have external linkage.
/// A record whose date-time follows a plain integer column.
struct LegacyDateTimeRecord
{
    Field<int, PrimaryKey::Manual> id;
    Field<int> amount;
    Field<SqlDateTime> at;
};

/// A record whose nullable date-time follows a plain integer column.
struct LegacyOptionalDateTimeRecord
{
    Field<int, PrimaryKey::Manual> id;
    Field<int> amount;
    Field<std::optional<SqlDateTime>> at;
};

/// A record shaped like a typical legacy table: an auto-assigned key, then nullable numbers around a date-time.
struct LegacyMixedColumnsRecord
{
    static constexpr std::string_view TableName = "LegacyMixedColumns";

    Field<int32_t, PrimaryKey::AutoAssign, SqlRealName { "id" }> id;
    Field<std::optional<double>, SqlRealName { "before_value" }> before;
    Field<std::optional<int32_t>, SqlRealName { "parent_id" }> parentId;
    Field<std::optional<SqlDateTime>, SqlRealName { "occurred_at" }> occurredAt;
    Field<std::optional<double>, SqlRealName { "after_value" }> after;
};

namespace
{

/// Checks that @p actual equals @p expected to the second.
void CheckSameSecond(SqlDateTime const& actual, SqlDateTime const& expected)
{
    CHECK(actual.sqlValue.year == expected.sqlValue.year);
    CHECK(actual.sqlValue.month == expected.sqlValue.month);
    CHECK(actual.sqlValue.day == expected.sqlValue.day);
    CHECK(actual.sqlValue.hour == expected.sqlValue.hour);
    CHECK(actual.sqlValue.minute == expected.sqlValue.minute);
    CHECK(actual.sqlValue.second == expected.sqlValue.second);
}

/// Returns 2026-05-06 07:08:09 with a sub-millisecond fraction.
SqlDateTime MakeTimeWithSubMilliseconds()
{
    auto value = SqlDateTime { std::chrono::year { 2026 }, std::chrono::month { 5 }, std::chrono::day { 6 }, 7h, 8min, 9s };
    value.sqlValue.fraction = 123'456'700;
    return value;
}

} // namespace

TEST_CASE_METHOD(SqlTestFixture, "DataMapper writes a date-time that follows an integer field", "[LegacySqlServerDriver]")
{
    auto dm = DataMapper {};
    if (dm.Connection().ServerType() != SqlServerType::MICROSOFT_SQL)
        return;

    dm.CreateTable<LegacyDateTimeRecord>();
    auto record = LegacyDateTimeRecord { .id = 1, .amount = 42, .at = MakeTimeWithSubMilliseconds() };
    REQUIRE_NOTHROW(dm.Create(record));
    CheckSameSecond(ReadRecord<LegacyDateTimeRecord>(dm, 1).at.Value(), MakeTimeWithSubMilliseconds());

    SECTION("Update")
    {
        record.at = MakeDay2026(7, 1);
        REQUIRE_NOTHROW(dm.Update(record));
        CheckSameSecond(ReadRecord<LegacyDateTimeRecord>(dm, 1).at.Value(), MakeDay2026(7, 1));
    }

    SECTION("CreateAll and UpdateAll")
    {
        auto records = std::vector {
            LegacyDateTimeRecord { .id = 2, .amount = 1, .at = MakeDay2026(8, 1) },
            LegacyDateTimeRecord { .id = 3, .amount = 2, .at = MakeTimeWithSubMilliseconds() },
        };
        REQUIRE_NOTHROW(dm.CreateAll(records));
        CheckSameSecond(ReadRecord<LegacyDateTimeRecord>(dm, 2).at.Value(), MakeDay2026(8, 1));
        CheckSameSecond(ReadRecord<LegacyDateTimeRecord>(dm, 3).at.Value(), MakeTimeWithSubMilliseconds());

        records[0].at = MakeDay2026(9, 1);
        records[1].at = MakeDay2026(10, 1);
        REQUIRE_NOTHROW(dm.UpdateAll(records));
        CheckSameSecond(ReadRecord<LegacyDateTimeRecord>(dm, 2).at.Value(), MakeDay2026(9, 1));
        CheckSameSecond(ReadRecord<LegacyDateTimeRecord>(dm, 3).at.Value(), MakeDay2026(10, 1));
    }
}

TEST_CASE_METHOD(SqlTestFixture,
                 "DataMapper writes a nullable date-time that follows an integer field",
                 "[LegacySqlServerDriver]")
{
    auto dm = DataMapper {};
    if (dm.Connection().ServerType() != SqlServerType::MICROSOFT_SQL)
        return;

    dm.CreateTable<LegacyOptionalDateTimeRecord>();
    auto engaged = LegacyOptionalDateTimeRecord { .id = 1, .amount = 42, .at = MakeTimeWithSubMilliseconds() };
    auto disengaged = LegacyOptionalDateTimeRecord { .id = 2, .amount = 43, .at = std::nullopt };
    REQUIRE_NOTHROW(dm.Create(engaged));
    REQUIRE_NOTHROW(dm.Create(disengaged));

    auto const readEngaged = ReadRecord<LegacyOptionalDateTimeRecord>(dm, 1).at.Value();
    REQUIRE(readEngaged.has_value());
    CheckSameSecond(*readEngaged, MakeTimeWithSubMilliseconds());
    CHECK(!ReadRecord<LegacyOptionalDateTimeRecord>(dm, 2).at.Value().has_value());

    SECTION("Update")
    {
        engaged.at = std::nullopt;
        disengaged.at = MakeDay2026(7, 1);
        REQUIRE_NOTHROW(dm.Update(engaged));
        REQUIRE_NOTHROW(dm.Update(disengaged));
        CHECK(!ReadRecord<LegacyOptionalDateTimeRecord>(dm, 1).at.Value().has_value());
        auto const readDisengaged = ReadRecord<LegacyOptionalDateTimeRecord>(dm, 2).at.Value();
        REQUIRE(readDisengaged.has_value());
        CheckSameSecond(*readDisengaged, MakeDay2026(7, 1));
    }

    SECTION("CreateAll and UpdateAll")
    {
        auto records = std::vector {
            LegacyOptionalDateTimeRecord { .id = 3, .amount = 1, .at = MakeDay2026(8, 1) },
            LegacyOptionalDateTimeRecord { .id = 4, .amount = 2, .at = std::nullopt },
        };
        REQUIRE_NOTHROW(dm.CreateAll(records));
        CheckSameSecond(RequireValue(ReadRecord<LegacyOptionalDateTimeRecord>(dm, 3).at.Value()), MakeDay2026(8, 1));
        CHECK(!ReadRecord<LegacyOptionalDateTimeRecord>(dm, 4).at.Value().has_value());

        records[0].at = std::nullopt;
        records[1].at = MakeDay2026(9, 1);
        REQUIRE_NOTHROW(dm.UpdateAll(records));
        CHECK(!ReadRecord<LegacyOptionalDateTimeRecord>(dm, 3).at.Value().has_value());
        CheckSameSecond(RequireValue(ReadRecord<LegacyOptionalDateTimeRecord>(dm, 4).at.Value()), MakeDay2026(9, 1));
    }
}

TEST_CASE_METHOD(SqlTestFixture,
                 "DataMapper writes a date-time between nullable numbers into a DATETIME column",
                 "[LegacySqlServerDriver]")
{
    auto dm = DataMapper {};
    if (dm.Connection().ServerType() != SqlServerType::MICROSOFT_SQL)
        return;

    (void) SqlStatement { dm.Connection() }.ExecuteDirect(
        R"(CREATE TABLE "LegacyMixedColumns" ("id" INT NOT NULL PRIMARY KEY, "before_value" FLOAT NULL, )"
        R"("parent_id" INT NULL, "occurred_at" DATETIME NULL, "after_value" FLOAT NULL))");

    auto record = LegacyMixedColumnsRecord {};
    record.before = 1.5;
    record.parentId = 1;
    record.occurredAt = MakeTimeWithSubMilliseconds();
    record.after = 2.5;
    auto const primaryKey = dm.Create(record);

    auto const stored = ReadRecord<LegacyMixedColumnsRecord>(dm, primaryKey);
    REQUIRE(stored.occurredAt.Value().has_value());
    CheckSameSecond(*stored.occurredAt.Value(), MakeTimeWithSubMilliseconds());
    CHECK(stored.after.Value() == 2.5);

    SECTION("Update")
    {
        record.occurredAt = MakeDay2026(7, 1);
        record.after = std::nullopt;
        REQUIRE_NOTHROW(dm.Update(record));
        auto const updated = ReadRecord<LegacyMixedColumnsRecord>(dm, primaryKey);
        REQUIRE(updated.occurredAt.Value().has_value());
        CheckSameSecond(*updated.occurredAt.Value(), MakeDay2026(7, 1));
        CHECK(!updated.after.Value().has_value());
    }
}

// NOLINTEND(bugprone-unchecked-optional-access)
