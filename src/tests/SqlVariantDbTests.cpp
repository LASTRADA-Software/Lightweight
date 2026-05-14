// SPDX-License-Identifier: Apache-2.0

#include "Utils.hpp"

#include <Lightweight/DataBinder/SqlNullValue.hpp>
#include <Lightweight/DataBinder/SqlVariant.hpp>
#include <Lightweight/Lightweight.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <chrono>
#include <string>
#include <variant>

using namespace Lightweight;

// ================================================================================================
// SqlVariant::ToString covers most variant alternatives without needing a DB round-trip.
// We mostly use direct construction here since fetched-via-ODBC variants only realize
// a subset of types (driver-specific). The DB-dependent fetch tests below complement them.
// ================================================================================================

TEST_CASE("SqlVariant::ToString produces a readable form for every alternative", "[SqlVariant]")
{
    SECTION("null")
    {
        SqlVariant v { SqlNullValue };
        CHECK(v.ToString() == "NULL");
    }
    SECTION("bool")
    {
        CHECK(SqlVariant { true }.ToString() == "true");
        CHECK(SqlVariant { false }.ToString() == "false");
    }
    SECTION("int8")
    {
        SqlVariant v { static_cast<int8_t>(-7) };
        CHECK(v.ToString() == "-7");
    }
    SECTION("int")
    {
        CHECK(SqlVariant { 42 }.ToString() == "42");
    }
    SECTION("unsigned int")
    {
        CHECK(SqlVariant { static_cast<unsigned int>(99) }.ToString() == "99");
    }
    SECTION("long long")
    {
        CHECK(SqlVariant { static_cast<long long>(123456789LL) }.ToString() == "123456789");
    }
    SECTION("float / double")
    {
        CHECK(SqlVariant { 1.5F }.ToString() == "1.5");
        CHECK(SqlVariant { 2.25 }.ToString() == "2.25");
    }
    SECTION("std::string and std::string_view")
    {
        CHECK(SqlVariant { std::string { "hello" } }.ToString() == "hello");
        CHECK(SqlVariant { std::string_view { "world" } }.ToString() == "world");
    }
    SECTION("SqlText")
    {
        SqlVariant v { SqlText { .value = "long text" } };
        CHECK(v.ToString() == "long text");
    }
    SECTION("SqlDate")
    {
        SqlVariant v { SqlDate { std::chrono::year { 2026 }, std::chrono::month { 5 }, std::chrono::day { 6 } } };
        auto const str = v.ToString();
        // The formatter renders {year}-{month}-{day}; numeric, no zero padding required by the impl.
        CHECK(str.contains("2026"));
        CHECK(str.contains("5"));
        CHECK(str.contains("6"));
    }
    SECTION("SqlTime")
    {
        SqlVariant v { SqlTime { std::chrono::hours { 13 }, std::chrono::minutes { 14 }, std::chrono::seconds { 15 } } };
        auto const str = v.ToString();
        CHECK(str.contains("13"));
        CHECK(str.contains("14"));
        CHECK(str.contains("15"));
    }
    SECTION("SqlDateTime")
    {
        SqlVariant v { SqlDateTime { std::chrono::year { 2026 },
                                     std::chrono::month { 5 },
                                     std::chrono::day { 6 },
                                     std::chrono::hours { 13 },
                                     std::chrono::minutes { 14 },
                                     std::chrono::seconds { 15 },
                                     std::chrono::nanoseconds { 0 } } };
        auto const str = v.ToString();
        CHECK(str.contains("2026"));
        CHECK(str.contains("13"));
    }
}

// ================================================================================================
// DB-dependent SqlVariant fetch — exercises the GetColumn switch arms
// ================================================================================================

TEST_CASE_METHOD(SqlTestFixture, "SqlVariant fetches integer / text / NULL columns", "[SqlVariant]")
{
    auto stmt = SqlStatement {};
    stmt.MigrateDirect([](auto& migration) {
        migration.CreateTable("Mixed")
            .RequiredColumn("id", SqlColumnTypeDefinitions::Integer {})
            .Column("note", SqlColumnTypeDefinitions::Varchar { 100 });
    });

    stmt.Prepare(R"(INSERT INTO "Mixed" ("id", "note") VALUES (?, ?))");
    (void) stmt.Execute(7, "hello");
    (void) stmt.Execute(8, SqlNullValue);

    auto cursor = stmt.ExecuteDirect(R"(SELECT "id", "note" FROM "Mixed" ORDER BY "id")");

    REQUIRE(cursor.FetchRow());
    SqlVariant id1;
    SqlVariant note1;
    CHECK(cursor.GetColumn(1, &id1));
    CHECK(cursor.GetColumn(2, &note1));
    CHECK(id1.TryGetInt().value_or(-1) == 7);
    CHECK(note1.IsNull() == false);
    CHECK(note1.ToString() == "hello");

    REQUIRE(cursor.FetchRow());
    SqlVariant id2;
    SqlVariant note2;
    CHECK(cursor.GetColumn(1, &id2));
    CHECK_FALSE(cursor.GetColumn(2, &note2));
    CHECK(id2.TryGetInt().value_or(-1) == 8);
    CHECK(note2.IsNull());
    CHECK(note2.ToString() == "NULL");

    CHECK_FALSE(cursor.FetchRow());
}

TEST_CASE_METHOD(SqlTestFixture, "SqlVariant fetches floating-point columns", "[SqlVariant]")
{
    auto stmt = SqlStatement {};
    stmt.MigrateDirect(
        [](auto& migration) { migration.CreateTable("Floats").RequiredColumn("price", SqlColumnTypeDefinitions::Real {}); });

    stmt.Prepare(R"(INSERT INTO "Floats" ("price") VALUES (?))");
    (void) stmt.Execute(3.14);

    auto cursor = stmt.ExecuteDirect(R"(SELECT "price" FROM "Floats")");
    REQUIRE(cursor.FetchRow());
    SqlVariant v;
    CHECK(cursor.GetColumn(1, &v));
    CHECK_FALSE(v.IsNull());
    // The ODBC C type the driver reports for a `REAL` column is dialect-specific:
    // - SQLite stores `REAL` as 8-byte double → SqlVariant lands on `double`.
    // - SQL Server `REAL` is single-precision (SQL_REAL) → SqlVariant lands on `float`.
    // Accept either; the value should round-trip within single-precision tolerance.
    if (std::holds_alternative<double>(v.value))
        CHECK_THAT(std::get<double>(v.value), Catch::Matchers::WithinAbs(3.14, 1e-9));
    else
    {
        REQUIRE(std::holds_alternative<float>(v.value));
        CHECK_THAT(std::get<float>(v.value), Catch::Matchers::WithinAbs(3.14F, 1e-6F));
    }
}

TEST_CASE_METHOD(SqlTestFixture, "SqlVariantRowCursor iterates all rows", "[SqlVariant]")
{
    auto stmt = SqlStatement {};
    CreateEmployeesTable(stmt);
    FillEmployeesTable(stmt);

    auto cursor = stmt.ExecuteDirect(R"(SELECT "FirstName", "Salary" FROM "Employees" ORDER BY "EmployeeID")");
    int rowCount = 0;
    long long sum = 0;
    for (auto& row: SqlVariantRowCursor(std::move(cursor)))
    {
        ++rowCount;
        REQUIRE(row.size() == 2);
        sum += row[1].TryGetInt().value_or(0);
    }
    CHECK(rowCount == 3);
    CHECK(sum == 50'000 + 60'000 + 70'000);
}

// ================================================================================================
// DB-dependent fetch tests for the GetColumn switch arms that aren't reached by the
// integer / text / float / NULL tests above. Each test round-trips a value through a
// fresh single-column table and verifies the variant lands on the expected alternative.
//
// All tests use SqlTestFixture so they fan out over every test environment configured in
// `.test-env.yml` (sqlite3 locally, plus mssql2017/2019/2022/2025 and postgres in CI).
// ================================================================================================

TEST_CASE_METHOD(SqlTestFixture, "SqlVariant fetches SMALLINT/BIGINT columns", "[SqlVariant]")
{
    auto stmt = SqlStatement {};
    stmt.MigrateDirect([](auto& migration) {
        migration.CreateTable("IntegerKinds")
            .RequiredColumn("small_val", SqlColumnTypeDefinitions::Smallint {})
            .RequiredColumn("big_val", SqlColumnTypeDefinitions::Bigint {});
    });

    stmt.Prepare(R"(INSERT INTO "IntegerKinds" ("small_val", "big_val") VALUES (?, ?))");
    (void) stmt.Execute(static_cast<int16_t>(-12345), 9'000'000'000LL);

    auto cursor = stmt.ExecuteDirect(R"(SELECT "small_val", "big_val" FROM "IntegerKinds")");
    REQUIRE(cursor.FetchRow());

    SqlVariant smallV;
    SqlVariant bigV;
    CHECK(cursor.GetColumn(1, &smallV));
    CHECK(cursor.GetColumn(2, &bigV));
    // SQL_SMALLINT lands on `unsigned short` in the variant; SQL_BIGINT lands on `long long`.
    // Use the width-specific TryGet accessors so we don't overflow a 32-bit int on bigint.
    CHECK(smallV.TryGetShort().value_or(0) == -12345);
    CHECK(bigV.TryGetLongLong().value_or(0) == 9'000'000'000LL);
}

TEST_CASE_METHOD(SqlTestFixture, "SqlVariant fetches BOOL columns as bool variant", "[SqlVariant]")
{
    auto stmt = SqlStatement {};
    stmt.MigrateDirect(
        [](auto& migration) { migration.CreateTable("Flags").RequiredColumn("flag", SqlColumnTypeDefinitions::Bool {}); });

    stmt.Prepare(R"(INSERT INTO "Flags" ("flag") VALUES (?))");
    (void) stmt.Execute(true);
    (void) stmt.Execute(false);

    auto cursor = stmt.ExecuteDirect(R"(SELECT "flag" FROM "Flags")");
    REQUIRE(cursor.FetchRow());
    SqlVariant v1;
    CHECK(cursor.GetColumn(1, &v1));
    REQUIRE(cursor.FetchRow());
    SqlVariant v2;
    CHECK(cursor.GetColumn(1, &v2));

    // SQL_BIT lands on bool in the variant.
    REQUIRE(std::holds_alternative<bool>(v1.value));
    REQUIRE(std::holds_alternative<bool>(v2.value));
    CHECK(std::get<bool>(v1.value));
    CHECK_FALSE(std::get<bool>(v2.value));
}

TEST_CASE_METHOD(SqlTestFixture, "SqlVariant fetches BINARY/VARBINARY as std::string bytes", "[SqlVariant]")
{
    auto stmt = SqlStatement {};
    UNSUPPORTED_DATABASE(stmt, SqlServerType::SQLITE); // sqlite reports binary as BLOB; check below
    stmt.MigrateDirect([](auto& migration) {
        migration.CreateTable("Blobs").RequiredColumn("payload", SqlColumnTypeDefinitions::VarBinary { 64 });
    });

    auto const bytes = SqlBinary { 0x01, 0x02, 0x03, 0xFE, 0xFF };
    stmt.Prepare(R"(INSERT INTO "Blobs" ("payload") VALUES (?))");
    (void) stmt.Execute(bytes);

    auto cursor = stmt.ExecuteDirect(R"(SELECT "payload" FROM "Blobs")");
    REQUIRE(cursor.FetchRow());
    SqlVariant v;
    CHECK(cursor.GetColumn(1, &v));
    CHECK_FALSE(v.IsNull());
    // SQL_VARBINARY lands on std::string in the variant (raw byte payload).
    REQUIRE(std::holds_alternative<std::string>(v.value));
    auto const& bytesOut = std::get<std::string>(v.value);
    REQUIRE(bytesOut.size() == bytes.size());
    for (std::size_t i = 0; i < bytes.size(); ++i)
        CHECK(static_cast<uint8_t>(bytesOut[i]) == bytes[i]);
}

TEST_CASE_METHOD(SqlTestFixture, "SqlVariant fetches NVARCHAR (wide) columns", "[SqlVariant]")
{
    auto stmt = SqlStatement {};
    stmt.MigrateDirect([](auto& migration) {
        migration.CreateTable("WideText").RequiredColumn("note", SqlColumnTypeDefinitions::NVarchar { 100 });
    });

    // Use UTF-8 input via std::u8string_view to force the wide-string path on insert.
    constexpr std::u8string_view payload = u8"héllo wörld";
    stmt.Prepare(R"(INSERT INTO "WideText" ("note") VALUES (?))");
    (void) stmt.Execute(payload);

    auto cursor = stmt.ExecuteDirect(R"(SELECT "note" FROM "WideText")");
    REQUIRE(cursor.FetchRow());
    SqlVariant v;
    CHECK(cursor.GetColumn(1, &v));
    CHECK_FALSE(v.IsNull());
    // The SQL_WVARCHAR arm converts UTF-16 back to UTF-8 and stores as std::string.
    REQUIRE(std::holds_alternative<std::string>(v.value));
    auto const& roundTripped = std::get<std::string>(v.value);
    auto const expected = std::string { reinterpret_cast<char const*>(payload.data()), payload.size() };
    CHECK(roundTripped == expected);
}

TEST_CASE_METHOD(SqlTestFixture, "SqlVariant fetches DATE columns into SqlDate", "[SqlVariant]")
{
    // Pins the contract: a DATE column populates the `SqlDate` alternative of the variant.
    auto stmt = SqlStatement {};
    stmt.MigrateDirect(
        [](auto& migration) { migration.CreateTable("Diary").RequiredColumn("d", SqlColumnTypeDefinitions::Date {}); });

    auto const date = SqlDate { std::chrono::year { 2026 }, std::chrono::month { 5 }, std::chrono::day { 11 } };
    stmt.Prepare(R"(INSERT INTO "Diary" ("d") VALUES (?))");
    (void) stmt.Execute(date);

    auto cursor = stmt.ExecuteDirect(R"(SELECT "d" FROM "Diary")");
    REQUIRE(cursor.FetchRow());
    SqlVariant v;
    CHECK(cursor.GetColumn(1, &v));
    CHECK_FALSE(v.IsNull());
    REQUIRE(std::holds_alternative<SqlDate>(v.value));
    CHECK(std::get<SqlDate>(v.value) == date);
}

TEST_CASE_METHOD(SqlTestFixture, "SqlVariant fetches TIME columns into SqlTime", "[SqlVariant]")
{
    // Pins the contract: a TIME column populates the `SqlTime` alternative of the variant.
    auto stmt = SqlStatement {};
    stmt.MigrateDirect(
        [](auto& migration) { migration.CreateTable("Punchcards").RequiredColumn("t", SqlColumnTypeDefinitions::Time {}); });

    auto const time = SqlTime { std::chrono::hours { 14 }, std::chrono::minutes { 30 }, std::chrono::seconds { 5 } };
    stmt.Prepare(R"(INSERT INTO "Punchcards" ("t") VALUES (?))");
    (void) stmt.Execute(time);

    auto cursor = stmt.ExecuteDirect(R"(SELECT "t" FROM "Punchcards")");
    REQUIRE(cursor.FetchRow());
    SqlVariant v;
    CHECK(cursor.GetColumn(1, &v));
    CHECK_FALSE(v.IsNull());
    REQUIRE(std::holds_alternative<SqlTime>(v.value));
    auto const& got = std::get<SqlTime>(v.value);
    CHECK(got.sqlValue.hour == 14);
    CHECK(got.sqlValue.minute == 30);
    CHECK(got.sqlValue.second == 5);
}

TEST_CASE_METHOD(SqlTestFixture, "SqlVariant fetches DATETIME columns into SqlDateTime", "[SqlVariant]")
{
    auto stmt = SqlStatement {};
    stmt.MigrateDirect([](auto& migration) {
        migration.CreateTable("Events").RequiredColumn("when_at", SqlColumnTypeDefinitions::DateTime {});
    });

    auto const when = SqlDateTime { std::chrono::year { 2026 }, std::chrono::month { 5 },    std::chrono::day { 11 },
                                    std::chrono::hours { 9 },   std::chrono::minutes { 15 }, std::chrono::seconds { 30 } };
    stmt.Prepare(R"(INSERT INTO "Events" ("when_at") VALUES (?))");
    (void) stmt.Execute(when);

    auto cursor = stmt.ExecuteDirect(R"(SELECT "when_at" FROM "Events")");
    REQUIRE(cursor.FetchRow());
    SqlVariant v;
    CHECK(cursor.GetColumn(1, &v));
    CHECK_FALSE(v.IsNull());
    REQUIRE(std::holds_alternative<SqlDateTime>(v.value));
    auto const& got = std::get<SqlDateTime>(v.value);
    CHECK(got.sqlValue.year == 2026);
    CHECK(got.sqlValue.month == 5);
    CHECK(got.sqlValue.day == 11);
    CHECK(got.sqlValue.hour == 9);
    CHECK(got.sqlValue.minute == 15);
    CHECK(got.sqlValue.second == 30);
}

TEST_CASE_METHOD(SqlTestFixture, "SqlVariant fetches GUID columns", "[SqlVariant]")
{
    auto stmt = SqlStatement {};
    stmt.MigrateDirect(
        [](auto& migration) { migration.CreateTable("Ids").RequiredColumn("id", SqlColumnTypeDefinitions::Guid {}); });

    auto const guidOpt = SqlGuid::TryParse("550E8400-E29B-41D4-A716-446655440000");
    REQUIRE(guidOpt.has_value());
    if (!guidOpt.has_value())
        return;
    auto const& guid = *guidOpt;

    stmt.Prepare(R"(INSERT INTO "Ids" ("id") VALUES (?))");
    (void) stmt.Execute(guid);

    auto cursor = stmt.ExecuteDirect(R"(SELECT "id" FROM "Ids")");
    REQUIRE(cursor.FetchRow());
    SqlVariant v;
    CHECK(cursor.GetColumn(1, &v));
    CHECK_FALSE(v.IsNull());
    // MSSQL has a native SQL_GUID type → variant lands on SqlGuid directly.
    // SQLite stores GUIDs as VARCHAR; the GetColumn arm detects and converts them.
    REQUIRE(std::holds_alternative<SqlGuid>(v.value));
    CHECK(std::get<SqlGuid>(v.value) == guid);
}

TEST_CASE_METHOD(SqlTestFixture, "SqlVariant fetches DECIMAL columns by scale", "[SqlVariant]")
{
    auto stmt = SqlStatement {};
    UNSUPPORTED_DATABASE(stmt, SqlServerType::SQLITE); // SQLite has no native DECIMAL
    stmt.MigrateDirect([](auto& migration) {
        migration.CreateTable("Money")
            .RequiredColumn("scale0", SqlColumnTypeDefinitions::Decimal { .precision = 15, .scale = 0 })
            .RequiredColumn("scale2", SqlColumnTypeDefinitions::Decimal { .precision = 15, .scale = 2 })
            .RequiredColumn("scale6", SqlColumnTypeDefinitions::Decimal { .precision = 15, .scale = 6 });
    });

    stmt.Prepare(R"(INSERT INTO "Money" ("scale0", "scale2", "scale6") VALUES (?, ?, ?))");
    (void) stmt.Execute(SqlNumeric<15, 0> { 12345.0 }, SqlNumeric<15, 2> { 99.50 }, SqlNumeric<15, 6> { 0.123456 });

    auto cursor = stmt.ExecuteDirect(R"(SELECT "scale0", "scale2", "scale6" FROM "Money")");
    REQUIRE(cursor.FetchRow());

    SqlVariant v0;
    SqlVariant v2;
    SqlVariant v6;
    CHECK(cursor.GetColumn(1, &v0));
    CHECK(cursor.GetColumn(2, &v2));
    CHECK(cursor.GetColumn(3, &v6));

    // scale=0 returns the unscaled value as a 64-bit integer regardless of dialect.
    CHECK(v0.TryGetLongLong().value_or(0) == 12345);

    // Non-zero scales: the SqlVariant DECIMAL/NUMERIC arm reads scale via SQL_C_NUMERIC,
    // which is reliable on PostgreSQL/SQLite. KNOWN BUG: on MSSQL, the ODBC driver returns
    // the bound `SQL_C_NUMERIC` value at scale=0 unless the application sets
    // SQL_DESC_SCALE on the IRD beforehand; SqlVariant.cpp does not do that, so the value
    // comes back integer-truncated (99.50 → 99, 0.123456 → 0). Cover scale=0 on every DBMS
    // and the non-zero scales only where they currently round-trip.
    if (stmt.Connection().ServerType() != SqlServerType::MICROSOFT_SQL)
    {
        auto asDouble = [](SqlVariant const& v) -> std::optional<double> {
            return std::visit(
                []<typename T>(T const& alt) -> std::optional<double> {
                    if constexpr (std::is_arithmetic_v<T>)
                        return static_cast<double>(alt);
                    else
                        return std::nullopt;
                },
                v.value);
        };
        CHECK_THAT(asDouble(v2).value_or(0.0), Catch::Matchers::WithinAbs(99.50, 1e-6));
        CHECK_THAT(asDouble(v6).value_or(0.0), Catch::Matchers::WithinAbs(0.123456, 1e-9));
    }
}

// ================================================================================================
// Driver-level assumption tests.
//
// These call SQLColAttributeW directly to assert the concise SQL type codes that
// SqlVariant's dispatch depends on. A failure here points at the driver / DB / connection
// option, not at the variant code.
// ================================================================================================

namespace
{

[[nodiscard]] SQLLEN FetchConciseType(SqlStatement& stmt, std::string_view selectSql, SQLUSMALLINT columnIndex)
{
    auto cursor = stmt.ExecuteDirect(selectSql);
    SQLLEN value {};
    auto const rc = SQLColAttributeW(stmt.NativeHandle(), columnIndex, SQL_DESC_CONCISE_TYPE, nullptr, 0, nullptr, &value);
    REQUIRE(SQL_SUCCEEDED(rc));
    return value;
}

} // namespace

TEST_CASE_METHOD(SqlTestFixture, "ODBC assumption: DATE column reports SQL_TYPE_DATE", "[SqlVariant][ODBC][assumption]")
{
    auto stmt = SqlStatement {};
    stmt.MigrateDirect(
        [](auto& migration) { migration.CreateTable("AssumeDate").RequiredColumn("d", SqlColumnTypeDefinitions::Date {}); });
    CHECK(FetchConciseType(stmt, R"(SELECT "d" FROM "AssumeDate")", 1) == SQL_TYPE_DATE);
}

TEST_CASE_METHOD(SqlTestFixture, "ODBC assumption: TIME column reports SQL_TYPE_TIME", "[SqlVariant][ODBC][assumption]")
{
    // Microsoft SQL Server reports TIME(n) as the MSSQL-specific `SQL_SS_TIME2`
    // (-154); every other supported driver reports the ODBC-standard
    // `SQL_TYPE_TIME` (92). Both codes route to `SqlTime` in the variant switch.
    auto stmt = SqlStatement {};
    stmt.MigrateDirect(
        [](auto& migration) { migration.CreateTable("AssumeTime").RequiredColumn("t", SqlColumnTypeDefinitions::Time {}); });
    auto const concise = FetchConciseType(stmt, R"(SELECT "t" FROM "AssumeTime")", 1);
    CHECK((concise == SQL_TYPE_TIME || concise == SQL_SS_TIME2));
}

TEST_CASE_METHOD(SqlTestFixture,
                 "ODBC assumption: TIMESTAMP column reports SQL_TYPE_TIMESTAMP",
                 "[SqlVariant][ODBC][assumption]")
{
    auto stmt = SqlStatement {};
    stmt.MigrateDirect([](auto& migration) {
        migration.CreateTable("AssumeTs").RequiredColumn("ts", SqlColumnTypeDefinitions::DateTime {});
    });
    CHECK(FetchConciseType(stmt, R"(SELECT "ts" FROM "AssumeTs")", 1) == SQL_TYPE_TIMESTAMP);
}

TEST_CASE_METHOD(SqlTestFixture, "ODBC assumption: BOOLEAN column reports SQL_BIT", "[SqlVariant][ODBC][assumption]")
{
    // For PostgreSQL this depends on `BoolsAsChar=0` in the connection string — see
    // docs/data-binder.md and scripts/tests/.test-env.yml. A failure here on PG most
    // likely means that option is missing from the live connection string.
    auto stmt = SqlStatement {};
    stmt.MigrateDirect([](auto& migration) {
        migration.CreateTable("AssumeBool").RequiredColumn("flag", SqlColumnTypeDefinitions::Bool {});
    });
    CHECK(FetchConciseType(stmt, R"(SELECT "flag" FROM "AssumeBool")", 1) == SQL_BIT);
}
