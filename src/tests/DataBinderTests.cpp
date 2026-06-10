// SPDX-License-Identifier: Apache-2.0

#include "Utils.hpp"

#include <Lightweight/Lightweight.hpp>

#include <reflection-cpp/reflection.hpp>

#include <catch2/catch_session.hpp>
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <array>
#include <cstdlib>
#include <format>
#include <limits>
#include <numbers>
#include <ranges>
#include <type_traits>

// NOLINTBEGIN(readability-container-size-empty, bugprone-throwing-static-initialization, bugprone-unchecked-optional-access)

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
    SqlBasicStringOperations<SqlTrimmedFixedString<20>>::TrimRight(&str, 5);
    REQUIRE(str == "Hello");
    SqlTrimmedWideFixedString<20> wstr { L"Hello        " };
    SqlBasicStringOperations<SqlTrimmedWideFixedString<20>>::TrimRight(&wstr, 10);
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
    stmt.MigrateDirect(
        [](auto& migration) { migration.CreateTable("Test").Column("Value", SqlColumnTypeDefinitions::NVarchar { 300 }); });

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
    static constexpr auto sqlColumnTypeNameOverride = SqlColumnTypeDefinitions::NVarchar { 20 };
    static constexpr auto inputValue = ValueType { L"Hello" };
    static constexpr auto expectedOutputValue = ValueType { L"Hello" };
};


template <>
struct TestTypeTraits<SqlAnsiString<20>>
{
    using ValueType = SqlAnsiString<20>;
    static constexpr auto sqlColumnTypeNameOverride = SqlColumnTypeDefinitions::Varchar { 20 };
    static constexpr auto inputValue = ValueType { "Hello" };
    static constexpr auto expectedOutputValue = ValueType { "Hello" };
};

template <>
struct TestTypeTraits<SqlUtf16String<20>>
{
    using ValueType = SqlUtf16String<20>;
    static constexpr auto sqlColumnTypeNameOverride = SqlColumnTypeDefinitions::NVarchar { 20 };
    static constexpr auto inputValue = ValueType { u"Hello" };
    static constexpr auto expectedOutputValue = ValueType { u"Hello" };
};

template <>
struct TestTypeTraits<SqlUtf32String<20>>
{
    using ValueType = SqlUtf32String<20>;
    static constexpr auto sqlColumnTypeNameOverride = SqlColumnTypeDefinitions::NVarchar { 20 };
    static constexpr auto inputValue = ValueType { U"Hello" };
    static constexpr auto expectedOutputValue = ValueType { U"Hello" };
};

template <>
struct TestTypeTraits<SqlWideString<20>>
{
    using ValueType = SqlWideString<20>;
    static constexpr auto sqlColumnTypeNameOverride = SqlColumnTypeDefinitions::NVarchar { 20 };
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
    static constexpr auto sqlColumnTypeNameOverride = SqlColumnTypeDefinitions::Varchar { 50 };
    static auto const inline inputValue = std::string { "Alice" };
    static auto const inline expectedOutputValue = std::string { "Alice" };
    static auto const inline outputInitializer = &MakeStringOuputInitializer<std::string>;
};

template <>
struct TestTypeTraits<std::string_view>
{
    static constexpr auto sqlColumnTypeNameOverride = SqlColumnTypeDefinitions::Varchar { 50 };
    static auto const inline inputValue = std::string_view { "Alice" };
    static auto const inline expectedOutputValue = std::string_view { "Alice" };
    static auto const inline outputInitializer = &MakeStringOuputInitializer<std::string>;
    using GetColumnTypeOverride = std::string;
};

template <>
struct TestTypeTraits<std::u8string>
{
    static auto constexpr sqlColumnTypeNameOverride = SqlColumnTypeDefinitions::NVarchar { 50 };
    static auto const inline inputValue = u8"Hell\u00F6"s;
    static auto const inline expectedOutputValue = u8"Hell\u00F6"s;
    static auto const inline outputInitializer = &MakeStringOuputInitializer<std::u8string>;
};

template <>
struct TestTypeTraits<std::u8string_view>
{
    static constexpr auto sqlColumnTypeNameOverride = SqlColumnTypeDefinitions::NVarchar { 50 };
    static auto const inline inputValue = std::u8string_view { u8"Hell\u00F6" };
    static auto const inline expectedOutputValue = std::u8string_view { u8"Hell\u00F6" };
    static auto const inline outputInitializer = &MakeStringOuputInitializer<std::u8string>;
    using GetColumnTypeOverride = std::u8string;
};

template <>
struct TestTypeTraits<std::u16string>
{
    static auto constexpr sqlColumnTypeNameOverride = SqlColumnTypeDefinitions::NVarchar { 50 };
    static auto const inline inputValue = u"Alice"s;
    static auto const inline expectedOutputValue = u"Alice"s;
    static auto const inline outputInitializer = &MakeStringOuputInitializer<std::u16string>;
};

template <>
struct TestTypeTraits<std::u16string_view>
{
    static constexpr auto sqlColumnTypeNameOverride = SqlColumnTypeDefinitions::NVarchar { 50 };
    static auto const inline inputValue = std::u16string_view { u"Alice" };
    static auto const inline expectedOutputValue = std::u16string_view { u"Alice" };
    static auto const inline outputInitializer = &MakeStringOuputInitializer<std::u16string>;
    using GetColumnTypeOverride = std::u16string;
};

template <>
struct TestTypeTraits<std::u32string>
{
    static auto constexpr sqlColumnTypeNameOverride = SqlColumnTypeDefinitions::NVarchar { 50 };
    static auto const inline inputValue = U"Alice"s;
    static auto const inline expectedOutputValue = U"Alice"s;
    static auto const inline outputInitializer = &MakeStringOuputInitializer<std::u32string>;
};

template <>
struct TestTypeTraits<std::u32string_view>
{
    static constexpr auto sqlColumnTypeNameOverride = SqlColumnTypeDefinitions::NVarchar { 50 };
    static auto const inline inputValue = std::u32string_view { U"Alice" };
    static auto const inline expectedOutputValue = std::u32string_view { U"Alice" };
    static auto const inline outputInitializer = &MakeStringOuputInitializer<std::u32string>;
    using GetColumnTypeOverride = std::u32string;
};

template <>
struct TestTypeTraits<std::wstring>
{
    static auto constexpr sqlColumnTypeNameOverride = SqlColumnTypeDefinitions::NVarchar { 50 };
    static auto const inline inputValue = L"Alice"s;
    static auto const inline expectedOutputValue = L"Alice"s;
    static auto const inline outputInitializer = &MakeStringOuputInitializer<std::wstring>;
};

template <>
struct TestTypeTraits<std::wstring_view>
{
    static constexpr auto sqlColumnTypeNameOverride = SqlColumnTypeDefinitions::NVarchar { 50 };
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
    static auto constexpr sqlColumnTypeNameOverride = SqlColumnTypeDefinitions::VarBinary { 50 };
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
        migration.CreateTable("WideStringFromVarcharTest").Column("value", SqlColumnTypeDefinitions::Varchar { 100 });
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
        migration.CreateTable("UnicodeRoundTrip").Column("value", SqlColumnTypeDefinitions::NVarchar { 64 });
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
}

struct UnicodeAcrossDynamicStringTypes
{
    Field<uint64_t, PrimaryKey::ServerSideAutoIncrement> id {};
    Field<SqlDynamicUtf16String<256>> stringUtf16 {};
    Field<SqlDynamicUtf32String<256>> stringUtf32 {};
    Field<SqlDynamicWideString<256>> stringWide {};
};

TEST_CASE_METHOD(SqlTestFixture, "Unicode round-trip across DataMapper dynamic string types", "[DataMapper][Unicode]")
{
    auto dm = DataMapper {};
    dm.CreateTable<UnicodeAcrossDynamicStringTypes>();

    UnicodeAcrossDynamicStringTypes record {};
    record.stringUtf16 = std::u16string { u"Hello \U0001F601 World" };
    record.stringUtf32 = std::u32string { U"Hello \U0001F601 World" };
    record.stringWide = std::wstring { L"Hello \U0001F601 World" };
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
    Field<SqlTrimmedFixedString<32>> stringNarrow {};
    Field<SqlTrimmedWideFixedString<32>> stringWide {};
};

TEST_CASE_METHOD(SqlTestFixture, "Trimmed fixed strings strip trailing padding through DataMapper", "[DataMapper][Unicode]")
{
    auto dm = DataMapper {};
    dm.CreateTable<UnicodeTrimmedFixedRow>();

    UnicodeTrimmedFixedRow row {};
    row.stringNarrow = "Hello";
    row.stringWide = L"Hellö";
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
    Field<SqlTrimmedFixedString<3>> code {};
};

TEST_CASE_METHOD(SqlTestFixture,
                 "Trimmed fixed string preserves a value that fills its capacity",
                 "[DataMapper][SqlFixedString][regression]")
{
    auto dm = DataMapper {};
    dm.CreateTable<FullCapacityFixedRow>();

    FullCapacityFixedRow row {};
    row.code = "EUR";
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
// for SQL Server) actually take for streaming columns, so these tests pin the
// arithmetic for that branch. Branch (b) is rare in practice but its byte/char
// math was previously off by sizeof(CharType); the tests below also serve as
// forward-looking coverage if a future driver routes here.
//
// The streaming column type — NVARCHAR(MAX) on SQL Server, TEXT on Postgres /
// SQLite — plus payloads that overflow the 255-char initial buffer in
// GetColumnUtf16 are what get us into the re-fetch path at all.
TEST_CASE_METHOD(SqlTestFixture, "GetRawColumnArrayData: long Unicode round-trip", "[SqlDataBinder][Unicode]")
{
    auto stmt = SqlStatement {};
    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.CreateTable("LongUnicode").Column("value", SqlColumnTypeDefinitions::NVarchar { 0 });
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
        RoundTripStringValue(SqlDynamicAnsiString<64> { "Hello" }, SqlColumnTypeDefinitions::Varchar { 64 });
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
        RoundTripStringValue(SqlDynamicUtf16String<64> { u"Hello" }, SqlColumnTypeDefinitions::NVarchar { 64 });
    }

    SECTION("supplementary-plane value")
    {
        auto const expected = SqlDynamicUtf16String<64> { u"\U0001F601 Hello" };
        RoundTripStringValue(expected, SqlColumnTypeDefinitions::NVarchar { 64 });
    }

    SECTION("BindOutputColumns")
    {
        auto stmt = SqlStatement {};
        (void) stmt.ExecuteDirect(
            std::format("CREATE TABLE Test (Value {} NULL)",
                        stmt.Connection().QueryFormatter().ColumnType(SqlColumnTypeDefinitions::NVarchar { 64 })));
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
    RoundTripStringValue(SqlDynamicUtf32String<64> { U"Hello \U0001F601" }, SqlColumnTypeDefinitions::NVarchar { 64 });
}

TEST_CASE_METHOD(SqlTestFixture, "SqlDynamicWideString: round-trip (binder)", "[SqlDynamicString][SqlDataBinder][Unicode]")
{
    RoundTripStringValue(SqlDynamicWideString<64> { L"Hello \U0001F601" }, SqlColumnTypeDefinitions::NVarchar { 64 });
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
        RoundTripStringValue(SqlString<32, char> { "Alias" }, SqlColumnTypeDefinitions::Varchar { 32 });
    }

    SECTION("default char specialization")
    {
        RoundTripStringValue(SqlString<32> { "DefaultAlias" }, SqlColumnTypeDefinitions::Varchar { 32 });
    }

    SECTION("wchar_t specialization")
    {
        RoundTripStringValue(SqlString<32, wchar_t> { L"WideAlias" }, SqlColumnTypeDefinitions::NVarchar { 32 });
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
    auto const sqlColumnType = stmt.Connection().QueryFormatter().ColumnType(SqlColumnTypeDefinitions::NVarchar { 32 });
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
    auto const sqlColumnType = stmt.Connection().QueryFormatter().ColumnType(SqlColumnTypeDefinitions::NVarchar { 32 });
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
    auto const sqlColumnType = stmt.Connection().QueryFormatter().ColumnType(SqlColumnTypeDefinitions::NVarchar { 32 });
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
    auto const sqlColumnType = stmt.Connection().QueryFormatter().ColumnType(SqlColumnTypeDefinitions::Varchar { 32 });
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
    auto const sqlColumnType = stmt.Connection().QueryFormatter().ColumnType(SqlColumnTypeDefinitions::NVarchar { 32 });
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
    auto const sqlColumnType = stmt.Connection().QueryFormatter().ColumnType(SqlColumnTypeDefinitions::NVarchar { 64 });
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
    auto const sqlColumnType = stmt.Connection().QueryFormatter().ColumnType(SqlColumnTypeDefinitions::NVarchar { 32 });
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

    auto const sqlColumnType = stmt.Connection().QueryFormatter().ColumnType(SqlColumnTypeDefinitions::NVarchar { 64 });
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
    SqlBasicStringOperations<SqlTrimmedFixedString<32>>::TrimRight(&str, 10);
    CHECK(str == "abc");

    SECTION("trim stops at non-whitespace even with extra padding")
    {
        SqlTrimmedFixedString<32> nonTrimmable { "abc def    " };
        SqlBasicStringOperations<SqlTrimmedFixedString<32>>::TrimRight(&nonTrimmable, 11);
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
    auto const sqlColumnType = stmt.Connection().QueryFormatter().ColumnType(SqlColumnTypeDefinitions::NVarchar { 8 });
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

// NOLINTEND(readability-container-size-empty, bugprone-throwing-static-initialization, bugprone-unchecked-optional-access)
