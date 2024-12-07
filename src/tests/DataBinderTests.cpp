// SPDX-License-Identifier: Apache-2.0

#include "Utils.hpp"

#include <Lightweight/DataBinder/UnicodeConverter.hpp>
#include <Lightweight/SqlConnection.hpp>
#include <Lightweight/SqlDataBinder.hpp>
#include <Lightweight/SqlQuery.hpp>
#include <Lightweight/SqlQueryFormatter.hpp>
#include <Lightweight/SqlScopedTraceLogger.hpp>
#include <Lightweight/SqlStatement.hpp>
#include <Lightweight/SqlTransaction.hpp>

#include <reflection-cpp/reflection.hpp>

#include <catch2/catch_session.hpp>
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cstdlib>
#include <format>
#include <numbers>
#include <ranges>
#include <type_traits>

// NOLINTBEGIN(readability-container-size-empty)

#if defined(_MSC_VER)
    // Disable the warning C4834: discarding return value of function with 'nodiscard' attribute.
    // Because we are simply testing and demonstrating the library and not using it in production code.
    #pragma warning(disable : 4834)
#endif

using namespace std::chrono_literals;
using namespace std::string_literals;
using namespace std::string_view_literals;

struct CustomType
{
    int value;

    constexpr auto operator<=>(CustomType const&) const noexcept = default;
};

std::ostream& operator<<(std::ostream& os, CustomType const& value)
{
    return os << std::format("CustomType({})", value.value);
}

template <>
struct SqlDataBinder<CustomType>
{
    static constexpr auto ColumnType = SqlDataBinder<decltype(CustomType::value)>::ColumnType;

    static SQLRETURN InputParameter(SQLHSTMT hStmt,
                                    SQLUSMALLINT column,
                                    CustomType const& value,
                                    SqlDataBinderCallback& cb) noexcept
    {
        return SqlDataBinder<int>::InputParameter(hStmt, column, value.value, cb);
    }

    static SQLRETURN OutputColumn(SQLHSTMT hStmt,
                                  SQLUSMALLINT column,
                                  CustomType* result,
                                  SQLLEN* indicator,
                                  SqlDataBinderCallback& callback) noexcept
    {
        callback.PlanPostProcessOutputColumn([result]() { result->value = PostProcess(result->value); });
        return SqlDataBinder<int>::OutputColumn(hStmt, column, &result->value, indicator, callback);
    }

    static SQLRETURN GetColumn(SQLHSTMT hStmt,
                               SQLUSMALLINT column,
                               CustomType* result,
                               SQLLEN* indicator,
                               SqlDataBinderCallback const& cb) noexcept
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
    REQUIRE(str.size() == 0);
    REQUIRE(str == "");
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
    REQUIRE(str == "");

    // no-op
    str.pop_back();
    REQUIRE(str == "");
}

TEST_CASE_METHOD(SqlTestFixture, "SqlFixedString: assign", "[SqlFixedString]")
{
    SqlFixedString<12> str;
    str.assign("Hello, World");
    REQUIRE(str == "Hello, World");
    // str.assign("Hello, World!"); <-- would fail due to static_assert
    str.assign("Hello, World!"sv);
    REQUIRE(str == "Hello, World");

    str = "Something";
    REQUIRE(str == "Something");
    // str = ("Hello, World!"); // <-- would fail due to static_assert
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

TEST_CASE_METHOD(SqlTestFixture, "SqlVariant: GetColumn in-place store variant", "[SqlDataBinder]")
{
    auto stmt = SqlStatement {};
    CreateEmployeesTable(stmt);

    stmt.Prepare(R"(INSERT INTO "Employees" ("FirstName", "LastName", "Salary") VALUES (?, ?, ?))");
    stmt.Execute("Alice", SqlNullValue, 50'000);

    stmt.ExecuteDirect(R"(SELECT "FirstName", "LastName", "Salary" FROM "Employees")");
    (void) stmt.FetchRow();

    CHECK(stmt.GetColumn<std::string>(1) == "Alice");

    SqlVariant lastName;
    CHECK(!stmt.GetColumn(2, &lastName));
    CHECK(lastName.IsNull());

    UNSUPPORTED_DATABASE(stmt, SqlServerType::ORACLE);
    SqlVariant salary;
    CHECK(stmt.GetColumn(3, &salary));
    CHECK(salary.TryGetInt().value_or(0) == 50'000);
}

TEST_CASE_METHOD(SqlTestFixture, "SqlVariant: NULL values", "[SqlDataBinder],[SqlVariant]")
{
    auto stmt = SqlStatement();
    stmt.ExecuteDirect("CREATE TABLE Test (Remarks VARCHAR(50) NULL)");

    SECTION("Test for inserting/getting NULL values")
    {
        stmt.Prepare("INSERT INTO Test (Remarks) VALUES (?)");
        stmt.Execute(SqlNullValue);
        stmt.ExecuteDirect("SELECT Remarks FROM Test");

        auto reader = stmt.GetResultCursor();
        (void) stmt.FetchRow();

        auto const actual = reader.GetColumn<SqlVariant>(1);
        CHECK(std::holds_alternative<SqlNullType>(actual.value));
    }

    SECTION("Using ExecuteDirectScalar")
    {
        stmt.Prepare("INSERT INTO Test (Remarks) VALUES (?)");
        stmt.Execute(SqlNullValue);
        auto const result = stmt.ExecuteDirectScalar<SqlVariant>("SELECT Remarks FROM Test");
        CHECK(result.IsNull());
    }
}

TEST_CASE_METHOD(SqlTestFixture, "SqlVariant: SqlDate", "[SqlDataBinder],[SqlVariant]")
{
    auto stmt = SqlStatement {};
    stmt.MigrateDirect([](auto& migration) {
        migration.CreateTable("Test").Column("Value", SqlColumnTypeDefinitions::Date {});
    });

    using namespace std::chrono_literals;
    auto const expected = SqlVariant { SqlDate { 2017y, std::chrono::August, 16d } };

    stmt.Prepare(stmt.Query("Test").Insert().Set("Value", SqlWildcard));
    stmt.Execute(expected);

    stmt.ExecuteDirect(stmt.Query("Test").Select().Field("Value").All());
    {
        auto reader = stmt.GetResultCursor();
        (void) stmt.FetchRow();
        auto const actual = reader.GetColumn<SqlVariant>(1);
        CHECK(actual.TryGetDate().value() == std::get<SqlDate>(expected.value));
    }

    // Test for inserting/getting NULL values
    stmt.ExecuteDirect(stmt.Query("Test").Delete());
    stmt.Prepare(stmt.Query("Test").Insert().Set("Value", SqlWildcard));
    stmt.Execute(SqlNullValue);
    auto const result = stmt.ExecuteDirectScalar<SqlVariant>(stmt.Query("Test").Select().Field("Value").All());
    CHECK(result.IsNull());
}

TEST_CASE_METHOD(SqlTestFixture, "SqlVariant: SqlTime", "[SqlDataBinder],[SqlVariant]")
{
    auto stmt = SqlStatement {};
    stmt.MigrateDirect([](auto& migration) {
        migration.CreateTable("Test").Column("Value", SqlColumnTypeDefinitions::Time {});
    });

    using namespace std::chrono_literals;
    auto const expected = SqlVariant { SqlTime { 12h, 34min, 56s } };

    stmt.Prepare(stmt.Query("Test").Insert().Set("Value", SqlWildcard));
    stmt.Execute(expected);

    auto const actual = stmt.ExecuteDirectScalar<SqlVariant>(stmt.Query("Test").Select().Field("Value").All());

    if (stmt.Connection().ServerType() == SqlServerType::POSTGRESQL)
    {
        WARN("PostgreSQL seems to report SQL_TYPE_DATE here. Skipping check, that would fail otherwise.");
        // TODO: Find out why PostgreSQL reports SQL_TYPE_DATE instead of SQL_TYPE_TIME for SQL column type TIME.
        return;
    }

    CHECK(actual.TryGetTime().value() == std::get<SqlTime>(expected.value));

    // Test for inserting/getting NULL values
    stmt.ExecuteDirect(stmt.Query("Test").Delete());
    stmt.Prepare(stmt.Query("Test").Insert().Set("Value", SqlWildcard));
    stmt.Execute(SqlNullValue);
    auto const result = stmt.ExecuteDirectScalar<SqlVariant>(stmt.Query("Test").Select().Field("Value").All());
    CHECK(result.IsNull());
}

TEST_CASE_METHOD(SqlTestFixture, "InputParameter and GetColumn for very large values", "[SqlDataBinder]")
{
    auto const MakeLargeText = [](size_t size) {
        auto text = std::string(size, '\0');
        std::ranges::generate(text, [i = 0]() mutable { return char('A' + (i++ % 26)); });
        return text;
    };

    auto stmt = SqlStatement {};
    UNSUPPORTED_DATABASE(stmt, SqlServerType::ORACLE);
    auto const expectedText = MakeLargeText(8 * 1000);
    stmt.MigrateDirect([size = expectedText.size()](auto& migration) {
        migration.CreateTable("Test").Column("Value", SqlColumnTypeDefinitions::Text { size });
    });
    stmt.Prepare(stmt.Query("Test").Insert().Set("Value", SqlWildcard));
    stmt.Execute(expectedText);

    SECTION("check handling for explicitly fetched output columns")
    {
        stmt.ExecuteDirect(stmt.Query("Test").Select().Field("Value").All());
        (void) stmt.FetchRow();
        CHECK(stmt.GetColumn<std::string>(1) == expectedText);
    }

    SECTION("check handling for explicitly fetched output columns (in-place store)")
    {
        stmt.ExecuteDirect(stmt.Query("Test").Select().Field("Value").All());
        (void) stmt.FetchRow();
        std::string actualText;
        CHECK(stmt.GetColumn(1, &actualText));
        CHECK(actualText == expectedText);
    }

    SECTION("check handling for bound output columns")
    {
        stmt.Prepare(stmt.Query("Test").Select().Field("Value").All());
        stmt.Execute();
        auto reader = stmt.GetResultCursor();

        // Intentionally an empty string, auto-growing behind the scenes
        std::string actualText;

        // For Microsoft SQL Server, we need to allocate a large enough buffer for the output column.
        // Because MS SQL's ODBC driver does not support SQLGetData after SQLFetch for truncated data, it seems.
        if (stmt.Connection().ServerType() == SqlServerType::MICROSOFT_SQL)
        {
            WARN("Preallocate the buffer for MS SQL Server");
            actualText = std::string(expectedText.size() + 1, '\0');
        }

        reader.BindOutputColumns(&actualText);
        (void) stmt.FetchRow();
        REQUIRE(actualText.size() == expectedText.size());
        CHECK(actualText == expectedText);
    }
}

TEST_CASE_METHOD(SqlTestFixture, "SqlDataBinder: Unicode", "[SqlDataBinder],[Unicode]")
{
    auto stmt = SqlStatement {};
    UNSUPPORTED_DATABASE(stmt, SqlServerType::ORACLE);

    if (stmt.Connection().ServerType() == SqlServerType::SQLITE)
        // SQLite does UTF-8 by default, so we need to switch to UTF-16
        stmt.ExecuteDirect("PRAGMA encoding = 'UTF-16'");

    // Create table with Unicode column.
    // Mind, for PostgreSQL, we need to use VARCHAR instead of NVARCHAR,
    // because supports Unicode only via UTF-8.
    stmt.ExecuteDirect(
        std::format("CREATE TABLE Test (Value {}(50) NULL)",
                    stmt.Connection().ServerType() == SqlServerType::POSTGRESQL ? "VARCHAR" : "NVARCHAR"));

    stmt.Prepare("INSERT INTO Test (Value) VALUES (?)");

    // Insert via wide string literal
    stmt.Execute(WTEXT("Wide string literal \U0001F600"));

    // Insert via wide string view
    stmt.Execute(WideStringView(WTEXT("Wide string literal \U0001F600")));

    // Insert via wide string object
    WideString const inputValue = WTEXT("Wide string literal \U0001F600");
    stmt.Execute(inputValue);

    stmt.ExecuteDirect("SELECT Value FROM Test");
    {
        auto reader = stmt.GetResultCursor();

        // Fetch and check GetColumn for wide string
        (void) stmt.FetchRow();
        auto const actualValue = reader.GetColumn<WideString>(1);
        CHECK(actualValue == inputValue);

        // Bind output column, fetch, and check result in output column for wide string
        WideString actualValue2;
        reader.BindOutputColumns(&actualValue2);
        (void) stmt.FetchRow();
        CHECK(actualValue2 == inputValue);
    }

    SECTION("Test for inserting/getting NULL VALUES")
    {
        stmt.ExecuteDirect("DELETE FROM Test");
        stmt.Prepare("INSERT INTO Test (Value) VALUES (?)");
        stmt.Execute(SqlNullValue);
        auto const result = stmt.ExecuteDirectScalar<WideString>("SELECT Value FROM Test");
        CHECK(!result.has_value());
    }
}

TEST_CASE_METHOD(SqlTestFixture, "SqlNumeric", "[SqlDataBinder],[SqlNumeric]")
{
    auto const expectedValue = SqlNumeric<10, 2> { 123.45 };

    INFO(expectedValue);
    CHECK_THAT(expectedValue.ToDouble(), Catch::Matchers::WithinAbs(123.45, 0.001));
    CHECK_THAT(expectedValue.ToFloat(), Catch::Matchers::WithinAbs(123.45F, 0.001));
    CHECK(expectedValue.ToString() == "123.45");
}

// clang-format off
template <typename T>
struct TestTypeTraits;

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
    static constexpr auto blacklist = std::array {
        std::pair { SqlServerType::ORACLE, "Oracle does not support 64-bit integers via SQL_BIGINT it seems"sv },
    };
    static constexpr auto inputValue = (std::numeric_limits<int64_t>::max)();
    static constexpr auto expectedOutputValue = (std::numeric_limits<int64_t>::max)();
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
struct TestTypeTraits<SqlTrimmedFixedString<20, char>>
{
    using ValueType = SqlTrimmedFixedString<20, char>;
    static constexpr auto sqlColumnTypeNameOverride = SqlColumnTypeDefinitions::Char { 20 };
    static constexpr auto inputValue = ValueType { "Hello " };
    static constexpr auto expectedOutputValue = ValueType { "Hello" };
};

template <>
struct TestTypeTraits<SqlString<20>>
{
    using ValueType = SqlString<20>;
    static constexpr auto sqlColumnTypeNameOverride = SqlColumnTypeDefinitions::Varchar { 20 };
    static constexpr auto inputValue = ValueType { "Hello" };
    static constexpr auto expectedOutputValue = ValueType { "Hello" };
};

template <>
struct TestTypeTraits<SqlString<20, char16_t>>
{
    using ValueType = SqlString<20, char16_t>;
    static constexpr auto sqlColumnTypeNameOverride = SqlColumnTypeDefinitions::NVarchar { 20 };
    static constexpr auto inputValue = ValueType { u"Hello" };
    static constexpr auto expectedOutputValue = ValueType { u"Hello" };
};

template <>
struct TestTypeTraits<SqlString<20, char32_t>>
{
    using ValueType = SqlString<20, char32_t>;
    static constexpr auto sqlColumnTypeNameOverride = SqlColumnTypeDefinitions::NVarchar { 20 };
    static constexpr auto inputValue = ValueType { U"Hello" };
    static constexpr auto expectedOutputValue = ValueType { U"Hello" };
};

template <>
struct TestTypeTraits<SqlString<20, wchar_t>>
{
    using ValueType = SqlString<20, wchar_t>;
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
struct TestTypeTraits<SqlNumeric<15, 2>>
{
    static constexpr auto blacklist = std::array {
        std::pair { SqlServerType::SQLITE, "SQLite does not support NUMERIC type"sv },
    };
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

using TypesToTest = std::tuple<
    CustomType,
    SqlDate,
    SqlDateTime,
    SqlGuid,
    SqlNumeric<15, 2>,
    SqlString<20, char16_t>,
    SqlString<20, char32_t>,
    SqlString<20, wchar_t>,
    SqlString<20>,
    SqlText,
    SqlTime,
    SqlTrimmedFixedString<20, char>,
    double,
    float,
    int16_t,
    int32_t,
    int64_t,
    std::string,
    std::string_view,
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

    GIVEN(Reflection::TypeName<TestType>)
    {
        SqlTestFixture::DropAllTablesInDatabase();

        // Connecting to the database (using the default connection) the verbose way,
        // purely to demonstrate how to do it.
        auto conn = SqlConnection { SqlConnection::DefaultConnectionString() };

        if constexpr (requires { TestTypeTraits<TestType>::blacklist; })
        {
            for (auto const& [serverType, reason]: TestTypeTraits<TestType>::blacklist)
            {
                if (serverType == conn.ServerType())
                {
                    WARN("Skipping blacklisted test for " << Reflection::TypeName<TestType> << ": " << reason);
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

        stmt.ExecuteDirect(std::format("CREATE TABLE Test (Value {} NULL)", sqlColumnType));

        WHEN("Inserting a value")
        {
            stmt.Prepare("INSERT INTO Test (Value) VALUES (?)");
            stmt.Execute(TestTypeTraits<TestType>::inputValue);

            THEN("Retrieve value via GetColumn()")
            {
                stmt.ExecuteDirect("SELECT Value FROM Test");
                CAPTURE(stmt.FetchRow());
                if constexpr (std::is_convertible_v<TestType, double> && !std::integral<TestType>)
                    CHECK_THAT(
                        stmt.GetColumn<TestType>(1),
                        (Catch::Matchers::WithinAbs(double(TestTypeTraits<TestType>::expectedOutputValue), 0.001)));
                else if constexpr (requires { typename TestTypeTraits<TestType>::GetColumnTypeOverride; })
                    CHECK(stmt.GetColumn<typename TestTypeTraits<TestType>::GetColumnTypeOverride>(1)
                          == TestTypeTraits<TestType>::expectedOutputValue);
                else
                    CHECK(stmt.GetColumn<TestType>(1) == TestTypeTraits<TestType>::expectedOutputValue);
            }

            if constexpr (!requires { typename TestTypeTraits<TestType>::GetColumnTypeOverride; })
            {
                THEN("Retrieve value via BindOutputColumns()")
                {
                    stmt.ExecuteDirect("SELECT Value FROM Test");
                    auto actualValue = [&]() -> TestType {
                        if constexpr (requires(SqlServerType st) { TestTypeTraits<TestType>::outputInitializer(st); })
                            return TestTypeTraits<TestType>::outputInitializer(conn.ServerType());
                        else if constexpr (requires { TestTypeTraits<TestType>::outputInitializer; })
                            return TestTypeTraits<TestType>::outputInitializer;
                        else
                            return TestType {};
                    }();
                    stmt.BindOutputColumns(&actualValue);
                    (void) stmt.FetchRow();
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
                stmt.Execute(SqlNullValue);

                THEN("Retrieve value via GetNullableColumn()")
                {
                    stmt.ExecuteDirect("SELECT Value FROM Test");
                    (void) stmt.FetchRow();
                    CHECK(!stmt.GetNullableColumn<TestType>(1).has_value());
                }

                THEN("Retrieve value via GetColumn()")
                {
                    stmt.ExecuteDirect("SELECT Value FROM Test");
                    (void) stmt.FetchRow();
                    CHECK_THROWS_AS(stmt.GetColumn<TestType>(1), std::runtime_error);
                }

                THEN("Retrieve value via BindOutputColumns()")
                {
                    stmt.Prepare("SELECT Value FROM Test");
                    stmt.Execute();
                    auto actualValue = std::optional<TestType> {};
                    stmt.BindOutputColumns(&actualValue);
                    (void) stmt.FetchRow();
                    CHECK(!actualValue.has_value());
                }
            }
        }
    }
}

// NOLINTEND(readability-container-size-empty)
