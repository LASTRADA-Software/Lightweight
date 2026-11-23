// SPDX-License-Identifier: Apache-2.0

#include "Utils.hpp"

#include <Lightweight/Lightweight.hpp>

#include <reflection-cpp/reflection.hpp>

#include <catch2/catch_session.hpp>
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <array>
#include <concepts>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <format>
#include <functional>
#include <limits>
#include <numbers>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

// NOLINTBEGIN(readability-container-size-empty, bugprone-unchecked-optional-access)

#if defined(_MSC_VER)
    // Disable the warning C4834: discarding return value of function with 'nodiscard' attribute.
    // Because we are simply testing and demonstrating the library and not using it in production code.
    #pragma warning(disable : 4834)
#endif

using namespace std::chrono_literals;
using namespace std::string_literals;
using namespace std::string_view_literals;
using namespace Lightweight;

struct CustomType
{
    int value;

    constexpr auto operator<=>(CustomType const&) const noexcept = default;
};

std::ostream& operator<<(std::ostream& os, CustomType const& value)
{
    return os << std::format("CustomType({})", value.value);
}

namespace std
{

std::ostream& operator<<(std::ostream& os, std::u8string const& value)
{
    return os << std::format("\"{}\"", value);
}

std::ostream& operator<<(std::ostream& os, std::u8string_view value)
{
    return os << std::format("\"{}\"", (char const*) value.data());
}

} // namespace std

template <>
struct Lightweight::SqlDataBinder<CustomType>
{
    static constexpr auto ColumnType = SqlDataBinder<decltype(CustomType::value)>::ColumnType;

    static SQLRETURN InputParameter(SQLHSTMT hStmt,
                                    SQLUSMALLINT column,
                                    CustomType const& value,
                                    SqlDataBinderCallback& cb) noexcept
    {
        return SqlDataBinder<int>::InputParameter(hStmt, column, value.value, cb);
    }

    static SQLRETURN OutputColumn(
        SQLHSTMT hStmt, SQLUSMALLINT column, CustomType* result, SQLLEN* indicator, SqlDataBinderCallback& callback) noexcept
    {
        callback.PlanPostProcessOutputColumn([result]() { result->value = PostProcess(result->value); });
        return SqlDataBinder<int>::OutputColumn(hStmt, column, &result->value, indicator, callback);
    }

    static SQLRETURN GetColumn(
        SQLHSTMT hStmt, SQLUSMALLINT column, CustomType* result, SQLLEN* indicator, SqlDataBinderCallback const& cb) noexcept
    {
        return SqlDataBinder<int>::GetColumn(hStmt, column, &result->value, indicator, cb);
    }

    static constexpr int PostProcess(int value) noexcept
    {
        return value; // | 0x01;
    }

    static std::string Inspect(CustomType const& value) noexcept
    {
        return std::format("CustomType({})", value.value);
    }
};

TEST_CASE_METHOD(SqlTestFixture, "SqlFixedString: retrieval", "[SqlFixedString]")
{
    SqlFixedString<8> sqlFixedString = "Blurb";

    auto const stdStringView = sqlFixedString.ToStringView();
    CHECK(stdStringView == "Blurb");

    auto const stdString = sqlFixedString.ToString();
    CHECK(stdString == "Blurb");
}

TEST_CASE_METHOD(SqlTestFixture, "SqlFixedString: resize and clear", "[SqlFixedString]")
{
    SqlFixedString<8> str;

    REQUIRE(str.size() == 0);
    REQUIRE(str.empty());

    str.resize(1);
    REQUIRE(!str.empty());
    REQUIRE(str.size() == 1);

    str.resize(4);
    REQUIRE(str.size() == 4);

    // one-off overflow truncates
    str.resize(9);
    REQUIRE(str.size() == 8);

    // resize down
    str.resize(2);
    REQUIRE(str.size() == 2);

    str.clear();
    REQUIRE(str.empty());
}

TEST_CASE_METHOD(SqlTestFixture, "SqlFixedString: push_back and pop_back", "[SqlFixedString]")
{
    SqlFixedString<2> str;

    str.push_back('a');
    str.push_back('b');
    REQUIRE(str == "ab");

    // overflow: no-op (truncates)
    str.push_back('c');
    REQUIRE(str == "ab");

    str.pop_back();
    REQUIRE(str == "a");

    str.pop_back();
    REQUIRE(str.empty());

    // no-op
    str.pop_back();
    REQUIRE(str.empty());
}

TEST_CASE_METHOD(SqlTestFixture, "SqlFixedString: c_str", "[SqlFixedString]")
{
    SqlFixedString<12> str { "Hello, World" };
    str.resize(5);

    SqlFixedString<12> const& constStr = str;
    REQUIRE(constStr.c_str() == "Hello"sv);

    str.resize(2);
    REQUIRE(str.c_str() == "He"sv); // Call to `c_str()` also mutates [2] to NUL
}

TEST_CASE_METHOD(SqlTestFixture, "SqlFixedString: TrimRight", "[SqlFixedString]")
{
    SqlTrimmedFixedString<20> str { "Hello        " };
    SqlBasicStringOperations<SqlTrimmedFixedString<20>>::TrimRight(&str);
    REQUIRE(str == "Hello");
    SqlTrimmedWideFixedString<20> wstr { L"Hello        " };
    SqlBasicStringOperations<SqlTrimmedWideFixedString<20>>::TrimRight(&wstr);
    REQUIRE(wstr == L"Hello");
    SqlTrimmedWideFixedString<20> wstrWrongIndicator { L"Hello        " };
    REQUIRE(wstrWrongIndicator != L"Hello");
}

TEST_CASE_METHOD(SqlTestFixture, "SqlVariant: GetColumn in-place store variant", "[SqlDataBinder]")
{
    auto stmt = SqlStatement {};
    CreateEmployeesTable(stmt);

    stmt.Prepare(R"(INSERT INTO "Employees" ("FirstName", "LastName", "Salary") VALUES (?, ?, ?))");
    (void) stmt.Execute("Alice", SqlNullValue, 50'000);

    auto cursor = stmt.ExecuteDirect(R"(SELECT "FirstName", "LastName", "Salary" FROM "Employees")");
    (void) cursor.FetchRow();

    CHECK(cursor.GetColumn<std::string>(1) == "Alice");

    SqlVariant lastName;
    CHECK(!cursor.GetColumn(2, &lastName));
    CHECK(lastName.IsNull());

    SqlVariant salary;
    CHECK(cursor.GetColumn(3, &salary));
    CHECK(salary.TryGetInt().value_or(0) == 50'000);
}

TEST_CASE_METHOD(SqlTestFixture, "SqlVariant: NULL values", "[SqlDataBinder],[SqlVariant]")
{
    auto stmt = SqlStatement();
    (void) stmt.ExecuteDirect("CREATE TABLE Test (Remarks VARCHAR(50) NULL)");

    SECTION("Test for inserting/getting NULL values")
    {
        stmt.Prepare("INSERT INTO Test (Remarks) VALUES (?)");
        (void) stmt.Execute(SqlNullValue);

        auto reader = stmt.ExecuteDirect("SELECT Remarks FROM Test");
        (void) reader.FetchRow();

        auto const actual = reader.GetColumn<SqlVariant>(1);
        CHECK(std::holds_alternative<SqlNullType>(actual.value));
    }

    SECTION("Using ExecuteDirectScalar")
    {
        stmt.Prepare("INSERT INTO Test (Remarks) VALUES (?)");
        (void) stmt.Execute(SqlNullValue);
        auto const result = stmt.ExecuteDirectScalar<SqlVariant>("SELECT Remarks FROM Test");
        CHECK(result.IsNull());
    }
}

TEST_CASE_METHOD(SqlTestFixture, "SqlVariant: SqlGuid", "[SqlDataBinder],[SqlVariant]")
{
    auto stmt = SqlStatement {};
    stmt.MigrateDirect(
        [](auto& migration) { migration.CreateTable("Test").Column("Value", SqlColumnTypeDefinitions::Guid {}); });

    using namespace std::chrono_literals;
    auto const expectedVariant = SqlVariant { SqlGuid::Create() };
    auto const& expectedValue = std::get<SqlGuid>(expectedVariant.value);

    stmt.Prepare(stmt.Query("Test").Insert().Set("Value", SqlWildcard));
    (void) stmt.Execute(expectedVariant);

    {
        auto reader = stmt.ExecuteDirect(stmt.Query("Test").Select().Field("Value").All());
        (void) reader.FetchRow();
        auto const actualVariant = reader.GetColumn<SqlVariant>(1);
        CHECK(actualVariant.TryGetGuid().value_or(SqlGuid {}) == expectedValue);
    }

    // Test for inserting/getting NULL values
    (void) stmt.ExecuteDirect(stmt.Query("Test").Delete());
    stmt.Prepare(stmt.Query("Test").Insert().Set("Value", SqlWildcard));
    (void) stmt.Execute(SqlNullValue);
    {
        auto reader = stmt.ExecuteDirect(stmt.Query("Test").Select().Field("Value").All());
        (void) reader.FetchRow();
        auto const actualVariant = reader.GetColumn<SqlVariant>(1);
        CHECK(actualVariant.IsNull());
        CHECK(actualVariant.TryGetGuid().has_value() == false);
    }

    // Test for TryGetGuid() on non-GUID variant
    auto const nonGuidVariant = SqlVariant { 42 };
    CHECK_THROWS_AS(nonGuidVariant.TryGetGuid(), std::bad_variant_access);
}

// Regression test for https://github.com/LASTRADA-Software/Lightweight/issues/596:
// SqlDataBinder<SqlGuid> used to pass SqlGuid::data straight through as raw SQL_C_GUID bytes, but
// the SQL_C_GUID/Win32 GUID wire format transmits Data1/Data2/Data3 in native byte order, not the
// textual order SqlGuid::data is stored in. A WHERE-clause comparison against the exact literal
// string exercises the DBMS's own GUID<->text conversion, independent of (and previously
// disagreeing with) SqlDataBinder<SqlGuid>'s own insert/fetch round trip.
TEST_CASE_METHOD(SqlTestFixture,
                 "SqlGuid: inserted value matches the DB's own literal GUID string",
                 "[SqlDataBinder],[SqlGuid]")
{
    auto stmt = SqlStatement {};
    stmt.MigrateDirect(
        [](auto& migration) { migration.CreateTable("Test").Column("Value", SqlColumnTypeDefinitions::Guid {}); });

    constexpr std::string_view text = "1E772AED-3E73-4C72-8684-5DFFAA17330E";
    auto const guid = SqlGuid::TryParse(text);
    REQUIRE(guid.has_value());

    stmt.Prepare(stmt.Query("Test").Insert().Set("Value", SqlWildcard));
    (void) stmt.Execute(*guid);

    auto const matchCount =
        stmt.ExecuteDirectScalar<int>(std::format(R"(SELECT COUNT(*) FROM "Test" WHERE "Value" = '{}')", text));
    CHECK(matchCount.value_or(-1) == 1);

    // The value we read back through our own binder must still match what we wrote (and, by the
    // check above, what the DB itself considers that value to be).
    auto const readBack = stmt.ExecuteDirectScalar<SqlGuid>(R"(SELECT "Value" FROM "Test")");
    REQUIRE(readBack.has_value());
    CHECK(*readBack == *guid);
}

// Companion to the test above (issue #596): a GUID does not only live in a native GUID column —
// the same value is also stored as text, here embedded inside a larger binary payload (BLOB),
// where it is produced and consumed by to_string()/TryParse() and never passes through the raw
// SQL_C_GUID bind path. For the two representations to keep denoting the same value, the DBMS' own
// GUID<->text conversion has to agree with ours in both directions: a GUID bound through
// SqlDataBinder<SqlGuid> must match, inside the database, the string the BLOB carries for it, and
// a GUID the *database* parsed from such a string must read back through SqlDataBinder<SqlGuid> as
// the very same SqlGuid. Both fail — byte-reversed in the first three GUID groups — on MS SQL
// Server and PostgreSQL without the SwapGuidWireByteOrder() conversion, which the string/BLOB side
// never applies.
TEST_CASE_METHOD(SqlTestFixture,
                 "SqlGuid: native GUID column and GUID text stored in a BLOB denote the same value",
                 "[SqlDataBinder],[SqlGuid]")
{
    auto stmt = SqlStatement {};
    stmt.MigrateDirect([](auto& migration) {
        migration.CreateTable("Test")
            .Column("Id", SqlColumnTypeDefinitions::Integer {})
            .Column("NativeGuid", SqlColumnTypeDefinitions::Guid {})
            .Column("BlobPayload", SqlColumnTypeDefinitions::VarBinary { .size = 64 });
    });

    constexpr auto text = "1E772AED-3E73-4C72-8684-5DFFAA17330E"sv;
    auto const guid = SqlGuid::TryParse(text);
    REQUIRE(guid.has_value());

    // An opaque binary payload carrying the GUID as text at a known offset, mimicking a record
    // blob that embeds the identifier as a string rather than as a typed column.
    constexpr auto blobPrefix = "REF:"sv;
    constexpr auto blobSuffix = ";END"sv;
    auto const payloadText = std::format("{}{}{}", blobPrefix, text, blobSuffix);
    auto const payload = SqlBinary(payloadText.begin(), payloadText.end());

    // Row 1: the GUID enters the typed column through SqlDataBinder<SqlGuid>, alongside the BLOB.
    stmt.Prepare(
        stmt.Query("Test").Insert().Set("Id", SqlWildcard).Set("NativeGuid", SqlWildcard).Set("BlobPayload", SqlWildcard));
    (void) stmt.Execute(1, *guid, payload);

    // Row 2: the same GUID enters the typed column as a string literal the *database* parses.
    (void) stmt.ExecuteDirect(std::format(R"(INSERT INTO "Test" ("Id", "NativeGuid") VALUES (2, '{}'))", text));

    // Recover the GUID string from the BLOB exactly as application code would: read the payload
    // back byte-exact, then parse the identifier out of it.
    auto const blob = stmt.ExecuteDirectScalar<SqlBinary>(R"(SELECT "BlobPayload" FROM "Test" WHERE "Id" = 1)");
    REQUIRE(blob.has_value());
    auto const blobText = std::string_view(reinterpret_cast<char const*>(blob->data()), blob->size());
    REQUIRE(blobText == payloadText);
    auto const embeddedText = blobText.substr(blobPrefix.size(), text.size());
    REQUIRE(SqlGuid::TryParse(embeddedText) == guid);

    // Leg 1: inside the database, the typed-bound GUID must equal the string the BLOB carries.
    auto const matchCount = stmt.ExecuteDirectScalar<int>(
        std::format(R"(SELECT COUNT(*) FROM "Test" WHERE "Id" = 1 AND "NativeGuid" = '{}')", embeddedText));
    CHECK(matchCount.value_or(-1) == 1);

    // Leg 2: both rows — the one bound as a typed parameter and the one the database parsed from
    // the string form — must read back through SqlDataBinder<SqlGuid> as the same value.
    auto cursor = stmt.ExecuteDirect(R"(SELECT "NativeGuid" FROM "Test" ORDER BY "Id")");
    REQUIRE(cursor.FetchRow());
    auto const fromTypedInsert = cursor.GetColumn<SqlGuid>(1);
    REQUIRE(cursor.FetchRow());
    auto const fromStringInsert = cursor.GetColumn<SqlGuid>(1);

    CHECK(fromTypedInsert == *guid);
    CHECK(fromStringInsert == *guid);
}

TEST_CASE_METHOD(SqlTestFixture, "SqlVariant: SqlDate", "[SqlDataBinder],[SqlVariant]")
{
    auto stmt = SqlStatement {};
    stmt.MigrateDirect(
        [](auto& migration) { migration.CreateTable("Test").Column("Value", SqlColumnTypeDefinitions::Date {}); });

    using namespace std::chrono_literals;
    auto const expected = SqlVariant { SqlDate { 2017y, std::chrono::August, 16d } };
    auto const& expectedDateTime = std::get<SqlDate>(expected.value);

    stmt.Prepare(stmt.Query("Test").Insert().Set("Value", SqlWildcard));
    (void) stmt.Execute(expected);

    {
        auto reader = stmt.ExecuteDirect(stmt.Query("Test").Select().Field("Value").All());
        (void) reader.FetchRow();
        auto const actual = reader.GetColumn<SqlVariant>(1);
        CHECK(actual.TryGetDate().value_or(SqlDate {}) == expectedDateTime);
    }

    // Test for inserting/getting NULL values
    (void) stmt.ExecuteDirect(stmt.Query("Test").Delete());
    stmt.Prepare(stmt.Query("Test").Insert().Set("Value", SqlWildcard));
    (void) stmt.Execute(SqlNullValue);
    auto const result = stmt.ExecuteDirectScalar<SqlVariant>(stmt.Query("Test").Select().Field("Value").All());
    CHECK(result.IsNull());
}

TEST_CASE_METHOD(SqlTestFixture, "SqlVariant: SqlTime", "[SqlDataBinder],[SqlVariant]")
{
    auto stmt = SqlStatement {};
    stmt.MigrateDirect(
        [](auto& migration) { migration.CreateTable("Test").Column("Value", SqlColumnTypeDefinitions::Time {}); });

    using namespace std::chrono_literals;
    auto const expected = SqlVariant { SqlTime { 12h, 34min, 56s } };

    stmt.Prepare(stmt.Query("Test").Insert().Set("Value", SqlWildcard));
    (void) stmt.Execute(expected);

    auto const actual = stmt.ExecuteDirectScalar<SqlVariant>(stmt.Query("Test").Select().Field("Value").All());

    if (stmt.Connection().ServerType() == SqlServerType::POSTGRESQL)
    {
        WARN("PostgreSQL seems to report SQL_TYPE_DATE here. Skipping check, that would fail otherwise.");
        // TODO: Find out why PostgreSQL reports SQL_TYPE_DATE instead of SQL_TYPE_TIME for SQL column type TIME.
        return;
    }

    REQUIRE(actual.TryGetTime().has_value());
    if (actual.TryGetTime())
        CHECK(actual.TryGetTime().value() == std::get<SqlTime>(expected.value));

    // Test for inserting/getting NULL values
    (void) stmt.ExecuteDirect(stmt.Query("Test").Delete());
    stmt.Prepare(stmt.Query("Test").Insert().Set("Value", SqlWildcard));
    (void) stmt.Execute(SqlNullValue);
    auto const result = stmt.ExecuteDirectScalar<SqlVariant>(stmt.Query("Test").Select().Field("Value").All());
    CHECK(result.IsNull());
}

TEST_CASE_METHOD(SqlTestFixture, "GetColumn at the exact internal buffer boundary", "[SqlDataBinder]")
{
    // The chunked SQLGetData reader starts with a 255-char buffer. A value whose length EXACTLY
    // fills that buffer still truncates for character types (the driver spends the final slot on
    // the NUL terminator), so the continuation fetch must run for indicator == buffer size —
    // a strict '>' comparison silently loses the last character.
    auto stmt = SqlStatement {};
    stmt.MigrateDirect([](auto& migration) {
        migration.CreateTable("Test").Column("Value", SqlColumnTypeDefinitions::NVarchar { .size = 300 });
    });

    for (auto const length: { std::size_t { 254 }, std::size_t { 255 }, std::size_t { 256 } })
    {
        auto const expected = std::string(length, 'x');
        (void) stmt.ExecuteDirect(stmt.Query("Test").Delete());
        stmt.Prepare(stmt.Query("Test").Insert().Set("Value", SqlWildcard));
        (void) stmt.Execute(expected);

        {
            auto cursor = stmt.ExecuteDirect(stmt.Query("Test").Select().Field("Value").All());
            (void) cursor.FetchRow();
            auto const narrow = cursor.GetNullableColumn<std::string>(1);
            REQUIRE(narrow.has_value());
            CHECK(narrow->size() == length);
        }
        {
            auto cursor = stmt.ExecuteDirect(stmt.Query("Test").Select().Field("Value").All());
            (void) cursor.FetchRow();
            auto const wide = cursor.GetNullableColumn<std::u16string>(1);
            REQUIRE(wide.has_value());
            CHECK(wide->size() == length);
        }
    }
}

TEST_CASE_METHOD(SqlTestFixture, "InputParameter and GetColumn for very large values", "[SqlDataBinder]")
{
    auto stmt = SqlStatement {};
    auto const expectedText = MakeLargeText(8 * 1000);
    stmt.MigrateDirect([size = expectedText.size()](auto& migration) {
        migration.CreateTable("Test").Column("Value", SqlColumnTypeDefinitions::Text { size });
    });
    stmt.Prepare(stmt.Query("Test").Insert().Set("Value", SqlWildcard));
    (void) stmt.Execute(expectedText);

    SECTION("check handling for explicitly fetched output columns")
    {
        auto cursor = stmt.ExecuteDirect(stmt.Query("Test").Select().Field("Value").All());
        (void) cursor.FetchRow();
        CHECK(cursor.GetColumn<std::string>(1) == expectedText);
    }

    SECTION("check handling for explicitly fetched output columns (in-place store)")
    {
        auto cursor = stmt.ExecuteDirect(stmt.Query("Test").Select().Field("Value").All());
        (void) cursor.FetchRow();
        std::string actualText;
        CHECK(cursor.GetColumn(1, &actualText));
        CHECK(actualText == expectedText);
    }

    SECTION("check handling for bound output columns")
    {
        stmt.Prepare(stmt.Query("Test").Select().Field("Value").All());
        auto cursor = stmt.Execute();

        // Intentionally an empty string, auto-growing behind the scenes
        std::string actualText;

        // For Microsoft SQL Server, we need to allocate a large enough buffer for the output column.
        // Because MS SQL's ODBC driver does not support SQLGetData after SQLFetch for truncated data, it seems.
        if (stmt.Connection().ServerType() == SqlServerType::MICROSOFT_SQL)
        {
            WARN("Preallocate the buffer for MS SQL Server");
            actualText = std::string(expectedText.size() + 1, '\0');
        }

        cursor.BindOutputColumns(&actualText);
        (void) cursor.FetchRow();
        REQUIRE(actualText.size() == expectedText.size());
        CHECK(actualText == expectedText);
    }
}

TEST_CASE_METHOD(SqlTestFixture, "SqlDataBinder: Unicode", "[SqlDataBinder],[Unicode]")
{
    auto stmt = SqlStatement {};

    if (stmt.Connection().ServerType() == SqlServerType::SQLITE)
        // SQLite does UTF-8 by default, so we need to switch to UTF-16
        (void) stmt.ExecuteDirect("PRAGMA encoding = 'UTF-16'");

    // Create table with Unicode column.
    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.CreateTable("Test").Column("Value", SqlColumnTypeDefinitions::NVarchar(50));
    });

    stmt.Prepare(stmt.Query("Test").Insert().Set("Value", SqlWildcard));

    // Insert via wide string literal
    (void) stmt.Execute(WTEXT("Wide string literal \U0001F600"));

    // Insert via wide string view
    (void) stmt.Execute(WideStringView(WTEXT("Wide string literal \U0001F600")));

    // Insert via wide string object
    WideString const inputValue = WTEXT("Wide string literal \U0001F600");
    (void) stmt.Execute(inputValue);

    {
        auto reader = stmt.ExecuteDirect(stmt.Query("Test").Select().Field("Value").All());

        // Fetch and check GetColumn for wide string
        (void) reader.FetchRow();
        auto const actualValue = reader.GetColumn<WideString>(1);
        CHECK(actualValue == inputValue);

        // Bind output column, fetch, and check result in output column for wide string
        WideString actualValue2;
        reader.BindOutputColumns(&actualValue2);
        (void) reader.FetchRow();
        CHECK(actualValue2 == inputValue);
    }

    SECTION("Test for inserting/getting NULL VALUES")
    {
        (void) stmt.ExecuteDirect(stmt.Query("Test").Delete());
        stmt.Prepare(stmt.Query("Test").Insert().Set("Value", SqlWildcard));
        (void) stmt.Execute(SqlNullValue);
        auto const result = stmt.ExecuteDirectScalar<WideString>(stmt.Query("Test").Select().Field("Value").First());
        CHECK(!result.has_value());
    }
}

TEST_CASE_METHOD(SqlTestFixture, "SqlDataBinder: Unicode mixed", "[SqlDataBinder],[Unicode]")
{
    auto stmt = SqlStatement {};

    if (stmt.Connection().ServerType() == SqlServerType::SQLITE)
        // SQLite does UTF-8 by default, so we need to switch to UTF-16
        (void) stmt.ExecuteDirect("PRAGMA encoding = 'UTF-16'");

    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.CreateTable("Test").Column("Value", SqlColumnTypeDefinitions::NVarchar(10));
    });

    {
        auto constexpr inputValue = u8"The \u00f6"sv;
        auto constexpr expectedWideValue = WTEXT("The \u00f6");

        // Write value: UTF-8 encoded
        stmt.Prepare(stmt.Query("Test").Insert().Set("Value", SqlWildcard));
        (void) stmt.Execute(inputValue);

        // Read value: UTF-16 encoded
        auto actualValue = stmt.ExecuteDirectScalar<WideString>(stmt.Query("Test").Select().Field("Value").First());
        REQUIRE(actualValue.has_value());
        CHECK(*actualValue == expectedWideValue); // NOLINT(bugprone-unchecked-optional-access)
    }
}

TEST_CASE_METHOD(SqlTestFixture, "SqlNumeric", "[SqlDataBinder],[SqlNumeric]")
{
    SECTION("Positive number")
    {
        auto const expectedValue = SqlNumeric<10, 2> { 87654321.99 };
        INFO(expectedValue);
        CHECK_THAT(expectedValue.ToDouble(), Catch::Matchers::WithinAbs(87654321.99, 0.001));
        CHECK_THAT(expectedValue.ToFloat(), Catch::Matchers::WithinAbs(87654321.99F, 0.001));
        CHECK(expectedValue.ToString() == "87654321.99");
    }

    SECTION("Negative number")
    {
        auto const expectedValue = SqlNumeric<10, 2> { -123.45 };
        INFO(expectedValue);
        CHECK_THAT(expectedValue.ToDouble(), Catch::Matchers::WithinAbs(-123.45, 0.001));
        CHECK_THAT(expectedValue.ToFloat(), Catch::Matchers::WithinAbs(-123.45F, 0.001));
        CHECK(expectedValue.ToString() == "-123.45");
    }
}

TEST_CASE_METHOD(SqlTestFixture, "SqlNumeric.StoreAndLoad", "[SqlDataBinder],[SqlNumeric]")
{
    // NOLINTBEGIN(bugprone-unchecked-optional-access)

    auto stmt = SqlStatement {};
    stmt.MigrateDirect([](auto& migration) {
        migration.CreateTable("Test").Column("Value", SqlColumnTypeDefinitions::Decimal { .precision = 10, .scale = 2 });
    });

    auto const inputValue = SqlNumeric<10, 2> { 99999999.99 };

    stmt.Prepare(stmt.Query("Test").Insert().Set("Value", SqlWildcard));
    (void) stmt.Execute(inputValue);

    // Check retrieval via type: string
    auto const receivedStr = stmt.ExecuteDirectScalar<std::string>(stmt.Query("Test").Select().Field("Value").All());
    CHECK(receivedStr == "99999999.99");

    // Check retrieval via type: double
    auto const receivedDouble = stmt.ExecuteDirectScalar<double>(stmt.Query("Test").Select().Field("Value").All());
    REQUIRE(receivedDouble.has_value());
    CHECK_THAT(receivedDouble.value(), Catch::Matchers::WithinAbs(99999999.99, 0.001));

    // Check retrieval via type: SqlNumeric
    auto const receivedNumeric =
        stmt.ExecuteDirectScalar<SqlNumeric<10, 2>>(stmt.Query("Test").Select().Field("Value").All());
    REQUIRE(receivedNumeric.has_value());
    CHECK_THAT(receivedNumeric->ToDouble(), Catch::Matchers::WithinAbs(99999999.99, 0.001));

    // NOLINTEND(bugprone-unchecked-optional-access)
}

// Largest relative error a value may carry after a round-trip through the `SQL_C_DOUBLE` fallback
// of `SqlDataBinder<SqlNumeric<P, S>>`.
//
// That path stores the value in an IEEE-754 double, which by definition carries only
// `std::numeric_limits<double>::digits10` (15) significant decimal digits; digit 16 and beyond are
// not defined. The bound is therefore one unit in the last *defined* significant digit, which for a
// decimal mantissa in [1, 10) is at most `10^-(digits10 - 1)` == 1e-14. It is stated per unit and
// not per half unit because the round-trip rounds at that granularity more than once — the driver's
// decimal-to-double conversion, the multiply by 10^Scale in ToUnscaledValue(), the 128-bit-to-double
// conversion and the final divide — so a single half-ulp claim would not hold by construction.
//
// The bound is deliberately independent of any observed error: exceeding it means the fallback lost
// a significant digit that a double is documented to preserve, which is a real defect.
constexpr double SqlNumericDoubleFallbackTolerance = [] {
    auto tolerance = 1.0;
    for ([[maybe_unused]] auto const digit: std::views::iota(1, std::numeric_limits<double>::digits10))
        tolerance /= 10.0;
    return tolerance;
}();

// Whether the driver in use transfers a numeric's full mantissa through `SQL_C_NUMERIC`.
//
// `SqlDataBinder<SqlNumeric<..>>::NativeNumericSupportIsBroken()` decides the binding path from
// `SqlServerType` alone, but losslessness is really a property of the *driver build*: the psqlODBC
// that ships for Windows converts the value via a double before filling `SQL_NUMERIC_STRUCT` (a
// 19-digit value arrives as the mantissa of its nearest double), while the Linux build hands over
// the mantissa verbatim. The strict assertion below is therefore scoped to where the guarantee is
// actually verified instead of being relaxed everywhere to whatever the weakest driver delivers.
//
// NB: `false` here does *not* mean "assert nothing". The narrowed branch below still pins the sign
// — which is exactly what an int64_t-carrier overflow destroys — and holds both the value and its
// rendering to the accuracy a double is documented to preserve. The carrier's own guarantee, that
// every digit up to SqlMaxNumericPrecision survives ToUnscaledValue(), is driver-independent and is
// asserted without a database in "SqlNumeric carries every digit up to SqlMaxNumericPrecision"
// (SqlNumericTests.cpp), which runs on every platform.
constexpr bool SqlNumericNativeMantissaIsLossless =
#if defined(_WIN32)
    false;
#else
    true;
#endif

// Scale used by the maximum-precision probe below: enough fractional digits that the value exercises
// the scaling path, few enough that the column stays a realistic `DECIMAL(p, 4)` — the shape of
// MS SQL Server's `money`.
constexpr std::size_t SqlNumericProbeScale = 4;

// The digits of the probe value: `1234567890123...`, truncated to `precision` digits. A repeating
// ramp rather than all-nines so that a dropped or reordered digit is visible in the failure output.
static std::string MakeNumericProbeDigits(std::size_t precision)
{
    std::string digits;
    for (auto const index: std::views::iota(std::size_t { 0 }, precision))
        digits.push_back(static_cast<char>('0' + ((index + 1) % 10)));
    return digits;
}

TEST_CASE_METHOD(SqlTestFixture, "SqlNumeric.MaxPrecisionRoundTrip", "[SqlDataBinder],[SqlNumeric]")
{
    // Round-trips a value at exactly `SqlMaxNumericPrecision` through a real driver. Nothing below
    // this width exercises the two limits the bound is derived from: the width of the unscaled
    // carrier, and the `long double` every accessor but ToUnscaledValue() and ToString() divides
    // through.
    //
    // The precision is taken from the bound rather than hard-coded at 19 so that the test follows
    // the bound if it ever moves. The bound is 19 on every toolchain, which makes this exactly
    // MS SQL Server's `money`, DECIMAL(19, 4) — what ddl2cpp emits (issue #519).
    constexpr auto precision = SqlMaxNumericPrecision;
    constexpr auto scale = SqlNumericProbeScale;
    using Probe = SqlNumeric<precision, scale>;

    auto const digits = MakeNumericProbeDigits(precision);
    auto const integralDigits = digits.substr(0, precision - scale);
    auto const expectedText = std::format("{}.{}", integralDigits, digits.substr(precision - scale));
    auto const expectedValue = std::stod(expectedText);

    auto stmt = SqlStatement {};
    stmt.MigrateDirect([](auto& migration) {
        migration.CreateTable("Test").Column("Value",
                                             SqlColumnTypeDefinitions::Decimal { .precision = precision, .scale = scale });
    });

    // Written as a plain SQL literal so the stored value carries all `precision` digits regardless
    // of what the C++ side can express.
    (void) stmt.ExecuteDirect(std::format(R"(INSERT INTO "Test" ("Value") VALUES ({}))", expectedText));

    auto const received = stmt.ExecuteDirectScalar<Probe>(stmt.Query("Test").Select().Field("Value").All());
    REQUIRE(received.has_value());

    // NOLINTBEGIN(bugprone-unchecked-optional-access) - guarded by the REQUIRE above

    // How many of the digits survive is a property of the *binding*, not of the declared precision,
    // so gate on exactly the predicate the binder itself uses, plus the driver-build caveat above.
    if (!SqlDataBinder<Probe>::NativeNumericSupportIsBroken(stmt.Connection().ServerType())
        && SqlNumericNativeMantissaIsLossless)
    {
        // Native SQL_C_NUMERIC (PostgreSQL on Linux): the mantissa is transferred verbatim, so every
        // digit comes back exactly. This is what the bound buys — one digit more and ToString()
        // would already be wrong.
        // The unscaled value is a `__int128` on this path, which std::format does not handle; at
        // `precision <= 19` digits its magnitude always fits a std::uint64_t, so narrow it there.
        CHECK(std::format("{}", static_cast<std::uint64_t>(received->ToUnscaledValue())) == digits);
        CHECK(received->ToString() == expectedText);
    }
    else
    {
        // Either the SQL_C_DOUBLE fallback (SQLite, MS SQL Server) or a driver that narrows through
        // a double on the native path (Windows psqlODBC). Only the leading `digits10` significant
        // digits are meaningful — but that is still a concrete claim, not a free pass:
        //
        //   - the sign must survive (this is what an int64_t-carrier overflow destroys: it turns
        //     the money maximum into its own negation),
        //   - the value must agree with the stored one to within one unit in the last digit a
        //     double defines,
        //   - and ToString() must render *that* value, not some other one — so the rendering is
        //     parsed back and held to the same bound.
        //
        // Deliberately no assertion on the digits of the rendered string. Rounding to `digits10`
        // significant digits can carry into the integral part (this probe's 15 integral digits plus
        // a fraction >= 0.5 round 123456789012345.6789 to 123456789012346), and how far a given
        // driver or `long double` narrows is exactly what is *not* guaranteed here.
        CHECK(received->ToDouble() > 0.0);
        CHECK_THAT(received->ToDouble(), Catch::Matchers::WithinRel(expectedValue, SqlNumericDoubleFallbackTolerance));
        CHECK_THAT(std::stod(received->ToString()),
                   Catch::Matchers::WithinRel(expectedValue, SqlNumericDoubleFallbackTolerance));
    }

    // NOLINTEND(bugprone-unchecked-optional-access)
}

TEST_CASE_METHOD(SqlTestFixture, "SqlNumeric.Digits10AreExactOnEveryBackend", "[SqlDataBinder],[SqlNumeric]")
{
    // Pins the accuracy floor that `docs/data-binder.md` promises for the SQL_C_DOUBLE fallback:
    // a value that fits into std::numeric_limits<double>::digits10 (15) significant decimal digits
    // round-trips *exactly*, on every backend and regardless of which binding path is taken.
    // 12345678901.2345 is 11 integral + 4 fractional = 15 significant digits.
    STATIC_CHECK(std::numeric_limits<double>::digits10 == 15);

    auto stmt = SqlStatement {};
    stmt.MigrateDirect([](auto& migration) {
        migration.CreateTable("Test").Column("Value", SqlColumnTypeDefinitions::Decimal { .precision = 15, .scale = 4 });
    });

    (void) stmt.ExecuteDirect(R"(INSERT INTO "Test" ("Value") VALUES (12345678901.2345))");

    auto const received = stmt.ExecuteDirectScalar<SqlNumeric<15, 4>>(stmt.Query("Test").Select().Field("Value").All());
    REQUIRE(received.has_value());
    CHECK(received->ToString() == "12345678901.2345"); // NOLINT(bugprone-unchecked-optional-access)
}

TEST_CASE_METHOD(SqlTestFixture, "SqlNumeric.PurelyFractional", "[SqlDataBinder],[SqlNumeric]")
{
    // DECIMAL(4, 4) — every digit sits after the decimal point, so the column holds [0, 1).
    // `SqlNumeric<4, 4>` is a legal instantiation: Scale may equal Precision (issue #519).
    auto stmt = SqlStatement {};
    stmt.MigrateDirect([](auto& migration) {
        migration.CreateTable("Test").Column("Value", SqlColumnTypeDefinitions::Decimal { .precision = 4, .scale = 4 });
    });

    auto const inputValue = SqlNumeric<4, 4> { 0.1234 };
    stmt.Prepare(stmt.Query("Test").Insert().Set("Value", SqlWildcard));
    (void) stmt.Execute(inputValue);

    auto const received = stmt.ExecuteDirectScalar<SqlNumeric<4, 4>>(stmt.Query("Test").Select().Field("Value").All());
    REQUIRE(received.has_value());
    CHECK(received->ToString() == "0.1234");                                    // NOLINT(bugprone-unchecked-optional-access)
    CHECK_THAT(received->ToDouble(), Catch::Matchers::WithinAbs(0.1234, 1e-9)); // NOLINT(bugprone-unchecked-optional-access)
    CHECK(static_cast<long long>(received->ToUnscaledValue()) == 1234);         // NOLINT(bugprone-unchecked-optional-access)
}

TEST_CASE("SqlDateTime construction", "[SqlDataBinder],[SqlDateTime]")
{
    namespace chrono = std::chrono;
    auto const baseDateTime = SqlDateTime(chrono::year(2025),
                                          chrono::January,
                                          chrono::day(2),
                                          chrono::hours(12),
                                          chrono::minutes(34),
                                          chrono::seconds(56),
                                          chrono::nanoseconds(123'456'700));
    CHECK(baseDateTime.sqlValue.year == 2025);
    CHECK(baseDateTime.sqlValue.month == 1);
    CHECK(baseDateTime.sqlValue.day == 2);
    CHECK(baseDateTime.sqlValue.hour == 12);
    CHECK(baseDateTime.sqlValue.minute == 34);
    CHECK(baseDateTime.sqlValue.second == 56);
    CHECK(baseDateTime.sqlValue.fraction == 123'456'700);
}

TEST_CASE("SqlDateTime construction from a date and a time of day", "[SqlDataBinder],[SqlDateTime]")
{
    using namespace std::chrono_literals;
    auto const date =
        std::chrono::year_month_day { std::chrono::year { 2025 }, std::chrono::January, std::chrono::day { 2 } };
    // A time of day of duration_type holds what system_clock counts: 100 ns (MSVC), 1 ns (libstdc++) or only
    // 1 us (libc++), so the 100 ns digit of 123'456'700 ns reaches the constructor only where the clock has it.
    auto const subseconds = std::chrono::duration_cast<SqlDateTime::duration_type>(123'456'700ns);
    auto const timeOfDay = std::chrono::hh_mm_ss<SqlDateTime::duration_type> { 12h + 34min + 56s + subseconds };
    auto const value = SqlDateTime { date, timeOfDay };

    // The fraction is the part below one second, not the whole time of day in nanoseconds.
    auto const fraction = std::chrono::duration_cast<std::chrono::nanoseconds>(subseconds);
    CHECK(value.sqlValue.hour == 12);
    CHECK(value.sqlValue.minute == 34);
    CHECK(value.sqlValue.second == 56);
    CHECK(value.sqlValue.fraction == fraction.count());
    CHECK(value.sqlValue.fraction >= 123'456'000);
    CHECK(
        value
        == SqlDateTime(std::chrono::year { 2025 }, std::chrono::January, std::chrono::day { 2 }, 12h, 34min, 56s, fraction));

    // A whole second has no fraction at all.
    CHECK(SqlDateTime { date, std::chrono::hh_mm_ss<SqlDateTime::duration_type> { 1s } }.sqlValue.fraction == 0);
}

TEST_CASE("SqlDateTime operations", "[SqlDataBinder],[SqlDateTime]")
{
    namespace chrono = std::chrono;
    auto const baseDateTime = SqlDateTime(chrono::year(2025),
                                          chrono::January,
                                          chrono::day(2),
                                          chrono::hours(12),
                                          chrono::minutes(34),
                                          chrono::seconds(56),
                                          chrono::nanoseconds(123'456'700));
    SECTION("plus")
    {
        auto const outputValue = SqlDateTime(chrono::year(2025),
                                             chrono::January,
                                             chrono::day(2),
                                             chrono::hours(13),
                                             chrono::minutes(4),
                                             chrono::seconds(56),
                                             chrono::nanoseconds(123'456'700));
        auto actualValue = baseDateTime + chrono::minutes(30);
        CHECK(actualValue == outputValue);
    }

    SECTION("minus minutes")
    {
        auto const outputValue = SqlDateTime(chrono::year(2025),
                                             chrono::January,
                                             chrono::day(2),
                                             chrono::hours(12),
                                             chrono::minutes(4),
                                             chrono::seconds(56),
                                             chrono::nanoseconds(123'456'700));
        auto actualValue = baseDateTime - chrono::minutes(30);
        CHECK(actualValue == outputValue);
    }

    SECTION("minus seconds")
    {
        auto const outputValue = SqlDateTime(chrono::year(2025),
                                             chrono::January,
                                             chrono::day(2),
                                             chrono::hours(12),
                                             chrono::minutes(33),
                                             chrono::seconds(56),
                                             chrono::nanoseconds(123'456'700));
        auto actualValue = baseDateTime - chrono::seconds(60);
        CHECK(actualValue == outputValue);
    }
}

// clang-format off
template <typename T>
struct TestTypeTraits;

template <>
struct TestTypeTraits<int8_t>
{
    static constexpr auto inputValue = (std::numeric_limits<int8_t>::max)();
    static constexpr auto expectedOutputValue = (std::numeric_limits<int8_t>::max)();
};

template <>
struct TestTypeTraits<int16_t>
{
    static constexpr auto inputValue = (std::numeric_limits<int16_t>::max)();
    static constexpr auto expectedOutputValue = (std::numeric_limits<int16_t>::max)();
};

template <>
struct TestTypeTraits<int32_t>
{
    static constexpr auto inputValue = (std::numeric_limits<int32_t>::max)();
    static constexpr auto expectedOutputValue = (std::numeric_limits<int32_t>::max)();
};

template <>
struct TestTypeTraits<int64_t>
{
    static constexpr auto inputValue = (std::numeric_limits<int64_t>::max)();
    static constexpr auto expectedOutputValue = (std::numeric_limits<int64_t>::max)();
};

template <>
struct TestTypeTraits<uint8_t>
{
    static constexpr auto inputValue = (std::numeric_limits<uint8_t>::max)();
    static constexpr auto expectedOutputValue = (std::numeric_limits<uint8_t>::max)();
};

template <>
struct TestTypeTraits<uint16_t>
{
    static constexpr auto inputValue = (std::numeric_limits<uint16_t>::max)();
    static constexpr auto expectedOutputValue = (std::numeric_limits<uint16_t>::max)();
};

template <>
struct TestTypeTraits<uint32_t>
{
    static constexpr auto inputValue = (std::numeric_limits<uint32_t>::max)();
    static constexpr auto expectedOutputValue = (std::numeric_limits<uint32_t>::max)();
};

template <>
struct TestTypeTraits<uint64_t>
{
    static constexpr auto inputValue = (std::numeric_limits<uint64_t>::max)();
    static constexpr auto expectedOutputValue = (std::numeric_limits<uint64_t>::max)();
};

template <>
struct TestTypeTraits<float>
{
    static constexpr auto inputValue = (std::numeric_limits<float>::max)();
    static constexpr auto expectedOutputValue = (std::numeric_limits<float>::max)();
};

template <>
struct TestTypeTraits<double>
{
    static constexpr auto inputValue =  std::numbers::pi_v<double>;
    static constexpr auto expectedOutputValue = std::numbers::pi_v<double>;
};

template <>
struct TestTypeTraits<CustomType>
{
    static constexpr auto inputValue = CustomType { 42 };
    static constexpr auto expectedOutputValue = CustomType { SqlDataBinder<CustomType>::PostProcess(42) };
};

template <>
struct TestTypeTraits<SqlTrimmedFixedString<20>>
{
    using ValueType = SqlTrimmedFixedString<20>;
    static constexpr auto sqlColumnTypeNameOverride = SqlColumnTypeDefinitions::Char { 20 };
    static constexpr auto inputValue = ValueType { "Hello " };
    static constexpr auto expectedOutputValue = ValueType { "Hello" };
};


template <>
struct TestTypeTraits<SqlTrimmedWideFixedString<20>>
{
    using ValueType = SqlTrimmedWideFixedString<20>;
    static constexpr auto sqlColumnTypeNameOverride = SqlColumnTypeDefinitions::NVarchar { .size = 20 };
    static constexpr auto inputValue = ValueType { L"Hello" };
    static constexpr auto expectedOutputValue = ValueType { L"Hello" };
};


template <>
struct TestTypeTraits<SqlAnsiString<20>>
{
    using ValueType = SqlAnsiString<20>;
    static constexpr auto sqlColumnTypeNameOverride = SqlColumnTypeDefinitions::Varchar { .size = 20 };
    static constexpr auto inputValue = ValueType { "Hello" };
    static constexpr auto expectedOutputValue = ValueType { "Hello" };
};

template <>
struct TestTypeTraits<SqlUtf16String<20>>
{
    using ValueType = SqlUtf16String<20>;
    static constexpr auto sqlColumnTypeNameOverride = SqlColumnTypeDefinitions::NVarchar { .size = 20 };
    static constexpr auto inputValue = ValueType { u"Hello" };
    static constexpr auto expectedOutputValue = ValueType { u"Hello" };
};

template <>
struct TestTypeTraits<SqlUtf32String<20>>
{
    using ValueType = SqlUtf32String<20>;
    static constexpr auto sqlColumnTypeNameOverride = SqlColumnTypeDefinitions::NVarchar { .size = 20 };
    static constexpr auto inputValue = ValueType { U"Hello" };
    static constexpr auto expectedOutputValue = ValueType { U"Hello" };
};

template <>
struct TestTypeTraits<SqlWideString<20>>
{
    using ValueType = SqlWideString<20>;
    static constexpr auto sqlColumnTypeNameOverride = SqlColumnTypeDefinitions::NVarchar { .size = 20 };
    static constexpr auto inputValue = ValueType { L"Hello" };
    static constexpr auto expectedOutputValue = ValueType { L"Hello" };
};

template <>
struct TestTypeTraits<SqlText>
{
    static constexpr auto sqlColumnTypeNameOverride = SqlColumnTypeDefinitions::Text { 255 };
    static auto const inline inputValue = SqlText { "Hello, World!" };
    static auto const inline expectedOutputValue = SqlText { "Hello, World!" };
    static auto const inline outputInitializer = SqlText { std::string(255, '\0') };
};

template <>
struct TestTypeTraits<SqlDate>
{
    static constexpr auto inputValue = SqlDate { 2017y, std::chrono::August, 16d };
    static constexpr auto expectedOutputValue = SqlDate { 2017y, std::chrono::August, 16d };
};

template <>
struct TestTypeTraits<SqlTime>
{
    static constexpr auto inputValue = SqlTime { 12h, 34min, 56s };
    static constexpr auto expectedOutputValue = SqlTime { 12h, 34min, 56s };
};

template <>
struct TestTypeTraits<SqlDateTime>
{
    static constexpr auto inputValue = SqlDateTime { 2017y, std::chrono::August, 16d, 17h, 30min, 45s, 123'000'000ns };
    static constexpr auto expectedOutputValue = SqlDateTime { 2017y, std::chrono::August, 16d, 17h, 30min, 45s, 123'000'000ns };
};

template <>
struct TestTypeTraits<SqlGuid>
{
    static constexpr auto inputValue = SqlGuid::UnsafeParse("1e772aed-3e73-4c72-8684-5dffaa17330e");
    static constexpr auto expectedOutputValue = SqlGuid::UnsafeParse("1e772aed-3e73-4c72-8684-5dffaa17330e");
};

template <>
struct TestTypeTraits<SqlNumeric<5, 2>>
{
    static constexpr auto sqlColumnTypeNameOverride = SqlColumnTypeDefinitions::Decimal { .precision=5, .scale=2 };
    static const inline auto inputValue = SqlNumeric<5, 2> { 123.45 };
    static const inline auto expectedOutputValue = SqlNumeric<5, 2> { 123.45 };
};


template <>
struct TestTypeTraits<SqlNumeric<9, 6>>
{
    static constexpr auto sqlColumnTypeNameOverride = SqlColumnTypeDefinitions::Decimal { .precision=9, .scale=6 };
    static const inline auto inputValue = SqlNumeric<9, 6> { 123.456789 };
    static const inline auto expectedOutputValue = SqlNumeric<9, 6> { 123.456789 };
};

template <>
struct TestTypeTraits<SqlNumeric<10, 2>>
{
    static constexpr auto sqlColumnTypeNameOverride = SqlColumnTypeDefinitions::Decimal { .precision=10, .scale=2 };
    static const inline auto inputValue = SqlNumeric<10, 2> { 99'999'999.99 };
    static const inline auto expectedOutputValue = SqlNumeric<10, 2> { 99'999'999.99 };
};

template <>
struct TestTypeTraits<SqlNumeric<15, 2>>
{
    static constexpr auto sqlColumnTypeNameOverride = SqlColumnTypeDefinitions::Decimal { .precision=15, .scale=2 };
    static const inline auto inputValue = SqlNumeric<15, 2> { 123.45 };
    static const inline auto expectedOutputValue = SqlNumeric<15, 2> { 123.45 };
};

template <typename T>
T MakeStringOuputInitializer(SqlServerType serverType)
{
    if (serverType == SqlServerType::MICROSOFT_SQL)
        // For MS SQL Server, we need to allocate a large enough buffer for the output column.
        // Because MS SQL's ODBC driver does not support SQLGetData after SQLFetch for truncated data, it seems.
        return T(50, '\0');
    else
        return T {};
}

template <>
struct TestTypeTraits<std::string>
{
    static constexpr auto sqlColumnTypeNameOverride = SqlColumnTypeDefinitions::Varchar { .size = 50 };
    static auto const inline inputValue = std::string { "Alice" };
    static auto const inline expectedOutputValue = std::string { "Alice" };
    static auto const inline outputInitializer = &MakeStringOuputInitializer<std::string>;
};

template <>
struct TestTypeTraits<std::string_view>
{
    static constexpr auto sqlColumnTypeNameOverride = SqlColumnTypeDefinitions::Varchar { .size = 50 };
    static auto const inline inputValue = std::string_view { "Alice" };
    static auto const inline expectedOutputValue = std::string_view { "Alice" };
    static auto const inline outputInitializer = &MakeStringOuputInitializer<std::string>;
    using GetColumnTypeOverride = std::string;
};

template <>
struct TestTypeTraits<std::u8string>
{
    static auto constexpr sqlColumnTypeNameOverride = SqlColumnTypeDefinitions::NVarchar { .size = 50 };
    static auto const inline inputValue = u8"Hell\u00F6"s;
    static auto const inline expectedOutputValue = u8"Hell\u00F6"s;
    static auto const inline outputInitializer = &MakeStringOuputInitializer<std::u8string>;
};

template <>
struct TestTypeTraits<std::u8string_view>
{
    static constexpr auto sqlColumnTypeNameOverride = SqlColumnTypeDefinitions::NVarchar { .size = 50 };
    static auto const inline inputValue = std::u8string_view { u8"Hell\u00F6" };
    static auto const inline expectedOutputValue = std::u8string_view { u8"Hell\u00F6" };
    static auto const inline outputInitializer = &MakeStringOuputInitializer<std::u8string>;
    using GetColumnTypeOverride = std::u8string;
};

template <>
struct TestTypeTraits<std::u16string>
{
    static auto constexpr sqlColumnTypeNameOverride = SqlColumnTypeDefinitions::NVarchar { .size = 50 };
    static auto const inline inputValue = u"Alice"s;
    static auto const inline expectedOutputValue = u"Alice"s;
    static auto const inline outputInitializer = &MakeStringOuputInitializer<std::u16string>;
};

template <>
struct TestTypeTraits<std::u16string_view>
{
    static constexpr auto sqlColumnTypeNameOverride = SqlColumnTypeDefinitions::NVarchar { .size = 50 };
    static auto const inline inputValue = std::u16string_view { u"Alice" };
    static auto const inline expectedOutputValue = std::u16string_view { u"Alice" };
    static auto const inline outputInitializer = &MakeStringOuputInitializer<std::u16string>;
    using GetColumnTypeOverride = std::u16string;
};

template <>
struct TestTypeTraits<std::u32string>
{
    static auto constexpr sqlColumnTypeNameOverride = SqlColumnTypeDefinitions::NVarchar { .size = 50 };
    static auto const inline inputValue = U"Alice"s;
    static auto const inline expectedOutputValue = U"Alice"s;
    static auto const inline outputInitializer = &MakeStringOuputInitializer<std::u32string>;
};

template <>
struct TestTypeTraits<std::u32string_view>
{
    static constexpr auto sqlColumnTypeNameOverride = SqlColumnTypeDefinitions::NVarchar { .size = 50 };
    static auto const inline inputValue = std::u32string_view { U"Alice" };
    static auto const inline expectedOutputValue = std::u32string_view { U"Alice" };
    static auto const inline outputInitializer = &MakeStringOuputInitializer<std::u32string>;
    using GetColumnTypeOverride = std::u32string;
};

template <>
struct TestTypeTraits<std::wstring>
{
    static auto constexpr sqlColumnTypeNameOverride = SqlColumnTypeDefinitions::NVarchar { .size = 50 };
    static auto const inline inputValue = L"Alice"s;
    static auto const inline expectedOutputValue = L"Alice"s;
    static auto const inline outputInitializer = &MakeStringOuputInitializer<std::wstring>;
};

template <>
struct TestTypeTraits<std::wstring_view>
{
    static constexpr auto sqlColumnTypeNameOverride = SqlColumnTypeDefinitions::NVarchar { .size = 50 };
    static auto const inline inputValue = std::wstring_view { L"Alice" };
    static auto const inline expectedOutputValue = std::wstring_view { L"Alice" };
    static auto const inline outputInitializer = &MakeStringOuputInitializer<std::wstring>;
    using GetColumnTypeOverride = std::wstring;
};

template <>
struct TestTypeTraits<SqlBinary>
{
    static constexpr auto sqlColumnTypeNameOverride = SqlColumnTypeDefinitions::Binary { 50 };
    static auto const inline inputValue = SqlBinary { 0x00, 0x02, 0x03, 0x00, 0x05 };
    static auto const inline expectedOutputValue = SqlBinary { 0x00, 0x02, 0x03, 0x00, 0x05 };
};

template <>
struct TestTypeTraits<SqlDynamicBinary<8>>
{
    static auto constexpr sqlColumnTypeNameOverride = SqlColumnTypeDefinitions::VarBinary { .size = 50 };
    static auto const inline inputValue = SqlDynamicBinary<8> {{ 0x00, 0x02, 0x03, 0x00, 0x05, 0x06 }};
    static auto const inline expectedOutputValue = SqlDynamicBinary<8> {{ 0x00, 0x02, 0x03, 0x00, 0x05, 0x06 }};
};

using TypesToTest = std::tuple<
    CustomType,
    SqlBinary,
    SqlDate,
    SqlDateTime,
    SqlDynamicBinary<8>,
    SqlGuid,
    SqlNumeric<5, 2>,
    SqlNumeric<9, 6>,
    SqlNumeric<10, 2>,
    SqlNumeric<15, 2>,
    SqlAnsiString<20>,
    SqlUtf16String<20>,
    SqlUtf32String<20>,
    SqlWideString<20>,
    SqlText,
    SqlTime,
    SqlTrimmedFixedString<20>,
    SqlTrimmedWideFixedString<20>,
    double,
    float,
    int8_t,
    int16_t,
    int32_t,
    int64_t,
    uint8_t,
    // uint16_t, // (not supported by MS-SQL)
    // uint32_t, // (not supported by MS-SQL)
    // TODO: uint64_t,
    std::string,
    std::string_view,
    std::u8string,
    std::u8string_view,
    std::u16string,
    std::u16string_view,
    std::u32string,
    std::u32string_view,
    std::wstring,
    std::wstring_view
>;
// clang-format on

TEMPLATE_LIST_TEST_CASE("SqlDataBinder specializations", "[SqlDataBinder]", TypesToTest)
{
    SqlLogger::SetLogger(TestSuiteSqlLogger::GetLogger());

    GIVEN(Reflection::TypeNameOf<TestType>)
    {
        {
            auto stmt = SqlStatement {};
            SqlTestFixture::DropAllTablesInDatabase(stmt);
        }

        // Connecting to the database (using the default connection) the verbose way,
        // purely to demonstrate how to do it.
        auto conn = SqlConnection { SqlConnection::DefaultConnectionString() };

        if constexpr (requires { TestTypeTraits<TestType>::blacklist; })
        {
            for (auto const& [serverType, reason]: TestTypeTraits<TestType>::blacklist)
            {
                if (serverType == conn.ServerType())
                {
                    WARN("Skipping blacklisted test for " << Reflection::TypeNameOf<TestType> << ": " << reason);
                    return;
                }
            }
        }

        auto stmt = SqlStatement { conn };
        // This suite exercises the per-cell SqlDataBinder round-trip (including custom binders with
        // PostProcess hooks that the block-prefetch fast path intentionally defers to the per-row path).
        // Disable transparent prefetch so every fetch exercises the single-row binder under test.
        stmt.Connection().SetDefaultPrefetchDepth(1);

        auto const sqlColumnType = [&]() -> std::string {
            if constexpr (requires { TestTypeTraits<TestType>::sqlColumnTypeNameOverride; })
                return conn.QueryFormatter().ColumnType(TestTypeTraits<TestType>::sqlColumnTypeNameOverride);
            else
                return conn.QueryFormatter().ColumnType(SqlDataBinder<TestType>::ColumnType);
        }();

        (void) stmt.ExecuteDirect(std::format("CREATE TABLE Test (Value {} NULL)", sqlColumnType));

        WHEN("Inserting a value")
        {
            stmt.Prepare("INSERT INTO Test (Value) VALUES (?)");
            CAPTURE(TestTypeTraits<TestType>::inputValue);
            (void) stmt.Execute(TestTypeTraits<TestType>::inputValue);

            THEN("Retrieve value via GetColumn()")
            {
                auto cursor = stmt.ExecuteDirect("SELECT Value FROM Test");
                CAPTURE(cursor.FetchRow());
                if constexpr (std::is_convertible_v<TestType, double> && !std::integral<TestType>)
                {
                    auto const actualValue = cursor.GetColumn<TestType>(1);
                    CHECK_THAT(actualValue,
                               (Catch::Matchers::WithinAbs(double(TestTypeTraits<TestType>::expectedOutputValue), 0.001)));
                }
                else if constexpr (requires { typename TestTypeTraits<TestType>::GetColumnTypeOverride; })
                {
                    auto const actualValue = cursor.GetColumn<typename TestTypeTraits<TestType>::GetColumnTypeOverride>(1);
                    CHECK(actualValue == TestTypeTraits<TestType>::expectedOutputValue);
                }
                else
                {
                    auto const actualValue = cursor.GetColumn<TestType>(1);
                    CHECK(actualValue == TestTypeTraits<TestType>::expectedOutputValue);
                }
            }

            if constexpr (!requires { typename TestTypeTraits<TestType>::GetColumnTypeOverride; })
            {
                THEN("Retrieve value via BindOutputColumns()")
                {
                    auto cursor = stmt.ExecuteDirect("SELECT Value FROM Test");
                    auto actualValue = [&]() -> TestType {
                        if constexpr (requires(SqlServerType st) { TestTypeTraits<TestType>::outputInitializer(st); })
                            return TestTypeTraits<TestType>::outputInitializer(conn.ServerType());
                        else if constexpr (requires { TestTypeTraits<TestType>::outputInitializer; })
                            return TestTypeTraits<TestType>::outputInitializer;
                        else
                            return TestType {};
                    }();
                    cursor.BindOutputColumns(&actualValue);
                    (void) cursor.FetchRow();
                    if constexpr (std::is_convertible_v<TestType, double> && !std::integral<TestType>)
                        CHECK_THAT(
                            double(actualValue),
                            (Catch::Matchers::WithinAbs(double(TestTypeTraits<TestType>::expectedOutputValue), 0.001)));
                    else
                        CHECK(actualValue == TestTypeTraits<TestType>::expectedOutputValue);
                }
            }
        }

        if constexpr (!requires { typename TestTypeTraits<TestType>::GetColumnTypeOverride; })
        {
            WHEN("Inserting a NULL value")
            {
                stmt.Prepare("INSERT INTO Test (Value) VALUES (?)");
                (void) stmt.Execute(SqlNullValue);

                THEN("Retrieve value via GetNullableColumn()")
                {
                    auto cursor = stmt.ExecuteDirect("SELECT Value FROM Test");
                    (void) cursor.FetchRow();
                    CHECK(!cursor.GetNullableColumn<TestType>(1).has_value());
                }

                THEN("Retrieve value via GetColumn()")
                {
                    auto cursor = stmt.ExecuteDirect("SELECT Value FROM Test");
                    (void) cursor.FetchRow();
                    CHECK_THROWS_AS(cursor.GetColumn<TestType>(1), std::runtime_error);
                }

                THEN("Retrieve value via BindOutputColumns()")
                {
                    stmt.Prepare("SELECT Value FROM Test");
                    auto cursor = stmt.Execute();
                    auto actualValue = std::optional<TestType> {};
                    cursor.BindOutputColumns(&actualValue);
                    (void) cursor.FetchRow();
                    CHECK(!actualValue.has_value());
                }
            }
        }
    }
}

struct WideStringFromVarcharEntity
{
    Field<SqlWideString<100>> value;
    static constexpr std::string_view TableName = "WideStringFromVarcharTest";
};

TEST_CASE_METHOD(SqlTestFixture, "SqlWideString read from VARCHAR column", "[SqlDataBinder],[Unicode]")
{
    auto stmt = SqlStatement {};

    // Create a table with a VARCHAR(100) column (narrow/ANSI, not wide/Unicode NVarchar).
    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.CreateTable("WideStringFromVarcharTest")
            .Column("value", SqlColumnTypeDefinitions::Varchar { .size = 100 });
    });

    stmt.Prepare(stmt.Query("WideStringFromVarcharTest").Insert().Set("value", SqlWildcard));

    SECTION("short value")
    {
        std::ignore = stmt.Execute("Hello, World!"sv);
        auto constexpr expectedWide = SqlWideString<100> { L"Hello, World!" };

        SECTION("BindOutputColumns")
        {
            stmt.Prepare(stmt.Query("WideStringFromVarcharTest").Select().Field("value").All());
            auto reader = stmt.Execute();
            SqlWideString<100> actual;
            reader.BindOutputColumns(&actual);
            (void) reader.FetchRow();
            CHECK(actual == expectedWide);
        }

        SECTION("entity via DataMapper")
        {
            auto dm = DataMapper();
            auto const result = dm.Query<WideStringFromVarcharEntity>().First();
            REQUIRE(result.has_value());
            CHECK(result->value.Value() == expectedWide);
        }
    }

    SECTION("max-width value")
    {
        auto const narrowStr = std::string(100, 'A');
        auto const expectedWide = SqlWideString<100> { std::wstring(100, L'A') };
        std::ignore = stmt.Execute(narrowStr);

        SECTION("BindOutputColumns")
        {
            stmt.Prepare(stmt.Query("WideStringFromVarcharTest").Select().Field("value").All());
            auto reader = stmt.Execute();
            SqlWideString<100> actual;
            reader.BindOutputColumns(&actual);
            (void) reader.FetchRow();
            CHECK(actual == expectedWide);
        }

        SECTION("entity via DataMapper")
        {
            auto dm = DataMapper();
            auto const result = dm.Query<WideStringFromVarcharEntity>().First();
            REQUIRE(result.has_value());
            CHECK(result->value.Value() == expectedWide);
        }
    }

    SECTION("umlaut value")
    {
        // Insert UTF-8 encoded umlauts into VARCHAR; the ODBC driver converts to wide on read-back.
        std::ignore = stmt.Execute(u8"Straße mit Häusern"sv);
        auto constexpr expectedWide = SqlWideString<100> { L"Straße mit Häusern" };

        SECTION("BindOutputColumns")
        {
            stmt.Prepare(stmt.Query("WideStringFromVarcharTest").Select().Field("value").All());
            auto reader = stmt.Execute();
            SqlWideString<100> actual;
            reader.BindOutputColumns(&actual);
            (void) reader.FetchRow();
            CHECK(actual == expectedWide);
        }

        SECTION("entity via DataMapper")
        {
            auto dm = DataMapper();
            auto const result = dm.Query<WideStringFromVarcharEntity>().First();
            REQUIRE(result.has_value());
            CHECK(result->value.Value() == expectedWide);
        }
    }

    SECTION("max-width umlaut value")
    {
        // Build a narrow UTF-8 string of exactly 100 umlaut characters (ä, U+00E4).
        // Each ä occupies 2 UTF-8 bytes (0xC3 0xA4), so the narrow payload is 200 bytes.
        // VARCHAR(100) with character semantics (SQLite, PostgreSQL UTF-8) holds 100 chars.
        // MSSQL VARCHAR(100) uses byte semantics (CP1252), so 200 bytes don't fit;
        // the MSSQL equivalent is covered by the "max-width codepage umlaut value" section.
        if (stmt.Connection().ServerType() == SqlServerType::MICROSOFT_SQL)
            SKIP();

        auto narrowStr = std::string {};
        narrowStr.reserve(200);
        for ([[maybe_unused]] auto const _: std::views::iota(0, 100))
            narrowStr.append("\xc3\xa4", 2); // UTF-8 encoding of ä (U+00E4)

        auto const expectedWide = SqlWideString<100> { std::wstring(100, L'ä') };
        [[maybe_unused]] auto cursor = stmt.Execute(narrowStr);

        SECTION("BindOutputColumns")
        {
            stmt.Prepare(stmt.Query("WideStringFromVarcharTest").Select().Field("value").All());
            auto reader = stmt.Execute();
            SqlWideString<100> actual;
            reader.BindOutputColumns(&actual);
            (void) reader.FetchRow();
            CHECK(actual == expectedWide);
        }

        SECTION("entity via DataMapper")
        {
            auto dm = DataMapper();
            auto const result = dm.Query<WideStringFromVarcharEntity>().First();
            REQUIRE(result.has_value());
            CHECK(result->value.Value() == expectedWide);
        }
    }

    SECTION("codepage umlaut value (SQLite)")
    {
        // On SQLite the ODBC driver stores narrow bytes without any encoding validation
        // and naively widens each byte to wchar_t on read-back (Latin-1 widening).
        // For bytes in 0xA0-0xFF the Latin-1 code point equals the Unicode code point,
        // so the raw Windows-1252 bytes round-trip correctly without any conversion.
        if (stmt.Connection().ServerType() != SqlServerType::SQLITE)
            SKIP();

        std::ignore = stmt.Execute("Stra\xdf"
                                   "e mit H\xe4usern"sv);
        auto constexpr expectedWide = SqlWideString<100> { L"Straße mit Häusern" };

        SECTION("BindOutputColumns")
        {
            stmt.Prepare(stmt.Query("WideStringFromVarcharTest").Select().Field("value").All());
            auto reader = stmt.Execute();
            SqlWideString<100> actual;
            reader.BindOutputColumns(&actual);
            (void) reader.FetchRow();
            CHECK(actual == expectedWide);
        }

        SECTION("entity via DataMapper")
        {
            auto dm = DataMapper();
            auto const result = dm.Query<WideStringFromVarcharEntity>().First();
            REQUIRE(result.has_value());
            CHECK(result->value.Value() == expectedWide);
        }
    }

    SECTION("codepage umlaut value")
    {
        // Raw Windows-1252 bytes cannot be inserted via ODBC on Linux: the driver treats
        // narrow strings as UTF-8 and replaces invalid sequences with '?'.
        // Use SQL Server's CHAR(n) function to store the raw CP1252 bytes directly,
        // bypassing any ODBC encoding layer. Only CP1252-collation databases are meaningful.
        if (stmt.Connection().ServerType() != SqlServerType::MICROSOFT_SQL)
            SKIP();

        std::ignore = stmt.ExecuteDirect("INSERT INTO WideStringFromVarcharTest (value) "
                                         "VALUES ('Stra' + CHAR(223) + 'e mit H' + CHAR(228) + 'usern')");
        auto constexpr expectedWide = SqlWideString<100> { L"Straße mit Häusern" };

        SECTION("BindOutputColumns")
        {
            stmt.Prepare(stmt.Query("WideStringFromVarcharTest").Select().Field("value").All());
            auto reader = stmt.Execute();
            SqlWideString<100> actual;
            reader.BindOutputColumns(&actual);
            (void) reader.FetchRow();
            CHECK(actual == expectedWide);
        }

        SECTION("entity via DataMapper")
        {
            auto dm = DataMapper();
            auto const result = dm.Query<WideStringFromVarcharEntity>().First();
            REQUIRE(result.has_value());
            CHECK(result->value.Value() == expectedWide);
        }
    }

    SECTION("max-width codepage umlaut value (SQLite)")
    {
        // Same rationale as "codepage umlaut value (SQLite)": raw bytes flow through
        // unmodified on SQLite, so 100 'ä' bytes (0xE4) widen to 100 L'\u00e4' on read-back.
        if (stmt.Connection().ServerType() != SqlServerType::SQLITE)
            SKIP();

        std::ignore = stmt.Execute(std::string(100, '\xe4'));
        auto const expectedWide = SqlWideString<100> { std::wstring(100, L'\u00e4') };

        SECTION("BindOutputColumns")
        {
            stmt.Prepare(stmt.Query("WideStringFromVarcharTest").Select().Field("value").All());
            auto reader = stmt.Execute();
            SqlWideString<100> actual;
            reader.BindOutputColumns(&actual);
            (void) reader.FetchRow();
            CHECK(actual == expectedWide);
        }

        SECTION("entity via DataMapper")
        {
            auto dm = DataMapper();
            auto const result = dm.Query<WideStringFromVarcharEntity>().First();
            REQUIRE(result.has_value());
            CHECK(result->value.Value() == expectedWide);
        }
    }

    SECTION("max-width codepage umlaut value")
    {
        // Same rationale as "codepage umlaut value": use SQL Server's REPLICATE(CHAR(n), 100)
        // to store 100 raw CP1252 'ä' bytes (0xE4) directly in the column.
        if (stmt.Connection().ServerType() != SqlServerType::MICROSOFT_SQL)
            SKIP();

        std::ignore = stmt.ExecuteDirect("INSERT INTO WideStringFromVarcharTest (value) "
                                         "VALUES (REPLICATE(CHAR(228), 100))");
        auto const expectedWide = SqlWideString<100> { std::wstring(100, L'\u00e4') };

        SECTION("BindOutputColumns")
        {
            stmt.Prepare(stmt.Query("WideStringFromVarcharTest").Select().Field("value").All());
            auto reader = stmt.Execute();
            SqlWideString<100> actual;
            reader.BindOutputColumns(&actual);
            (void) reader.FetchRow();
            CHECK(actual == expectedWide);
        }

        SECTION("entity via DataMapper")
        {
            auto dm = DataMapper();
            auto const result = dm.Query<WideStringFromVarcharEntity>().First();
            REQUIRE(result.has_value());
            CHECK(result->value.Value() == expectedWide);
        }
    }
}

// Pins the regression that motivated the SQLDriverConnectW switch: psqlODBC on
// Windows used to put DBC handles opened with SQLDriverConnectA into ANSI mode and
// run every SQL_C_CHAR payload through cp1252, which mangled UTF-8 input bytes ≥ 0x80
// (`Hellö` came back as `HellÃ¶`) and dropped UTF-16 surrogate pairs (emoji came back
// as `??`). The fix is the entire `[Lightweight] {SqlConnection,SqlStatement,
// SqlSchema} ... via W variants` series of commits plus the BasicStringBinder cleanup;
// these tests run on every backend so a future regression on any one of them surfaces.
TEST_CASE_METHOD(SqlTestFixture, "Unicode round-trip across binders", "[SqlDataBinder][Unicode]")
{
    auto stmt = SqlStatement {};
    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.CreateTable("UnicodeRoundTrip").Column("value", SqlColumnTypeDefinitions::NVarchar { .size = 64 });
    });

    SECTION("std::u8string with single non-ASCII char")
    {
        // Was: u8"Hellö" round-tripped as `HellÃ¶` on PostgreSQL/Windows because the
        // pre-fix PG hatch in BasicStringBinder bound the UTF-8 bytes as SQL_C_CHAR.
        stmt.Prepare(stmt.Query("UnicodeRoundTrip").Insert().Set("value", SqlWildcard));
        std::ignore = stmt.Execute(u8"Hellö"s);
        stmt.Prepare(stmt.Query("UnicodeRoundTrip").Select().Field("value").All());
        auto reader = stmt.Execute();
        REQUIRE(reader.FetchRow());
        CHECK(reader.GetColumn<std::u8string>(1) == u8"Hellö"s);
    }

    SECTION("std::u16string with supplementary-plane emoji")
    {
        // Was: u"Unicode \U0001F601" came back as `Unicode ??` on PostgreSQL/Windows
        // because the surrogate pair could not be encoded as cp1252.
        stmt.Prepare(stmt.Query("UnicodeRoundTrip").Insert().Set("value", SqlWildcard));
        std::ignore = stmt.Execute(u"\U0001F601"s);
        stmt.Prepare(stmt.Query("UnicodeRoundTrip").Select().Field("value").All());
        auto reader = stmt.Execute();
        REQUIRE(reader.FetchRow());
        CHECK(reader.GetColumn<std::u16string>(1) == u"\U0001F601"s);
    }

    SECTION("mixed BMP + supplementary-plane via std::u16string")
    {
        // Tests that surrogate pairs sandwiched between BMP characters round-trip
        // intact — i.e. the driver doesn't truncate / re-encode at the boundary.
        stmt.Prepare(stmt.Query("UnicodeRoundTrip").Insert().Set("value", SqlWildcard));
        std::ignore = stmt.Execute(u"Hello \U0001F601 World"s);
        stmt.Prepare(stmt.Query("UnicodeRoundTrip").Select().Field("value").All());
        auto reader = stmt.Execute();
        REQUIRE(reader.FetchRow());
        CHECK(reader.GetColumn<std::u16string>(1) == u"Hello \U0001F601 World"s);
    }

    SECTION("BindOutputColumns with std::u8string")
    {
        stmt.Prepare(stmt.Query("UnicodeRoundTrip").Insert().Set("value", SqlWildcard));
        std::ignore = stmt.Execute(u8"Hellö"s);
        stmt.Prepare(stmt.Query("UnicodeRoundTrip").Select().Field("value").All());
        auto reader = stmt.Execute();
        std::u8string actual;
        reader.BindOutputColumns(&actual);
        REQUIRE(reader.FetchRow());
        CHECK(actual == u8"Hellö"s);
    }

    SECTION("std::wstring with mixed BMP + supplementary-plane")
    {
        // Pins the wchar_t arm of the W-binder dispatch — on Windows std::wstring
        // routes through the Utf16StringType specialization (sizeof(wchar_t) == 2),
        // on Linux through the Utf32StringType specialization (sizeof(wchar_t) == 4).
        // Both must round-trip surrogate-pair payloads intact.
        stmt.Prepare(stmt.Query("UnicodeRoundTrip").Insert().Set("value", SqlWildcard));
        std::ignore = stmt.Execute(L"Hello \U0001F601 World"s);
        stmt.Prepare(stmt.Query("UnicodeRoundTrip").Select().Field("value").All());
        auto reader = stmt.Execute();
        REQUIRE(reader.FetchRow());
        CHECK(reader.GetColumn<std::wstring>(1) == L"Hello \U0001F601 World"s);
    }

    SECTION("narrow std::string read-back preserves UTF-8 bytes")
    {
        // Pins the regression fixed alongside this section: the ANSI std::string binder used to read
        // narrow columns via SQL_C_CHAR, which psqlODBC on Windows transcodes to cp1252 — so a stored
        // "café €" came back mangled (caf? ?). The read path now mirrors the write path (SQL_C_WCHAR
        // + UTF-8 conversion) on PostgreSQL. The other backends already round-tripped these bytes and
        // must keep doing so.
        auto const utf8Value = "caf\xC3\xA9 \xE2\x82\xAC"s; // "café €" as UTF-8 bytes
        stmt.Prepare(stmt.Query("UnicodeRoundTrip").Insert().Set("value", SqlWildcard));
        std::ignore = stmt.Execute(utf8Value);

        SECTION("GetColumn<std::string>")
        {
            stmt.Prepare(stmt.Query("UnicodeRoundTrip").Select().Field("value").All());
            auto reader = stmt.Execute();
            REQUIRE(reader.FetchRow());
            CHECK(reader.GetColumn<std::string>(1) == utf8Value);
        }

        SECTION("BindOutputColumns with std::string")
        {
            stmt.Prepare(stmt.Query("UnicodeRoundTrip").Select().Field("value").All());
            auto reader = stmt.Execute();
            std::string actual;
            reader.BindOutputColumns(&actual);
            REQUIRE(reader.FetchRow());
            CHECK(actual == utf8Value);
        }
    }
}

struct UnicodeAcrossDynamicStringTypes
{
    Field<uint64_t, PrimaryKey::ServerSideAutoIncrement> id {};
    Field<SqlDynamicUtf16String<256>> stringUtf16;
    Field<SqlDynamicUtf32String<256>> stringUtf32;
    Field<SqlDynamicWideString<256>> stringWide;
};

TEST_CASE_METHOD(SqlTestFixture, "Unicode round-trip across DataMapper dynamic string types", "[DataMapper][Unicode]")
{
    auto dm = DataMapper {};
    dm.CreateTable<UnicodeAcrossDynamicStringTypes>();

    auto record = UnicodeAcrossDynamicStringTypes {
        .stringUtf16 = std::u16string { u"Hello \U0001F601 World" },
        .stringUtf32 = std::u32string { U"Hello \U0001F601 World" },
        .stringWide = std::wstring { L"Hello \U0001F601 World" },
    };
    dm.Create(record);

    auto const result = dm.QuerySingle<UnicodeAcrossDynamicStringTypes>(record.id);
    REQUIRE(result.has_value());
    CHECK(result->stringUtf16.Value() == record.stringUtf16.Value());
    CHECK(result->stringUtf32.Value() == record.stringUtf32.Value());
    CHECK(result->stringWide.Value() == record.stringWide.Value());
}

struct UnicodeTrimmedFixedRow
{
    Field<uint64_t, PrimaryKey::ServerSideAutoIncrement> id {};
    Field<SqlTrimmedFixedString<32>> stringNarrow;
    Field<SqlTrimmedWideFixedString<32>> stringWide;
};

TEST_CASE_METHOD(SqlTestFixture, "Trimmed fixed strings strip trailing padding through DataMapper", "[DataMapper][Unicode]")
{
    auto dm = DataMapper {};
    dm.CreateTable<UnicodeTrimmedFixedRow>();

    auto row = UnicodeTrimmedFixedRow { .stringNarrow = "Hello", .stringWide = L"Hellö" };
    dm.Create(row);

    auto const result = dm.QuerySingle<UnicodeTrimmedFixedRow>(row.id);
    REQUIRE(result.has_value());
    CHECK(result->stringNarrow == SqlTrimmedFixedString<32> { "Hello" });
    CHECK(result->stringWide == SqlTrimmedWideFixedString<32> { L"Hellö" });
}

// Regression for #485: BasicStringBinder::OutputColumn passes BufferLength=N
// (not N+1) to SQLBindCol, so ODBC truncates any value of length N to N-1
// data chars + null. The bug only surfaces when the stored value fills the
// full capacity with no trailing whitespace -- e.g. a 3-letter currency code
// stored in a CHAR(3) column.
struct FullCapacityFixedRow
{
    Field<uint64_t, PrimaryKey::ServerSideAutoIncrement> id {};
    Field<SqlTrimmedFixedString<3>> code;
};

TEST_CASE_METHOD(SqlTestFixture,
                 "Trimmed fixed string preserves a value that fills its capacity",
                 "[DataMapper][SqlFixedString][regression]")
{
    auto dm = DataMapper {};
    dm.CreateTable<FullCapacityFixedRow>();

    auto row = FullCapacityFixedRow { .code = "EUR" };
    dm.Create(row);

    auto const result = dm.QuerySingle<FullCapacityFixedRow>(row.id);
    REQUIRE(result.has_value());
    CHECK(result->code.Value().size() == 3);
    CHECK(result->code.Value().str() == "EUR");
}

// Coverage for the SQL_C_WCHAR truncation arithmetic in
// detail::GetRawColumnArrayData (BasicStringBinder.hpp). The function has two
// re-fetch branches: (a) the driver reports total length up front and we re-
// fetch into a sized-up buffer, (b) the driver reports SQL_NO_TOTAL and we
// loop, doubling the buffer until we hit success. Both branches must compute
// BufferLength in bytes (= chars * sizeof(CharType)) — a miscount on the
// SQL_C_WCHAR path corrupts supplementary-plane data because each surrogate
// pair occupies two char16_t units.
//
// Branch (a) is what the drivers in our test matrix (psqlODBC, ODBC Driver 18
// for SQL Server) take for wide streaming columns, so the test case below pins the
// arithmetic for that branch. Branch (b) is what ODBC Driver 18 takes when it has
// to widen a narrow LOB (VARCHAR(MAX), TEXT) to SQL_C_WCHAR; the test case after it
// covers that one.
//
// The streaming column type — NVARCHAR(MAX) on SQL Server, TEXT on Postgres /
// SQLite — plus payloads that overflow the 255-char initial buffer in
// GetColumnUtf16 are what get us into the re-fetch path at all.
TEST_CASE_METHOD(SqlTestFixture, "GetRawColumnArrayData: long Unicode round-trip", "[SqlDataBinder][Unicode]")
{
    auto stmt = SqlStatement {};
    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.CreateTable("LongUnicode").Column("value", SqlColumnTypeDefinitions::NVarchar { .size = 0 });
    });

    SECTION("4 KiB std::u16string ASCII forces the truncation path")
    {
        auto const expected = MakeLargeText<char16_t>(4 * 1024);
        stmt.Prepare(stmt.Query("LongUnicode").Insert().Set("value", SqlWildcard));
        std::ignore = stmt.Execute(expected);
        stmt.Prepare(stmt.Query("LongUnicode").Select().Field("value").All());
        auto reader = stmt.Execute();
        REQUIRE(reader.FetchRow());
        CHECK(reader.GetColumn<std::u16string>(1) == expected);
    }

    SECTION("3 KiB std::u16string with supplementary-plane characters throughout")
    {
        // Each iteration appends one BMP letter + one surrogate pair (3 char16_t).
        // 1024 iterations = 3072 char16_t (~6 KiB) — well past the 255-char buffer.
        // Any miscount in the byte/char arithmetic of the re-fetch path
        // desynchronizes the surrogate-pair boundary in a visible way.
        std::u16string expected;
        expected.reserve(3 * 1024);
        for ([[maybe_unused]] auto i: std::views::iota(0, 1024))
            expected += u"A\U0001F601";
        stmt.Prepare(stmt.Query("LongUnicode").Insert().Set("value", SqlWildcard));
        std::ignore = stmt.Execute(expected);
        stmt.Prepare(stmt.Query("LongUnicode").Select().Field("value").All());
        auto reader = stmt.Execute();
        REQUIRE(reader.FetchRow());
        CHECK(reader.GetColumn<std::u16string>(1) == expected);
    }

    SECTION("supplementary-plane surrogate pair straddles the 255-char buffer boundary")
    {
        // The initial GetColumnUtf16 buffer is 255 char16_t. Place a high surrogate
        // at index 254 and the low surrogate at index 255 to verify that the
        // re-fetch (whichever branch fires) does not split the pair across calls.
        std::u16string expected(254, u'A');
        expected += u"\U0001F601"; // surrogate pair occupies indices 254..255
        expected += MakeLargeText<char16_t>(1024);
        stmt.Prepare(stmt.Query("LongUnicode").Insert().Set("value", SqlWildcard));
        std::ignore = stmt.Execute(expected);
        stmt.Prepare(stmt.Query("LongUnicode").Select().Field("value").All());
        auto reader = stmt.Execute();
        REQUIRE(reader.FetchRow());
        CHECK(reader.GetColumn<std::u16string>(1) == expected);
    }

    SECTION("4 KiB std::u8string round-trip via GetColumnUtf16")
    {
        // SqlDataBinder<Utf8StringType>::GetColumn fetches into a u16 scratch via
        // GetColumnUtf16, then converts to UTF-8 — so the same fix must hold for
        // u8 callers reading large columns.
        auto const expected = MakeLargeText<char8_t>(4 * 1024);
        stmt.Prepare(stmt.Query("LongUnicode").Insert().Set("value", SqlWildcard));
        std::ignore = stmt.Execute(expected);
        stmt.Prepare(stmt.Query("LongUnicode").Select().Field("value").All());
        auto reader = stmt.Execute();
        REQUIRE(reader.FetchRow());
        CHECK(reader.GetColumn<std::u8string>(1) == expected);
    }

    SECTION("4 KiB std::wstring round-trip")
    {
        auto const expected = MakeLargeText<wchar_t>(4 * 1024);
        stmt.Prepare(stmt.Query("LongUnicode").Insert().Set("value", SqlWildcard));
        std::ignore = stmt.Execute(expected);
        stmt.Prepare(stmt.Query("LongUnicode").Select().Field("value").All());
        auto reader = stmt.Execute();
        REQUIRE(reader.FetchRow());
        CHECK(reader.GetColumn<std::wstring>(1) == expected);
    }

    SECTION("empty std::u16string hits the SQL_SUCCESS path with zero indicator")
    {
        std::u16string const expected;
        stmt.Prepare(stmt.Query("LongUnicode").Insert().Set("value", SqlWildcard));
        std::ignore = stmt.Execute(expected);
        stmt.Prepare(stmt.Query("LongUnicode").Select().Field("value").All());
        auto reader = stmt.Execute();
        REQUIRE(reader.FetchRow());
        CHECK(reader.GetColumn<std::u16string>(1) == expected);
    }
}

// Branch (b) of detail::GetRawColumnArrayData: ODBC Driver 18 cannot predict the widened length of
// a narrow LOB, answers SQL_NO_TOTAL and is read chunk by chunk. Every continuation must start right
// after the chars the previous transfer wrote; advancing by the whole buffer instead left gaps and
// doubled the reported length per chunk, so a few KiB came back as tens of millions of chars.
TEST_CASE_METHOD(SqlTestFixture, "GetRawColumnArrayData: narrow long text read as UTF-16", "[SqlDataBinder][Unicode]")
{
    auto stmt = SqlStatement {};
    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.CreateTable("LongNarrowText").Column("value", SqlColumnTypeDefinitions::Text {});
    });

    // Long enough for many chunks past the 255-char initial buffer.
    auto const stored = MakeLargeText(5000);
    stmt.Prepare(stmt.Query("LongNarrowText").Insert().Set("value", SqlWildcard));
    std::ignore = stmt.Execute(stored);
    auto const expected = std::u16string(stored.begin(), stored.end());

    SECTION("std::u16string")
    {
        stmt.Prepare(stmt.Query("LongNarrowText").Select().Field("value").All());
        auto reader = stmt.Execute();
        REQUIRE(reader.FetchRow());
        auto const actual = reader.GetColumn<std::u16string>(1);
        CHECK(actual.size() == expected.size());
        CHECK(actual == expected);
    }

    SECTION("std::wstring")
    {
        stmt.Prepare(stmt.Query("LongNarrowText").Select().Field("value").All());
        auto reader = stmt.Execute();
        REQUIRE(reader.FetchRow());
        auto const actual = reader.GetColumn<std::wstring>(1);
        CHECK(actual.size() == expected.size());
        CHECK(actual == std::wstring(stored.begin(), stored.end()));
    }
}

// =============================================================================
// SqlFixedString class-level coverage
// =============================================================================

TEST_CASE_METHOD(SqlTestFixture, "SqlFixedString: constructors", "[SqlFixedString]")
{
    SECTION("default constructor produces empty string")
    {
        SqlFixedString<8> str;
        CHECK(str.size() == 0);
        CHECK(str.empty());
        CHECK(str.capacity() == 8);
    }

    SECTION("from string literal")
    {
        SqlFixedString<8> const str { "Hello" };
        CHECK(str.size() == 5);
        CHECK(str == "Hello"sv);
    }

    SECTION("from std::string")
    {
        std::string const source { "World" };
        SqlFixedString<8> const str { source };
        CHECK(str.size() == 5);
        CHECK(str == "World"sv);
    }

    SECTION("from std::string truncates when source exceeds capacity")
    {
        std::string const source { "TooLongForCapacity" };
        SqlFixedString<8> const str { source };
        CHECK(str.size() == 8);
        CHECK(str == "TooLongF"sv);
    }

    SECTION("from std::string_view")
    {
        SqlFixedString<8> const str { "Slice"sv };
        CHECK(str.size() == 5);
        CHECK(str == "Slice"sv);
    }

    SECTION("from pointer + length")
    {
        char const* source = "PointerData";
        SqlFixedString<8> const str { source, 5 };
        CHECK(str.size() == 5);
        CHECK(str == "Point"sv);
    }

    SECTION("from pointer range [begin, end)")
    {
        char const* source = "RangeData";
        SqlFixedString<8> const str { source, source + 5 };
        CHECK(str.size() == 5);
        CHECK(str == "Range"sv);
    }

    SECTION("copy constructor")
    {
        SqlFixedString<8> const original { "Copy" };
        SqlFixedString<8> const copy { original };
        CHECK(copy == original);
        CHECK(copy.size() == original.size());
    }

    SECTION("wide character types")
    {
        SqlFixedString<8, char16_t> const u16 { u"u16" };
        CHECK(u16.size() == 3);
        SqlFixedString<8, char32_t> const u32 { U"u32" };
        CHECK(u32.size() == 3);
        SqlFixedString<8, wchar_t> const wide { L"wide" };
        CHECK(wide.size() == 4);
    }
}

TEST_CASE_METHOD(SqlTestFixture, "SqlFixedString: reserve throws on overflow", "[SqlFixedString]")
{
    SqlFixedString<8> str;
    CHECK_NOTHROW(str.reserve(8));
    CHECK_THROWS_AS(str.reserve(9), std::length_error);
}

TEST_CASE_METHOD(SqlTestFixture, "SqlFixedString: substr and str", "[SqlFixedString]")
{
    SqlFixedString<16> const str { "Hello, World!" };

    SECTION("substr() returns full view")
    {
        CHECK(str.substr() == "Hello, World!"sv);
    }

    SECTION("substr(offset)")
    {
        CHECK(str.substr(7) == "World!"sv);
    }

    SECTION("substr(offset, count)")
    {
        CHECK(str.substr(7, 5) == "World"sv);
    }

    SECTION("substr(offset, count) where count exceeds remaining size")
    {
        CHECK(str.substr(7, 100) == "World!"sv);
    }

    SECTION("substr(offset) where offset >= size returns empty")
    {
        CHECK(str.substr(50).empty());
        CHECK(str.substr(str.size()).empty());
    }

    SECTION("str() returns full view")
    {
        CHECK(str.str() == "Hello, World!"sv);
    }
}

TEST_CASE_METHOD(SqlTestFixture, "SqlFixedString: iteration and operator[]", "[SqlFixedString]")
{
    SqlFixedString<8> str { "Hello" };

    SECTION("range-based for")
    {
        std::string collected;
        for (auto const ch: str)
            collected += ch;
        CHECK(collected == "Hello");
    }

    SECTION("const operator[]")
    {
        SqlFixedString<8> const& cstr = str;
        CHECK(cstr[0] == 'H');
        CHECK(cstr[4] == 'o');
    }

    SECTION("mutable operator[]")
    {
        str[0] = 'J';
        CHECK(str == "Jello"sv);
    }

    SECTION("begin()/end() distance equals size")
    {
        CHECK(static_cast<std::size_t>(std::distance(str.begin(), str.end())) == str.size());
    }
}

TEST_CASE_METHOD(SqlTestFixture, "SqlFixedString: comparison operators", "[SqlFixedString]")
{
    SqlFixedString<8> const a { "Apple" };
    SqlFixedString<8> const b { "Apple" };
    SqlFixedString<8> const c { "Banana" };

    SECTION("equality and inequality with same-size SqlFixedString")
    {
        CHECK(a == b);
        CHECK_FALSE(a != b);
        CHECK(a != c);
        CHECK_FALSE(a == c);
    }

    SECTION("three-way comparison ordering")
    {
        CHECK((a <=> c) == std::weak_ordering::less);
        CHECK((c <=> a) == std::weak_ordering::greater);
        CHECK((a <=> b) == std::weak_ordering::equivalent);
    }

    SECTION("equality with std::string_view")
    {
        CHECK(a == "Apple"sv);
        CHECK(a != "Banana"sv);
    }

    SECTION("comparison across different capacities")
    {
        SqlFixedString<5> const smallStr { "Apple" };
        SqlFixedString<20> const largeStr { "Apple" };
        CHECK(smallStr == largeStr);
    }

    SECTION("comparison across different modes")
    {
        SqlAnsiString<10> const variableMode { "Apple" };
        SqlTrimmedFixedString<10> const trimmedMode { "Apple" };
        CHECK(variableMode == trimmedMode);
    }
}

TEST_CASE_METHOD(SqlTestFixture, "SqlFixedString: ToString and conversion operators", "[SqlFixedString]")
{
    SqlFixedString<8> const str { "Hello" };

    SECTION("ToString()")
    {
        CHECK(str.ToString() == "Hello"s);
    }

    SECTION("ToStringView()")
    {
        CHECK(str.ToStringView() == "Hello"sv);
    }

    SECTION("explicit conversion to std::string")
    {
        auto const s = static_cast<std::string>(str);
        CHECK(s == "Hello");
    }

    SECTION("explicit conversion to std::string_view")
    {
        auto const v = static_cast<std::string_view>(str);
        CHECK(v == "Hello"sv);
    }

    SECTION("data() returns a writable pointer")
    {
        SqlFixedString<8> mutableStr { "Hello" };
        char* ptr = mutableStr.data();
        ptr[0] = 'J';
        CHECK(mutableStr == "Jello"sv);
    }
}

TEST_CASE_METHOD(SqlTestFixture, "SqlFixedString: std::format formatter", "[SqlFixedString]")
{
    SECTION("char")
    {
        SqlFixedString<10> const str { "Hello" };
        CHECK(std::format("{}", str) == "Hello");
    }

    SECTION("wchar_t encoded as UTF-8 in formatter output")
    {
        SqlFixedString<10, wchar_t> const wstr { L"Hellö" };
        // The formatter converts non-narrow strings to UTF-8 for std::format.
        auto const formatted = std::format("{}", wstr);
        // "Hellö" encodes to "Hell" + 0xC3 0xB6 in UTF-8 (5 chars, 6 bytes).
        CHECK(formatted == reinterpret_cast<char const*>(u8"Hellö"));
    }

    SECTION("char16_t encoded as UTF-8 in formatter output")
    {
        SqlFixedString<10, char16_t> const u16 { u"abc" };
        CHECK(std::format("{}", u16) == "abc");
    }
}

TEST_CASE_METHOD(SqlTestFixture, "SqlFixedString: setsize null-terminates the buffer", "[SqlFixedString]")
{
    SqlFixedString<8> str { "Hello, !" };
    str.setsize(5);
    CHECK(str.size() == 5);
    CHECK(str.c_str() == "Hello"sv);
    CHECK(str.data()[5] == '\0');
}

TEST_CASE_METHOD(SqlTestFixture, "SqlFixedString: SqlStringInterface concept holds for all char types", "[SqlFixedString]")
{
    STATIC_REQUIRE(SqlStringInterface<SqlFixedString<10, char>>);
    STATIC_REQUIRE(SqlStringInterface<SqlFixedString<10, char16_t>>);
    STATIC_REQUIRE(SqlStringInterface<SqlFixedString<10, char32_t>>);
    STATIC_REQUIRE(SqlStringInterface<SqlFixedString<10, wchar_t>>);
    STATIC_REQUIRE(SqlStringInterface<SqlAnsiString<10>>);
    STATIC_REQUIRE(SqlStringInterface<SqlUtf16String<10>>);
    STATIC_REQUIRE(SqlStringInterface<SqlUtf32String<10>>);
    STATIC_REQUIRE(SqlStringInterface<SqlWideString<10>>);
    STATIC_REQUIRE(SqlStringInterface<SqlTrimmedFixedString<10>>);
    STATIC_REQUIRE(SqlStringInterface<SqlTrimmedWideFixedString<10>>);
}

TEST_CASE_METHOD(SqlTestFixture, "SqlFixedString: IsSqlFixedString trait", "[SqlFixedString]")
{
    STATIC_REQUIRE(IsSqlFixedString<SqlFixedString<10>>);
    STATIC_REQUIRE(IsSqlFixedString<SqlAnsiString<5>>);
    STATIC_REQUIRE(IsSqlFixedString<SqlWideString<5>>);
    STATIC_REQUIRE(!IsSqlFixedString<std::string>);
    STATIC_REQUIRE(!IsSqlFixedString<SqlText>);
}

// =============================================================================
// SqlDynamicString class-level coverage
// =============================================================================

TEST_CASE_METHOD(SqlTestFixture, "SqlDynamicString: constructors", "[SqlDynamicString]")
{
    SECTION("default constructor produces empty string")
    {
        SqlDynamicString<32> str;
        CHECK(str.size() == 0);
        CHECK(str.empty());
        CHECK(str.capacity() == 32);
    }

    SECTION("from string literal")
    {
        SqlDynamicString<32> const str { "Hello" };
        CHECK(str.size() == 5);
        CHECK(str == "Hello"sv);
    }

    SECTION("from std::string")
    {
        std::string const source { "Dynamic" };
        SqlDynamicString<32> const str { source };
        CHECK(str.size() == 7);
        CHECK(str == "Dynamic"sv);
    }

    SECTION("from std::string_view")
    {
        SqlDynamicString<32> const str { "View"sv };
        CHECK(str.size() == 4);
        CHECK(str == "View"sv);
    }

    SECTION("from pointer range [begin, end)")
    {
        char const* source = "RangeData";
        SqlDynamicString<32> const str { source, source + 5 };
        CHECK(str.size() == 5);
        CHECK(str == "Range"sv);
    }

    SECTION("wide character types")
    {
        SqlDynamicString<32, char16_t> const u16 { u"u16" };
        CHECK(u16.size() == 3);
        SqlDynamicString<32, char32_t> const u32 { U"u32" };
        CHECK(u32.size() == 3);
        SqlDynamicString<32, wchar_t> const wide { L"wide" };
        CHECK(wide.size() == 4);
    }

    SECTION("copy constructor produces an independent copy")
    {
        SqlDynamicString<32> const original { "Copy" };
        SqlDynamicString<32> copy { original };
        copy.push_back('!');
        CHECK(original == "Copy"sv);
        CHECK(copy == "Copy!"sv);
    }
}

TEST_CASE_METHOD(SqlTestFixture, "SqlDynamicString: modifiers", "[SqlDynamicString]")
{
    SqlDynamicString<32> str;

    SECTION("push_back / pop_back")
    {
        str.push_back('a');
        str.push_back('b');
        str.push_back('c');
        CHECK(str == "abc"sv);
        str.pop_back();
        CHECK(str == "ab"sv);
    }

    SECTION("clear")
    {
        str = SqlDynamicString<32> { "Filled" };
        REQUIRE(!str.empty());
        str.clear();
        CHECK(str.empty());
    }

    SECTION("reserve and resize")
    {
        str.reserve(16);
        str.resize(5);
        CHECK(str.size() == 5);
    }

    SECTION("setsize caps at the dynamic capacity")
    {
        SqlDynamicString<8> tiny;
        tiny.setsize(20);
        CHECK(tiny.size() == 8);
    }
}

TEST_CASE_METHOD(SqlTestFixture, "SqlDynamicString: substr / str / data / c_str", "[SqlDynamicString]")
{
    SqlDynamicString<32> const str { "Hello, World!" };
    CHECK(str.substr(7, 5) == "World"sv);
    CHECK(str.substr(7) == "World!"sv);
    CHECK(str.str() == "Hello, World!"sv);
    CHECK(std::string_view { str.data(), str.size() } == "Hello, World!"sv);
    CHECK(std::string_view { str.c_str() } == "Hello, World!"sv);
}

TEST_CASE_METHOD(SqlTestFixture, "SqlDynamicString: iteration and operator[]", "[SqlDynamicString]")
{
    SqlDynamicString<32> str { "Hello" };

    SECTION("range-based for")
    {
        std::string collected;
        for (auto const ch: str)
            collected += ch;
        CHECK(collected == "Hello");
    }

    SECTION("operator[] read and write")
    {
        SqlDynamicString<32> const& cstr = str;
        CHECK(cstr[0] == 'H');
        str[0] = 'J';
        CHECK(str == "Jello"sv);
    }
}

TEST_CASE_METHOD(SqlTestFixture, "SqlDynamicString: comparison operators", "[SqlDynamicString]")
{
    SqlDynamicString<32> const a { "Apple" };
    SqlDynamicString<32> const b { "Apple" };
    SqlDynamicString<32> const c { "Banana" };

    CHECK(a == b);
    CHECK(a != c);
    CHECK((a <=> c) == std::weak_ordering::less);
    CHECK((c <=> a) == std::weak_ordering::greater);

    SECTION("comparison across different capacities")
    {
        SqlDynamicString<8> const tiny { "Apple" };
        SqlDynamicString<64> const large { "Apple" };
        CHECK(tiny == large);
        CHECK_FALSE(tiny != large);
    }

    SECTION("comparison with std::string_view")
    {
        CHECK(a == "Apple"sv);
        CHECK(a != "Banana"sv);
    }
}

TEST_CASE_METHOD(SqlTestFixture, "SqlDynamicString: ToString and conversion operators", "[SqlDynamicString]")
{
    SqlDynamicString<32> const str { "Hello" };
    CHECK(str.ToString() == "Hello"s);
    CHECK(str.ToStringView() == "Hello"sv);
    CHECK(static_cast<std::string>(str) == "Hello");
    CHECK(static_cast<std::string_view>(str) == "Hello"sv);
}

TEST_CASE_METHOD(SqlTestFixture, "SqlDynamicString: std::format formatter", "[SqlDynamicString]")
{
    SECTION("char")
    {
        SqlDynamicString<32> const str { "Hello" };
        CHECK(std::format("{}", str) == "Hello");
    }

    SECTION("wchar_t encoded as UTF-8")
    {
        SqlDynamicString<32, wchar_t> const wstr { L"Hellö" };
        auto const formatted = std::format("{}", wstr);
        CHECK(formatted == reinterpret_cast<char const*>(u8"Hellö"));
    }

    SECTION("std::optional<SqlDynamicString>: nullopt")
    {
        std::optional<SqlDynamicString<32>> const opt;
        CHECK(std::format("{}", opt) == "nullopt");
    }

    SECTION("std::optional<SqlDynamicString>: with value")
    {
        std::optional<SqlDynamicString<32>> const opt { SqlDynamicString<32> { "Hello" } };
        CHECK(std::format("{}", opt) == "Hello");
    }
}

TEST_CASE_METHOD(SqlTestFixture, "SqlDynamicString: IsSqlDynamicString trait", "[SqlDynamicString]")
{
    STATIC_REQUIRE(IsSqlDynamicString<SqlDynamicString<10>>);
    STATIC_REQUIRE(IsSqlDynamicString<SqlDynamicAnsiString<10>>);
    STATIC_REQUIRE(IsSqlDynamicString<SqlDynamicUtf16String<10>>);
    STATIC_REQUIRE(IsSqlDynamicString<SqlDynamicUtf32String<10>>);
    STATIC_REQUIRE(IsSqlDynamicString<SqlDynamicWideString<10>>);
    STATIC_REQUIRE(IsSqlDynamicString<std::optional<SqlDynamicString<10>>>);
    STATIC_REQUIRE(!IsSqlDynamicString<std::string>);
    STATIC_REQUIRE(!IsSqlDynamicString<SqlFixedString<10>>);
}

TEST_CASE_METHOD(SqlTestFixture, "SqlDynamicBinary: data() const/non-const overloads", "[SqlDynamicBinary]")
{
    // Regression test for the data()-sentinel follow-up on #535 (PR review, ea21f2a6/4d9f04a1):
    // data() const must not mutate `_base` (that was the const_cast-on-a-const-object UB the
    // formatter test above already covers for the null-buffer case). This exercises the other half
    // of both overloads: the non-const overload once a buffer already exists (no resize needed), and
    // the const overload's non-null early return once `_base` has real data — neither of which the
    // formatter test above reaches, since it only ever reads through a `const SqlDynamicBinary<8>`.
    SqlDynamicBinary<8> data;

    // Non-const data() on a null buffer: materializes storage (the resize/clear branch).
    auto* const firstPtr = data.data();
    REQUIRE(firstPtr != nullptr);
    CHECK(data.empty());

    // Non-const data() again: buffer already materialized, no resize needed.
    CHECK(data.data() == firstPtr);

    data.resize(3);
    auto* const nonConstPtr = data.data();
    nonConstPtr[0] = 'A';
    nonConstPtr[1] = 'B';
    nonConstPtr[2] = 'C';

    // const data() once `_base` genuinely holds data: must return the real buffer, not the sentinel.
    // Checked via pointer identity against the non-const buffer rather than indexing
    // `constAlias.data()` directly: clang-tidy's static analyzer models data() const's two return
    // paths (the real buffer vs. the 1-byte sentinel) independently of the resize() call above, so it
    // can't prove which one this call takes and flags indexing past element 0 as reading past the
    // sentinel (clang-analyzer-security.ArrayBound) even though the sentinel path is genuinely
    // unreachable here at runtime. Pointer-identity avoids indexing the ambiguous pointer at all,
    // while still proving the const overload returns the real, populated buffer.
    SqlDynamicBinary<8> const& constAlias = data;
    CHECK(constAlias.data() == nonConstPtr);
}

TEST_CASE_METHOD(SqlTestFixture, "SqlDynamicBinary: std::format formatter", "[SqlDynamicBinary]")
{
    // Regression test for #535: the formatter used to iterate via range-for, but
    // SqlDynamicBinary<N> only exposes data()/size(), never begin()/end() — a pure byte buffer, not
    // an iterable range. That made the formatter fail to compile (MSVC C3312) the moment it was
    // actually instantiated, e.g. via Field<SqlDynamicBinary<N>, ...>'s forwarding formatter.
    SECTION("printable bytes render as characters")
    {
        SqlDynamicBinary<8> const data { { 'H', 'i', '!' } };
        CHECK(std::format("{}", data) == "Hi!");
    }

    SECTION("non-printable bytes render as \\xHH escapes")
    {
        SqlDynamicBinary<8> const data { { 0x00, 0x41, 0xFF } };
        CHECK(std::format("{}", data) == "\\x00A\\xFF");
    }

    SECTION("empty buffer renders as an empty string")
    {
        SqlDynamicBinary<8> const data;
        CHECK(std::format("{}", data) == "");
    }
}

TEST_CASE_METHOD(SqlTestFixture, "SqlDynamicString: SqlStringInterface concept", "[SqlDynamicString]")
{
    STATIC_REQUIRE(SqlStringInterface<SqlDynamicString<10>>);
    STATIC_REQUIRE(SqlStringInterface<SqlDynamicAnsiString<10>>);
    STATIC_REQUIRE(SqlStringInterface<SqlDynamicUtf16String<10>>);
    STATIC_REQUIRE(SqlStringInterface<SqlDynamicUtf32String<10>>);
    STATIC_REQUIRE(SqlStringInterface<SqlDynamicWideString<10>>);
}

// =============================================================================
// SqlText class-level coverage
// =============================================================================

TEST_CASE_METHOD(SqlTestFixture, "SqlText: equality and ordering", "[SqlText]")
{
    SqlText const a { "Hello" };
    SqlText const b { "Hello" };
    SqlText const c { "World" };
    CHECK(a == b);
    CHECK(a != c);
    CHECK((a <=> c) == std::weak_ordering::less);
}

TEST_CASE_METHOD(SqlTestFixture, "SqlText: std::format formatter", "[SqlText]")
{
    SqlText const text { "Sample text content" };
    CHECK(std::format("{}", text) == "Sample text content");
}

TEST_CASE_METHOD(SqlTestFixture, "SqlText: empty value round-trip", "[SqlText][SqlDataBinder]")
{
    auto stmt = SqlStatement {};
    stmt.MigrateDirect(
        [](auto& migration) { migration.CreateTable("Test").Column("Value", SqlColumnTypeDefinitions::Text { 255 }); });

    stmt.Prepare(stmt.Query("Test").Insert().Set("Value", SqlWildcard));
    (void) stmt.Execute(SqlText { "" });

    auto const result = stmt.ExecuteDirectScalar<SqlText>(stmt.Query("Test").Select().Field("Value").All());
    REQUIRE(result.has_value());
    CHECK(result->value.empty());
}

// =============================================================================
// Database round-trip coverage for types that were missing from the matrix.
// =============================================================================

// Common fixture: create a simple Test(Value <type>) table, INSERT a value with
// SqlWildcard, then SELECT it back and validate via GetColumn / GetNullableColumn.
template <typename ValueT, typename ColumnTypeDefT>
void RoundTripStringValue(ValueT const& expected, ColumnTypeDefT const& columnType)
{
    auto stmt = SqlStatement {};
    auto const sqlColumnType = stmt.Connection().QueryFormatter().ColumnType(columnType);
    (void) stmt.ExecuteDirect(std::format("CREATE TABLE Test (Value {} NULL)", sqlColumnType));

    stmt.Prepare("INSERT INTO Test (Value) VALUES (?)");
    (void) stmt.Execute(expected);

    auto cursor = stmt.ExecuteDirect("SELECT Value FROM Test");
    REQUIRE(cursor.FetchRow());
    auto const actual = cursor.GetColumn<ValueT>(1);
    CHECK(actual == expected);
}

TEST_CASE_METHOD(SqlTestFixture, "SqlDynamicAnsiString: round-trip (binder)", "[SqlDynamicString][SqlDataBinder]")
{
    SECTION("ASCII value")
    {
        RoundTripStringValue(SqlDynamicAnsiString<64> { "Hello" }, SqlColumnTypeDefinitions::Varchar { .size = 64 });
    }

    SECTION("empty value")
    {
        // Some drivers treat empty/NULL ambiguously; verify that an explicitly-empty payload comes back empty.
        auto stmt = SqlStatement {};
        (void) stmt.ExecuteDirect("CREATE TABLE Test (Value VARCHAR(64) NULL)");
        stmt.Prepare("INSERT INTO Test (Value) VALUES (?)");
        (void) stmt.Execute(SqlDynamicAnsiString<64> {});

        auto cursor = stmt.ExecuteDirect("SELECT Value FROM Test");
        REQUIRE(cursor.FetchRow());
        auto const actual = cursor.GetColumn<SqlDynamicAnsiString<64>>(1);
        CHECK(actual.empty());
    }

    SECTION("NULL value via GetNullableColumn")
    {
        auto stmt = SqlStatement {};
        (void) stmt.ExecuteDirect("CREATE TABLE Test (Value VARCHAR(64) NULL)");
        stmt.Prepare("INSERT INTO Test (Value) VALUES (?)");
        (void) stmt.Execute(SqlNullValue);

        auto cursor = stmt.ExecuteDirect("SELECT Value FROM Test");
        REQUIRE(cursor.FetchRow());
        CHECK(!cursor.GetNullableColumn<SqlDynamicAnsiString<64>>(1).has_value());
    }
}

TEST_CASE_METHOD(SqlTestFixture, "SqlDynamicUtf16String: round-trip (binder)", "[SqlDynamicString][SqlDataBinder][Unicode]")
{
    SECTION("BMP value")
    {
        RoundTripStringValue(SqlDynamicUtf16String<64> { u"Hello" }, SqlColumnTypeDefinitions::NVarchar { .size = 64 });
    }

    SECTION("supplementary-plane value")
    {
        auto const expected = SqlDynamicUtf16String<64> { u"\U0001F601 Hello" };
        RoundTripStringValue(expected, SqlColumnTypeDefinitions::NVarchar { .size = 64 });
    }

    SECTION("BindOutputColumns")
    {
        auto stmt = SqlStatement {};
        (void) stmt.ExecuteDirect(
            std::format("CREATE TABLE Test (Value {} NULL)",
                        stmt.Connection().QueryFormatter().ColumnType(SqlColumnTypeDefinitions::NVarchar { .size = 64 })));
        stmt.Prepare("INSERT INTO Test (Value) VALUES (?)");
        std::ignore = stmt.Execute(SqlDynamicUtf16String<64> { u"OutBound" });

        stmt.Prepare("SELECT Value FROM Test");
        auto cursor = stmt.Execute();
        SqlDynamicUtf16String<64> actual;
        cursor.BindOutputColumns(&actual);
        REQUIRE(cursor.FetchRow());
        CHECK(actual == u"OutBound"sv);
    }
}

TEST_CASE_METHOD(SqlTestFixture, "SqlDynamicUtf32String: round-trip (binder)", "[SqlDynamicString][SqlDataBinder][Unicode]")
{
    RoundTripStringValue(SqlDynamicUtf32String<64> { U"Hello \U0001F601" },
                         SqlColumnTypeDefinitions::NVarchar { .size = 64 });
}

TEST_CASE_METHOD(SqlTestFixture, "SqlDynamicWideString: round-trip (binder)", "[SqlDynamicString][SqlDataBinder][Unicode]")
{
    RoundTripStringValue(SqlDynamicWideString<64> { L"Hello \U0001F601" },
                         SqlColumnTypeDefinitions::NVarchar { .size = 64 });
}

TEST_CASE_METHOD(SqlTestFixture, "SqlFixedString FIXED_SIZE mode round-trip", "[SqlFixedString][SqlDataBinder]")
{
    // Pin the FIXED_SIZE mode through the binder. Unlike FIXED_SIZE_RIGHT_TRIMMED,
    // this mode does not trim trailing whitespace on retrieval — but it does strip
    // trailing nulls (see SqlBasicStringOperations<SqlFixedString>::PostProcessOutputColumn).
    // The exact size we get back depends on whether the DBMS pads CHAR(N) columns
    // (MSSQL does; SQLite does not), so we only assert the leading content.
    using FixedNarrow = SqlFixedString<10, char, SqlFixedStringMode::FIXED_SIZE>;

    auto stmt = SqlStatement {};
    auto const sqlColumnType = stmt.Connection().QueryFormatter().ColumnType(SqlColumnTypeDefinitions::Char { 10 });
    (void) stmt.ExecuteDirect(std::format("CREATE TABLE Test (Value {} NULL)", sqlColumnType));

    stmt.Prepare("INSERT INTO Test (Value) VALUES (?)");
    auto const input = FixedNarrow { "Hello" };
    (void) stmt.Execute(input);

    auto cursor = stmt.ExecuteDirect("SELECT Value FROM Test");
    REQUIRE(cursor.FetchRow());
    auto const actual = cursor.GetColumn<FixedNarrow>(1);

    // The leading content must round-trip identically regardless of CHAR(N) padding policy.
    CHECK(actual.substr(0, 5) == "Hello"sv);
    // The size is at least the source length and at most the declared capacity.
    CHECK(actual.size() >= 5);
    CHECK(actual.size() <= 10);
}

TEST_CASE_METHOD(SqlTestFixture, "SqlString<N, T> alias round-trip", "[SqlFixedString][SqlDataBinder]")
{
    // SqlString<N, T> is the generic VARIABLE_SIZE alias. This pins that the alias
    // resolves through the same binder paths as the explicit SqlAnsiString / SqlWideString aliases.
    SECTION("char specialization")
    {
        RoundTripStringValue(SqlString<32, char> { "Alias" }, SqlColumnTypeDefinitions::Varchar { .size = 32 });
    }

    SECTION("default char specialization")
    {
        RoundTripStringValue(SqlString<32> { "DefaultAlias" }, SqlColumnTypeDefinitions::Varchar { .size = 32 });
    }

    SECTION("wchar_t specialization")
    {
        RoundTripStringValue(SqlString<32, wchar_t> { L"WideAlias" }, SqlColumnTypeDefinitions::NVarchar { .size = 32 });
    }
}

namespace
{
// A single char bound as SQL_C_CHAR is not NUL-terminated, so the binder must hand ODBC its length.
// The value sits in front of non-NUL bytes: a binder that leaves the length out (the driver then reads
// it as a NUL-terminated string) sends "XYZ..." instead of "X" on every run, not only by chance.
struct CharFollowedByText
{
    char value = 'X';
    std::array<char, 7> trailing { 'Y', 'Z', 'Y', 'Z', 'Y', 'Z', 'Y' };
};

// Row-wise batch row carrying a char and a nullable char. sizeof is a multiple of alignof(SQLLEN), so
// the native row-wise path (with its row-strided indicators) applies.
struct CharBatchRow
{
    int64_t id {};
    char value {};
    std::optional<char> maybe {};
};
static_assert(sizeof(CharBatchRow) % alignof(SQLLEN) == 0);

void CreateCharTestTable(SqlStatement& stmt)
{
    auto const& formatter = stmt.Connection().QueryFormatter();
    (void) stmt.ExecuteDirect(std::format(R"(CREATE TABLE "Test" ("Id" {} NOT NULL, "Value" {} NULL, "Maybe" {} NULL))",
                                          formatter.ColumnType(SqlColumnTypeDefinitions::Bigint {}),
                                          formatter.ColumnType(SqlColumnTypeDefinitions::Char { 1 }),
                                          formatter.ColumnType(SqlColumnTypeDefinitions::Char { 1 })));
}
} // namespace

TEST_CASE_METHOD(SqlTestFixture, "SqlDataBinder<char>: round-trip", "[SqlDataBinder]")
{
    auto stmt = SqlStatement {};
    CreateCharTestTable(stmt);

    SECTION("InputParameter, GetColumn")
    {
        // A character SQL type takes its length in characters as the ODBC column size, and MS SQL Server's
        // ODBC Driver 18 rejects a zero there with HY104 "Invalid precision value".
        auto const input = CharFollowedByText {};
        stmt.Prepare(R"(INSERT INTO "Test" ("Id", "Value", "Maybe") VALUES (1, ?, ?))");
        std::ignore = stmt.Execute(input.value, std::optional<char> { 'Q' });

        auto cursor = stmt.ExecuteDirect(R"(SELECT "Value", "Value", "Maybe", "Maybe" FROM "Test")");
        REQUIRE(cursor.FetchRow());
        CHECK(cursor.GetColumn<std::string>(1) == "X");
        CHECK(cursor.GetColumn<char>(2) == 'X');
        CHECK(cursor.GetColumn<std::string>(3) == "Q");
        CHECK(cursor.GetColumn<std::optional<char>>(4) == std::optional<char> { 'Q' });
    }

    SECTION("NULL through std::optional<char>")
    {
        stmt.Prepare(R"(INSERT INTO "Test" ("Id", "Value", "Maybe") VALUES (1, ?, ?))");
        std::ignore = stmt.Execute('X', std::optional<char> {});

        auto cursor = stmt.ExecuteDirect(R"(SELECT "Maybe" FROM "Test")");
        REQUIRE(cursor.FetchRow());
        CHECK_FALSE(cursor.GetNullableColumn<char>(1).has_value());
    }

    SECTION("BindOutputColumns")
    {
        (void) stmt.ExecuteDirect(R"(INSERT INTO "Test" ("Id", "Value", "Maybe") VALUES (1, 'X', NULL))");

        stmt.Prepare(R"(SELECT "Value", "Maybe" FROM "Test")");
        auto cursor = stmt.Execute();
        auto value = CharFollowedByText { .value = '?' };
        auto maybe = std::optional<char> { '?' };
        cursor.BindOutputColumns(&value.value, &maybe);
        REQUIRE(cursor.FetchRow());
        CHECK(value.value == 'X');
        CHECK(value.trailing == CharFollowedByText {}.trailing);
        CHECK_FALSE(maybe.has_value());
    }

    SECTION("ExecuteBatchNative (column-wise)")
    {
        auto const ids = std::array<int64_t, 3> { 1, 2, 3 };
        auto const values = std::array { 'A', 'B', 'C' };

        stmt.Prepare(R"(INSERT INTO "Test" ("Id", "Value") VALUES (?, ?))");
        std::ignore = stmt.ExecuteBatchNative(ids, values);

        auto cursor = stmt.ExecuteDirect(R"(SELECT "Value" FROM "Test" ORDER BY "Id")");
        for (auto const value: values)
        {
            REQUIRE(cursor.FetchRow());
            CHECK(cursor.GetColumn<char>(1) == value);
        }
        CHECK_FALSE(cursor.FetchRow());
    }

    SECTION("ExecuteBatch (row-wise)")
    {
        auto const rows = std::array {
            CharBatchRow { .id = 1, .value = 'A', .maybe = 'a' },
            CharBatchRow { .id = 2, .value = 'B', .maybe = std::nullopt },
            CharBatchRow { .id = 3, .value = 'C', .maybe = 'c' },
        };

        stmt.Prepare(R"(INSERT INTO "Test" ("Id", "Value", "Maybe") VALUES (?, ?, ?))");
        std::ignore = stmt.ExecuteBatch(
            std::span { rows },
            [](CharBatchRow const& r) -> int64_t const& { return r.id; },
            [](CharBatchRow const& r) -> char const& { return r.value; },
            [](CharBatchRow const& r) -> std::optional<char> const& { return r.maybe; });

        auto cursor = stmt.ExecuteDirect(R"(SELECT "Value", "Maybe" FROM "Test" ORDER BY "Id")");
        for (auto const& expected: rows)
        {
            REQUIRE(cursor.FetchRow());
            CHECK(cursor.GetColumn<char>(1) == expected.value);
            CHECK(cursor.GetColumn<std::optional<char>>(2) == expected.maybe);
        }
        CHECK_FALSE(cursor.FetchRow());
    }
}

// =============================================================================
// String literal / C-array input parameter binding
// (StringLiteral.hpp covers `T[N]` overloads — they were unexercised.)
// =============================================================================

TEST_CASE_METHOD(SqlTestFixture, "C-array string literal: char[N] InputParameter", "[SqlDataBinder]")
{
    auto stmt = SqlStatement {};
    (void) stmt.ExecuteDirect("CREATE TABLE Test (Value VARCHAR(32) NULL)");

    char const literal[] = "Literal";
    stmt.Prepare("INSERT INTO Test (Value) VALUES (?)");
    (void) stmt.Execute(literal);

    auto const actual = stmt.ExecuteDirectScalar<std::string>("SELECT Value FROM Test");
    REQUIRE(actual.has_value());
    CHECK(*actual == "Literal");
}

#if defined(_WIN32) || defined(_WIN64)
TEST_CASE_METHOD(SqlTestFixture, "C-array string literal: wchar_t[N] InputParameter", "[SqlDataBinder][Unicode]")
{
    // Exercises the dedicated SqlDataBinder<wchar_t[N]> overload, which is only
    // declared on platforms where sizeof(wchar_t) == 2 (Windows). On Linux/macOS
    // wchar_t is 4 bytes and StringLiteral.hpp does not specialize for it; the
    // Linux equivalent is the std::wstring path covered in the binder matrix above.
    auto stmt = SqlStatement {};
    auto const sqlColumnType =
        stmt.Connection().QueryFormatter().ColumnType(SqlColumnTypeDefinitions::NVarchar { .size = 32 });
    (void) stmt.ExecuteDirect(std::format("CREATE TABLE Test (Value {} NULL)", sqlColumnType));

    wchar_t const literal[] = L"WideLit";
    stmt.Prepare("INSERT INTO Test (Value) VALUES (?)");
    (void) stmt.Execute(literal);

    auto const actual = stmt.ExecuteDirectScalar<std::wstring>("SELECT Value FROM Test");
    REQUIRE(actual.has_value());
    CHECK(*actual == L"WideLit");
}
#endif

TEST_CASE_METHOD(SqlTestFixture, "C-array string literal: char16_t[N] InputParameter", "[SqlDataBinder][Unicode]")
{
    auto stmt = SqlStatement {};
    auto const sqlColumnType =
        stmt.Connection().QueryFormatter().ColumnType(SqlColumnTypeDefinitions::NVarchar { .size = 32 });
    (void) stmt.ExecuteDirect(std::format("CREATE TABLE Test (Value {} NULL)", sqlColumnType));

    char16_t const literal[] = u"u16Lit";
    stmt.Prepare("INSERT INTO Test (Value) VALUES (?)");
    (void) stmt.Execute(literal);

    auto const actual = stmt.ExecuteDirectScalar<std::u16string>("SELECT Value FROM Test");
    REQUIRE(actual.has_value());
    CHECK(*actual == u"u16Lit");
}

// =============================================================================
// Empty-string and edge-case round-trips
// =============================================================================

TEST_CASE_METHOD(SqlTestFixture, "Empty string round-trip across string types", "[SqlDataBinder][Unicode]")
{
    auto stmt = SqlStatement {};
    auto const sqlColumnType =
        stmt.Connection().QueryFormatter().ColumnType(SqlColumnTypeDefinitions::NVarchar { .size = 32 });
    (void) stmt.ExecuteDirect(std::format("CREATE TABLE Test (Value {} NULL)", sqlColumnType));

    stmt.Prepare("INSERT INTO Test (Value) VALUES (?)");

    SECTION("std::string empty")
    {
        (void) stmt.Execute(std::string {});
        auto const actual = stmt.ExecuteDirectScalar<std::string>("SELECT Value FROM Test");
        REQUIRE(actual.has_value());
        CHECK(actual->empty());
    }

    SECTION("std::u8string empty")
    {
        (void) stmt.Execute(std::u8string {});
        auto const actual = stmt.ExecuteDirectScalar<std::u8string>("SELECT Value FROM Test");
        REQUIRE(actual.has_value());
        CHECK(actual->empty());
    }

    SECTION("std::u16string empty")
    {
        (void) stmt.Execute(std::u16string {});
        auto const actual = stmt.ExecuteDirectScalar<std::u16string>("SELECT Value FROM Test");
        REQUIRE(actual.has_value());
        CHECK(actual->empty());
    }

    SECTION("std::u32string empty")
    {
        (void) stmt.Execute(std::u32string {});
        auto const actual = stmt.ExecuteDirectScalar<std::u32string>("SELECT Value FROM Test");
        REQUIRE(actual.has_value());
        CHECK(actual->empty());
    }

    SECTION("std::wstring empty")
    {
        (void) stmt.Execute(std::wstring {});
        auto const actual = stmt.ExecuteDirectScalar<std::wstring>("SELECT Value FROM Test");
        REQUIRE(actual.has_value());
        CHECK(actual->empty());
    }

    SECTION("SqlAnsiString<N> empty")
    {
        (void) stmt.Execute(SqlAnsiString<32> {});
        auto const actual = stmt.ExecuteDirectScalar<SqlAnsiString<32>>("SELECT Value FROM Test");
        REQUIRE(actual.has_value());
        CHECK(actual->empty());
    }

    SECTION("SqlWideString<N> empty")
    {
        (void) stmt.Execute(SqlWideString<32> {});
        auto const actual = stmt.ExecuteDirectScalar<SqlWideString<32>>("SELECT Value FROM Test");
        REQUIRE(actual.has_value());
        CHECK(actual->empty());
    }
}

TEST_CASE_METHOD(SqlTestFixture, "Max-capacity ASCII round-trip", "[SqlDataBinder]")
{
    // Pin the boundary case: a value that fills the declared column width round-trips
    // intact via std::string (which auto-grows on retrieval) and via SqlAnsiString<N+1>
    // (so the output buffer has room for both the data and the trailing null terminator
    // that the ANSI driver path writes). The asymmetry is a known quirk of
    // SqlFixedString<N>: input binders pass N bytes of payload, but GetColumn passes
    // `AnsiStringType::Capacity` as the SQLGetData buffer length — which the driver
    // treats as `bytes-including-null-terminator`, capping the readable payload at N-1.
    auto stmt = SqlStatement {};
    auto const sqlColumnType =
        stmt.Connection().QueryFormatter().ColumnType(SqlColumnTypeDefinitions::Varchar { .size = 32 });
    (void) stmt.ExecuteDirect(std::format("CREATE TABLE Test (Value {} NULL)", sqlColumnType));

    auto const payload = std::string(32, 'A');
    stmt.Prepare("INSERT INTO Test (Value) VALUES (?)");
    (void) stmt.Execute(payload);

    SECTION("via std::string GetColumn")
    {
        auto cursor = stmt.ExecuteDirect("SELECT Value FROM Test");
        REQUIRE(cursor.FetchRow());
        auto const actual = cursor.GetColumn<std::string>(1);
        CHECK(actual.size() == 32);
        CHECK(actual == payload);
    }

    SECTION("via SqlAnsiString with one byte of headroom")
    {
        auto cursor = stmt.ExecuteDirect("SELECT Value FROM Test");
        REQUIRE(cursor.FetchRow());
        auto const actual = cursor.GetColumn<SqlAnsiString<33>>(1);
        CHECK(actual.size() == 32);
        CHECK(actual == SqlAnsiString<33> { payload });
    }
}

TEST_CASE_METHOD(SqlTestFixture, "Max-capacity wide round-trip", "[SqlDataBinder][Unicode]")
{
    auto stmt = SqlStatement {};
    auto const sqlColumnType =
        stmt.Connection().QueryFormatter().ColumnType(SqlColumnTypeDefinitions::NVarchar { .size = 32 });
    (void) stmt.ExecuteDirect(std::format("CREATE TABLE Test (Value {} NULL)", sqlColumnType));

    auto const payload = std::wstring(32, L'X');
    stmt.Prepare("INSERT INTO Test (Value) VALUES (?)");
    (void) stmt.Execute(payload);

    SECTION("via std::wstring GetColumn")
    {
        auto cursor = stmt.ExecuteDirect("SELECT Value FROM Test");
        REQUIRE(cursor.FetchRow());
        auto const actual = cursor.GetColumn<std::wstring>(1);
        CHECK(actual.size() == 32);
        CHECK(actual == payload);
    }

    SECTION("via SqlWideString with one char of headroom")
    {
        auto cursor = stmt.ExecuteDirect("SELECT Value FROM Test");
        REQUIRE(cursor.FetchRow());
        auto const actual = cursor.GetColumn<SqlWideString<33>>(1);
        CHECK(actual.size() == 32);
        CHECK(actual == SqlWideString<33> { payload });
    }
}

TEST_CASE_METHOD(SqlTestFixture, "ASCII round-trip across all writable string types", "[SqlDataBinder]")
{
    // A single payload that all five storage types must round-trip through a
    // VARCHAR-shaped column. Catches divergent encoding decisions in the
    // ANSI vs UTF-16 vs UTF-32 vs UTF-8 binder paths for plain-ASCII input —
    // a regression here would mean one of the encoding paths corrupted bytes
    // that should be passthrough.
    auto stmt = SqlStatement {};
    auto const sqlColumnType =
        stmt.Connection().QueryFormatter().ColumnType(SqlColumnTypeDefinitions::NVarchar { .size = 64 });
    (void) stmt.ExecuteDirect(std::format("CREATE TABLE Test (Value {} NULL)", sqlColumnType));

    constexpr std::string_view payloadAscii = "ASCII Round Trip 0123456789";
    stmt.Prepare("INSERT INTO Test (Value) VALUES (?)");
    (void) stmt.Execute(std::string { payloadAscii });

    SECTION("std::string round-trip")
    {
        auto const actual = stmt.ExecuteDirectScalar<std::string>("SELECT Value FROM Test");
        REQUIRE(actual.has_value());
        CHECK(*actual == payloadAscii);
    }

    SECTION("std::u8string round-trip")
    {
        auto const actual = stmt.ExecuteDirectScalar<std::u8string>("SELECT Value FROM Test");
        REQUIRE(actual.has_value());
        auto const view = std::string_view(reinterpret_cast<char const*>(actual->data()), actual->size());
        CHECK(view == payloadAscii);
    }

    SECTION("std::u16string round-trip")
    {
        auto const actual = stmt.ExecuteDirectScalar<std::u16string>("SELECT Value FROM Test");
        REQUIRE(actual.has_value());
        CHECK(actual->size() == payloadAscii.size());
        for (std::size_t i = 0; i < payloadAscii.size(); ++i)
            CHECK(static_cast<char>((*actual)[i]) == payloadAscii[i]);
    }

    SECTION("std::u32string round-trip")
    {
        auto const actual = stmt.ExecuteDirectScalar<std::u32string>("SELECT Value FROM Test");
        REQUIRE(actual.has_value());
        CHECK(actual->size() == payloadAscii.size());
        for (std::size_t i = 0; i < payloadAscii.size(); ++i)
            CHECK(static_cast<char>((*actual)[i]) == payloadAscii[i]);
    }

    SECTION("std::wstring round-trip")
    {
        auto const actual = stmt.ExecuteDirectScalar<std::wstring>("SELECT Value FROM Test");
        REQUIRE(actual.has_value());
        CHECK(actual->size() == payloadAscii.size());
        for (std::size_t i = 0; i < payloadAscii.size(); ++i)
            CHECK(static_cast<char>((*actual)[i]) == payloadAscii[i]);
    }
}

TEST_CASE_METHOD(SqlTestFixture, "Unicode insert via std::string_view, retrieve via wide types", "[SqlDataBinder][Unicode]")
{
    // Same value, multiple retrieval encodings: pins that a single non-ASCII payload
    // arrives intact regardless of whether the caller asks for u8/u16/u32/wstring.
    auto stmt = SqlStatement {};
    auto const sqlColumnType =
        stmt.Connection().QueryFormatter().ColumnType(SqlColumnTypeDefinitions::NVarchar { .size = 32 });
    (void) stmt.ExecuteDirect(std::format("CREATE TABLE Test (Value {} NULL)", sqlColumnType));

    stmt.Prepare("INSERT INTO Test (Value) VALUES (?)");
    (void) stmt.Execute(u8"Käse \U0001F9C0"sv); // 5 + 1 emoji = 6 chars

    SECTION("retrieve as std::u8string")
    {
        auto const actual = stmt.ExecuteDirectScalar<std::u8string>("SELECT Value FROM Test");
        REQUIRE(actual.has_value());
        CHECK(*actual == u8"Käse \U0001F9C0");
    }

    SECTION("retrieve as std::u16string")
    {
        auto const actual = stmt.ExecuteDirectScalar<std::u16string>("SELECT Value FROM Test");
        REQUIRE(actual.has_value());
        CHECK(*actual == u"Käse \U0001F9C0");
    }

    SECTION("retrieve as std::u32string")
    {
        auto const actual = stmt.ExecuteDirectScalar<std::u32string>("SELECT Value FROM Test");
        REQUIRE(actual.has_value());
        CHECK(*actual == U"Käse \U0001F9C0");
    }

    SECTION("retrieve as std::wstring")
    {
        auto const actual = stmt.ExecuteDirectScalar<std::wstring>("SELECT Value FROM Test");
        REQUIRE(actual.has_value());
        CHECK(*actual == L"Käse \U0001F9C0");
    }
}

TEST_CASE_METHOD(SqlTestFixture, "Whitespace and control-character round-trip", "[SqlDataBinder]")
{
    // Pins that whitespace and printable-control bytes survive a VARCHAR round-trip.
    // \t and a leading/trailing space have historically been candidates for "helpful"
    // trimming by drivers and ORMs — this test catches any such silent normalization.
    auto stmt = SqlStatement {};
    (void) stmt.ExecuteDirect("CREATE TABLE Test (Value VARCHAR(32) NULL)");

    constexpr std::string_view payload = " a\tb c ";
    stmt.Prepare("INSERT INTO Test (Value) VALUES (?)");
    (void) stmt.Execute(std::string { payload });

    auto const actual = stmt.ExecuteDirectScalar<std::string>("SELECT Value FROM Test");
    REQUIRE(actual.has_value());
    CHECK(*actual == payload);
}

// =============================================================================
// SqlVariant: branch coverage for variant constructors, accessors, ToString.
// =============================================================================

TEST_CASE_METHOD(SqlTestFixture, "SqlVariant: construct from each supported alternative", "[SqlVariant]")
{
    SECTION("from string literal (char[N])")
    {
        SqlVariant v { "Hello" };
        REQUIRE(v.Is<std::string_view>());
        CHECK(v.Get<std::string_view>() == "Hello"sv);
    }

    SECTION("from char16_t literal")
    {
        SqlVariant v { u"World" };
        REQUIRE(v.Is<std::u16string_view>());
        CHECK(v.Get<std::u16string_view>() == u"World"sv);
    }

    SECTION("from SqlFixedString")
    {
        SqlVariant v { SqlAnsiString<8> { "Fixed" } };
        REQUIRE(v.Is<std::string>());
        CHECK(v.Get<std::string>() == "Fixed"s);
    }

    SECTION("from optional<int> with value")
    {
        SqlVariant v { std::optional<int> { 42 } };
        REQUIRE(v.Is<int>());
        CHECK(v.Get<int>() == 42);
    }

    SECTION("from optional<int> nullopt produces NULL")
    {
        SqlVariant const v { std::optional<int> {} };
        CHECK(v.IsNull());
    }
}

TEST_CASE_METHOD(SqlTestFixture, "SqlVariant: ToString covers each alternative", "[SqlVariant]")
{
    CHECK(SqlVariant { SqlNullValue }.ToString() == "NULL");
    CHECK(SqlVariant { true }.ToString() == "true");
    CHECK(SqlVariant { false }.ToString() == "false");
    CHECK(SqlVariant { static_cast<int8_t>(-12) }.ToString() == "-12");
    CHECK(SqlVariant { static_cast<short>(-1234) }.ToString() == "-1234");
    CHECK(SqlVariant { static_cast<unsigned short>(2222) }.ToString() == "2222");
    CHECK(SqlVariant { 42 }.ToString() == "42");
    CHECK(SqlVariant { 42U }.ToString() == "42");
    CHECK(SqlVariant { 1234567890123LL }.ToString() == "1234567890123");
    CHECK(SqlVariant { 1234567890123ULL }.ToString() == "1234567890123");
    CHECK(SqlVariant { std::string { "narrow" } }.ToString() == "narrow");
    CHECK(SqlVariant { std::string_view { "view" } }.ToString() == "view");
    CHECK(SqlVariant { SqlText { "txt" } }.ToString() == "txt");
    CHECK(SqlVariant { std::u16string { u"hi" } }.ToString() == "hi");
    CHECK(SqlVariant { std::u16string_view { u"hello" } }.ToString() == "hello");
    CHECK(SqlVariant { 3.14F }.ToString().substr(0, 4) == "3.14");
    CHECK(SqlVariant { 2.5 }.ToString().substr(0, 3) == "2.5");
    using namespace std::chrono;
    CHECK(SqlVariant { SqlDate { 2025y, January, 2d } }.ToString() == "2025-1-2");
    CHECK(SqlVariant { SqlTime { 12h, 34min, 56s } }.ToString() == "12:34:56");
}

TEST_CASE_METHOD(SqlTestFixture, "SqlVariant: equality and inequality operators", "[SqlVariant]")
{
    SqlVariant const a { 42 };
    SqlVariant const b { 42 };
    SqlVariant const c { 43 };
    CHECK(a == b);
    CHECK(a != c);
    CHECK_FALSE(a != b);
    CHECK_FALSE(a == c);
}

TEST_CASE_METHOD(SqlTestFixture, "SqlVariant: TryGetIntegral conversions", "[SqlVariant]")
{
    SqlVariant const i { 42 };
    CHECK(i.TryGetInt().value_or(0) == 42);
    CHECK(i.TryGetShort().value_or(0) == 42);
    CHECK(i.TryGetUShort().value_or(0) == 42);
    CHECK(i.TryGetUInt().value_or(0) == 42);
    CHECK(i.TryGetLongLong().value_or(0) == 42);
    CHECK(i.TryGetULongLong().value_or(0) == 42);
    CHECK(i.TryGetInt8().value_or(0) == 42);
    CHECK(i.TryGetBool().value_or(false));

    SECTION("TryGetInt on NULL returns nullopt")
    {
        SqlVariant const nullVariant { SqlNullValue };
        CHECK(!nullVariant.TryGetInt().has_value());
    }

    // Note: TryGetIntegral on a non-integral variant throws std::bad_variant_access,
    // but the public TryGet* wrappers are marked noexcept — calling them on a wrong
    // type aborts (std::terminate) rather than throwing. Catch2 cannot test that.
}

TEST_CASE_METHOD(SqlTestFixture, "SqlVariant: TryGetStringView paths", "[SqlVariant]")
{
    SECTION("from std::string")
    {
        SqlVariant const v { std::string { "Hello" } };
        auto const sv = v.TryGetStringView();
        REQUIRE(sv.has_value());
        CHECK(*sv == "Hello"sv);
    }

    SECTION("from std::string_view")
    {
        SqlVariant const v { std::string_view { "ViewStr" } };
        auto const sv = v.TryGetStringView();
        REQUIRE(sv.has_value());
        CHECK(*sv == "ViewStr"sv);
    }

    SECTION("from SqlText")
    {
        SqlVariant const v { SqlText { "Body" } };
        auto const sv = v.TryGetStringView();
        REQUIRE(sv.has_value());
        CHECK(*sv == "Body"sv);
    }

    SECTION("on NULL returns nullopt")
    {
        SqlVariant const v { SqlNullValue };
        CHECK(!v.TryGetStringView().has_value());
    }
    // Note: TryGetStringView on a non-string variant aborts (the function is noexcept
    // and throws bad_variant_access inside std::visit, which calls std::terminate).
}

TEST_CASE_METHOD(SqlTestFixture, "SqlVariant: TryGetUtf16StringView paths", "[SqlVariant]")
{
    SECTION("from std::u16string")
    {
        SqlVariant const v { std::u16string { u"Hello" } };
        auto const sv = v.TryGetUtf16StringView();
        REQUIRE(sv.has_value());
        CHECK(*sv == u"Hello"sv);
    }

    SECTION("from std::u16string_view")
    {
        SqlVariant const v { std::u16string_view { u"ViewU16" } };
        auto const sv = v.TryGetUtf16StringView();
        REQUIRE(sv.has_value());
        CHECK(*sv == u"ViewU16"sv);
    }

    SECTION("on NULL returns nullopt")
    {
        SqlVariant const v { SqlNullValue };
        CHECK(!v.TryGetUtf16StringView().has_value());
    }
    // Same caveat as TryGetStringView: noexcept + throw inside visit aborts on non-u16 variants.
}

TEST_CASE_METHOD(SqlTestFixture, "SqlVariant: TryGetDate / TryGetTime / TryGetDateTime", "[SqlVariant]")
{
    using namespace std::chrono;
    SECTION("TryGetDate from SqlDate")
    {
        SqlVariant const v { SqlDate { 2026y, May, 5d } };
        REQUIRE(v.TryGetDate().has_value());
    }

    SECTION("TryGetDate from SqlDateTime extracts the date portion")
    {
        SqlVariant const v { SqlDateTime { 2026y, May, 5d, 12h, 0min, 0s, 0ns } };
        auto const date = v.TryGetDate();
        REQUIRE(date.has_value());
        CHECK(date->sqlValue.year == 2026);
        CHECK(date->sqlValue.month == 5);
        CHECK(date->sqlValue.day == 5);
    }

    SECTION("TryGetDate on NULL returns nullopt")
    {
        SqlVariant const v { SqlNullValue };
        CHECK(!v.TryGetDate().has_value());
    }

    SECTION("TryGetDate on a wrong type throws")
    {
        SqlVariant const v { 42 };
        CHECK_THROWS_AS(v.TryGetDate(), std::bad_variant_access);
    }

    SECTION("TryGetTime from SqlTime")
    {
        SqlVariant const v { SqlTime { 12h, 34min, 56s } };
        REQUIRE(v.TryGetTime().has_value());
    }

    SECTION("TryGetTime from SqlDateTime extracts the time portion")
    {
        SqlVariant const v { SqlDateTime { 2026y, May, 5d, 12h, 0min, 0s, 0ns } };
        auto const time = v.TryGetTime();
        REQUIRE(time.has_value());
        CHECK(time->sqlValue.hour == 12);
    }

    SECTION("TryGetTime on NULL returns nullopt")
    {
        SqlVariant const v { SqlNullValue };
        CHECK(!v.TryGetTime().has_value());
    }

    SECTION("TryGetTime on wrong type throws")
    {
        SqlVariant const v { 42 };
        CHECK_THROWS_AS(v.TryGetTime(), std::bad_variant_access);
    }

    SECTION("TryGetDateTime from SqlDateTime")
    {
        SqlVariant const v { SqlDateTime { 2026y, May, 5d, 12h, 0min, 0s, 0ns } };
        REQUIRE(v.TryGetDateTime().has_value());
    }

    SECTION("TryGetDateTime on NULL returns nullopt")
    {
        SqlVariant const v { SqlNullValue };
        CHECK(!v.TryGetDateTime().has_value());
    }

    SECTION("TryGetDateTime on wrong type throws")
    {
        SqlVariant const v { 42 };
        CHECK_THROWS_AS(v.TryGetDateTime(), std::bad_variant_access);
    }
}

TEST_CASE_METHOD(SqlTestFixture, "SqlVariant: ValueOr returns default for NULL", "[SqlVariant]")
{
    SqlVariant const nullV { SqlNullValue };
    CHECK(nullV.ValueOr(99) == 99);
    CHECK(nullV.ValueOr(std::string { "fallback" }) == "fallback");

    SqlVariant const intV { 42 };
    CHECK(intV.ValueOr(99) == 42);
}

TEST_CASE_METHOD(SqlTestFixture, "SqlVariant: Get<optional<T>> handles NULL", "[SqlVariant]")
{
    SqlVariant nullV { SqlNullValue };
    auto const result = nullV.Get<std::optional<int>>();
    CHECK(!result.has_value());

    SqlVariant intV { 42 };
    auto const result2 = intV.Get<std::optional<int>>();
    REQUIRE(result2.has_value());
    CHECK(*result2 == 42);
}

TEST_CASE_METHOD(SqlTestFixture, "SqlVariant: GetColumn for INTEGER columns", "[SqlVariant][SqlDataBinder]")
{
    auto stmt = SqlStatement {};
    (void) stmt.ExecuteDirect("CREATE TABLE Test (Value INTEGER NULL)");
    stmt.Prepare("INSERT INTO Test (Value) VALUES (?)");
    (void) stmt.Execute(42);

    auto const result = stmt.ExecuteDirectScalar<SqlVariant>("SELECT Value FROM Test");
    REQUIRE(result.TryGetInt().has_value());
    CHECK(result.TryGetInt().value_or(0) == 42);
}

TEST_CASE_METHOD(SqlTestFixture, "SqlVariant: GetColumn for FLOAT/DOUBLE columns", "[SqlVariant][SqlDataBinder]")
{
    auto stmt = SqlStatement {};
    (void) stmt.ExecuteDirect("CREATE TABLE Test (Value DOUBLE PRECISION NULL)");
    stmt.Prepare("INSERT INTO Test (Value) VALUES (?)");
    (void) stmt.Execute(1.5);

    auto const result = stmt.ExecuteDirectScalar<SqlVariant>("SELECT Value FROM Test");
    REQUIRE_FALSE(result.IsNull());
    auto const asDouble = [&]() -> double {
        if (result.Is<double>())
            return std::get<double>(result.value);
        if (result.Is<float>())
            return static_cast<double>(std::get<float>(result.value));
        return 0.0;
    }();
    CHECK_THAT(asDouble, Catch::Matchers::WithinAbs(1.5, 0.001));
}

TEST_CASE_METHOD(SqlTestFixture, "SqlVariant: GetColumn for VARCHAR columns", "[SqlVariant][SqlDataBinder]")
{
    auto stmt = SqlStatement {};
    (void) stmt.ExecuteDirect("CREATE TABLE Test (Value VARCHAR(64) NULL)");
    stmt.Prepare("INSERT INTO Test (Value) VALUES (?)");
    (void) stmt.Execute(std::string { "Hello" });

    auto const result = stmt.ExecuteDirectScalar<SqlVariant>("SELECT Value FROM Test");
    auto const sv = result.TryGetStringView();
    REQUIRE(sv.has_value());
    CHECK(*sv == "Hello"sv);
}

TEST_CASE_METHOD(SqlTestFixture, "SqlVariant: GetColumn for NVARCHAR columns", "[SqlVariant][SqlDataBinder][Unicode]")
{
    auto stmt = SqlStatement {};
    if (stmt.Connection().ServerType() == SqlServerType::SQLITE)
        (void) stmt.ExecuteDirect("PRAGMA encoding = 'UTF-16'");

    auto const sqlColumnType =
        stmt.Connection().QueryFormatter().ColumnType(SqlColumnTypeDefinitions::NVarchar { .size = 64 });
    (void) stmt.ExecuteDirect(std::format("CREATE TABLE Test (Value {} NULL)", sqlColumnType));
    stmt.Prepare("INSERT INTO Test (Value) VALUES (?)");
    (void) stmt.Execute(std::u16string { u"Hello" });

    auto const result = stmt.ExecuteDirectScalar<SqlVariant>("SELECT Value FROM Test");
    // SqlVariant stores wide strings as UTF-8 std::string.
    auto const sv = result.TryGetStringView();
    REQUIRE(sv.has_value());
    CHECK(*sv == "Hello"sv);
}

TEST_CASE_METHOD(SqlTestFixture, "SqlVariant: GetColumn for SMALLINT columns", "[SqlVariant][SqlDataBinder]")
{
    auto stmt = SqlStatement {};
    (void) stmt.ExecuteDirect("CREATE TABLE Test (Value SMALLINT NULL)");
    stmt.Prepare("INSERT INTO Test (Value) VALUES (?)");
    (void) stmt.Execute(static_cast<short>(1234));

    auto const result = stmt.ExecuteDirectScalar<SqlVariant>("SELECT Value FROM Test");
    CHECK(result.ValueOr(0) == 1234);
}

TEST_CASE_METHOD(SqlTestFixture, "SqlVariant: GetColumn for BIGINT columns", "[SqlVariant][SqlDataBinder]")
{
    auto stmt = SqlStatement {};
    (void) stmt.ExecuteDirect("CREATE TABLE Test (Value BIGINT NULL)");
    stmt.Prepare("INSERT INTO Test (Value) VALUES (?)");
    (void) stmt.Execute(1234567890123LL);

    auto const result = stmt.ExecuteDirectScalar<SqlVariant>("SELECT Value FROM Test");
    CHECK(result.TryGetLongLong().value_or(0) == 1234567890123LL);
}

// Regression test for the SQLite ODBC driver quirk: it reports `SQL_DESC_CONCISE_TYPE` as
// `SQL_C_SBIGINT (-25)` rather than `SQL_BIGINT (-5)` for BIGINT columns. Before the binder
// learned to accept the C-type spelling, fetching a BIGINT column into a `SqlVariant` on
// SQLite raised `SQL_UNSUPPORTED_TYPE`. The asserts below pin both the signed-range round-trip
// and the variant alternative the binder picks, so a future regression that misroutes the
// type (e.g. as `int`) is also caught.
TEST_CASE_METHOD(SqlTestFixture,
                 "SqlVariant: BIGINT round-trips across the full signed range",
                 "[SqlVariant][SqlDataBinder]")
{
    auto stmt = SqlStatement {};
    (void) stmt.ExecuteDirect("CREATE TABLE Test (Value BIGINT NULL)");

    constexpr std::array<long long, 4> kSamples {
        std::numeric_limits<long long>::min(),
        -1234567890123LL,
        0LL,
        std::numeric_limits<long long>::max(),
    };

    for (auto const sample: kSamples)
    {
        (void) stmt.ExecuteDirect("DELETE FROM Test");
        stmt.Prepare("INSERT INTO Test (Value) VALUES (?)");
        (void) stmt.Execute(sample);

        auto const result = stmt.ExecuteDirectScalar<SqlVariant>("SELECT Value FROM Test");
        INFO("sample=" << sample);
        REQUIRE_FALSE(result.IsNull());
        // The driver must surface the value as `long long` — not `int` (which would silently
        // truncate INT64_MIN/MAX) and not a string (which would mean the binder fell through
        // to the SQL_VARCHAR fallback).
        REQUIRE(result.Is<long long>());
        CHECK(std::get<long long>(result.value) == sample);
        CHECK(result.TryGetLongLong().value_or(0) == sample);
    }
}

TEST_CASE_METHOD(SqlTestFixture, "SqlVariant: GetColumn for REAL columns", "[SqlVariant][SqlDataBinder]")
{
    // SQLite's type affinity stores REAL values as DOUBLE-class internally, so the
    // SQL_DESC_TYPE comes back as SQL_DOUBLE rather than SQL_REAL. MSSQL routes to
    // SQL_REAL, then the binder writes a float into the variant. The test only
    // verifies that the round-trip preserves the numeric value within tolerance,
    // regardless of which variant alternative the driver picks.
    auto stmt = SqlStatement {};
    (void) stmt.ExecuteDirect("CREATE TABLE Test (Value REAL NULL)");
    stmt.Prepare("INSERT INTO Test (Value) VALUES (?)");
    (void) stmt.Execute(1.5F);

    auto const result = stmt.ExecuteDirectScalar<SqlVariant>("SELECT Value FROM Test");
    REQUIRE_FALSE(result.IsNull());
    auto const asDouble = [&]() -> double {
        if (result.Is<double>())
            return std::get<double>(result.value);
        if (result.Is<float>())
            return static_cast<double>(std::get<float>(result.value));
        return 0.0;
    }();
    CHECK_THAT(asDouble, Catch::Matchers::WithinAbs(1.5, 0.001));
}

TEST_CASE_METHOD(SqlTestFixture, "SqlVariant: GetColumn for DATE columns", "[SqlVariant][SqlDataBinder]")
{
    auto stmt = SqlStatement {};
    stmt.MigrateDirect(
        [](auto& migration) { migration.CreateTable("Test").Column("Value", SqlColumnTypeDefinitions::Date {}); });
    using namespace std::chrono;
    auto const expected = SqlDate { 2025y, January, 2d };
    stmt.Prepare(stmt.Query("Test").Insert().Set("Value", SqlWildcard));
    (void) stmt.Execute(expected);

    auto const result = stmt.ExecuteDirectScalar<SqlVariant>(stmt.Query("Test").Select().Field("Value").All());
    auto const date = result.TryGetDate();
    REQUIRE(date.has_value());
    CHECK(date.value() == expected);
}

TEST_CASE_METHOD(SqlTestFixture, "SqlVariant: GetColumn for DATETIME columns", "[SqlVariant][SqlDataBinder]")
{
    auto stmt = SqlStatement {};
    stmt.MigrateDirect(
        [](auto& migration) { migration.CreateTable("Test").Column("Value", SqlColumnTypeDefinitions::DateTime {}); });
    using namespace std::chrono;
    auto const expected = SqlDateTime { 2025y, January, 2d, 12h, 34min, 56s, 0ns };
    stmt.Prepare(stmt.Query("Test").Insert().Set("Value", SqlWildcard));
    (void) stmt.Execute(expected);

    auto const result = stmt.ExecuteDirectScalar<SqlVariant>(stmt.Query("Test").Select().Field("Value").All());
    auto const dt = result.TryGetDateTime();
    REQUIRE(dt.has_value());
    // Compare individual components — fraction may not round-trip exactly across DBMSes.
    CHECK(dt->sqlValue.year == 2025);
    CHECK(dt->sqlValue.month == 1);
    CHECK(dt->sqlValue.day == 2);
    CHECK(dt->sqlValue.hour == 12);
    CHECK(dt->sqlValue.minute == 34);
    CHECK(dt->sqlValue.second == 56);
}

TEST_CASE_METHOD(SqlTestFixture, "SqlVariant: GetColumn for NUMERIC columns", "[SqlVariant][SqlDataBinder]")
{
    auto stmt = SqlStatement {};
    stmt.MigrateDirect([](auto& migration) {
        migration.CreateTable("Test").Column("Value", SqlColumnTypeDefinitions::Decimal { .precision = 15, .scale = 2 });
    });
    auto const expected = SqlNumeric<15, 2> { 12345.67 };
    stmt.Prepare(stmt.Query("Test").Insert().Set("Value", SqlWildcard));
    (void) stmt.Execute(expected);

    auto const result = stmt.ExecuteDirectScalar<SqlVariant>(stmt.Query("Test").Select().Field("Value").All());
    // SQLite drivers often report DECIMAL as VARCHAR; MSSQL routes through SQL_NUMERIC.
    // Either path stores something representable, so just verify the variant has data.
    CHECK_FALSE(result.IsNull());
}

TEST_CASE_METHOD(SqlTestFixture, "SqlVariant: GetColumn for NULL across types", "[SqlVariant][SqlDataBinder]")
{
    auto stmt = SqlStatement {};
    (void) stmt.ExecuteDirect("CREATE TABLE Test (Value INTEGER NULL)");
    stmt.Prepare("INSERT INTO Test (Value) VALUES (?)");
    (void) stmt.Execute(SqlNullValue);

    auto const result = stmt.ExecuteDirectScalar<SqlVariant>("SELECT Value FROM Test");
    CHECK(result.IsNull());
}

// =============================================================================
// Inspect functions: cover the data-binder Inspect overloads that drivers call
// when logging input parameter values for diagnostics.
// =============================================================================

TEST_CASE_METHOD(SqlTestFixture, "SqlDataBinder Inspect: char[N]", "[SqlDataBinder]")
{
    char const literal[] = "InspectAscii";
    auto const inspected = SqlDataBinder<char[sizeof(literal)]>::Inspect(literal);
    auto const view = std::string_view(inspected.data(), inspected.size());
    CHECK(view.contains("InspectAscii"));
}

TEST_CASE_METHOD(SqlTestFixture, "SqlDataBinder Inspect: char16_t[N]", "[SqlDataBinder][Unicode]")
{
    char16_t const literal[] = u"InspectU16";
    auto const inspected = SqlDataBinder<char16_t[sizeof(literal) / sizeof(char16_t)]>::Inspect(literal);
    CHECK(inspected.contains("InspectU16"));
}

#if defined(_WIN32) || defined(_WIN64)
TEST_CASE_METHOD(SqlTestFixture, "SqlDataBinder Inspect: wchar_t[N]", "[SqlDataBinder][Unicode]")
{
    wchar_t const literal[] = L"InspectWide";
    auto const inspected = SqlDataBinder<wchar_t[sizeof(literal) / sizeof(wchar_t)]>::Inspect(literal);
    CHECK(inspected.contains("InspectWide"));
}
#endif

TEST_CASE_METHOD(SqlTestFixture, "SqlDataBinder Inspect: std::string_view", "[SqlDataBinder]")
{
    auto const sv = std::string_view { "ViewInspect" };
    auto const inspected = SqlDataBinder<std::string_view>::Inspect(sv);
    CHECK(inspected == "ViewInspect"sv);
}

TEST_CASE_METHOD(SqlTestFixture, "SqlDataBinder Inspect: std::u16string_view", "[SqlDataBinder][Unicode]")
{
    auto const sv = std::u16string_view { u"U16ViewInspect" };
    auto const inspected = SqlDataBinder<std::u16string_view>::Inspect(sv);
    CHECK(inspected.contains("U16ViewInspect"));
}

TEST_CASE_METHOD(SqlTestFixture, "SqlDataBinder Inspect: std::u32string_view", "[SqlDataBinder][Unicode]")
{
    auto const sv = std::u32string_view { U"U32ViewInspect" };
    auto const inspected = SqlDataBinder<std::u32string_view>::Inspect(sv);
    CHECK(inspected.contains("U32ViewInspect"));
}

TEST_CASE_METHOD(SqlTestFixture, "SqlDataBinder Inspect: std::u8string_view", "[SqlDataBinder][Unicode]")
{
    auto const sv = std::u8string_view { u8"U8ViewInspect" };
    auto const inspected = SqlDataBinder<std::u8string_view>::Inspect(sv);
    CHECK(inspected == "U8ViewInspect"sv);
}

TEST_CASE_METHOD(SqlTestFixture, "SqlDataBinder Inspect: SqlVariant", "[SqlVariant]")
{
    auto const v = SqlVariant { 42 };
    CHECK(SqlDataBinder<SqlVariant>::Inspect(v) == "42");
}

TEST_CASE_METHOD(SqlTestFixture, "SqlDataBinder Inspect: std::optional", "[SqlDataBinder]")
{
    SECTION("with value forwards to T's Inspect")
    {
        std::optional<int> const v { 42 };
        CHECK(SqlDataBinder<std::optional<int>>::Inspect(v) == std::string(SqlDataBinder<int>::Inspect(42)));
    }

    SECTION("nullopt produces \"NULL\"")
    {
        std::optional<int> const v;
        CHECK(SqlDataBinder<std::optional<int>>::Inspect(v) == "NULL");
    }
}

TEST_CASE_METHOD(SqlTestFixture, "SqlDataBinder Inspect: SqlBinary", "[SqlDataBinder]")
{
    auto const v = SqlBinary { 0x01, 0x02, 0x03, 0x04 };
    CHECK(SqlDataBinder<SqlBinary>::Inspect(v) == "SqlBinary(size=4)");
}

TEST_CASE_METHOD(SqlTestFixture, "SqlDataBinder Inspect: SqlNumeric", "[SqlDataBinder]")
{
    auto const v = SqlNumeric<10, 2> { 123.45 };
    auto const inspected = SqlDataBinder<SqlNumeric<10, 2>>::Inspect(v);
    CHECK(inspected.contains("123.45"));
}

TEST_CASE_METHOD(SqlTestFixture, "SqlBinary: comparison operators", "[SqlDataBinder]")
{
    auto const a = SqlBinary { 0x01, 0x02 };
    auto const b = SqlBinary { 0x01, 0x02 };
    auto const c = SqlBinary { 0x01, 0x02, 0x03 };
    CHECK(a == b);
    CHECK(a != c);
    CHECK(a < c);
}

TEST_CASE_METHOD(SqlTestFixture, "SqlNumeric: ToString and conversions", "[SqlNumeric]")
{
    auto const v = SqlNumeric<10, 2> { 42.5 };
    auto const s = v.ToString();
    CHECK((s == "42.5" || s == "42.50")); // Trailing-zero handling depends on the formatter.
    CHECK_THAT(v.ToFloat(), Catch::Matchers::WithinAbs(42.5F, 0.001));
    CHECK_THAT(v.ToDouble(), Catch::Matchers::WithinAbs(42.5, 0.001));
}

TEST_CASE_METHOD(SqlTestFixture, "SqlNumeric: assignment from floating point", "[SqlNumeric]")
{
    SqlNumeric<10, 2> v;
    v = 99.99;
    CHECK_THAT(v.ToDouble(), Catch::Matchers::WithinAbs(99.99, 0.01));
}

TEST_CASE_METHOD(SqlTestFixture, "SqlNumeric: zero value", "[SqlNumeric]")
{
    auto const v = SqlNumeric<10, 2> { 0.0 };
    CHECK_THAT(v.ToDouble(), Catch::Matchers::WithinAbs(0.0, 0.001));
}

TEST_CASE_METHOD(SqlTestFixture, "SqlNumeric: negative value", "[SqlNumeric]")
{
    auto const v = SqlNumeric<10, 2> { -42.5 };
    CHECK_THAT(v.ToDouble(), Catch::Matchers::WithinAbs(-42.5, 0.001));
    CHECK(v.ToString().starts_with("-"));
}

TEST_CASE_METHOD(SqlTestFixture, "SqlVariant: GetColumn for BIT/TINYINT columns", "[SqlVariant][SqlDataBinder]")
{
    SECTION("BIT")
    {
        auto stmt = SqlStatement {};
        stmt.MigrateDirect(
            [](auto& migration) { migration.CreateTable("Test").Column("Value", SqlColumnTypeDefinitions::Bool {}); });
        stmt.Prepare(stmt.Query("Test").Insert().Set("Value", SqlWildcard));
        (void) stmt.Execute(true);

        auto const result = stmt.ExecuteDirectScalar<SqlVariant>(stmt.Query("Test").Select().Field("Value").All());
        REQUIRE_FALSE(result.IsNull());
    }

    SECTION("TINYINT")
    {
        auto stmt = SqlStatement {};
        // PostgreSQL has no TINYINT — the formatter maps `Tinyint` to SMALLINT on PG and
        // TINYINT on the others, so MigrateDirect keeps the test portable.
        stmt.MigrateDirect(
            [](auto& migration) { migration.CreateTable("Test").Column("Value", SqlColumnTypeDefinitions::Tinyint {}); });
        stmt.Prepare(stmt.Query("Test").Insert().Set("Value", SqlWildcard));
        (void) stmt.Execute(static_cast<int8_t>(42));

        auto const result = stmt.ExecuteDirectScalar<SqlVariant>(stmt.Query("Test").Select().Field("Value").All());
        REQUIRE_FALSE(result.IsNull());
    }
}

TEST_CASE_METHOD(SqlTestFixture, "SqlVariant: GetColumn for VARBINARY columns", "[SqlVariant][SqlDataBinder]")
{
    auto stmt = SqlStatement {};
    // Use MigrateDirect so CREATE TABLE and the Query builder agree on identifier quoting —
    // PostgreSQL folds unquoted `Test` to `test`, while `stmt.Query("Test")` emits quoted "Test".
    stmt.MigrateDirect([](auto& migration) {
        migration.CreateTable("Test").Column("Value", SqlColumnTypeDefinitions::VarBinary { .size = 32 });
    });
    stmt.Prepare(stmt.Query("Test").Insert().Set("Value", SqlWildcard));
    (void) stmt.Execute(SqlBinary { 0x01, 0x02, 0x03 });

    auto const result = stmt.ExecuteDirectScalar<SqlVariant>(stmt.Query("Test").Select().Field("Value").All());
    REQUIRE_FALSE(result.IsNull());
}

TEST_CASE_METHOD(SqlTestFixture, "std::optional<T>::OutputColumn: nullptr result returns SQL_ERROR", "[SqlDataBinder]")
{
    // Pin the early-return branch when the result pointer is null. We cannot
    // bind to a null result via the normal path, so we just call OutputColumn
    // directly with nullptr.
    SQLLEN indicator = 0;
    struct DummyCallback: public SqlDataBinderCallback
    {
        void PlanPostExecuteCallback(std::function<void()>&& cb) override
        {
            // Drop the callback on the floor (this stub never executes it), but actually move
            // it into a discarded local so clang-tidy's rvalue-not-moved check is satisfied.
            [[maybe_unused]] auto consumed = std::move(cb);
        }
        void PlanPostProcessOutputColumn(std::function<void()>&& cb) override
        {
            [[maybe_unused]] auto consumed = std::move(cb);
        }

        void PlanPreFetchOutputColumn(SqlOutputColumnPreFetch const& /*step*/) override {}
        SQLLEN* ProvideInputIndicator() override
        {
            return nullptr;
        }
        SQLLEN* ProvideInputIndicators(size_t /*rowCount*/) override
        {
            return nullptr;
        }
        std::byte* ProvideBatchStagingBuffer(std::size_t /*byteCount*/) override
        {
            return nullptr;
        }
        [[nodiscard]] SqlServerType ServerType() const noexcept override
        {
            return SqlServerType::SQLITE;
        }
        [[nodiscard]] std::string const& DriverName() const noexcept override
        {
            static std::string const name = "test";
            return name;
        }
    };
    DummyCallback cb;
    auto const rv = SqlDataBinder<std::optional<int>>::OutputColumn(nullptr, 1, nullptr, &indicator, cb);
    CHECK(rv == SQL_ERROR);

    // A callback that knows nothing about parameter types says so, rather than guessing one.
    CHECK(!cb.DescribeInputParameterType(1).has_value());
}

// =============================================================================
// SqlFixedString self-comparison and additional branch coverage
// =============================================================================

TEST_CASE_METHOD(SqlTestFixture, "SqlFixedString: self-comparison short-circuits", "[SqlFixedString]")
{
    SqlFixedString<8> const str { "Hello" };
    // Pin the `if ((void*) this == (void*) &other)` early-return branch in
    // SqlFixedString::operator<=> — comparing the same object against itself.
    CHECK(str == str);
    CHECK_FALSE(str != str);
    CHECK((str <=> str) == std::weak_ordering::equivalent);
}

TEST_CASE_METHOD(SqlTestFixture, "SqlDynamicString: self-comparison and edge cases", "[SqlDynamicString]")
{
    SqlDynamicString<32> const str { "Hello" };
    CHECK(str == str);
    CHECK_FALSE(str != str);

    SECTION("substr with offset > size returns empty view")
    {
        CHECK(str.substr(100).empty());
        CHECK(str.substr(50, 10).empty());
    }

    SECTION("substr with explicit max count")
    {
        CHECK(str.substr(0, 100) == "Hello"sv);
    }

    SECTION("setsize caps at dynamic capacity")
    {
        SqlDynamicString<8> tiny;
        tiny.push_back('a');
        tiny.setsize(20); // exceeds capacity, capped at 8
        CHECK(tiny.size() == 8);
        tiny.setsize(2);
        CHECK(tiny.size() == 2);
    }

    SECTION("const-iteration via const reference")
    {
        SqlDynamicString<32> const& cref = str;
        std::size_t count = 0;
        for ([[maybe_unused]] auto const ch: cref)
            ++count;
        CHECK(count == str.size());
    }

    SECTION("inequality with different sized SqlDynamicString")
    {
        SqlDynamicString<8> const tiny { "Abc" };
        SqlDynamicString<32> const large { "Xyz" };
        CHECK(tiny != large);
        CHECK_FALSE(tiny == large);
    }
}

TEST_CASE_METHOD(SqlTestFixture, "SqlFixedString: TrimRight stops at non-whitespace", "[SqlFixedString]")
{
    // Pin a non-trivial right-trim path: trailing spaces and tabs are all stripped.
    SqlTrimmedFixedString<32> str { "abc   \t   " };
    SqlBasicStringOperations<SqlTrimmedFixedString<32>>::TrimRight(&str);
    CHECK(str == "abc");

    SECTION("trim stops at non-whitespace even with extra padding")
    {
        SqlTrimmedFixedString<32> nonTrimmable { "abc def    " };
        SqlBasicStringOperations<SqlTrimmedFixedString<32>>::TrimRight(&nonTrimmable);
        CHECK(nonTrimmable == "abc def");
    }
}

TEST_CASE_METHOD(SqlTestFixture, "SqlFixedString: PostProcessOutputColumn modes", "[SqlFixedString]")
{
    using Variable = SqlAnsiString<10>;
    using Trimmed = SqlTrimmedFixedString<10>;
    using Fixed = SqlFixedString<10, char, SqlFixedStringMode::FIXED_SIZE>;

    SECTION("VARIABLE_SIZE strips trailing nulls")
    {
        Variable str;
        str.resize(10);
        // Fill with "Hi" + 8 zeros simulating an insertion of "Hi" followed by trailing nulls.
        str[0] = 'H';
        str[1] = 'i';
        for (std::size_t i = 2; i < 10; ++i)
            str[i] = '\0';
        SqlBasicStringOperations<Variable>::PostProcessOutputColumn(&str, static_cast<SQLLEN>(10));
        CHECK(str == "Hi"sv);
    }

    SECTION("FIXED_SIZE_RIGHT_TRIMMED strips trailing whitespace and nulls")
    {
        Trimmed str;
        str.resize(10);
        str[0] = 'H';
        str[1] = 'i';
        for (std::size_t i = 2; i < 10; ++i)
            str[i] = ' ';
        SqlBasicStringOperations<Trimmed>::PostProcessOutputColumn(&str, static_cast<SQLLEN>(10));
        CHECK(str == "Hi"sv);
    }

    SECTION("FIXED_SIZE keeps the buffer at full capacity except for trailing nulls")
    {
        Fixed str;
        str.resize(10);
        // "abcde" + 5 spaces — under FIXED_SIZE, spaces are kept, only nulls are stripped.
        for (std::size_t i = 0; i < 5; ++i)
            str[i] = static_cast<char>('a' + i);
        for (std::size_t i = 5; i < 10; ++i)
            str[i] = ' ';
        SqlBasicStringOperations<Fixed>::PostProcessOutputColumn(&str, static_cast<SQLLEN>(10));
        CHECK(str.size() == 10);
        CHECK(str.substr(0, 5) == "abcde"sv);
    }

    SECTION("SQL_NULL_DATA clears the result")
    {
        Variable str { "Filled" };
        SqlBasicStringOperations<Variable>::PostProcessOutputColumn(&str, SQL_NULL_DATA);
        CHECK(str.empty());
    }

    SECTION("SQL_NO_TOTAL resizes to capacity")
    {
        Fixed str;
        SqlBasicStringOperations<Fixed>::PostProcessOutputColumn(&str, SQL_NO_TOTAL);
        CHECK(str.size() == 10);
    }
}

TEST_CASE_METHOD(SqlTestFixture, "Single-character string round-trip", "[SqlDataBinder][Unicode]")
{
    // Min-size boundary: one ASCII char and one non-BMP codepoint. The single-codepoint
    // case probes the indicator-handling path where the reported length is sizeof(CharType).
    auto stmt = SqlStatement {};
    auto const sqlColumnType =
        stmt.Connection().QueryFormatter().ColumnType(SqlColumnTypeDefinitions::NVarchar { .size = 8 });
    (void) stmt.ExecuteDirect(std::format("CREATE TABLE Test (Value {} NULL)", sqlColumnType));

    SECTION("single ASCII char")
    {
        stmt.Prepare("INSERT INTO Test (Value) VALUES (?)");
        (void) stmt.Execute(std::string { "X" });
        auto const actual = stmt.ExecuteDirectScalar<std::string>("SELECT Value FROM Test");
        REQUIRE(actual.has_value());
        CHECK(*actual == "X");
    }

    SECTION("single supplementary-plane char as u16string")
    {
        stmt.Prepare("INSERT INTO Test (Value) VALUES (?)");
        (void) stmt.Execute(u"\U0001F600"s);
        auto const actual = stmt.ExecuteDirectScalar<std::u16string>("SELECT Value FROM Test");
        REQUIRE(actual.has_value());
        CHECK(*actual == u"\U0001F600");
        CHECK(actual->size() == 2); // surrogate pair
    }
}

// detail::ReadRawColumnArrayData is the body of GetRawColumnArrayData with its SQLGetData call made
// replaceable. No conforming driver reports these indicators, so a scripted source is the only way to
// reach the checks. An unchecked negative indicator became a huge size for resize(), and since the
// function is noexcept the resulting std::length_error ended the process.
TEST_CASE("GetRawColumnArrayData: length indicators", "[SqlDataBinder]")
{
    auto const read = [](std::vector<std::pair<SQLRETURN, SQLLEN>> const& script) {
        auto result = std::u16string(8, u'x');
        auto indicator = SQLLEN {};
        auto next = std::size_t { 0 };
        auto const returned = detail::ReadRawColumnArrayData<SQL_C_WCHAR>(
            [&](SQLPOINTER /*buffer*/, SQLLEN /*bufferBytes*/, SQLLEN* reported) noexcept -> SQLRETURN {
                if (next >= script.size())
                    return SQL_ERROR;
                auto const& [scriptedReturn, scriptedIndicator] = script[next++];
                *reported = scriptedIndicator;
                return scriptedReturn;
            },
            &result,
            &indicator);
        return std::pair { returned, result };
    };

    SECTION("values a conforming driver reports keep working")
    {
        CHECK(read({ { SQL_SUCCESS, 8 } }) == std::pair<SQLRETURN, std::u16string> { SQL_SUCCESS, u"xxxx" });
        CHECK(read({ { SQL_SUCCESS, SQL_NULL_DATA } }) == std::pair<SQLRETURN, std::u16string> { SQL_SUCCESS, u"" });
        CHECK(read({ { SQL_NO_DATA, 0 } }) == std::pair<SQLRETURN, std::u16string> { SQL_NO_DATA, u"" });

        // Known total: 100 bytes are 50 characters, and 7 characters arrived with the first call.
        auto const knownTotal = read({ { SQL_SUCCESS_WITH_INFO, 100 }, { SQL_SUCCESS, 86 } });
        CHECK(knownTotal.first == SQL_SUCCESS);
        CHECK(knownTotal.second.size() == 50);

        // Unknown total: 7 characters arrived with the first call and 3 with the last.
        auto const unknownTotal = read({ { SQL_SUCCESS_WITH_INFO, SQL_NO_TOTAL }, { SQL_SUCCESS, 6 } });
        CHECK(unknownTotal.first == SQL_SUCCESS);
        CHECK(unknownTotal.second.size() == 10);
    }

    SECTION("NULL alongside a warning is NULL")
    {
        CHECK(read({ { SQL_SUCCESS_WITH_INFO, SQL_NULL_DATA } })
              == std::pair<SQLRETURN, std::u16string> { SQL_SUCCESS_WITH_INFO, u"" });
    }

    SECTION("first call, any negative value on SQL_SUCCESS")
    {
        CHECK(read({ { SQL_SUCCESS, -3 } }).first == SQL_ERROR);
        // SQL_NO_TOTAL is valid only with SQL_SUCCESS_WITH_INFO; here no length can be derived from it.
        CHECK(read({ { SQL_SUCCESS, SQL_NO_TOTAL } }).first == SQL_ERROR);
    }

    SECTION("first call, a negative value on SQL_SUCCESS_WITH_INFO")
    {
        CHECK(read({ { SQL_SUCCESS_WITH_INFO, -3 } }).first == SQL_ERROR);
    }

    SECTION("continuation of a known total")
    {
        CHECK(read({ { SQL_SUCCESS_WITH_INFO, 100 }, { SQL_SUCCESS, -3 } }).first == SQL_ERROR);
        CHECK(read({ { SQL_SUCCESS_WITH_INFO, 100 }, { SQL_SUCCESS, SQL_NO_TOTAL } }).first == SQL_ERROR);
    }

    SECTION("continuation of an unknown total")
    {
        CHECK(read({ { SQL_SUCCESS_WITH_INFO, SQL_NO_TOTAL }, { SQL_SUCCESS, -3 } }).first == SQL_ERROR);
    }

    SECTION("a length the array cannot hold")
    {
        CHECK(read({ { SQL_SUCCESS_WITH_INFO, (std::numeric_limits<SQLLEN>::max)() } }).first == SQL_ERROR);
    }
}

// NOLINTEND(readability-container-size-empty, bugprone-unchecked-optional-access)

TEST_CASE_METHOD(SqlTestFixture, "SqlGuid: text that is not a GUID fails the read", "[SqlDataBinder],[SqlGuid]")
{
    auto stmt = SqlStatement {};
    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.CreateTable("guid_text")
            .PrimaryKey("Id", SqlColumnTypeDefinitions::Integer {})
            .Column("Value", SqlColumnTypeDefinitions::Guid {});
    });
    auto const lower = SqlGuid::TryParse("0f8e7d6c-5b4a-4321-9876-0123456789ab").value_or(SqlGuid {});
    auto const upper = SqlGuid::TryParse("A1B2C3D4-E5F6-4789-ABCD-EF0123456789").value_or(SqlGuid {});
    REQUIRE(lower != SqlGuid {});
    REQUIRE(upper != SqlGuid {});
    auto const sqlStateOf = [](auto&& read) -> std::string {
        try
        {
            read();
            return "no error";
        }
        catch (SqlException const& error)
        {
            return error.info().sqlState;
        }
    };

    if (stmt.Connection().ServerType() != SqlServerType::SQLITE)
    {
        // The column holds a native GUID there, which cannot hold anything else: valid ones round-trip.
        stmt.Prepare(R"(INSERT INTO "guid_text" ("Id", "Value") VALUES (?, ?))");
        (void) stmt.Execute(1, lower);
        (void) stmt.Execute(2, upper);
        (void) stmt.Execute(3, std::optional<SqlGuid> {});
        auto cursor = stmt.ExecuteDirect(R"(SELECT "Value" FROM "guid_text" ORDER BY "Id")"sv);
        auto values = std::vector<std::optional<SqlGuid>> {};
        while (cursor.FetchRow())
            values.push_back(cursor.GetNullableColumn<SqlGuid>(1));
        CHECK(values == std::vector<std::optional<SqlGuid>> { lower, upper, std::nullopt });
        return;
    }

    // SQLite stores a GUID as text, so the column can hold any text. TryParse's form is accepted: the
    // 8-4-4-4-12 hex digits in either case, without braces. NULL stays NULL; anything else fails with 22018
    // rather than reading as the all-zero GUID.
    (void) stmt.ExecuteDirect(R"(INSERT INTO "guid_text" ("Id", "Value") VALUES
        (1, '0f8e7d6c-5b4a-4321-9876-0123456789ab'), (2, 'A1B2C3D4-E5F6-4789-ABCD-EF0123456789'), (3, NULL),
        (4, ''), (5, 'not a guid'), (6, '{0f8e7d6c-5b4a-4321-9876-0123456789ab}'))"sv);
    struct Expected
    {
        int id;
        std::optional<SqlGuid> value;
        std::string_view sqlState;
        std::string_view text; // what the column holds, where it is not a GUID
    };
    auto const expected = std::array {
        Expected { .id = 1, .value = lower, .sqlState = "no error", .text = {} },
        Expected { .id = 2, .value = upper, .sqlState = "no error", .text = {} },
        Expected { .id = 3, .value = std::nullopt, .sqlState = "no error", .text = {} },
        Expected { .id = 4, .value = std::nullopt, .sqlState = "22018", .text = "" },
        Expected { .id = 5, .value = std::nullopt, .sqlState = "22018", .text = "not a guid" },
        Expected { .id = 6, .value = std::nullopt, .sqlState = "22018", .text = "{0f8e7d6c-5b4a-4321-9876-0123456789ab}" },
    };
    for (auto const& row: expected)
    {
        INFO("row " << row.id);
        auto const query = std::format(R"(SELECT "Value" FROM "guid_text" WHERE "Id" = {})", row.id);

        auto read = std::optional<SqlGuid> {};
        CHECK(sqlStateOf([&] {
                  auto cursor = stmt.ExecuteDirect(query);
                  REQUIRE(cursor.FetchRow());
                  read = cursor.GetNullableColumn<SqlGuid>(1);
              })
              == row.sqlState);
        if (row.sqlState == "no error")
            CHECK(read == row.value);

        auto optional = std::optional<SqlGuid> {};
        CHECK(sqlStateOf([&] {
                  auto cursor = stmt.ExecuteDirect(query);
                  REQUIRE(cursor.FetchRow());
                  optional = cursor.GetColumn<std::optional<SqlGuid>>(1);
              })
              == row.sqlState);
        if (row.sqlState == "no error")
            CHECK(optional == row.value);

        auto bound = std::optional<SqlGuid> {};
        CHECK(sqlStateOf([&] {
                  auto cursor = stmt.ExecuteDirect(query);
                  cursor.BindOutputColumns(&bound);
                  // Not REQUIRE: it would catch the failing fetch's exception itself.
                  if (!cursor.FetchRow())
                      throw std::runtime_error("no row");
              })
              == row.sqlState);
        if (row.sqlState == "no error")
            CHECK(bound == row.value);

        // A SqlVariant does not fail: the SQLite driver reports the column as text (SQL_VARCHAR), so a GUID's
        // text reads as a SqlGuid, and any other text as that text.
        auto variant = SqlVariant {};
        CHECK(sqlStateOf([&] {
                  auto cursor = stmt.ExecuteDirect(query);
                  REQUIRE(cursor.FetchRow());
                  variant = cursor.GetColumn<SqlVariant>(1);
              })
              == "no error");
        if (row.sqlState == "no error")
            CHECK(variant.TryGetGuid() == row.value);
        else
        {
            REQUIRE(std::holds_alternative<std::string>(variant.value));
            CHECK(std::get<std::string>(variant.value) == row.text);
        }
    }
}

namespace
{

/// Counts the native batch executes while it is installed.
class NativeBatchCountingLogger: public SqlLogger::Null
{
  public:
    NativeBatchCountingLogger()
    {
        SqlLogger::SetLogger(*this);
    }
    NativeBatchCountingLogger(NativeBatchCountingLogger const&) = delete;
    NativeBatchCountingLogger(NativeBatchCountingLogger&&) = delete;
    NativeBatchCountingLogger& operator=(NativeBatchCountingLogger const&) = delete;
    NativeBatchCountingLogger& operator=(NativeBatchCountingLogger&&) = delete;

    ~NativeBatchCountingLogger() override
    {
        SqlLogger::SetLogger(m_previousLogger);
    }

    void OnExecuteNativeBatch(std::string_view /*query*/) override
    {
        ++m_nativeBatches;
    }

    /// @return The native batch executes counted so far.
    [[nodiscard]] int NativeBatches() const noexcept
    {
        return m_nativeBatches;
    }

  private:
    SqlLogger& m_previousLogger = SqlLogger::GetLogger();
    int m_nativeBatches = 0;
};

/// A row of the row-wise batch of @ref IntegerRangeTest.
template <typename T>
struct IntegerRangeRow
{
    std::int64_t id {};
    T value {};
};

/// Writes @p values of @p T through every write path of its binder into a BIGINT column, and checks what the
/// database stored and what every read path reads back.
template <typename T>
void IntegerRangeTest(std::vector<T> const& values)
{
    INFO("type " << Reflection::TypeNameOf<T>);
    auto stmt = SqlStatement {};
    auto ids = std::vector<std::int64_t> {};
    auto expectedText = std::vector<std::string> {};
    for (auto const index: std::views::iota(std::size_t { 0 }, values.size()))
    {
        ids.push_back(static_cast<std::int64_t>(index) + 1);
        expectedText.push_back(std::to_string(values[index]));
    }

    // What the database holds, as text: a value stored wrongly also misleads every other reader.
    auto const storedText = [&] {
        auto stored = std::vector<std::string> {};
        auto cursor = stmt.ExecuteDirect(R"(SELECT CAST("Value" AS VARCHAR(40)) FROM "IntegerRange" ORDER BY "Id")"sv);
        while (cursor.FetchRow())
            stored.push_back(cursor.GetColumn<std::string>(1));
        return stored;
    };
    auto const reset = [&] {
        (void) stmt.ExecuteDirect(R"(DELETE FROM "IntegerRange")"sv);
        stmt.Prepare(R"(INSERT INTO "IntegerRange" ("Id", "Value") VALUES (?, ?))");
    };

    reset();
    for (auto const index: std::views::iota(std::size_t { 0 }, values.size()))
        (void) stmt.Execute(ids[index], values[index]);
    CHECK(storedText() == expectedText);

    reset();
    (void) stmt.ExecuteBatch(ids, values); // column-major
    CHECK(storedText() == expectedText);

    reset();
    auto rows = std::vector<IntegerRangeRow<T>> {};
    for (auto const index: std::views::iota(std::size_t { 0 }, values.size()))
        rows.push_back(IntegerRangeRow<T> { .id = ids[index], .value = values[index] });
    {
        auto const logger = NativeBatchCountingLogger {};
        (void) stmt.ExecuteBatch(
            std::span { rows },
            [](IntegerRangeRow<T> const& row) -> std::int64_t const& { return row.id; },
            [](IntegerRangeRow<T> const& row) -> T const& { return row.value; });
        // The row-wise batch stays one native execute, except where the binder must stage the value: on SQLite.
        auto const staged = SqlColumnMayStage<T>() && stmt.Connection().ServerType() == SqlServerType::SQLITE;
        CHECK(logger.NativeBatches() == (stmt.Connection().SupportsNativeRowBatch() && !staged ? 1 : 0));
    }
    CHECK(storedText() == expectedText);

    // Read back, from values the database holds correctly whatever the write paths did.
    (void) stmt.ExecuteDirect(R"(DELETE FROM "IntegerRange")"sv);
    for (auto const index: std::views::iota(std::size_t { 0 }, values.size()))
        (void) stmt.ExecuteDirect(
            std::format(R"(INSERT INTO "IntegerRange" ("Id", "Value") VALUES ({}, {}))", ids[index], expectedText[index]));
    for (auto const depth: { PrefetchDepthDefault, std::size_t { 1 } })
    {
        INFO("prefetch depth " << depth);
        stmt.Connection().SetDefaultPrefetchDepth(depth);
        auto readByGetColumn = std::vector<T> {};
        {
            auto cursor = stmt.ExecuteDirect(R"(SELECT "Value" FROM "IntegerRange" ORDER BY "Id")"sv);
            while (cursor.FetchRow())
                readByGetColumn.push_back(cursor.GetColumn<T>(1));
        }
        CHECK(readByGetColumn == values);
        auto readBound = std::vector<T> {};
        {
            auto value = T {};
            auto cursor = stmt.ExecuteDirect(R"(SELECT "Value" FROM "IntegerRange" ORDER BY "Id")"sv);
            cursor.BindOutputColumns(&value);
            while (cursor.FetchRow())
                readBound.push_back(value);
        }
        CHECK(readBound == values);
    }
    stmt.Connection().SetDefaultPrefetchDepth(PrefetchDepthDefault);
    CHECK(stmt.ExecuteDirectScalar<T>(std::format(R"(SELECT "Value" FROM "IntegerRange" WHERE "Id" = {})", ids.back()))
          == values.back());
}

} // namespace

TEST_CASE_METHOD(SqlTestFixture, "Integer binders store and read the full range of their type", "[SqlDataBinder]")
{
    auto stmt = SqlStatement {};
    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.CreateTable("IntegerRange")
            .PrimaryKey("Id", SqlColumnTypeDefinitions::Bigint {})
            .RequiredColumn("Value", SqlColumnTypeDefinitions::Bigint {});
    });

    IntegerRangeTest<std::int8_t>({ -128, -1, 0, 127 });
    IntegerRangeTest<std::uint8_t>({ 0, 127, 128, 255 });
    IntegerRangeTest<std::int16_t>({ -32768, -1, 32767 });
    IntegerRangeTest<std::uint16_t>({ 0, 32767, 32768, 65535 });
    IntegerRangeTest<std::int32_t>(
        { (std::numeric_limits<std::int32_t>::min)(), -1, (std::numeric_limits<std::int32_t>::max)() });
    IntegerRangeTest<std::uint32_t>({ 0, 2147483647U, 2147483648U, 4294967295U });
}

namespace
{
/// The SQLSTATE @p read throws, or "none".
template <typename Read>
std::string IntegerReadState(Read const& read)
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
                 "int8_t and uint32_t reads of a value outside their range fail with 22003",
                 "[SqlDataBinder]")
{
    auto stmt = SqlStatement {};
    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.CreateTable("NarrowOutOfRange")
            .PrimaryKey("Id", SqlColumnTypeDefinitions::Bigint {})
            .RequiredColumn("Small", SqlColumnTypeDefinitions::Bigint {})
            .RequiredColumn("Large", SqlColumnTypeDefinitions::Bigint {});
    });
    // psqlODBC converts a value outside the narrow C type's range without an error, and these types bind in place
    // there; SQL Server raises 22003 itself, and on SQLite the staged copy is checked.
    UNSUPPORTED_DATABASE(stmt, SqlServerType::POSTGRESQL);
    (void) stmt.ExecuteDirect(R"(INSERT INTO "NarrowOutOfRange" ("Id", "Small", "Large") VALUES (1, 300, 4294967296))"sv);
    auto const query = R"(SELECT "Small", "Large" FROM "NarrowOutOfRange")"sv;

    // 300 is no int8_t and 4294967296 no uint32_t, nor a form an older version stored on SQLite: the read fails
    // rather than wrapping the value, through the binders' own reads and under block prefetch alike.
    for (auto const depth: { std::size_t { 1 }, PrefetchDepthDefault })
    {
        INFO("prefetch depth " << depth);
        stmt.Connection().SetDefaultPrefetchDepth(depth);
        CHECK(IntegerReadState([&] {
                  auto cursor = stmt.ExecuteDirect(query);
                  (void) cursor.FetchRow();
                  (void) cursor.GetColumn<std::int8_t>(1);
              })
              == "22003");
        CHECK(IntegerReadState([&] {
                  auto cursor = stmt.ExecuteDirect(query);
                  (void) cursor.FetchRow();
                  (void) cursor.GetColumn<std::uint32_t>(2);
              })
              == "22003");
        CHECK(IntegerReadState([&] {
                  auto small = std::int8_t {};
                  auto large = std::uint32_t {};
                  auto cursor = stmt.ExecuteDirect(query);
                  cursor.BindOutputColumns(&small, &large);
                  (void) cursor.FetchRow();
              })
              == "22003");
    }
}

TEST_CASE_METHOD(SqlTestFixture,
                 "SQLite int8_t and uint32_t rows an older version stored read back unchanged",
                 "[SqlDataBinder]")
{
    auto stmt = SqlStatement {};
    // The legacy forms are those of the SQLite driver, which stored an int8_t -1 as 255 and a uint32_t above
    // INT32_MAX as its negative 32-bit pattern; SQL Server and PostgreSQL stored the values themselves.
    UNSUPPORTED_DATABASE(stmt, SqlServerType::MICROSOFT_SQL);
    UNSUPPORTED_DATABASE(stmt, SqlServerType::POSTGRESQL);
    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.CreateTable("NarrowLegacy")
            .PrimaryKey("Id", SqlColumnTypeDefinitions::Bigint {})
            .RequiredColumn("Small", SqlColumnTypeDefinitions::Bigint {})
            .RequiredColumn("Large", SqlColumnTypeDefinitions::Bigint {});
    });
    // Written with raw SQL, as an older version left them: the stored value, the expected int8_t, the expected
    // uint32_t (an empty expectation: SQLSTATE 22003).
    struct LegacyRow
    {
        std::int64_t small = 0;
        std::optional<std::int8_t> expectedSmall;
        std::int64_t large = 0;
        std::optional<std::uint32_t> expectedLarge;
    };
    auto const rows = std::array {
        LegacyRow { .small = 255, .expectedSmall = -1, .large = -1, .expectedLarge = 4'294'967'295U },
        LegacyRow { .small = 128, .expectedSmall = -128, .large = -2'147'483'648, .expectedLarge = 2'147'483'648U },
        LegacyRow { .small = 127, .expectedSmall = 127, .large = 2'147'483'647, .expectedLarge = 2'147'483'647U },
        LegacyRow { .small = -128, .expectedSmall = -128, .large = 4'294'967'295, .expectedLarge = 4'294'967'295U },
        LegacyRow { .small = 0, .expectedSmall = 0, .large = 0, .expectedLarge = 0U },
        LegacyRow { .small = 256, .expectedSmall = std::nullopt, .large = 4'294'967'296, .expectedLarge = std::nullopt },
        LegacyRow { .small = -129, .expectedSmall = std::nullopt, .large = -2'147'483'649, .expectedLarge = std::nullopt },
        // Outside int16_t: their 16-bit wrap (-1, -128) lies in the legacy range, the numbers do not.
        LegacyRow { .small = 65'535, .expectedSmall = std::nullopt, .large = 0, .expectedLarge = 0U },
        LegacyRow { .small = 65'408, .expectedSmall = std::nullopt, .large = 0, .expectedLarge = 0U },
    };
    for (auto const index: std::views::iota(std::size_t { 0 }, rows.size()))
        (void) stmt.ExecuteDirect(std::format(R"(INSERT INTO "NarrowLegacy" ("Id", "Small", "Large") VALUES ({}, {}, {}))",
                                              index,
                                              rows[index].small,
                                              rows[index].large));

    auto const outcome = [](auto const& read) -> std::string {
        try
        {
            return std::to_string(read());
        }
        catch (SqlException const& error)
        {
            return error.info().sqlState;
        }
    };
    auto const expected = [](auto const& value) -> std::string {
        return value ? std::to_string(*value) : std::string { "22003" };
    };
    for (auto const depth: { std::size_t { 1 }, PrefetchDepthDefault })
    {
        stmt.Connection().SetDefaultPrefetchDepth(depth);
        for (auto const index: std::views::iota(std::size_t { 0 }, rows.size()))
        {
            INFO(std::format("depth {} row {}", depth, index));
            auto const query = std::format(R"(SELECT "Small", "Large" FROM "NarrowLegacy" WHERE "Id" = {})", index);
            CHECK(outcome([&] {
                      auto cursor = stmt.ExecuteDirect(query);
                      (void) cursor.FetchRow();
                      return cursor.GetColumn<std::int8_t>(1);
                  })
                  == expected(rows[index].expectedSmall));
            CHECK(outcome([&] {
                      auto cursor = stmt.ExecuteDirect(query);
                      (void) cursor.FetchRow();
                      return cursor.GetColumn<std::uint32_t>(2);
                  })
                  == expected(rows[index].expectedLarge));
        }
    }
}

TEST_CASE_METHOD(SqlTestFixture,
                 "int8_t and uint32_t boundary values round-trip, with and without prefetch",
                 "[SqlDataBinder]")
{
    auto stmt = SqlStatement {};
    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.CreateTable("NarrowBoundaries")
            .PrimaryKey("Id", SqlColumnTypeDefinitions::Bigint {})
            .RequiredColumn("Small", SqlColumnTypeDefinitions::Bigint {})
            .RequiredColumn("Large", SqlColumnTypeDefinitions::Bigint {});
    });
    auto const smalls = std::array<std::int8_t, 5> { -128, -1, 0, 1, 127 };
    auto const larges = std::array<std::uint32_t, 5> { 0, 1, 2'147'483'647, 2'147'483'648, 4'294'967'295 };
    stmt.Prepare(R"(INSERT INTO "NarrowBoundaries" ("Id", "Small", "Large") VALUES (?, ?, ?))");
    for (auto const index: std::views::iota(std::size_t { 0 }, smalls.size()))
        (void) stmt.Execute(static_cast<std::int64_t>(index), smalls[index], larges[index]);

    for (auto const depth: { std::size_t { 1 }, PrefetchDepthDefault })
    {
        stmt.Connection().SetDefaultPrefetchDepth(depth);
        auto cursor = stmt.ExecuteDirect(R"(SELECT "Small", "Large" FROM "NarrowBoundaries" ORDER BY "Id")");
        auto index = std::size_t { 0 };
        while (cursor.FetchRow())
        {
            INFO(std::format("depth {} row {}", depth, index));
            REQUIRE(index < smalls.size());
            CHECK(cursor.GetColumn<std::int8_t>(1) == smalls[index]);
            CHECK(cursor.GetColumn<std::uint32_t>(2) == larges[index]);
            ++index;
        }
        CHECK(index == smalls.size());
    }
}

TEST_CASE_METHOD(SqlTestFixture,
                 "A NULL read into a bound int8_t or uint32_t resets it, with and without prefetch",
                 "[SqlDataBinder]")
{
    auto stmt = SqlStatement {};
    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.CreateTable("NarrowNull")
            .PrimaryKey("Id", SqlColumnTypeDefinitions::Bigint {})
            .Column("Small", SqlColumnTypeDefinitions::Bigint {})
            .Column("Large", SqlColumnTypeDefinitions::Bigint {});
    });
    // Bound in place, SQL Server's and PostgreSQL's drivers leave a non-optional variable untouched on NULL, as for
    // every other type; only SQLite stages these two types, and its staged read resets the variable as prefetch does.
    UNSUPPORTED_DATABASE(stmt, SqlServerType::MICROSOFT_SQL);
    UNSUPPORTED_DATABASE(stmt, SqlServerType::POSTGRESQL);
    (void) stmt.ExecuteDirect(R"(INSERT INTO "NarrowNull" ("Id", "Small", "Large") VALUES (1, 5, 7), (2, NULL, NULL))"sv);
    for (auto const depth: { std::size_t { 1 }, PrefetchDepthDefault })
    {
        INFO("prefetch depth " << depth);
        stmt.Connection().SetDefaultPrefetchDepth(depth);
        auto small = std::int8_t {};
        auto large = std::uint32_t {};
        auto cursor = stmt.ExecuteDirect(R"(SELECT "Small", "Large" FROM "NarrowNull" ORDER BY "Id")"sv);
        cursor.BindOutputColumns(&small, &large);
        REQUIRE(cursor.FetchRow());
        CHECK(small == 5);
        CHECK(large == 7U);
        REQUIRE(cursor.FetchRow());
        CHECK(small == 0);
        CHECK(large == 0U);
    }
}

TEST_CASE("SqlQueryFormatter: only SQLite stages int8_t and uint32_t", "[SqlDataBinder]")
{
    CHECK_FALSE(SqlQueryFormatter::Sqlite().ConvertsNarrowIntegerCTypes());
    CHECK(SqlQueryFormatter::PostgrSQL().ConvertsNarrowIntegerCTypes());
    CHECK(SqlQueryFormatter::SqlServer().ConvertsNarrowIntegerCTypes());
}

namespace
{

/// A record of the direct row-wise fetch test: the integer widths whose C type SQLite does not convert.
struct NarrowIntegerRow
{
    std::int64_t id {};
    std::uint32_t large {};
    std::int8_t small {};
    std::optional<std::uint32_t> maybeLarge {};
};

} // namespace

TEST_CASE_METHOD(SqlTestFixture, "FetchAllRowWise reads the full range of int8_t and uint32_t", "[SqlDataBinder]")
{
    auto stmt = SqlStatement {};
    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.CreateTable("NarrowRowWise")
            .PrimaryKey("Id", SqlColumnTypeDefinitions::Bigint {})
            .RequiredColumn("Large", SqlColumnTypeDefinitions::Bigint {})
            .RequiredColumn("Small", SqlColumnTypeDefinitions::Bigint {})
            .Column("MaybeLarge", SqlColumnTypeDefinitions::Bigint {});
    });
    (void) stmt.ExecuteDirect(R"(INSERT INTO "NarrowRowWise" ("Id", "Large", "Small", "MaybeLarge") VALUES )"
                              R"((1, 4294967295, -128, 2147483648), (2, 2147483648, -1, NULL), (3, 0, 127, 0))"sv);

    auto rows = std::vector<NarrowIntegerRow> {};
    auto cursor = stmt.ExecuteDirect(R"(SELECT "Id", "Large", "Small", "MaybeLarge" FROM "NarrowRowWise" ORDER BY "Id")"sv);
    cursor.FetchAllRowWise(
        rows,
        64,
        [](NarrowIntegerRow& row) -> std::int64_t& { return row.id; },
        [](NarrowIntegerRow& row) -> std::uint32_t& { return row.large; },
        [](NarrowIntegerRow& row) -> std::int8_t& { return row.small; },
        [](NarrowIntegerRow& row) -> std::optional<std::uint32_t>& { return row.maybeLarge; });

    REQUIRE(rows.size() == 3);
    CHECK(rows[0].large == 4294967295U);
    CHECK(rows[0].small == -128);
    CHECK(rows[0].maybeLarge == std::optional<std::uint32_t> { 2147483648U });
    CHECK(rows[1].large == 2147483648U);
    CHECK(rows[1].small == -1);
    CHECK_FALSE(rows[1].maybeLarge.has_value());
    CHECK(rows[2].large == 0U);
    CHECK(rows[2].small == 127);
}

/// A record that has no default constructor: its @c int8_t member is a required field. At namespace scope rather
/// than in an anonymous namespace, like the other records reflection may see (MSVC C7631).
struct RequiredNarrowRecord
{
    std::int64_t id {};
    Field<std::int8_t> small;
};

TEST_CASE_METHOD(SqlTestFixture,
                 "FetchAllRowWise reads a staged column per row into records without a default constructor",
                 "[SqlDataBinder]")
{
    static_assert(!std::default_initializable<RequiredNarrowRecord>);
    auto stmt = SqlStatement {};
    if (stmt.Connection().ServerType() != SqlServerType::SQLITE)
        SKIP("Only SQLite stages int8_t, which makes FetchAllRowWise read per row");
    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.CreateTable("RequiredNarrow")
            .PrimaryKey("Id", SqlColumnTypeDefinitions::Bigint {})
            .RequiredColumn("Small", SqlColumnTypeDefinitions::Bigint {});
    });
    // 255 is how an older version stored -1; 300 fits no int8_t.
    (void) stmt.ExecuteDirect(R"(INSERT INTO "RequiredNarrow" ("Id", "Small") VALUES (1, 255), (2, 127), (3, 300))"sv);

    auto rows = std::vector<RequiredNarrowRecord> {};
    auto cursor = stmt.ExecuteDirect(R"(SELECT "Id", "Small" FROM "RequiredNarrow" ORDER BY "Id")"sv);
    auto const sqlState = [&]() -> std::string {
        try
        {
            cursor.FetchAllRowWise(
                rows,
                64,
                [](RequiredNarrowRecord& row) -> std::int64_t& { return row.id; },
                [](RequiredNarrowRecord& row) -> std::int8_t& { return row.small.MutableValue(); });
            return "none";
        }
        catch (SqlException const& error)
        {
            return error.info().sqlState;
        }
    }();

    CHECK(sqlState == "22003");
    REQUIRE(rows.size() == 2); // the rows read before the failing one, and only those
    CHECK(rows[0].small.Value() == -1);
    CHECK(rows[1].small.Value() == 127);
}

namespace
{

/// A fixed-capacity string followed by memory a write past its end would change.
template <typename StringType>
struct GuardedFixedString
{
    StringType value {};
    std::array<typename StringType::value_type, 600> canary {};
};

constexpr auto FixedStringCanaryMarker = 0x5A;
constexpr auto LongFixedStringSourceLength = std::size_t { 500 };

/// Reads a 500-character value into a capacity-8 string through GetColumn and a bound output column, and
/// checks that the string holds the value's first 8 characters and nothing after it was written.
template <typename StringType>
void ReadLongValueIntoFixedString(SqlStatement& stmt, std::string const& source)
{
    using CharType = StringType::value_type;
    INFO("type " << Reflection::TypeNameOf<StringType>);
    auto const expected = std::basic_string<CharType>(source.begin(), source.begin() + StringType {}.capacity());
    auto const canaryIntact = [](GuardedFixedString<StringType> const& guarded) {
        return std::ranges::all_of(guarded.canary,
                                   [](CharType c) { return c == static_cast<CharType>(FixedStringCanaryMarker); });
    };
    auto const fresh = [] {
        auto guarded = GuardedFixedString<StringType> {};
        guarded.canary.fill(static_cast<CharType>(FixedStringCanaryMarker));
        return guarded;
    };

    {
        auto guarded = fresh();
        auto cursor = stmt.ExecuteDirect(R"(SELECT "Value" FROM "FixedOverrun")"sv);
        REQUIRE(cursor.FetchRow());
        (void) cursor.GetColumn(1, &guarded.value);
        CHECK(canaryIntact(guarded));
        CHECK((std::basic_string_view<CharType> { guarded.value.data(), guarded.value.size() } == expected));
    }
    {
        auto guarded = fresh();
        auto cursor = stmt.ExecuteDirect(R"(SELECT "Value" FROM "FixedOverrun")"sv);
        cursor.BindOutputColumns(&guarded.value);
        REQUIRE(cursor.FetchRow());
        CHECK(canaryIntact(guarded));
        CHECK((std::basic_string_view<CharType> { guarded.value.data(), guarded.value.size() } == expected));
    }
}

} // namespace

TEST_CASE_METHOD(SqlTestFixture, "A value longer than a fixed-capacity string is cut to its capacity", "[SqlDataBinder]")
{
    auto stmt = SqlStatement {};
    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.CreateTable("FixedOverrun").RequiredColumn("Value", SqlColumnTypeDefinitions::Varchar { .size = 600 });
    });
    auto source = std::string(LongFixedStringSourceLength, ' ');
    for (auto const index: std::views::iota(std::size_t { 0 }, source.size()))
        source[index] = static_cast<char>('a' + (index % 26));
    stmt.Prepare(R"(INSERT INTO "FixedOverrun" ("Value") VALUES (?))");
    (void) stmt.Execute(source);

    ReadLongValueIntoFixedString<SqlAnsiString<8>>(stmt, source);
    ReadLongValueIntoFixedString<SqlUtf16String<8>>(stmt, source);
    ReadLongValueIntoFixedString<SqlUtf32String<8>>(stmt, source);
    ReadLongValueIntoFixedString<SqlWideString<8>>(stmt, source);
}

TEST_CASE("ReadRawColumnArrayData offers a fixed-capacity array no more than its room", "[SqlDataBinder]")
{
    // A driver that fills every buffer it is given, reporting a 500-unit value with or without its length.
    struct Answer
    {
        SQLRETURN returnCode;
        SQLLEN indicator;
    };
    auto const read = [](std::vector<Answer> const& answers) {
        auto guarded = GuardedFixedString<SqlUtf16String<8>> {};
        guarded.canary.fill(static_cast<char16_t>(FixedStringCanaryMarker));
        guarded.value.resize(guarded.value.capacity()); // as GetColumn sizes it before reading
        auto calls = std::size_t { 0 };
        auto indicator = SQLLEN { 0 };
        auto const rv = detail::ReadRawColumnArrayData<SQL_C_WCHAR>(
            [&](SQLPOINTER target, SQLLEN bytes, SQLLEN* reported) noexcept {
                auto const& answer = answers.at((std::min) (calls++, answers.size() - 1));
                std::fill_n(static_cast<char16_t*>(target), static_cast<std::size_t>(bytes) / sizeof(char16_t), u'x');
                *reported = answer.indicator;
                return answer.returnCode;
            },
            &guarded.value,
            &indicator);
        CHECK(SQL_SUCCEEDED(rv));
        CHECK(std::ranges::all_of(guarded.canary,
                                  [](char16_t c) { return c == static_cast<char16_t>(FixedStringCanaryMarker); }));
        CHECK(guarded.value.size() <= guarded.value.capacity());
    };
    constexpr auto Total = SQLLEN { 500 * sizeof(char16_t) };
    read({ { .returnCode = SQL_SUCCESS_WITH_INFO, .indicator = Total }, { .returnCode = SQL_SUCCESS, .indicator = Total } });
    read({ { .returnCode = SQL_SUCCESS_WITH_INFO, .indicator = SQL_NO_TOTAL },
           { .returnCode = SQL_SUCCESS, .indicator = 4 } });
}

TEST_CASE("A right-trimmed UTF-16 fixed string read shorter than before keeps no stale characters", "[SqlDataBinder]")
{
    using String = SqlFixedString<8, char16_t, SqlFixedStringMode::FIXED_SIZE_RIGHT_TRIMMED>;
    // The variable held a longer value; the driver wrote the next one, "ab", over its start and reported
    // 2 characters in bytes.
    auto value = String { u"abcd" };
    SqlBasicStringOperations<String>::PostProcessOutputColumn(&value, static_cast<SQLLEN>(2 * sizeof(char16_t)));
    CHECK((std::u16string_view { value.data(), value.size() } == u"ab"));
}

TEST_CASE("SqlDynamicBinary: constructed from a list of bytes", "[SqlDataBinder]")
{
    auto const binary = SqlDynamicBinary<16> { 0xDE, 0xAD };
    // Compared as a whole, so no element is read past what the binary holds.
    CHECK(std::ranges::equal(std::span { binary.data(), binary.size() }, std::array<std::uint8_t, 2> { 0xDE, 0xAD }));
}

namespace
{

/// A SqlDateTime on @p year at 23:59:59.9999999 of December 31 (the latest moment of the year).
constexpr SqlDateTime EndOfYear(int year)
{
    using namespace std::chrono;
    return SqlDateTime { std::chrono::year { year }, December, day { 31 }, hours { 23 }, minutes { 59 }, seconds { 59 },
                         nanoseconds { 999'999'900 } };
}

/// A SqlDateTime at midnight of January 1 of @p year.
constexpr SqlDateTime StartOfYear(int year)
{
    using namespace std::chrono;
    return SqlDateTime { std::chrono::year { year }, January, day { 1 }, hours { 0 }, minutes { 0 }, seconds { 0 } };
}

// Compared field by field, in constant expressions, beyond what a nanosecond system_clock can count.
static_assert(StartOfYear(1) < StartOfYear(1600));
static_assert(StartOfYear(1600) < StartOfYear(2262));
static_assert(EndOfYear(2262) < StartOfYear(2263));
static_assert(StartOfYear(2263) < EndOfYear(9999));
static_assert(EndOfYear(9999) == EndOfYear(9999));
static_assert(StartOfYear(9999) != EndOfYear(9999));
static_assert((EndOfYear(9999) <=> StartOfYear(1)) == std::weak_ordering::greater);
// Beyond even a 100 ns clock (MSVC): SQL_TIMESTAMP_STRUCT holds years up to 32767.
static_assert(EndOfYear(9999) < StartOfYear(32000));

} // namespace

TEST_CASE("SqlDateTime: values of every year compare and sort in time order", "[SqlDataBinder],[SqlDateTime]")
{
    auto values = std::vector { EndOfYear(9999),   StartOfYear(2263), StartOfYear(1), EndOfYear(2262),
                                StartOfYear(1600), StartOfYear(9999), EndOfYear(1) };
    std::ranges::sort(values);
    auto const expected = std::vector { StartOfYear(1),    EndOfYear(1),      StartOfYear(1600), EndOfYear(2262),
                                        StartOfYear(2263), StartOfYear(9999), EndOfYear(9999) };
    CHECK(values == expected);
    // The fields order values 100 ns apart, the fraction's unit.
    using namespace std::chrono;
    auto const early = SqlDateTime { year { 2000 }, March, day { 1 }, hours { 12 }, minutes { 0 }, seconds { 0 } };
    auto const later =
        SqlDateTime { year { 2000 }, March, day { 1 }, hours { 12 }, minutes { 0 }, seconds { 0 }, nanoseconds { 100 } };
    CHECK(early < later);
    // Within the clock's range the order is the time points' order, down to what the clock counts: value() has
    // only whole microseconds on libc++, so this compares values 1 us apart.
    auto const late =
        SqlDateTime { year { 2000 }, March, day { 1 }, hours { 12 }, minutes { 0 }, seconds { 0 }, microseconds { 1 } };
    CHECK(early < late);
    CHECK((early.value() < late.value()));
}

TEST_CASE_METHOD(SqlTestFixture,
                 "SqlVariant reads binary cells as bytes and integers in 64 bits",
                 "[SqlDataBinder],[SqlVariant]")
{
    auto stmt = SqlStatement {};
    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.CreateTable("VariantCells")
            .PrimaryKey("Id", SqlColumnTypeDefinitions::Integer {})
            .Column("Bytes", SqlColumnTypeDefinitions::VarBinary { .size = 16 })
            .Column("Wide", SqlColumnTypeDefinitions::Bigint {});
    });
    auto const bytes = SqlBinary { 0xDE, 0xAD, 0xBE, 0xEF };
    stmt.Prepare(R"(INSERT INTO "VariantCells" ("Id", "Bytes", "Wide") VALUES (?, ?, ?))");
    (void) stmt.Execute(1, bytes, std::int64_t { 5'000'000'000 });

    for (auto const depth: { std::size_t { 1 }, PrefetchDepthDefault })
    {
        INFO("prefetch depth " << depth);
        stmt.Connection().SetDefaultPrefetchDepth(depth);
        auto cursor = stmt.ExecuteDirect(R"(SELECT "Bytes", "Wide" FROM "VariantCells")"sv);
        REQUIRE(cursor.FetchRow());
        auto const binary = cursor.GetColumn<SqlVariant>(1);
        auto const wide = cursor.GetColumn<SqlVariant>(2);
        REQUIRE(std::holds_alternative<SqlBinary>(binary.value));
        CHECK((std::get<SqlBinary>(binary.value) == bytes)); // no printer for SqlBinary
        CHECK(wide.TryGetLongLong() == std::optional<long long> { 5'000'000'000 });
    }
}

TEST_CASE_METHOD(SqlTestFixture,
                 "SqlVariant reads a SQLite cell whose value is not of its column's type as stored",
                 "[SqlDataBinder],[SqlVariant]")
{
    auto stmt = SqlStatement {};
    UNSUPPORTED_DATABASE(stmt, SqlServerType::MICROSOFT_SQL); // enforces column types: such a cell cannot exist
    UNSUPPORTED_DATABASE(stmt, SqlServerType::POSTGRESQL);    // the same

    (void) stmt.ExecuteDirect("DROP TABLE IF EXISTS variant_affinity"sv);
    (void) stmt.ExecuteDirect(
        "CREATE TABLE variant_affinity (k INTEGER, i INTEGER, d DATE, ts TIMESTAMP, tm TIME, b BLOB)"sv);
    (void) stmt.ExecuteDirect("INSERT INTO variant_affinity VALUES "
                              "(1, 'hello', 'notadate', 'nottimestamp', 'nottime', x'DEADBEEF'), "
                              "(2, 5000000000, '2024-01-02', '2024-01-02 03:04:05', '03:04:05', NULL), "
                              "(3, NULL, NULL, NULL, NULL, x'00')"sv);

    for (auto const depth: { std::size_t { 1 }, PrefetchDepthDefault })
    {
        INFO("prefetch depth " << depth);
        stmt.Connection().SetDefaultPrefetchDepth(depth);
        auto rows = std::vector<std::vector<SqlVariant>> {};
        auto cursor = stmt.ExecuteDirect("SELECT i, d, ts, tm, b FROM variant_affinity ORDER BY k"sv);
        while (cursor.FetchRow())
        {
            auto& row = rows.emplace_back();
            for (auto const column: std::views::iota(SQLUSMALLINT { 1 }, SQLUSMALLINT { 6 }))
                row.push_back(cursor.GetColumn<SqlVariant>(column));
        }
        REQUIRE(rows.size() == 3);

        // Text in typed columns comes back as the text, not as NULL.
        CHECK(rows[0][0].TryGetStringView() == std::optional<std::string_view> { "hello" });
        CHECK(rows[0][1].TryGetStringView() == std::optional<std::string_view> { "notadate" });
        CHECK(rows[0][2].TryGetStringView() == std::optional<std::string_view> { "nottimestamp" });
        CHECK(rows[0][3].TryGetStringView() == std::optional<std::string_view> { "nottime" });
        REQUIRE(std::holds_alternative<SqlBinary>(rows[0][4].value));
        CHECK((std::get<SqlBinary>(rows[0][4].value) == SqlBinary { 0xDE, 0xAD, 0xBE, 0xEF }));

        // A 64-bit integer in an INTEGER column, and typed values, read as such.
        CHECK(rows[1][0].TryGetLongLong() == std::optional<long long> { 5'000'000'000 });
        CHECK(std::holds_alternative<SqlDate>(rows[1][1].value));
        CHECK(std::holds_alternative<SqlDateTime>(rows[1][2].value));
        CHECK(std::holds_alternative<SqlTime>(rows[1][3].value));

        // Real NULLs stay NULL.
        for (auto const column: std::views::iota(std::size_t { 0 }, std::size_t { 4 }))
            CHECK(rows[2][column].IsNull());

        // An INTEGER-only result set, which block prefetch would otherwise read: the variant row cursor and the
        // variant scalar read it per row, so the text and the wide integer survive.
        auto values = std::vector<SqlVariant> {};
        for (auto& row: SqlVariantRowCursor(stmt.ExecuteDirect("SELECT i FROM variant_affinity ORDER BY k"sv)))
            values.push_back(row.at(0));
        REQUIRE(values.size() == 3);
        CHECK(values[0].TryGetStringView() == std::optional<std::string_view> { "hello" });
        CHECK(values[1].TryGetLongLong() == std::optional<long long> { 5'000'000'000 });
        CHECK(values[2].IsNull());
        auto const scalar = stmt.ExecuteDirectScalar<SqlVariant>("SELECT i FROM variant_affinity WHERE k = 1"sv);
        CHECK(scalar.TryGetStringView() == std::optional<std::string_view> { "hello" });
    }
}

TEST_CASE_METHOD(SqlTestFixture,
                 "Known limit: a raw SqlVariant read of a prefetched SQLite block reads a mistyped cell as NULL",
                 "[SqlDataBinder],[SqlVariant]")
{
    auto stmt = SqlStatement {};
    UNSUPPORTED_DATABASE(stmt, SqlServerType::MICROSOFT_SQL); // enforces column types: such a cell cannot exist
    UNSUPPORTED_DATABASE(stmt, SqlServerType::POSTGRESQL);    // the same

    (void) stmt.ExecuteDirect("DROP TABLE IF EXISTS variant_limit"sv);
    (void) stmt.ExecuteDirect("CREATE TABLE variant_limit (k INTEGER, i INTEGER)"sv);
    (void) stmt.ExecuteDirect("INSERT INTO variant_limit VALUES (1, 'hello'), (2, 7)"sv);
    auto const query = "SELECT i FROM variant_limit ORDER BY k"sv;

    auto const readRaw = [&] {
        auto values = std::vector<SqlVariant> {};
        auto cursor = stmt.ExecuteDirect(query);
        while (cursor.FetchRow())
            values.push_back(cursor.GetColumn<SqlVariant>(1));
        return values;
    };

    // The documented limit: the block was fetched before the variant read, and the driver reported the
    // text in the INTEGER column as NULL; a prefetched block cannot read the cell again as text.
    stmt.Connection().SetDefaultPrefetchDepth(PrefetchDepthDefault);
    auto const prefetched = readRaw();
    REQUIRE(prefetched.size() == 2);
    CHECK(prefetched[0].IsNull());
    CHECK(prefetched[1].TryGetLongLong() == std::optional<long long> { 7 });

    // The ways out the documentation names read it as the text.
    auto viaRowCursor = std::vector<SqlVariant> {};
    for (auto& row: SqlVariantRowCursor(stmt.ExecuteDirect(query)))
        viaRowCursor.push_back(row.at(0));
    REQUIRE(viaRowCursor.size() == 2);
    CHECK(viaRowCursor[0].TryGetStringView() == std::optional<std::string_view> { "hello" });
    CHECK(stmt.ExecuteDirectScalar<SqlVariant>("SELECT i FROM variant_limit WHERE k = 1"sv).TryGetStringView()
          == std::optional<std::string_view> { "hello" });

    stmt.Connection().SetDefaultPrefetchDepth(1);
    auto const perRow = readRaw();
    REQUIRE(perRow.size() == 2);
    CHECK(perRow[0].TryGetStringView() == std::optional<std::string_view> { "hello" });
}

TEST_CASE_METHOD(SqlTestFixture,
                 "SqlVariant reads of a statement describe each result set's columns anew",
                 "[SqlDataBinder],[SqlVariant]")
{
    auto stmt = SqlStatement {};
    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.CreateTable("VariantDescribe")
            .PrimaryKey("Id", SqlColumnTypeDefinitions::Integer {})
            .RequiredColumn("Name", SqlColumnTypeDefinitions::Varchar { .size = 20 })
            .RequiredColumn("Number", SqlColumnTypeDefinitions::Integer {});
    });
    std::ignore = stmt.ExecuteDirect(R"(INSERT INTO "VariantDescribe" ("Id", "Name", "Number") VALUES (1, 'first', 42))");
    std::ignore = stmt.ExecuteDirect(R"(INSERT INTO "VariantDescribe" ("Id", "Name", "Number") VALUES (2, 'second', 43))");

    // The statement keeps each column's type for the result set it described, so the rows after the first ask the
    // driver nothing; the next query's column 1 has another type and must be described again.
    auto const read = [&](std::string_view column) {
        auto cells = std::vector<SqlVariant> {};
        for (auto& row: SqlVariantRowCursor {
                 stmt.ExecuteDirect(std::format(R"(SELECT "{}" FROM "VariantDescribe" ORDER BY "Id")", column)) })
            cells.push_back(std::move(row.at(0)));
        return cells;
    };
    auto const names = read("Name");
    REQUIRE(names.size() == 2);
    CHECK(std::holds_alternative<std::string>(names[0].value));
    CHECK(names[1].ToString() == "second");
    auto const numbers = read("Number");
    REQUIRE(numbers.size() == 2);
    CHECK_FALSE(std::holds_alternative<std::string>(numbers[0].value));
    CHECK(numbers[0].ToString() == "42");
    CHECK(numbers[1].ToString() == "43");
    auto const namesAgain = read("Name");
    REQUIRE(namesAgain.size() == 2);
    CHECK(std::holds_alternative<std::string>(namesAgain[0].value));
}

TEST_CASE_METHOD(SqlTestFixture,
                 "SqlVariant reads of a prepared SELECT ? describe each execute's column anew",
                 "[SqlDataBinder],[SqlVariant]")
{
    // Executed again with another parameter type, the driver may give the column that type (SQL Server prepares
    // the statement again as nvarchar): a type kept from the previous execute would read the text as an integer.
    // Each read must equal the read of a statement that executes the query once.
    auto stmt = SqlStatement {};
    stmt.Connection().SetDefaultPrefetchDepth(1);
    stmt.Prepare("SELECT ?");
    auto const readThrough = [](SqlStatement& statement, auto const& value) {
        auto cells = std::vector<std::string> {};
        for (auto& row: SqlVariantRowCursor { statement.Execute(value) })
            cells.push_back(std::format(
                "{}:{}", std::holds_alternative<std::string>(row.at(0).value) ? "text" : "other", row.at(0).ToString()));
        return cells;
    };
    auto const once = [&](auto const& value) {
        auto fresh = SqlStatement {};
        fresh.Prepare("SELECT ?");
        return readThrough(fresh, value);
    };
    CHECK(readThrough(stmt, 42) == once(42));
    CHECK(readThrough(stmt, std::string { "abc" }) == once(std::string { "abc" }));
    CHECK(readThrough(stmt, 43) == once(43));
    CHECK(once(std::string { "abc" }) == std::vector<std::string> { "text:abc" });
}

TEST_CASE_METHOD(SqlTestFixture,
                 "DataMapper::Query<SqlVariantRow> reads a SQLite cell not of its column's type as stored",
                 "[SqlDataBinder],[SqlVariant]")
{
    auto dm = DataMapper {};
    auto stmt = SqlStatement { dm.Connection() };
    UNSUPPORTED_DATABASE(stmt, SqlServerType::MICROSOFT_SQL); // enforces column types: such a cell cannot exist
    UNSUPPORTED_DATABASE(stmt, SqlServerType::POSTGRESQL);    // the same

    (void) stmt.ExecuteDirect("DROP TABLE IF EXISTS variant_query"sv);
    (void) stmt.ExecuteDirect("CREATE TABLE variant_query (k INTEGER, i INTEGER, d DATE)"sv);
    (void) stmt.ExecuteDirect("INSERT INTO variant_query VALUES (1, 'hello', 'notadate'), (2, '', '2024-01-02')"sv);

    // At the default depth, which block-prefetches an INTEGER and DATE result set: a copy tool built on this
    // must get the text, not NULL.
    dm.Connection().SetDefaultPrefetchDepth(PrefetchDepthDefault);
    auto const rows = dm.Query<SqlVariantRow>("SELECT i, d FROM variant_query ORDER BY k"sv);
    REQUIRE(rows.size() == 2);
    REQUIRE(rows[0].size() == 2);
    CHECK(rows[0][0].TryGetStringView() == std::optional<std::string_view> { "hello" });
    CHECK(rows[0][1].TryGetStringView() == std::optional<std::string_view> { "notadate" });
    REQUIRE(rows[1].size() == 2);
    CHECK(rows[1][0].TryGetStringView() == std::optional<std::string_view> { "" });
    CHECK(std::holds_alternative<SqlDate>(rows[1][1].value));
}

namespace
{

/// A row of the batches of the negative-year test.
struct DatedRow
{
    std::int64_t id {};
    SqlDate date {};
    SqlDateTime moment {};
};

/// A row of the negative-year test whose dates are bound through the wrappers of a date.
struct WrappedDatedRow
{
    std::int64_t id {};
    std::optional<SqlDate> date;
    Field<SqlDateTime> moment;
};

} // namespace

TEST_CASE_METHOD(SqlTestFixture,
                 "A date with a negative year fails to be written instead of storing unreadable text",
                 "[SqlDataBinder]")
{
    using namespace std::chrono;
    auto stmt = SqlStatement {};
    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.CreateTable("NegativeYear")
            .PrimaryKey("Id", SqlColumnTypeDefinitions::Bigint {})
            .Column("D", SqlColumnTypeDefinitions::Date {})
            .Column("T", SqlColumnTypeDefinitions::DateTime {});
    });
    auto const negativeDate = SqlDate { year { -5 }, January, day { 2 } };
    auto const negativeMoment = SqlDateTime { year { -5 }, January, day { 2 }, hours { 3 }, minutes { 4 }, seconds { 5 } };
    auto const goodDate = SqlDate { year { 2024 }, January, day { 2 } };
    auto const goodMoment = SqlDateTime { year { 2024 }, January, day { 2 }, hours { 3 }, minutes { 4 }, seconds { 5 } };
    auto const sqlStateOf = [](auto const& write) -> std::string {
        try
        {
            write();
            return "none";
        }
        catch (SqlException const& error)
        {
            return error.info().sqlState;
        }
    };
    stmt.Prepare(R"(INSERT INTO "NegativeYear" ("Id", "D", "T") VALUES (?, ?, ?))");

    // A single row, and each column of a column-wise and a row-wise batch, a bad value in its second row.
    CHECK(sqlStateOf([&] { (void) stmt.Execute(1, negativeDate, goodMoment); }) == "22008");
    CHECK(sqlStateOf([&] { (void) stmt.Execute(2, goodDate, negativeMoment); }) == "22008");
    CHECK(sqlStateOf([&] { (void) stmt.Execute(3, std::optional { negativeDate }, goodMoment); }) == "22008");
    CHECK(sqlStateOf([&] {
              (void) stmt.ExecuteBatch(std::vector<std::int64_t> { 4, 5 },
                                       std::vector { goodDate, negativeDate },
                                       std::vector { goodMoment, goodMoment });
          })
          == "22008");
    auto const rows = std::vector { DatedRow { .id = 6, .date = goodDate, .moment = goodMoment },
                                    DatedRow { .id = 7, .date = goodDate, .moment = negativeMoment } };
    CHECK(sqlStateOf([&] {
              (void) stmt.ExecuteBatch(
                  std::span { rows },
                  [](DatedRow const& row) -> std::int64_t const& { return row.id; },
                  [](DatedRow const& row) -> SqlDate const& { return row.date; },
                  [](DatedRow const& row) -> SqlDateTime const& { return row.moment; });
          })
          == "22008");

    // The batches that execute row by row refuse the bad second row before the first one is stored: a column
    // batch that is not contiguous, and rows with a column that is not bound in place (a Field) or, on a
    // driver without row-wise parameter arrays, any rows.
    CHECK(sqlStateOf([&] {
              (void) stmt.ExecuteBatch(std::deque<std::int64_t> { 20, 21 },
                                       std::deque { goodDate, negativeDate },
                                       std::deque { goodMoment, goodMoment });
          })
          == "22008");
    CHECK(sqlStateOf([&] {
              (void) stmt.ExecuteBatch(std::deque<std::int64_t> { 22, 23 },
                                       std::deque { SqlVariant { goodDate }, SqlVariant { negativeDate } },
                                       std::deque { goodMoment, goodMoment });
          })
          == "22008");
    auto const badOptionalRows = std::vector {
        WrappedDatedRow { .id = 24, .date = std::nullopt, .moment = goodMoment },
        WrappedDatedRow { .id = 25, .date = negativeDate, .moment = goodMoment },
    };
    auto const badFieldRows = std::vector {
        WrappedDatedRow { .id = 26, .date = goodDate, .moment = goodMoment },
        WrappedDatedRow { .id = 27, .date = goodDate, .moment = negativeMoment },
    };
    CHECK(sqlStateOf([&] {
              (void) stmt.ExecuteBatch(
                  std::span { badOptionalRows },
                  [](WrappedDatedRow const& row) -> std::int64_t const& { return row.id; },
                  [](WrappedDatedRow const& row) -> std::optional<SqlDate> const& { return row.date; },
                  [](WrappedDatedRow const& row) -> Field<SqlDateTime> const& { return row.moment; });
          })
          == "22008");
    CHECK(sqlStateOf([&] {
              (void) stmt.ExecuteBatch(
                  std::span { badFieldRows },
                  [](WrappedDatedRow const& row) -> std::int64_t const& { return row.id; },
                  [](WrappedDatedRow const& row) -> std::optional<SqlDate> const& { return row.date; },
                  [](WrappedDatedRow const& row) -> Field<SqlDateTime> const& { return row.moment; });
          })
          == "22008");
    CHECK(stmt.ExecuteDirectScalar<int>(R"(SELECT COUNT(*) FROM "NegativeYear")"sv) == 0);

    // Years from 0 on are written as before (PostgreSQL has no year 0 and refuses it itself).
    stmt.Prepare(R"(INSERT INTO "NegativeYear" ("Id", "D", "T") VALUES (?, ?, ?))");
    (void) stmt.Execute(8, goodDate, goodMoment);
    CHECK((stmt.ExecuteDirectScalar<SqlDate>(R"(SELECT "D" FROM "NegativeYear" WHERE "Id" = 8)"sv) == goodDate));
}

/// A record with a @c bool member over a BIGINT column, read by SqlRowIterator. At namespace scope, not in an
/// anonymous namespace: MSVC cannot reflect a type with internal linkage.
struct BoolFromIntegerRecord
{
    static constexpr std::string_view TableName = "BoolFromInteger";

    Field<std::int64_t> Id;
    Field<bool> V;
};

namespace
{

/// A number read as @c bool: "true", "false", or the SQLSTATE of the failure.
std::string BoolOutcome(std::function<bool()> const& read)
{
    try
    {
        return read() ? "true" : "false";
    }
    catch (SqlException const& error)
    {
        return error.info().sqlState;
    }
}

/// Rows of a bool read through a row-wise fetch or a record iterator: the values, or the SQLSTATE of the failure.
std::string BoolRowsOutcome(std::function<std::vector<bool>()> const& read)
{
    try
    {
        auto text = std::string {};
        for (auto const value: read())
            text += value ? "1" : "0";
        return text;
    }
    catch (SqlException const& error)
    {
        return error.info().sqlState;
    }
}

} // namespace

TEST_CASE_METHOD(SqlTestFixture, "A bool reads 0 and 1 and fails with 22003 for any other number", "[SqlDataBinder]")
{
    auto stmt = SqlStatement {};
    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.CreateTable("BoolFromInteger")
            .PrimaryKey("Id", SqlColumnTypeDefinitions::Bigint {})
            .RequiredColumn("V", SqlColumnTypeDefinitions::Bigint {});
    });
    auto const values = std::array<std::int64_t, 6> { 0, 1, 2, -1, 255, 256 };
    for (auto const index: std::views::iota(std::size_t { 0 }, values.size()))
        (void) stmt.ExecuteDirect(
            std::format(R"(INSERT INTO "BoolFromInteger" ("Id", "V") VALUES ({}, {}))", index, values[index]));

    // Every read but the row-wise fetch reads the number itself, per row and from the block alike: 0 and 1, and -1
    // where the driver may report a boolean true as -1 (PostgreSQL), else 22003.
    auto const minusOneIsTrue = stmt.Connection().BooleanTrueMayBeMinusOne();
    auto const expected = [&](std::int64_t value) -> std::string {
        if (value == 1 || (value == -1 && minusOneIsTrue))
            return "true";
        return value == 0 ? "false" : "22003";
    };

    for (auto const depth: { std::size_t { 1 }, PrefetchDepthDefault })
    {
        stmt.Connection().SetDefaultPrefetchDepth(depth);
        for (auto const index: std::views::iota(std::size_t { 0 }, values.size()))
        {
            INFO(std::format("depth {} value {}", depth, values[index]));
            auto const query = std::format(R"(SELECT "V" FROM "BoolFromInteger" WHERE "Id" = {})", index);
            CHECK(BoolOutcome([&] {
                      auto cursor = stmt.ExecuteDirect(query);
                      if (!cursor.FetchRow())
                          throw std::runtime_error("no row");
                      return cursor.GetColumn<bool>(1);
                  })
                  == expected(values[index]));
            CHECK(BoolOutcome([&] {
                      auto cursor = stmt.ExecuteDirect(query);
                      if (!cursor.FetchRow())
                          throw std::runtime_error("no row");
                      return cursor.GetNullableColumn<bool>(1).value_or(false);
                  })
                  == expected(values[index]));
            CHECK(BoolOutcome([&] {
                      auto value = false;
                      auto cursor = stmt.ExecuteDirect(query);
                      cursor.BindOutputColumns(&value);
                      if (!cursor.FetchRow())
                          throw std::runtime_error("no row");
                      return value;
                  })
                  == expected(values[index]));
            CHECK(BoolOutcome([&] {
                      auto value = std::optional<bool> {};
                      auto cursor = stmt.ExecuteDirect(query);
                      cursor.BindOutputColumns(&value);
                      if (!cursor.FetchRow())
                          throw std::runtime_error("no row");
                      return value.value_or(false);
                  })
                  == expected(values[index]));
            CHECK(BoolOutcome([&] { return stmt.ExecuteDirectScalar<bool>(query).value_or(false); })
                  == expected(values[index]));
        }

        // A record member read per row: the first row that is neither 0 nor 1 (2) fails the read.
        CHECK(BoolRowsOutcome([&] {
                  auto rows = std::vector<bool> {};
                  for (auto const& record: SqlRowIterator<BoolFromIntegerRecord> { stmt.Connection() })
                      rows.push_back(record.V.Value());
                  return rows;
              })
              == "22003");
    }

    // The row-wise fetch (FetchAllRowWise, and DataMapper All() through it) binds the record's bool in place as
    // SQL_C_BIT and so takes what the driver writes there: SQL Server fails 2 with 22003, PostgreSQL writes the low
    // byte (2 fails with 22003, 256 reads false), SQLite writes 1 for any positive number and 0 otherwise.
    auto const isSqlite = stmt.Connection().ServerType() == SqlServerType::SQLITE;
    auto const isPostgres = stmt.Connection().ServerType() == SqlServerType::POSTGRESQL;
    struct BoolRow
    {
        std::int64_t id = 0;
        bool value = false;
    };
    auto const rowWise = [&](std::string_view where) {
        return BoolRowsOutcome([&] {
            auto rows = std::vector<BoolRow> {};
            auto cursor =
                stmt.ExecuteDirect(std::format(R"(SELECT "Id", "V" FROM "BoolFromInteger" WHERE {} ORDER BY "Id")", where));
            cursor.FetchAllRowWise(
                rows,
                8,
                [](BoolRow& row) -> std::int64_t& { return row.id; },
                [](BoolRow& row) -> bool& { return row.value; });
            auto result = std::vector<bool> {};
            for (auto const& row: rows)
                result.push_back(row.value);
            return result;
        });
    };
    CHECK(rowWise("1 = 1") == std::string { isSqlite ? "011011" : "22003" });
    auto const rowWise256 = [&]() -> std::string {
        if (isSqlite)
            return "1";
        return isPostgres ? "0" : "22003";
    }();
    CHECK(rowWise(R"("V" = 256)") == rowWise256);
    auto const rowWiseMinusOne = [&]() -> std::string {
        if (isSqlite)
            return "0";
        return isPostgres ? "1" : "22003";
    }();
    CHECK(rowWise(R"("V" = -1)") == rowWiseMinusOne);
    // PostgreSQL writes the low byte 0xFF, a boolean true reported as -1.
    CHECK(rowWise(R"("V" = 255)") == std::string { isSqlite || isPostgres ? "1" : "22003" });

    auto dm = DataMapper {};
    CHECK(BoolRowsOutcome([&] {
              auto rows = std::vector<bool> {};
              for (auto const& record: dm.Query<BoolFromIntegerRecord>().OrderBy("Id").All())
                  rows.push_back(record.V.Value());
              return rows;
          })
          == std::string { isSqlite ? "011011" : "22003" });
}

TEST_CASE_METHOD(SqlTestFixture, "A bool column round-trips, with and without prefetch", "[SqlDataBinder]")
{
    auto stmt = SqlStatement {};
    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.CreateTable("BoolRoundTrip")
            .PrimaryKey("Id", SqlColumnTypeDefinitions::Bigint {})
            .Column("V", SqlColumnTypeDefinitions::Bool {});
    });
    stmt.Prepare(R"(INSERT INTO "BoolRoundTrip" ("Id", "V") VALUES (?, ?))");
    (void) stmt.Execute(std::int64_t { 1 }, true);
    (void) stmt.Execute(std::int64_t { 2 }, std::optional<bool> {});
    (void) stmt.Execute(std::int64_t { 3 }, false);

    auto variantsPerRow = std::vector<std::string> {};
    for (auto const depth: { std::size_t { 1 }, PrefetchDepthDefault })
    {
        INFO("depth " << depth);
        stmt.Connection().SetDefaultPrefetchDepth(depth);
        auto value = true;
        auto maybe = std::optional<bool> {};
        auto variants = std::vector<std::string> {};
        auto cursor = stmt.ExecuteDirect(R"(SELECT "V", "V" FROM "BoolRoundTrip" ORDER BY "Id")");
        cursor.BindOutputColumns(&value, &maybe);
        auto read = std::string {};
        auto const optionalText = [](std::optional<bool> const& v) {
            if (!v.has_value())
                return "-";
            return *v ? "1" : "0";
        };
        while (cursor.FetchRow())
            read += std::format("{}{} ", value ? "1" : "0", optionalText(maybe));
        // A NULL read into a bound bool sets it to false, also after a true.
        CHECK(read == "11 0- 00 ");
        for (auto& row: SqlVariantRowCursor { stmt.ExecuteDirect(R"(SELECT "V" FROM "BoolRoundTrip" ORDER BY "Id")") })
            variants.push_back(row.at(0).ToString());
        // The variant's alternative follows the driver's column type (PostgreSQL reports a boolean as text by
        // default); both depths read the same.
        if (depth == 1)
            variantsPerRow = variants;
        CHECK(variants == variantsPerRow);
        CHECK(variants.size() == 3);
    }
}

/// A record with a @c bool member over a BOOLEAN column, read on a connection with psqlODBC's TrueIsMinus1. At
/// namespace scope: MSVC cannot reflect a type with internal linkage.
struct BoolMinusOneRecord
{
    static constexpr std::string_view TableName = "BoolMinusOne";

    Field<std::int64_t> Id;
    Field<std::optional<bool>> V;
};

TEST_CASE_METHOD(SqlTestFixture, "A bool reads a boolean true reported as -1 as true", "[SqlDataBinder]")
{
    auto stmt = SqlStatement {};
    if (!stmt.Connection().BooleanTrueMayBeMinusOne())
        SKIP("Only psqlODBC reports a boolean true as -1 (connection option TrueIsMinus1=1).");
    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.CreateTable("BoolMinusOne")
            .PrimaryKey("Id", SqlColumnTypeDefinitions::Bigint {})
            .Column("V", SqlColumnTypeDefinitions::Bool {});
    });
    stmt.Prepare(R"(INSERT INTO "BoolMinusOne" ("Id", "V") VALUES (?, ?))");
    (void) stmt.Execute(std::int64_t { 1 }, true);
    (void) stmt.Execute(std::int64_t { 2 }, std::optional<bool> {});
    (void) stmt.Execute(std::int64_t { 3 }, false);

    auto connectionString = SqlConnection::DefaultConnectionString();
    connectionString.value += ";TrueIsMinus1=1";
    auto connection = SqlConnection { connectionString };
    auto minusOne = SqlStatement { connection };
    // The driver does report it so: a true read as a number is -1.
    CHECK(minusOne.ExecuteDirectScalar<std::int64_t>(R"(SELECT "V" FROM "BoolMinusOne" WHERE "Id" = 1)").value_or(0) == -1);

    auto const optionalText = [](std::optional<bool> const& value) {
        if (!value.has_value())
            return "-";
        return *value ? "1" : "0";
    };
    for (auto const depth: { std::size_t { 1 }, PrefetchDepthDefault })
    {
        INFO("depth " << depth);
        connection.SetDefaultPrefetchDepth(depth);
        auto read = std::string {};
        auto cursor = minusOne.ExecuteDirect(R"(SELECT "V" FROM "BoolMinusOne" ORDER BY "Id")");
        while (cursor.FetchRow())
            read += optionalText(cursor.GetNullableColumn<bool>(1));
        CHECK(read == "1-0");

        auto value = false;
        auto bound = std::string {};
        cursor = minusOne.ExecuteDirect(R"(SELECT "V" FROM "BoolMinusOne" ORDER BY "Id")");
        cursor.BindOutputColumns(&value);
        while (cursor.FetchRow())
            bound += value ? "1" : "0";
        CHECK(bound == "100");

        auto records = std::string {};
        for (auto const& record: SqlRowIterator<BoolMinusOneRecord> { connection })
            records += optionalText(record.V.Value());
        CHECK(records == "1-0");
    }

    struct BoolRow
    {
        std::int64_t id = 0;
        std::optional<bool> value;
    };
    auto rows = std::vector<BoolRow> {};
    auto cursor = minusOne.ExecuteDirect(R"(SELECT "Id", "V" FROM "BoolMinusOne" ORDER BY "Id")");
    cursor.FetchAllRowWise(
        rows,
        8,
        [](BoolRow& row) -> std::int64_t& { return row.id; },
        [](BoolRow& row) -> std::optional<bool>& { return row.value; });
    auto rowWise = std::string {};
    for (auto const& row: rows)
        rowWise += optionalText(row.value);
    CHECK(rowWise == "1-0");

    auto dm = DataMapper { std::move(connection) };
    auto all = std::string {};
    for (auto const& record: dm.Query<BoolMinusOneRecord>().OrderBy("Id").All())
        all += optionalText(record.V.Value());
    CHECK(all == "1-0");
}

TEST_CASE_METHOD(SqlTestFixture,
                 "FetchAllRowWise keeps the rows of the blocks before a failing one, and only those",
                 "[SqlStatement]")
{
    auto stmt = SqlStatement {};
    if (stmt.Connection().ServerType() == SqlServerType::SQLITE)
        SKIP("The SQLite driver writes 1 for any positive number through SQL_C_BIT, so no row fails.");
    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.CreateTable("BoolBlocks")
            .PrimaryKey("Id", SqlColumnTypeDefinitions::Bigint {})
            .RequiredColumn("V", SqlColumnTypeDefinitions::Bigint {});
    });
    // Blocks of 4 rows: the second block's second row holds 2, which no bool reads.
    for (auto const id: std::views::iota(0, 8))
        (void) stmt.ExecuteDirect(
            std::format(R"(INSERT INTO "BoolBlocks" ("Id", "V") VALUES ({}, {}))", id, id == 5 ? 2 : id % 2));

    struct BoolRow
    {
        std::int64_t id = 0;
        bool value = false;
    };
    auto rows = std::vector<BoolRow> { BoolRow { .id = -1, .value = true } };
    auto cursor = stmt.ExecuteDirect(R"(SELECT "Id", "V" FROM "BoolBlocks" ORDER BY "Id")");
    auto error = std::string {};
    try
    {
        cursor.FetchAllRowWise(
            rows, 4, [](BoolRow& row) -> std::int64_t& { return row.id; }, [](BoolRow& row) -> bool& { return row.value; });
    }
    catch (SqlException const& exception)
    {
        error = exception.info().sqlState;
    }
    CHECK(error == "22003");
    // What the vector held before, then the first block.
    REQUIRE(rows.size() == 5);
    for (auto const index: std::views::iota(std::size_t { 0 }, rows.size()))
    {
        INFO("row " << index);
        CHECK(rows[index].id == static_cast<std::int64_t>(index) - 1);
        auto byte = static_cast<unsigned char>(0xAA);
        std::memcpy(&byte, &rows[index].value, 1);
        CHECK(byte <= 1);
        CHECK(rows[index].value == (index == 0 || index % 2 == 0));
    }
}

TEST_CASE_METHOD(SqlTestFixture, "FetchAllRowWise reports a row's error, not an earlier row's warning", "[SqlStatement]")
{
    auto stmt = SqlStatement {};
    if (stmt.Connection().ServerType() != SqlServerType::MICROSOFT_SQL)
        SKIP("Only the SQL Server driver warns of a truncated fraction (01S07) and fails a later row in the block.");
    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.CreateTable("RowErrorAfterWarning")
            .PrimaryKey("Id", SqlColumnTypeDefinitions::Bigint {})
            .RequiredColumn("V", SqlColumnTypeDefinitions::Real { .precision = 53 });
    });
    // Row 1 loses its fraction (a warning), row 2 does not fit an int32_t (an error).
    (void) stmt.ExecuteDirect(R"(INSERT INTO "RowErrorAfterWarning" ("Id", "V") VALUES (1, 1.5), (2, 5000000000))");

    struct IntRow
    {
        std::int64_t id = 0;
        std::int32_t value = 0;
    };
    auto rows = std::vector<IntRow> {};
    auto cursor = stmt.ExecuteDirect(R"(SELECT "Id", "V" FROM "RowErrorAfterWarning" ORDER BY "Id")");
    auto error = std::string {};
    try
    {
        cursor.FetchAllRowWise(
            rows,
            8,
            [](IntRow& row) -> std::int64_t& { return row.id; },
            [](IntRow& row) -> std::int32_t& { return row.value; });
    }
    catch (SqlException const& exception)
    {
        error = exception.info().sqlState;
    }
    CHECK(error == "22003");
    CHECK(rows.empty());
}

TEST_CASE_METHOD(SqlTestFixture,
                 "A SqlVariant reads a bit column by the bool rule, with and without prefetch",
                 "[SqlDataBinder]")
{
    auto stmt = SqlStatement {};
    if (stmt.Connection().ServerType() != SqlServerType::SQLITE)
        SKIP("Only SQLite stores a number other than 0 or 1 in a column it describes as SQL_BIT.");
    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.CreateTable("VariantBit")
            .PrimaryKey("Id", SqlColumnTypeDefinitions::Bigint {})
            .Column("V", SqlColumnTypeDefinitions::Bool {});
    });
    (void) stmt.ExecuteDirect(R"(INSERT INTO "VariantBit" ("Id", "V") VALUES (0, 0), (1, 1), (2, 2), (3, -1))");

    auto const variantOutcome = [](std::function<SqlVariant()> const& read) -> std::string {
        try
        {
            return read().ToString();
        }
        catch (SqlException const& error)
        {
            return error.info().sqlState;
        }
    };
    for (auto const depth: { std::size_t { 1 }, PrefetchDepthDefault })
    {
        stmt.Connection().SetDefaultPrefetchDepth(depth);
        auto outcomes = std::string {};
        for (auto const id: std::views::iota(0, 4))
        {
            outcomes += variantOutcome([&] {
                auto cursor = stmt.ExecuteDirect(std::format(R"(SELECT "V" FROM "VariantBit" WHERE "Id" = {})", id));
                if (!cursor.FetchRow())
                    throw std::runtime_error("no row");
                return cursor.GetColumn<SqlVariant>(1);
            });
            outcomes += ' ';
        }
        INFO("depth " << depth);
        CHECK(outcomes == "false true 22003 22003 ");

        auto rows = std::string {};
        try
        {
            for (auto& row: SqlVariantRowCursor { stmt.ExecuteDirect(R"(SELECT "V" FROM "VariantBit" ORDER BY "Id")") })
                rows += row.at(0).ToString() + ' ';
        }
        catch (SqlException const& error)
        {
            rows += error.info().sqlState;
        }
        CHECK(rows == "false true 22003");
    }
}

TEST_CASE_METHOD(SqlTestFixture,
                 "A bool reads text the driver cannot give as a number in a BOOLEAN column as NULL",
                 "[SqlDataBinder]")
{
    auto stmt = SqlStatement {};
    if (stmt.Connection().ServerType() != SqlServerType::SQLITE)
        SKIP("Only SQLite stores text in a column it describes as SQL_BIT.");
    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.CreateTable("BoolText")
            .PrimaryKey("Id", SqlColumnTypeDefinitions::Bigint {})
            .Column("V", SqlColumnTypeDefinitions::Bool {});
    });
    (void) stmt.ExecuteDirect(R"(INSERT INTO "BoolText" ("Id", "V") VALUES (1, 'true'))");
    auto const query = std::string_view { R"(SELECT "V" FROM "BoolText" WHERE "Id" = 1)" };
    auto const optionalText = [](std::optional<bool> const& value) -> std::string_view {
        if (!value.has_value())
            return "-";
        return *value ? "1" : "0";
    };

    // SQLite describes the column as SQL_BIT, so a bool is read through SQL_C_SBIGINT, which the driver answers
    // with NULL for text; the block prefetch reports the cell as NULL as well. (Through SQL_C_BIT the driver parses
    // 'true' as 1, which earlier versions returned per row.)
    for (auto const depth: { std::size_t { 1 }, PrefetchDepthDefault })
    {
        INFO("depth " << depth);
        stmt.Connection().SetDefaultPrefetchDepth(depth);
        auto read = std::string {};
        {
            auto cursor = stmt.ExecuteDirect(query);
            REQUIRE(cursor.FetchRow());
            read += optionalText(cursor.GetNullableColumn<bool>(1));
        }
        {
            auto value = std::optional<bool> { true };
            auto cursor = stmt.ExecuteDirect(query);
            cursor.BindOutputColumns(&value);
            REQUIRE(cursor.FetchRow());
            read += optionalText(value);
        }
        {
            auto cursor = stmt.ExecuteDirect(query);
            REQUIRE(cursor.FetchRow());
            auto const variant = cursor.GetColumn<SqlVariant>(1);
            read += variant.IsNull() ? "-" : variant.ToString();
        }
        // A SqlVariant read per row reads such a cell again as text; a block-prefetched one reads it as NULL (see
        // docs/data-binder.md).
        CHECK(read == (depth == 1 ? "--true" : "---"));
    }
}
