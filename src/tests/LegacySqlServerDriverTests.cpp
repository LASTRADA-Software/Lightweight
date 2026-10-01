// SPDX-License-Identifier: Apache-2.0

#include "Utils.hpp"

#include <Lightweight/DataBinder/SqlDateTime.hpp>
#include <Lightweight/DataBinder/SqlRawColumn.hpp>
#include <Lightweight/Lightweight.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <format>
#include <optional>
#include <span>
#include <string>
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

namespace
{

/// Creates the table @p tableName with an integer column "id" and a nullable text column "name" of @p textType.
void CreateTextTable(SqlStatement& stmt, std::string_view tableName, SqlColumnTypeDefinition const& textType)
{
    stmt.MigrateDirect([tableName, &textType](auto& migration) {
        migration.CreateTable(tableName).RequiredColumn("id", SqlColumnTypeDefinitions::Integer {}).Column("name", textType);
    });
}

/// Checks that every row of @p tableName stores "name" as an empty string, not NULL, and that there are @p rowCount.
void CheckAllNamesEmpty(SqlStatement& stmt, std::string_view tableName, int rowCount)
{
    auto cursor = stmt.ExecuteDirect(std::format(R"(SELECT "id", "name" FROM "{}" ORDER BY "id")", tableName));
    auto rowsRead = 0;
    while (cursor.FetchRow())
    {
        ++rowsRead;
        auto const name = cursor.GetNullableColumn<std::string>(2);
        REQUIRE(name.has_value());
        CHECK(name->empty());
    }
    CHECK(rowsRead == rowCount);
}

/// Inserts an empty @p Text after an integer parameter, reads it back and filters on it.
template <typename Text>
void CheckEmptyTextParameter(SqlStatement& stmt, SqlColumnTypeDefinition const& textType)
{
    CreateTextTable(stmt, "EmptyText", textType);

    stmt.Prepare(R"(INSERT INTO "EmptyText" ("id", "name") VALUES (?, ?))");
    REQUIRE_NOTHROW((void) stmt.Execute(1, Text {}));
    REQUIRE_NOTHROW((void) stmt.Execute(2, Text {}));
    CheckAllNamesEmpty(stmt, "EmptyText", 2);

    stmt.Prepare(R"(SELECT COUNT(*) FROM "EmptyText" WHERE "id" > ? AND "name" = ?)");
    SqlResultCursor cursor = stmt.Execute(1, Text {});
    REQUIRE(cursor.FetchRow());
    CHECK(cursor.GetColumn<int>(1) == 1);
}

/// Batch-inserts rows whose text column is empty in every row, through all three batch paths.
template <typename Text>
void CheckEmptyTextBatch(SqlStatement& stmt, SqlColumnTypeDefinition const& textType)
{
    CreateTextTable(stmt, "EmptyTextBatch", textType);
    stmt.Prepare(R"(INSERT INTO "EmptyTextBatch" ("id", "name") VALUES (?, ?))");

    auto const ids = std::array { 1, 2, 3 };
    auto const names = std::array { Text {}, Text {}, Text {} };

    SECTION("ExecuteBatchSoft")
    {
        REQUIRE_NOTHROW((void) stmt.ExecuteBatchSoft(ids, names));
        CheckAllNamesEmpty(stmt, "EmptyTextBatch", 3);
    }

    SECTION("ExecuteBatch")
    {
        REQUIRE_NOTHROW((void) stmt.ExecuteBatch(ids, names));
        CheckAllNamesEmpty(stmt, "EmptyTextBatch", 3);
    }

    SECTION("ExecuteBatch (row-wise)")
    {
        struct Row
        {
            int id {};
            Text name {};
        };
        auto const rows = std::array { Row { .id = 1 }, Row { .id = 2 }, Row { .id = 3 } };
        REQUIRE_NOTHROW((void) stmt.ExecuteBatch(
            std::span { rows },
            [](Row const& row) -> int const& { return row.id; },
            [](Row const& row) -> Text const& { return row.name; }));
        CheckAllNamesEmpty(stmt, "EmptyTextBatch", 3);
    }
}

} // namespace

TEST_CASE_METHOD(SqlTestFixture, "Empty string parameter after another parameter", "[LegacySqlServerDriver]")
{
    auto stmt = SqlStatement {};
    auto const narrowText = SqlColumnTypeDefinitions::Varchar { 20 };
    auto const wideText = SqlColumnTypeDefinitions::NVarchar { 20 };

    SECTION("std::string")
    {
        CheckEmptyTextParameter<std::string>(stmt, narrowText);
    }
    SECTION("std::wstring")
    {
        CheckEmptyTextParameter<std::wstring>(stmt, wideText);
    }
    SECTION("std::u16string")
    {
        CheckEmptyTextParameter<std::u16string>(stmt, wideText);
    }
    SECTION("std::u8string")
    {
        CheckEmptyTextParameter<std::u8string>(stmt, wideText);
    }
    SECTION("std::string_view")
    {
        CheckEmptyTextParameter<std::string_view>(stmt, narrowText);
    }
    SECTION("std::u16string_view")
    {
        CheckEmptyTextParameter<std::u16string_view>(stmt, wideText);
    }
    SECTION("std::u8string_view")
    {
        CheckEmptyTextParameter<std::u8string_view>(stmt, wideText);
    }
    SECTION("SqlAnsiString")
    {
        CheckEmptyTextParameter<SqlAnsiString<20>>(stmt, narrowText);
    }
    SECTION("SqlUtf16String")
    {
        CheckEmptyTextParameter<SqlUtf16String<20>>(stmt, wideText);
    }
    SECTION("SqlDynamicAnsiString")
    {
        CheckEmptyTextParameter<SqlDynamicAnsiString<20>>(stmt, narrowText);
    }
    SECTION("String literal")
    {
        CreateTextTable(stmt, "EmptyText", narrowText);
        stmt.Prepare(R"(INSERT INTO "EmptyText" ("id", "name") VALUES (?, ?))");
        REQUIRE_NOTHROW((void) stmt.Execute(1, ""));
        CheckAllNamesEmpty(stmt, "EmptyText", 1);
    }
}

TEST_CASE_METHOD(SqlTestFixture, "Empty string batch insert", "[LegacySqlServerDriver]")
{
    auto stmt = SqlStatement {};
    auto const narrowText = SqlColumnTypeDefinitions::Varchar { 20 };
    auto const wideText = SqlColumnTypeDefinitions::NVarchar { 20 };

    SECTION("std::string")
    {
        CheckEmptyTextBatch<std::string>(stmt, narrowText);
    }
    SECTION("std::wstring")
    {
        CheckEmptyTextBatch<std::wstring>(stmt, wideText);
    }
    SECTION("std::u8string")
    {
        CheckEmptyTextBatch<std::u8string>(stmt, wideText);
    }
    SECTION("SqlAnsiString")
    {
        CheckEmptyTextBatch<SqlAnsiString<20>>(stmt, narrowText);
    }
    SECTION("SqlAnsiString (ExecuteBatchNative)")
    {
        CreateTextTable(stmt, "EmptyTextBatch", narrowText);
        stmt.Prepare(R"(INSERT INTO "EmptyTextBatch" ("id", "name") VALUES (?, ?))");
        auto const ids = std::array { 1, 2, 3 };
        auto const names = std::array { SqlAnsiString<20> {}, SqlAnsiString<20> {}, SqlAnsiString<20> {} };
        REQUIRE_NOTHROW((void) stmt.ExecuteBatchNative(ids, names));
        CheckAllNamesEmpty(stmt, "EmptyTextBatch", 3);
    }
}

/// A record whose empty text fields follow a plain integer column.
struct LegacyTextRecord
{
    Field<int, PrimaryKey::Manual> id;
    Field<int> amount;
    Field<std::string> name;
    Field<SqlAnsiString<20>> code;
    Field<SqlUtf16String<20>> label;
    Field<SqlWideString<20>> wideText;
    Field<SqlDynamicString<20, char8_t>> utf8Text;
    Field<std::optional<std::string>> note;
};

TEST_CASE_METHOD(SqlTestFixture,
                 "DataMapper writes an empty string that follows an integer field",
                 "[LegacySqlServerDriver]")
{
    auto dm = DataMapper {};
    dm.CreateTable<LegacyTextRecord>();

    auto record = LegacyTextRecord { .id = 1,
                                     .amount = 42,
                                     .name = std::string {},
                                     .code = SqlAnsiString<20> {},
                                     .label = SqlUtf16String<20> {},
                                     .wideText = SqlWideString<20> {},
                                     .utf8Text = SqlDynamicString<20, char8_t> {},
                                     .note = std::nullopt };
    REQUIRE_NOTHROW(dm.Create(record));

    auto const checkEmpty = [&dm](int primaryKey) {
        auto const stored = ReadRecord<LegacyTextRecord>(dm, primaryKey);
        CHECK(stored.name.Value().empty());
        CHECK(stored.code.Value().empty());
        CHECK(stored.label.Value().empty());
        CHECK(stored.wideText.Value().empty());
        CHECK(stored.utf8Text.Value().empty());
        CHECK(!stored.note.Value().has_value());
        auto stmt = SqlStatement { dm.Connection() };
        CHECK(stmt.ExecuteDirectScalar<int>(
                      std::format(R"(SELECT COUNT(*) FROM "LegacyTextRecord" WHERE "id" = {} AND )"
                                  R"("name" IS NOT NULL AND "code" IS NOT NULL AND "label" IS NOT NULL )"
                                  R"(AND "wideText" IS NOT NULL AND "utf8Text" IS NOT NULL AND "note" IS NULL)",
                                  primaryKey))
                  .value_or(-1)
              == 1);
    };
    checkEmpty(1);

    SECTION("Update")
    {
        record.name = std::string { "filled" };
        record.code = SqlAnsiString<20> { "filled" };
        record.label = SqlUtf16String<20> { u"filled" };
        record.wideText = SqlWideString<20> { L"filled" };
        record.utf8Text = SqlDynamicString<20, char8_t> { u8"filled" };
        REQUIRE_NOTHROW(dm.Update(record));
        CHECK(ReadRecord<LegacyTextRecord>(dm, 1).name.Value() == "filled");

        record.name = std::string {};
        record.code = SqlAnsiString<20> {};
        record.label = SqlUtf16String<20> {};
        record.wideText = SqlWideString<20> {};
        record.utf8Text = SqlDynamicString<20, char8_t> {};
        REQUIRE_NOTHROW(dm.Update(record));
        checkEmpty(1);
    }

    SECTION("CreateAll")
    {
        auto records = std::vector {
            LegacyTextRecord { .id = 2,
                               .amount = 1,
                               .name = std::string {},
                               .code = SqlAnsiString<20> {},
                               .label = SqlUtf16String<20> {},
                               .wideText = SqlWideString<20> {},
                               .utf8Text = SqlDynamicString<20, char8_t> {},
                               .note = std::nullopt },
            LegacyTextRecord { .id = 3,
                               .amount = 2,
                               .name = std::string {},
                               .code = SqlAnsiString<20> {},
                               .label = SqlUtf16String<20> {},
                               .wideText = SqlWideString<20> {},
                               .utf8Text = SqlDynamicString<20, char8_t> {},
                               .note = std::nullopt },
        };
        REQUIRE_NOTHROW(dm.CreateAll(records));
        checkEmpty(2);
        checkEmpty(3);
    }

    SECTION("Filter")
    {
        auto stmt = SqlStatement { dm.Connection() };
        stmt.Prepare(R"(SELECT COUNT(*) FROM "LegacyTextRecord" WHERE "amount" > ? AND "code" = ?)");
        auto cursor = stmt.Execute(0, SqlAnsiString<20> {});
        REQUIRE(cursor.FetchRow());
        CHECK(cursor.GetColumn<int>(1) == 1);
    }
}

TEST_CASE_METHOD(SqlTestFixture, "Empty string stays distinct from NULL", "[LegacySqlServerDriver]")
{
    auto stmt = SqlStatement {};
    CreateTextTable(stmt, "EmptyOrNull", SqlColumnTypeDefinitions::Varchar { 20 });

    stmt.Prepare(R"(INSERT INTO "EmptyOrNull" ("id", "name") VALUES (?, ?))");
    REQUIRE_NOTHROW((void) stmt.Execute(1, std::string {}));
    REQUIRE_NOTHROW((void) stmt.Execute(2, std::optional<std::string> {}));
    REQUIRE_NOTHROW((void) stmt.Execute(3, SqlNullValue));
    REQUIRE_NOTHROW((void) stmt.Execute(4, std::optional<std::string> { std::string {} }));

    CHECK(stmt.ExecuteDirectScalar<int>(R"(SELECT COUNT(*) FROM "EmptyOrNull" WHERE "name" IS NULL)").value_or(-1) == 2);
    stmt.Prepare(R"(SELECT "id" FROM "EmptyOrNull" WHERE "id" > ? AND "name" = ? ORDER BY "id")");
    auto cursor = stmt.Execute(0, std::string {});
    REQUIRE(cursor.FetchRow());
    CHECK(cursor.GetColumn<int>(1) == 1);
    REQUIRE(cursor.FetchRow());
    CHECK(cursor.GetColumn<int>(1) == 4);
    CHECK_FALSE(cursor.FetchRow());
}

namespace
{

/// Raw batch metadata of a non-null integer column.
constexpr auto RawIntegerColumn = SqlRawColumnMetadata { .cType = SQL_C_SLONG,
                                                         .sqlType = SQL_INTEGER,
                                                         .size = 0,
                                                         .decimalDigits = 0,
                                                         .nullable = SQL_NO_NULLS,
                                                         .bufferLength = sizeof(int) };

/// Batch-inserts ids 1-3 through raw columns: two empty values of @p CharT, then a NULL.
template <typename CharT>
void CheckEmptyRawTextBatch(SqlStatement& stmt, SQLSMALLINT cType, SQLSMALLINT sqlType)
{
    stmt.Prepare(R"(INSERT INTO "EmptyRawText" ("id", "name") VALUES (?, ?))");

    auto const ids = std::array { 1, 2, 3 };
    auto const idIndicators = std::array<SQLLEN, 3> { sizeof(int), sizeof(int), sizeof(int) };
    auto const names = std::array<CharT, 3> {};
    auto const nameIndicators = std::array<SQLLEN, 3> { 0, 0, SQL_NULL_DATA };
    // The column size is 0, as a raw batch reader reports it for a column whose values are all empty.
    auto const columns = std::array {
        SqlRawColumn { .metadata = RawIntegerColumn, .data = std::as_bytes(std::span { ids }), .indicators = idIndicators },
        SqlRawColumn { .metadata = { .cType = cType,
                                     .sqlType = sqlType,
                                     .size = 0,
                                     .decimalDigits = 0,
                                     .nullable = SQL_NULLABLE,
                                     .bufferLength = sizeof(CharT) },
                       .data = std::as_bytes(std::span { names }),
                       .indicators = nameIndicators },
    };
    REQUIRE_NOTHROW((void) stmt.ExecuteBatch(columns, ids.size()));

    CHECK(stmt.ExecuteDirectScalar<int>(R"(SELECT COUNT(*) FROM "EmptyRawText" WHERE "name" IS NULL)").value_or(-1) == 1);
    auto cursor = stmt.ExecuteDirect(R"(SELECT "id", "name" FROM "EmptyRawText" WHERE "name" IS NOT NULL ORDER BY "id")");
    for (auto const expectedId: { 1, 2 })
    {
        REQUIRE(cursor.FetchRow());
        CHECK(cursor.GetColumn<int>(1) == expectedId);
        CHECK(cursor.GetColumn<std::string>(2).empty());
    }
    CHECK_FALSE(cursor.FetchRow());
}

} // namespace

TEST_CASE_METHOD(SqlTestFixture, "Empty string through raw batch columns", "[LegacySqlServerDriver]")
{
    auto stmt = SqlStatement {};

    SECTION("VARCHAR")
    {
        CreateTextTable(stmt, "EmptyRawText", SqlColumnTypeDefinitions::Varchar { 20 });
        CheckEmptyRawTextBatch<char>(stmt, SQL_C_CHAR, SQL_VARCHAR);
    }
    SECTION("NVARCHAR")
    {
        CreateTextTable(stmt, "EmptyRawText", SqlColumnTypeDefinitions::NVarchar { 20 });
        CheckEmptyRawTextBatch<char16_t>(stmt, SQL_C_WCHAR, SQL_WVARCHAR);
    }
}

TEST_CASE_METHOD(SqlTestFixture, "Empty binary parameter after another parameter", "[LegacySqlServerDriver]")
{
    auto stmt = SqlStatement {};
    stmt.MigrateDirect([](auto& migration) {
        migration.CreateTable("EmptyBinary")
            .RequiredColumn("id", SqlColumnTypeDefinitions::Integer {})
            .Column("data", SqlColumnTypeDefinitions::VarBinary { 20 });
    });

    stmt.Prepare(R"(INSERT INTO "EmptyBinary" ("id", "data") VALUES (?, ?))");
    REQUIRE_NOTHROW((void) stmt.Execute(1, SqlDynamicBinary<20> {}));
    REQUIRE_NOTHROW((void) stmt.Execute(2, SqlNullValue));

    auto cursor = stmt.ExecuteDirect(R"(SELECT "data" FROM "EmptyBinary" ORDER BY "id")");
    REQUIRE(cursor.FetchRow());
    auto const empty = cursor.GetNullableColumn<SqlDynamicBinary<20>>(1);
    REQUIRE(empty.has_value());
    CHECK(empty->empty());
    REQUIRE(cursor.FetchRow());
    CHECK(!cursor.GetNullableColumn<SqlDynamicBinary<20>>(1).has_value());
    CHECK_FALSE(cursor.FetchRow());
}

// NOLINTEND(bugprone-unchecked-optional-access)
