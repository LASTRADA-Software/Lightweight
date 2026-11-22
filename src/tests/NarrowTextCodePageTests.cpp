// SPDX-License-Identifier: Apache-2.0

#include "ForeignThreadLocale.hpp"
#include "Utils.hpp"

#include <Lightweight/Lightweight.hpp>

#include <catch2/catch_test_macros.hpp>

#if defined(_WIN32) || defined(_WIN64)
    #include <odbcinst.h>
#endif

#include <array>
#include <cstddef>
#include <format>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Narrow strings hold text in the process ANSI code page (GetACP()). The SQLite ODBC driver's OEMCP option converts
// SQL_C_CHAR data with the thread's ANSI code page instead, which differs whenever the user's format locale differs
// from the system locale. These tests run with such a thread locale and check that narrow text keeps its meaning.
#if defined(_WIN32) || defined(_WIN64)

using namespace std::string_literals;
using namespace Lightweight;
using namespace Lightweight::Tests;

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

/// A user data source for the test database through the SQLite ODBC driver, removed again on destruction.
class ScopedSqliteDataSource
{
  public:
    /// Adds the data source @p name; @p oemcp, when given, goes into its profile as the @c OEMCP option.
    ScopedSqliteDataSource(std::string name, std::optional<std::string_view> oemcp):
        _name { std::move(name) }
    {
        auto const options = ParseConnectionString(SqlConnection::DefaultConnectionString());
        auto driver = options.at("DRIVER");
        if (driver.starts_with('{') && driver.ends_with('}'))
            driver = driver.substr(1, driver.size() - 2);
        auto attributes = std::format("DSN={}", _name) + '\0' + std::format("Database={}", options.at("DATABASE")) + '\0';
        if (oemcp)
            attributes += std::format("OEMCP={}", *oemcp) + '\0';
        attributes += '\0';
        Remove();
        REQUIRE(SQLConfigDataSource(nullptr, ODBC_ADD_DSN, driver.c_str(), attributes.c_str()));
        _driver = std::move(driver);
    }

    ScopedSqliteDataSource(ScopedSqliteDataSource const&) = delete;
    ScopedSqliteDataSource(ScopedSqliteDataSource&&) = delete;
    ScopedSqliteDataSource& operator=(ScopedSqliteDataSource const&) = delete;
    ScopedSqliteDataSource& operator=(ScopedSqliteDataSource&&) = delete;

    ~ScopedSqliteDataSource()
    {
        Remove();
    }

    [[nodiscard]] std::string const& Name() const noexcept
    {
        return _name;
    }

  private:
    void Remove() const
    {
        auto const attributes = std::format("DSN={}", _name) + '\0' + '\0';
        // Removing a data source that does not exist fails, which is fine here.
        std::ignore = SQLConfigDataSource(
            nullptr, ODBC_REMOVE_DSN, _driver.empty() ? "SQLite3 ODBC Driver" : _driver.c_str(), attributes.c_str());
    }

    std::string _name;
    std::string _driver;
};

/// Connects to the data source @p name the data-source way (@c SQLConnect).
SqlConnection ConnectToDataSource(std::string const& name)
{
    auto connection = SqlConnection { std::nullopt };
    REQUIRE(connection.Connect(SqlConnectionDataSource { .datasource = name }));
    return connection;
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

TEST_CASE_METHOD(SqlTestFixture,
                 "The SQLite narrow-text mode follows the DSN profile and the data-source connect default",
                 "[SqlConnection][Unicode]")
{
    if (SqlConnection {}.ServerType() != SqlServerType::SQLITE)
        return;

    using enum detail::NarrowTextBinding;

    SECTION("OEMCP only in the profile")
    {
        auto const dataSource = ScopedSqliteDataSource { "LightweightTestOemcpOn", "1" };
        CHECK(SqlConnection { SqlConnectionString { "DSN=" + dataSource.Name() } }.NarrowTextBindingMode()
              == WideFromProcessCodePage);
        CHECK(ConnectToDataSource(dataSource.Name()).NarrowTextBindingMode() == WideFromProcessCodePage);
        CHECK(SqlConnection { SqlConnectionString { "DSN=" + dataSource.Name() + ";OEMCP=0" } }.NarrowTextBindingMode()
              == Narrow);
    }

    SECTION("OEMCP off in the profile")
    {
        auto const dataSource = ScopedSqliteDataSource { "LightweightTestOemcpOff", "0" };
        CHECK(ConnectToDataSource(dataSource.Name()).NarrowTextBindingMode() == Narrow);
        CHECK(SqlConnection { SqlConnectionString { "DSN=" + dataSource.Name() + ";OEMCP=1" } }.NarrowTextBindingMode()
              == WideFromProcessCodePage);
    }

    SECTION("OEMCP nowhere: on for a data-source connect, off for a connection string")
    {
        auto const dataSource = ScopedSqliteDataSource { "LightweightTestOemcpUnset", std::nullopt };
        CHECK(ConnectToDataSource(dataSource.Name()).NarrowTextBindingMode() == WideFromProcessCodePage);
        CHECK(SqlConnection { SqlConnectionString { "DSN=" + dataSource.Name() } }.NarrowTextBindingMode() == Narrow);
    }
}

TEST_CASE_METHOD(SqlTestFixture,
                 "Narrow text keeps its meaning on DSN-profile and data-source connections under a foreign thread locale",
                 "[SqlDataBinder][Unicode]")
{
    if (SqlConnection {}.ServerType() != SqlServerType::SQLITE)
        return;

    auto const narrowText = ToProcessCodePage(UnicodeText);
    auto const threadLocale = ScopedForeignThreadLocale {};

    auto const roundTrip = [&](SqlConnection& connection) {
        auto stmt = SqlStatement { connection };
        stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
            migration.CreateTable("NarrowDataSource").Column("value", SqlColumnTypeDefinitions::Varchar { .size = 32 });
        });
        stmt.Prepare(stmt.Query("NarrowDataSource").Insert().Set("value", SqlWildcard));
        std::ignore = stmt.Execute(narrowText);
        CHECK(ReadStoredText(connection, "NarrowDataSource", "value") == UnicodeText);
        CHECK(stmt.ExecuteDirectScalar<std::string>(R"(SELECT "value" FROM "NarrowDataSource")") == narrowText);
        std::ignore = stmt.ExecuteDirect(R"(DROP TABLE "NarrowDataSource")");
    };

    SECTION("DSN with OEMCP only in its profile")
    {
        auto const dataSource = ScopedSqliteDataSource { "LightweightTestOemcpOn", "1" };
        auto connection = SqlConnection { SqlConnectionString { "DSN=" + dataSource.Name() } };
        roundTrip(connection);
    }

    SECTION("data-source connect without OEMCP")
    {
        auto const dataSource = ScopedSqliteDataSource { "LightweightTestOemcpUnset", std::nullopt };
        auto connection = ConnectToDataSource(dataSource.Name());
        roundTrip(connection);
    }
}

TEST_CASE_METHOD(SqlTestFixture,
                 "A narrow char keeps its meaning under OEMCP and a foreign thread locale",
                 "[SqlDataBinder][Unicode]")
{
    if (SqlConnection {}.ServerType() != SqlServerType::SQLITE)
        return;

    constexpr auto unicodeCharacter = std::u16string_view { u"\u00DF" };
    auto const narrowCharacter = ToProcessCodePage(unicodeCharacter);
    REQUIRE(narrowCharacter.size() == 1);
    auto const threadLocale = ScopedForeignThreadLocale {};

    auto connection = ConnectWithDriverCodePage();
    auto stmt = SqlStatement { connection };
    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.CreateTable("NarrowChar").Column("value", SqlColumnTypeDefinitions::Char { .size = 1 });
    });

    SECTION("parameter, GetColumn and BindOutputColumns")
    {
        stmt.Prepare(stmt.Query("NarrowChar").Insert().Set("value", SqlWildcard));
        std::ignore = stmt.Execute(narrowCharacter.front());
        CHECK(ReadStoredText(connection, "NarrowChar", "value") == unicodeCharacter);

        {
            auto reader = stmt.ExecuteDirect(R"(SELECT "value" FROM "NarrowChar")");
            REQUIRE(reader.FetchRow());
            CHECK(reader.GetColumn<char>(1) == narrowCharacter.front());
        }

        stmt.Prepare(stmt.Query("NarrowChar").Select().Field("value").All());
        auto reader = stmt.Execute();
        auto boundCharacter = char {};
        reader.BindOutputColumns(&boundCharacter);
        REQUIRE(reader.FetchRow());
        CHECK(boundCharacter == narrowCharacter.front());
    }

    SECTION("column-wise native batch")
    {
        stmt.Prepare(stmt.Query("NarrowChar").Insert().Set("value", SqlWildcard));
        auto const batch = std::array { narrowCharacter.front(), narrowCharacter.front() };
        std::ignore = stmt.ExecuteBatchNative(batch);

        auto cursor = stmt.ExecuteDirect(R"(SELECT "value" FROM "NarrowChar")");
        for ([[maybe_unused]] auto const& _: batch)
        {
            REQUIRE(cursor.FetchRow());
            CHECK(cursor.GetColumn<std::u16string>(1) == unicodeCharacter);
        }
    }
}

namespace
{

constexpr int BoundOnceCodePageRowCount = 40;

/// Row @p i's text in the process code page: lengths vary up and down (0..30 characters), row 3 is empty,
/// every fifth row starts with non-ASCII text.
std::string BoundOnceCodePageText(int i)
{
    if (i == 3)
        return {};
    auto text = std::string(static_cast<std::size_t>((i * 7) % 31), static_cast<char>('a' + (i % 26)));
    if (i % 5 == 0)
        text.insert(0, ToProcessCodePage(UnicodeText));
    return text;
}

/// Row @p i's nullable text: NULL on every fourth row, otherwise the next row's text.
std::optional<std::string> BoundOnceCodePageMaybeText(int i)
{
    if (i % 4 == 1)
        return std::nullopt;
    return BoundOnceCodePageText(i + 1);
}

/// Row @p i's fixed-capacity text (1..16 bytes, some non-ASCII).
SqlAnsiString<20> BoundOnceCodePageFixed(int i)
{
    auto const prefix = i % 6 == 0 ? ToProcessCodePage(u"ü") : std::string {};
    return SqlAnsiString<20> { std::format("{}f{}", prefix, std::string(static_cast<std::size_t>((i * 3) % 15), 'z')) };
}

} // namespace

TEST_CASE_METHOD(SqlTestFixture,
                 "Narrow columns bound once keep each row's text under OEMCP and a foreign thread locale",
                 "[SqlDataBinder][Unicode][BindOnce]")
{
    if (SqlConnection {}.ServerType() != SqlServerType::SQLITE)
        return;

    auto connection = ConnectWithDriverCodePage();
    REQUIRE(connection.NarrowTextBindingMode() == detail::NarrowTextBinding::WideFromProcessCodePage);
    auto const threadLocale = ScopedForeignThreadLocale {};

    auto stmt = SqlStatement { connection };
    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.CreateTable("NarrowBoundOnce")
            .PrimaryKey("Id", SqlColumnTypeDefinitions::Integer {})
            .RequiredColumn("Text", SqlColumnTypeDefinitions::Varchar { .size = 64 })
            .Column("MaybeText", SqlColumnTypeDefinitions::Varchar { .size = 64 })
            .RequiredColumn("Fixed", SqlColumnTypeDefinitions::Varchar { .size = 20 });
    });
    stmt.Prepare(R"(INSERT INTO "NarrowBoundOnce" ("Id", "Text", "MaybeText", "Fixed") VALUES (?, ?, ?, ?))");
    for (auto const i: std::views::iota(0, BoundOnceCodePageRowCount))
        std::ignore = stmt.Execute(i, BoundOnceCodePageText(i), BoundOnceCodePageMaybeText(i), BoundOnceCodePageFixed(i));

    // Per-row SQLFetch with the binds issued right away, and with the binds deferred to the first fetch.
    for (auto const depth: std::array { std::size_t { 1 }, PrefetchDepthDefault })
    {
        INFO("prefetch depth " << depth);
        connection.SetDefaultPrefetchDepth(depth);

        auto id = 0;
        auto text = std::string {};
        auto maybeText = std::optional<std::string> {};
        auto fixed = SqlAnsiString<20> {};
        auto texts = std::vector<std::string> {};

        auto cursor =
            stmt.ExecuteDirect(R"(SELECT "Id", "Text", "MaybeText", "Fixed" FROM "NarrowBoundOnce" ORDER BY "Id")");
        cursor.BindOutputColumns(&id, &text, &maybeText, &fixed);
        auto rows = 0;
        while (cursor.FetchRow())
        {
            INFO("row " << rows);
            CHECK(id == rows);
            CHECK(text == BoundOnceCodePageText(rows));
            CHECK(maybeText == BoundOnceCodePageMaybeText(rows));
            CHECK(fixed == BoundOnceCodePageFixed(rows));
            // Taking the value out leaves the bound variable empty: the next row must still be copied into it
            // from the binder's staging buffer, and the value taken out must stay intact.
            texts.emplace_back(std::exchange(text, std::string {}));
            ++rows;
        }
        CHECK(rows == BoundOnceCodePageRowCount);
        REQUIRE(texts.size() == static_cast<std::size_t>(BoundOnceCodePageRowCount));
        for (auto const i: std::views::iota(0, BoundOnceCodePageRowCount))
            CHECK(texts[static_cast<std::size_t>(i)] == BoundOnceCodePageText(i));
    }
}

// NOLINTEND(bugprone-unchecked-optional-access)

#endif
