// SPDX-License-Identifier: Apache-2.0

#include "Utils.hpp"

#include <Lightweight/Lightweight.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string>
#include <string_view>
#include <vector>

// Narrow strings hold text in the process ANSI code page (GetACP()). The SQLite ODBC driver's OEMCP option converts
// SQL_C_CHAR data with the thread's ANSI code page instead, which differs whenever the user's format locale differs
// from the system locale. These tests run with such a thread locale and check that narrow text keeps its meaning.
#if defined(_WIN32) || defined(_WIN64)

using namespace std::string_literals;
using namespace Lightweight;

// NOLINTBEGIN(bugprone-unchecked-optional-access)

// Record types must have linkage (not in an anonymous namespace) for reflection-cpp.

struct NarrowTextCodePageRecord
{
    Field<int32_t, PrimaryKey::AutoAssign> id;
    Field<std::string> text;
    Field<SqlAnsiString<16>> fixedText;
    Field<SqlDynamicAnsiString<16>> dynamicText;
};

struct NarrowTextCodePageFixedRecord
{
    Field<int32_t, PrimaryKey::AutoAssign> id;
    Field<SqlAnsiString<16>> fixedText;
};

namespace
{

/// The text the tests store: "äöüßñ", all of which the common Western and Cyrillic code pages can hold.
constexpr auto UnicodeText = std::u16string_view { u"äöüßñ" };

/// Converts @p text to the process ANSI code page, the encoding narrow strings carry.
std::string ToProcessCodePage(std::u16string_view text)
{
    auto const* const wideText = reinterpret_cast<wchar_t const*>(text.data());
    auto const wideLength = static_cast<int>(text.size());
    auto result = std::string(
        static_cast<size_t>(WideCharToMultiByte(CP_ACP, 0, wideText, wideLength, nullptr, 0, nullptr, nullptr)), '\0');
    WideCharToMultiByte(CP_ACP, 0, wideText, wideLength, result.data(), static_cast<int>(result.size()), nullptr, nullptr);
    return result;
}

/// Sets a thread locale whose ANSI code page differs from the process one, and restores the previous on destruction.
class ScopedForeignThreadLocale
{
  public:
    ScopedForeignThreadLocale():
        _previousLocale { GetThreadLocale() }
    {
        constexpr auto Cyrillic = MAKELCID(MAKELANGID(LANG_RUSSIAN, SUBLANG_RUSSIAN_RUSSIA), SORT_DEFAULT);
        constexpr auto Western = MAKELCID(MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), SORT_DEFAULT);
        constexpr auto CyrillicCodePage = 1251U;
        SetThreadLocale(GetACP() == CyrillicCodePage ? Western : Cyrillic);
    }

    ScopedForeignThreadLocale(ScopedForeignThreadLocale const&) = delete;
    ScopedForeignThreadLocale(ScopedForeignThreadLocale&&) = delete;
    ScopedForeignThreadLocale& operator=(ScopedForeignThreadLocale const&) = delete;
    ScopedForeignThreadLocale& operator=(ScopedForeignThreadLocale&&) = delete;

    ~ScopedForeignThreadLocale()
    {
        SetThreadLocale(_previousLocale);
    }

  private:
    LCID _previousLocale;
};

/// Appends @p options to the test database's connection string.
SqlConnectionString WithOptions(std::string_view options)
{
    return SqlConnectionString { std::format("{};{}", SqlConnection::DefaultConnectionString().value, options) };
}

/// Connects to the test database with the SQLite ODBC driver's OEMCP option, under which the driver
/// converts narrow text with the thread's ANSI code page.
SqlConnection ConnectWithDriverCodePage()
{
    return SqlConnection { WithOptions("OEMCP=1") };
}

/// Reads the first row of @p tableName's @p columnName as UTF-16, independent of any narrow conversion.
std::u16string ReadStoredText(SqlConnection& connection, std::string_view tableName, std::string_view columnName)
{
    auto stmt = SqlStatement { connection };
    auto cursor = stmt.ExecuteDirect(std::format(R"(SELECT "{}" FROM "{}")", columnName, tableName));
    REQUIRE(cursor.FetchRow());
    return cursor.GetColumn<std::u16string>(1);
}

} // namespace

TEST_CASE_METHOD(SqlTestFixture,
                 "SQLite OEMCP option selects the process code page for narrow text",
                 "[SqlConnection][Unicode]")
{
    if (SqlConnection {}.ServerType() != SqlServerType::SQLITE)
        return;

    CHECK(ConnectWithDriverCodePage().NarrowTextBindingMode() == detail::NarrowTextBinding::WideFromProcessCodePage);
    CHECK(SqlConnection { WithOptions("OEMCP=yes") }.NarrowTextBindingMode()
          == detail::NarrowTextBinding::WideFromProcessCodePage);
    CHECK(SqlConnection { WithOptions("oemcp=True") }.NarrowTextBindingMode()
          == detail::NarrowTextBinding::WideFromProcessCodePage);
    CHECK(SqlConnection { WithOptions("OEMCP=0") }.NarrowTextBindingMode() == detail::NarrowTextBinding::Narrow);
    CHECK(SqlConnection { WithOptions("OEMCP=no") }.NarrowTextBindingMode() == detail::NarrowTextBinding::Narrow);
}

TEST_CASE_METHOD(SqlTestFixture, "Without OEMCP narrow text stays UTF-8 verbatim on SQLite", "[SqlDataBinder][Unicode]")
{
    if (SqlConnection {}.ServerType() != SqlServerType::SQLITE)
        return;

    auto connection = SqlConnection { WithOptions("OEMCP=0") };
    REQUIRE(connection.NarrowTextBindingMode() == detail::NarrowTextBinding::Narrow);
    auto const threadLocale = ScopedForeignThreadLocale {};

    auto stmt = SqlStatement { connection };
    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.CreateTable("NarrowUtf8").Column("value", SqlColumnTypeDefinitions::Varchar { .size = 32 });
    });
    auto const utf8Text = "\xC3\xA4\xC3\xB6\xC3\xBC\xC3\x9F\xC3\xB1"s; // "äöüßñ" as UTF-8 bytes
    stmt.Prepare(stmt.Query("NarrowUtf8").Insert().Set("value", SqlWildcard));
    std::ignore = stmt.Execute(utf8Text);
    CHECK(ReadStoredText(connection, "NarrowUtf8", "value") == UnicodeText);

    stmt.Prepare(stmt.Query("NarrowUtf8").Select().Field("value").All());
    auto reader = stmt.Execute();
    REQUIRE(reader.FetchRow());
    CHECK(reader.GetColumn<std::string>(1) == utf8Text);
}

TEST_CASE_METHOD(SqlTestFixture,
                 "Narrow text uses the process code page under a foreign thread locale",
                 "[SqlDataBinder][Unicode]")
{
    // Only the SQLite driver converts narrow text with the thread code page.
    if (SqlConnection {}.ServerType() != SqlServerType::SQLITE)
        return;

    auto connection = ConnectWithDriverCodePage();
    auto stmt = SqlStatement { connection };

    auto const narrowText = ToProcessCodePage(UnicodeText);
    auto const threadLocale = ScopedForeignThreadLocale {};

    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.CreateTable("NarrowCodePage").Column("value", SqlColumnTypeDefinitions::Varchar { .size = 32 });
    });
    auto const insertQuery = stmt.Query("NarrowCodePage").Insert().Set("value", SqlWildcard);
    auto const selectQuery = stmt.Query("NarrowCodePage").Select().Field("value").All();

    SECTION("std::string parameter, GetColumn and BindOutputColumns")
    {
        stmt.Prepare(insertQuery);
        std::ignore = stmt.Execute(narrowText);
        CHECK(ReadStoredText(stmt.Connection(), "NarrowCodePage", "value") == UnicodeText);

        {
            stmt.Prepare(selectQuery);
            auto reader = stmt.Execute();
            REQUIRE(reader.FetchRow());
            CHECK(reader.GetColumn<std::string>(1) == narrowText);
        }

        stmt.Prepare(selectQuery);
        auto boundReader = stmt.Execute();
        auto boundText = std::string {};
        boundReader.BindOutputColumns(&boundText);
        REQUIRE(boundReader.FetchRow());
        CHECK(boundText == narrowText);
    }

    SECTION("std::string_view parameter")
    {
        stmt.Prepare(insertQuery);
        std::ignore = stmt.Execute(std::string_view { narrowText });
        CHECK(ReadStoredText(stmt.Connection(), "NarrowCodePage", "value") == UnicodeText);
    }

    SECTION("fixed-capacity string parameter and column")
    {
        stmt.Prepare(insertQuery);
        std::ignore = stmt.Execute(SqlAnsiString<16> { narrowText });
        CHECK(ReadStoredText(stmt.Connection(), "NarrowCodePage", "value") == UnicodeText);

        {
            stmt.Prepare(selectQuery);
            auto reader = stmt.Execute();
            REQUIRE(reader.FetchRow());
            CHECK(reader.GetColumn<SqlAnsiString<16>>(1).str() == narrowText);
        }

        stmt.Prepare(selectQuery);
        auto boundReader = stmt.Execute();
        auto boundText = SqlAnsiString<16> {};
        boundReader.BindOutputColumns(&boundText);
        REQUIRE(boundReader.FetchRow());
        CHECK(boundText.str() == narrowText);
    }

    SECTION("dynamic string parameter and column")
    {
        stmt.Prepare(insertQuery);
        std::ignore = stmt.Execute(SqlDynamicAnsiString<16> { narrowText });
        CHECK(ReadStoredText(stmt.Connection(), "NarrowCodePage", "value") == UnicodeText);

        stmt.Prepare(selectQuery);
        auto reader = stmt.Execute();
        REQUIRE(reader.FetchRow());
        CHECK(reader.GetColumn<SqlDynamicAnsiString<16>>(1).str() == narrowText);
    }

    SECTION("text stored as UTF-16 reads back in the process code page")
    {
        stmt.Prepare(insertQuery);
        std::ignore = stmt.Execute(std::u16string { UnicodeText });

        {
            stmt.Prepare(selectQuery);
            auto reader = stmt.Execute();
            REQUIRE(reader.FetchRow());
            CHECK(reader.GetColumn<std::string>(1) == narrowText);
        }
        {
            stmt.Prepare(selectQuery);
            auto reader = stmt.Execute();
            REQUIRE(reader.FetchRow());
            CHECK(reader.GetColumn<SqlAnsiString<16>>(1).str() == narrowText);
        }
        {
            stmt.Prepare(selectQuery);
            auto reader = stmt.Execute();
            REQUIRE(reader.FetchRow());
            CHECK(reader.GetColumn<SqlDynamicAnsiString<16>>(1).str() == narrowText);
        }

        stmt.Prepare(selectQuery);
        auto boundReader = stmt.Execute();
        auto boundText = std::string {};
        boundReader.BindOutputColumns(&boundText);
        REQUIRE(boundReader.FetchRow());
        CHECK(boundText == narrowText);
    }

    SECTION("column-wise native batch of fixed-capacity strings")
    {
        stmt.Prepare(insertQuery);
        auto const batch = std::array { SqlAnsiString<16> { narrowText }, SqlAnsiString<16> { narrowText } };
        std::ignore = stmt.ExecuteBatchNative(batch);

        auto cursor = stmt.ExecuteDirect(R"(SELECT "value" FROM "NarrowCodePage")");
        for ([[maybe_unused]] auto const& _: batch)
        {
            REQUIRE(cursor.FetchRow());
            CHECK(cursor.GetColumn<std::u16string>(1) == UnicodeText);
        }
    }
}

TEST_CASE_METHOD(SqlTestFixture,
                 "Narrow DataMapper fields use the process code page under a foreign thread locale",
                 "[DataMapper][Unicode]")
{
    // Only the SQLite driver converts narrow text with the thread code page.
    if (SqlConnection {}.ServerType() != SqlServerType::SQLITE)
        return;

    auto dm = DataMapper { ConnectWithDriverCodePage() };

    auto const narrowText = ToProcessCodePage(UnicodeText);
    auto const threadLocale = ScopedForeignThreadLocale {};

    SECTION("single-row create and query")
    {
        dm.CreateTable<NarrowTextCodePageRecord>();
        auto record = NarrowTextCodePageRecord {
            .id = 1, .text = narrowText, .fixedText = SqlAnsiString<16> { narrowText }, .dynamicText = narrowText
        };
        dm.CreateExplicit(record);

        CHECK(ReadStoredText(dm.Connection(), "NarrowTextCodePageRecord", "text") == UnicodeText);
        CHECK(ReadStoredText(dm.Connection(), "NarrowTextCodePageRecord", "fixedText") == UnicodeText);
        CHECK(ReadStoredText(dm.Connection(), "NarrowTextCodePageRecord", "dynamicText") == UnicodeText);

        auto const actual = dm.QuerySingle<NarrowTextCodePageRecord>(1);
        REQUIRE(actual.has_value());
        CHECK(actual->text.Value() == narrowText);
        CHECK(actual->fixedText.Value().str() == narrowText);
        CHECK(actual->dynamicText.Value().str() == narrowText);
    }

    SECTION("row-wise batch create and block fetch")
    {
        dm.CreateTable<NarrowTextCodePageFixedRecord>();
        auto const records = std::vector {
            NarrowTextCodePageFixedRecord { .id = 1, .fixedText = SqlAnsiString<16> { narrowText } },
            NarrowTextCodePageFixedRecord { .id = 2, .fixedText = SqlAnsiString<16> { narrowText } },
        };
        dm.CreateAll(records);

        auto stmt = SqlStatement { dm.Connection() };
        auto cursor = stmt.ExecuteDirect(R"(SELECT "fixedText" FROM "NarrowTextCodePageFixedRecord")");
        for ([[maybe_unused]] auto const& _: records)
        {
            REQUIRE(cursor.FetchRow());
            CHECK(cursor.GetColumn<std::u16string>(1) == UnicodeText);
        }

        auto const actual = dm.Query<NarrowTextCodePageFixedRecord>().All();
        REQUIRE(actual.size() == records.size());
        for (auto const& record: actual)
            CHECK(record.fixedText.Value().str() == narrowText);
    }

    SECTION("block fetch of text stored as UTF-16")
    {
        dm.CreateTable<NarrowTextCodePageFixedRecord>();
        auto stmt = SqlStatement { dm.Connection() };
        stmt.Prepare(
            stmt.Query("NarrowTextCodePageFixedRecord").Insert().Set("id", SqlWildcard).Set("fixedText", SqlWildcard));
        std::ignore = stmt.Execute(1, std::u16string { UnicodeText });
        std::ignore = stmt.Execute(2, std::u16string { UnicodeText });

        auto const actual = dm.Query<NarrowTextCodePageFixedRecord>().All();
        REQUIRE(actual.size() == 2);
        for (auto const& record: actual)
            CHECK(record.fixedText.Value().str() == narrowText);
    }
}

// NOLINTEND(bugprone-unchecked-optional-access)

#endif
