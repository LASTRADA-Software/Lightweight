// SPDX-License-Identifier: Apache-2.0

#include "Utils.hpp"

#include <Lightweight/DataBinder/SqlBinary.hpp>
#include <Lightweight/Lightweight.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <format>
#include <optional>
#include <ranges>
#include <string>
#include <tuple>
#include <vector>

using namespace Lightweight;
using namespace std::string_view_literals;
using namespace std::chrono_literals;

namespace
{

// Counts the transparent block-prefetch signals so a test can prove that N rows collapsed into far
// fewer SQLFetchScroll round-trips (OnFetchBlock) than per-row SQLFetch calls.
struct PrefetchProbe: SqlLogger::Null
{
    std::size_t blockFetches = 0; // SQLFetchScroll round-trips (incl. the terminating empty fetch)
    std::size_t logicalRows = 0;  // rows handed back to the caller

    void OnFetchBlock(std::size_t /*rowsInBlock*/) override
    {
        ++blockFetches;
    }
    void OnFetchRow() override
    {
        ++logicalRows;
    }
};

// RAII swap of the active SqlLogger (restores the previous one on scope exit).
struct LoggerSwap
{
    SqlLogger* previous;
    explicit LoggerSwap(SqlLogger& replacement):
        previous { &SqlLogger::GetLogger() }
    {
        SqlLogger::SetLogger(replacement);
    }
    ~LoggerSwap()
    {
        SqlLogger::SetLogger(*previous);
    }
    LoggerSwap(LoggerSwap const&) = delete;
    LoggerSwap& operator=(LoggerSwap const&) = delete;
    LoggerSwap(LoggerSwap&&) = delete;
    LoggerSwap& operator=(LoggerSwap&&) = delete;
};

constexpr std::size_t TestPrefetchDepth = 64; // small so tests stay fast yet cross block boundaries

void CreateNumericTable(SqlStatement& stmt)
{
    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.CreateTable("Prefetch")
            .PrimaryKey("Id", SqlColumnTypeDefinitions::Bigint {})
            .Column("Big", SqlColumnTypeDefinitions::Bigint {})
            .Column("Ratio", SqlColumnTypeDefinitions::Real {})
            .Column("Mid", SqlColumnTypeDefinitions::Integer {});
    });
}

// Inserts @p rowCount rows of deterministic values into the numeric table.
void FillNumericTable(SqlStatement& stmt, std::size_t rowCount)
{
    stmt.Prepare(stmt.Query("Prefetch")
                     .Insert()
                     .Set("Id", SqlWildcard)
                     .Set("Big", SqlWildcard)
                     .Set("Ratio", SqlWildcard)
                     .Set("Mid", SqlWildcard));
    for (auto const i: std::views::iota(std::size_t { 1 }, rowCount + 1))
    {
        (void) stmt.Execute(static_cast<std::int64_t>(i),
                            static_cast<std::int64_t>(i) * 1000,
                            1.5 * static_cast<double>(i),
                            static_cast<int>(i % 97));
    }
}

constexpr auto kNumericSelect = R"(SELECT "Id", "Big", "Ratio", "Mid" FROM "Prefetch" ORDER BY "Id")"sv;

struct NumericRow
{
    std::int64_t id = 0;
    std::int64_t big = 0;
    double ratio = 0.0;
    int mid = 0;
    bool operator==(NumericRow const&) const = default;
};

std::vector<NumericRow> ReadNumeric(SqlStatement& stmt)
{
    std::vector<NumericRow> rows;
    auto cursor = stmt.ExecuteDirect(std::string { kNumericSelect });
    while (cursor.FetchRow())
        rows.push_back(NumericRow {
            .id = cursor.GetColumn<std::int64_t>(1),
            .big = cursor.GetColumn<std::int64_t>(2),
            .ratio = cursor.GetColumn<double>(3),
            .mid = cursor.GetColumn<int>(4),
        });
    return rows;
}

// A statement whose connection has prefetch disabled — the trusted per-row SQLGetData reference.
SqlStatement MakePerRowStatement()
{
    auto stmt = SqlStatement {};
    stmt.Connection().SetDefaultPrefetchDepth(1);
    return stmt;
}

// Strips the server's CHAR(N)/NCHAR(N) right-padding (trailing ASCII spaces) so a fixed-width
// column's value compares equal regardless of how much padding the driver returned it with.
std::optional<std::string> TrimRight(std::optional<std::string> value)
{
    if (!value.has_value())
        return std::nullopt;
    while (!value->empty() && value->back() == ' ')
        value->pop_back();
    return value;
}

} // namespace

TEST_CASE_METHOD(SqlTestFixture, "Prefetch: raw GetColumn loop collapses round-trips", "[prefetch]")
{
    constexpr std::size_t rowCount = (2 * TestPrefetchDepth) + 7; // crosses two block boundaries

    auto stmt = SqlStatement {};
    stmt.Connection().SetDefaultPrefetchDepth(TestPrefetchDepth);
    CreateNumericTable(stmt);
    FillNumericTable(stmt, rowCount);

    auto const expected = [&] {
        auto reference = MakePerRowStatement();
        return ReadNumeric(reference);
    }();
    REQUIRE(expected.size() == rowCount);

    PrefetchProbe probe;
    std::vector<NumericRow> actual;
    {
        LoggerSwap const swap { probe };
        actual = ReadNumeric(stmt);
    }

    CHECK(actual == expected);
    CHECK(probe.logicalRows == rowCount);
    // ceil(rowCount / depth) data blocks plus one terminating empty SQLFetchScroll.
    auto const expectedDataBlocks = (rowCount + TestPrefetchDepth - 1) / TestPrefetchDepth;
    CHECK(probe.blockFetches == expectedDataBlocks + 1);
    CHECK(probe.blockFetches < rowCount); // proves batching: far fewer round-trips than rows
}

TEST_CASE_METHOD(SqlTestFixture, "Prefetch: numeric columns read as text render their value (cross-type)", "[prefetch]")
{
    // A generic "print every column as a string" loop (e.g. dbtool's `exec`) reads numeric columns via
    // GetColumn<std::string>. On the per-row path the driver converts the value to text; the prefetch
    // path must do the same rather than yield an empty string. Integer text is exact; the double is only
    // checked for non-emptiness because its textual form is not portably defined.
    auto stmt = SqlStatement {};
    stmt.Connection().SetDefaultPrefetchDepth(TestPrefetchDepth);
    CreateNumericTable(stmt);
    constexpr std::size_t rowCount = TestPrefetchDepth + 5;
    FillNumericTable(stmt, rowCount);

    PrefetchProbe probe;
    std::size_t seen = 0;
    {
        LoggerSwap const swap { probe };
        auto cursor = stmt.ExecuteDirect(std::string { kNumericSelect });
        while (cursor.FetchRow())
        {
            auto const rowNumber = static_cast<std::int64_t>(seen) + 1;
            CHECK(cursor.GetColumn<std::string>(1) == std::format("{}", rowNumber));        // Id (exact)
            CHECK(cursor.GetColumn<std::string>(2) == std::format("{}", rowNumber * 1000)); // Big (exact)
            CHECK_FALSE(cursor.GetColumn<std::string>(3).empty());                          // Ratio (double)
            CHECK(cursor.GetColumn<std::string>(4) == std::format("{}", rowNumber % 97));   // Mid (exact)
            ++seen;
        }
    }
    REQUIRE(seen == rowCount);
    CHECK(probe.blockFetches >= 1); // the all-numeric result set is prefetched
}

TEST_CASE_METHOD(SqlTestFixture, "Prefetch: bound output columns with NULLs and optionals", "[prefetch]")
{
    auto stmt = SqlStatement {};
    stmt.Connection().SetDefaultPrefetchDepth(TestPrefetchDepth);
    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.CreateTable("PrefetchOpt")
            .PrimaryKey("Id", SqlColumnTypeDefinitions::Bigint {})
            .Column("MaybeInt", SqlColumnTypeDefinitions::Integer {});
    });
    // MaybeInt nullable: every 5th row is NULL.
    stmt.Prepare(stmt.Query("PrefetchOpt").Insert().Set("Id", SqlWildcard).Set("MaybeInt", SqlWildcard));
    constexpr std::size_t rowCount = TestPrefetchDepth + 10;
    for (auto const i: std::views::iota(std::size_t { 1 }, rowCount + 1))
    {
        auto const maybe = i % 5 == 0 ? std::optional<int> {} : std::optional<int> { static_cast<int>(i) };
        (void) stmt.Execute(static_cast<std::int64_t>(i), maybe);
    }

    PrefetchProbe probe;
    std::vector<std::pair<std::int64_t, std::optional<int>>> actual;
    {
        LoggerSwap const swap { probe };
        auto cursor = stmt.ExecuteDirect(R"(SELECT "Id", "MaybeInt" FROM "PrefetchOpt" ORDER BY "Id")"sv);

        std::int64_t id = 0;
        std::optional<int> maybeInt;
        cursor.BindOutputColumns(&id, &maybeInt);
        while (cursor.FetchRow())
        {
            actual.emplace_back(id, maybeInt);
            // The optional rebind idiom from docs/best-practices.md — must stay idempotent under prefetch.
            cursor.BindOutputColumns(&id, &maybeInt);
        }
    }

    REQUIRE(actual.size() == rowCount);
    CHECK(probe.blockFetches >= 2); // crossed at least one block boundary
    for (auto const i: std::views::iota(std::size_t { 0 }, rowCount))
    {
        auto const rowNumber = static_cast<std::int64_t>(i) + 1;
        CHECK(actual[i].first == rowNumber);
        if (rowNumber % 5 == 0)
            CHECK_FALSE(actual[i].second.has_value());
        else
            CHECK(actual[i].second == static_cast<int>(rowNumber));
    }
}

TEST_CASE_METHOD(SqlTestFixture, "Prefetch: text + temporal result set stays correct on per-row fallback", "[prefetch]")
{
    auto stmt = SqlStatement {};
    stmt.Connection().SetDefaultPrefetchDepth(TestPrefetchDepth);

    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.CreateTable("PrefetchMixed")
            .PrimaryKey("Id", SqlColumnTypeDefinitions::Bigint {})
            .Column("Name", SqlColumnTypeDefinitions::Varchar { 64 })
            .Column("When", SqlColumnTypeDefinitions::DateTime {});
    });

    constexpr std::size_t rowCount = TestPrefetchDepth + 5;
    auto const baseDate = SqlDateTime { 2026y, std::chrono::January, 1d, 12h, 0min, 0s };
    stmt.Prepare(
        stmt.Query("PrefetchMixed").Insert().Set("Id", SqlWildcard).Set("Name", SqlWildcard).Set("When", SqlWildcard));
    for (auto const i: std::views::iota(std::size_t { 1 }, rowCount + 1))
        // Mix ASCII and multibyte UTF-8 so the narrow/wide round-trip is exercised on every backend.
        (void) stmt.Execute(static_cast<std::int64_t>(i), std::format("{}-Grüße-{}", "näme", i), baseDate);

    auto readAll = [&](SqlStatement& source) {
        std::vector<std::tuple<std::int64_t, std::string, SqlDateTime>> rows;
        auto cursor = source.ExecuteDirect(R"(SELECT "Id", "Name", "When" FROM "PrefetchMixed" ORDER BY "Id")"sv);
        while (cursor.FetchRow())
        {
            // Read columns into sequenced locals in ascending order: on the per-row path MS SQL's driver
            // requires SQLGetData on unbound columns strictly left-to-right, but the evaluation order of
            // function arguments is unspecified. (The prefetch path reads from the buffer, order-free.)
            auto const id = cursor.GetColumn<std::int64_t>(1);
            auto name = cursor.GetColumn<std::string>(2);
            auto const when = cursor.GetColumn<SqlDateTime>(3);
            rows.emplace_back(id, std::move(name), when);
        }
        return rows;
    };

    auto const expected = [&] {
        auto reference = MakePerRowStatement();
        return readAll(reference);
    }();

    PrefetchProbe probe;
    std::remove_const_t<decltype(expected)> actual;
    {
        LoggerSwap const swap { probe };
        actual = readAll(stmt);
    }

    CHECK(actual == expected);
    REQUIRE(actual.size() == rowCount);
    // Character columns are not on the prefetch allowlist (faithful block reconstruction of text is not
    // achievable uniformly across backends — see PrefetchableSqlType), so a result set carrying one keeps
    // the per-row path. Correctness still holds, including the multibyte UTF-8 values.
    CHECK(probe.blockFetches == 0);
}

TEST_CASE_METHOD(SqlTestFixture, "Prefetch: SqlRowIterator and SqlVariantRowCursor inherit prefetch", "[prefetch]")
{
    constexpr std::size_t rowCount = TestPrefetchDepth + 11;

    auto stmt = SqlStatement {};
    stmt.Connection().SetDefaultPrefetchDepth(TestPrefetchDepth);
    CreateNumericTable(stmt);
    FillNumericTable(stmt, rowCount);

    SECTION("SqlVariantRowCursor")
    {
        // Compare prefetch vs per-row variants via their string form (alternative + value must match).
        auto toStrings = [](SqlStatement& source) {
            std::vector<std::string> out;
            auto cursor = source.ExecuteDirect(std::string { kNumericSelect });
            for (auto const& row: SqlVariantRowCursor { std::move(cursor) })
            {
                std::string line;
                for (auto const& cell: row)
                    line += cell.ToString() + "|";
                out.push_back(std::move(line));
            }
            return out;
        };

        auto const expected = [&] {
            auto reference = MakePerRowStatement();
            return toStrings(reference);
        }();

        PrefetchProbe probe;
        std::vector<std::string> actual;
        {
            LoggerSwap const swap { probe };
            actual = toStrings(stmt);
        }
        CHECK(actual == expected);
        REQUIRE(actual.size() == rowCount);
        CHECK(probe.blockFetches >= 2);
    }
}

TEST_CASE_METHOD(SqlTestFixture, "Prefetch: empty result set", "[prefetch]")
{
    auto stmt = SqlStatement {};
    stmt.Connection().SetDefaultPrefetchDepth(TestPrefetchDepth);
    CreateNumericTable(stmt);

    PrefetchProbe probe;
    {
        LoggerSwap const swap { probe };
        auto cursor = stmt.ExecuteDirect(std::string { kNumericSelect });
        CHECK_FALSE(cursor.FetchRow());
    }
    CHECK(probe.logicalRows == 0);
}

TEST_CASE_METHOD(SqlTestFixture, "Prefetch: SetDefaultPrefetchDepth(1) disables prefetch", "[prefetch]")
{
    constexpr std::size_t rowCount = TestPrefetchDepth + 3;

    auto stmt = SqlStatement {};
    stmt.Connection().SetDefaultPrefetchDepth(1);
    CreateNumericTable(stmt);
    FillNumericTable(stmt, rowCount);

    PrefetchProbe probe;
    std::vector<NumericRow> actual;
    {
        LoggerSwap const swap { probe };
        actual = ReadNumeric(stmt);
    }
    REQUIRE(actual.size() == rowCount);
    CHECK(probe.blockFetches == 0); // per-row SQLFetch path — no block fetches
    CHECK(probe.logicalRows == rowCount);
}

TEST_CASE_METHOD(SqlTestFixture, "Prefetch: ineligible column types fall back to per-row", "[prefetch]")
{
    auto stmt = SqlStatement {};
    stmt.Connection().SetDefaultPrefetchDepth(TestPrefetchDepth);

    // A binary column maps to SQL_*BINARY, which is not on the prefetch allowlist, so the whole result
    // set must keep the per-row path while still producing correct values for the other columns.
    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.CreateTable("PrefetchBinary")
            .PrimaryKey("Id", SqlColumnTypeDefinitions::Bigint {})
            .Column("Payload", SqlColumnTypeDefinitions::VarBinary { 64 });
    });

    constexpr std::size_t rowCount = TestPrefetchDepth + 2;
    stmt.Prepare(stmt.Query("PrefetchBinary").Insert().Set("Id", SqlWildcard).Set("Payload", SqlWildcard));
    for (auto const i: std::views::iota(std::size_t { 1 }, rowCount + 1))
        (void) stmt.Execute(static_cast<std::int64_t>(i), SqlBinary { 0x01, 0x02 });

    PrefetchProbe probe;
    std::vector<std::int64_t> ids;
    {
        LoggerSwap const swap { probe };
        auto cursor = stmt.ExecuteDirect(R"(SELECT "Id", "Payload" FROM "PrefetchBinary" ORDER BY "Id")"sv);
        while (cursor.FetchRow())
            ids.push_back(cursor.GetColumn<std::int64_t>(1));
    }
    REQUIRE(ids.size() == rowCount);
    CHECK(probe.blockFetches == 0); // the binary column forced the per-row fallback
}

TEST_CASE_METHOD(SqlTestFixture, "Prefetch: statement is reusable after a prefetched fetch", "[prefetch]")
{
    constexpr std::size_t rowCount = TestPrefetchDepth + 4;

    auto stmt = SqlStatement {};
    stmt.Connection().SetDefaultPrefetchDepth(TestPrefetchDepth);
    CreateNumericTable(stmt);
    FillNumericTable(stmt, rowCount);

    auto const first = ReadNumeric(stmt);
    REQUIRE(first.size() == rowCount);

    // Re-run on the same statement: the prefetch state must have been restored to single-row defaults.
    auto const second = ReadNumeric(stmt);
    CHECK(second == first);

    // A different query on the same statement must also work.
    auto cursor = stmt.ExecuteDirect(R"(SELECT COUNT(*) FROM "Prefetch")"sv);
    REQUIRE(cursor.FetchRow());
    CHECK(cursor.GetColumn<std::int64_t>(1) == static_cast<std::int64_t>(rowCount));
}

TEST_CASE_METHOD(SqlTestFixture, "Prefetch: string, fixed-string, wide and nullable text round-trip", "[prefetch]")
{
    auto stmt = SqlStatement {};
    stmt.Connection().SetDefaultPrefetchDepth(TestPrefetchDepth);

    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.CreateTable("PrefetchStr")
            .PrimaryKey("Id", SqlColumnTypeDefinitions::Bigint {})
            .Column("Name", SqlColumnTypeDefinitions::Varchar { 32 })  // narrow text (multibyte UTF-8)
            .Column("Wide", SqlColumnTypeDefinitions::NVarchar { 32 }) // wide (UTF-16) text
            .Column("Tag", SqlColumnTypeDefinitions::Char { 8 })       // CHAR(N): right-padded -> trim parity
            .Column("Note", SqlColumnTypeDefinitions::Varchar { 16 }); // nullable text
    });

    constexpr std::size_t rowCount = TestPrefetchDepth + 6;
    stmt.Prepare(stmt.Query("PrefetchStr")
                     .Insert()
                     .Set("Id", SqlWildcard)
                     .Set("Name", SqlWildcard)
                     .Set("Wide", SqlWildcard)
                     .Set("Tag", SqlWildcard)
                     .Set("Note", SqlWildcard));
    for (auto const i: std::views::iota(std::size_t { 1 }, rowCount + 1))
    {
        auto const name = [i] {
            if (i % 3 == 0)
                return std::format("Grüße-{}", i);
            if (i % 3 == 1)
                return std::string {};
            return std::format("row-{}", i);
        }();
        auto const note = i % 7 == 0 ? std::optional<std::string> {} : std::optional<std::string> { std::format("n{}", i) };
        (void) stmt.Execute(static_cast<std::int64_t>(i),
                            name,
                            std::format("wíde-{}", i),
                            std::format("T{}", i % 100), // short value -> server right-pads to CHAR(8)
                            note);
    }

    // SqlTrimmedFixedString trims the CHAR(N) padding (FIXED_SIZE_RIGHT_TRIMMED); the prefetch path must
    // reproduce that trim exactly. std::u16string exercises the wide round-trip; optional<std::string>
    // the NULL path. Comparing against the per-row reference proves byte-identical reconstruction.
    using Row =
        std::tuple<std::int64_t, SqlAnsiString<32>, std::u16string, SqlTrimmedFixedString<8>, std::optional<std::string>>;
    auto readRows = [&](SqlStatement& source) {
        std::vector<Row> rows;
        auto cursor =
            source.ExecuteDirect(R"(SELECT "Id", "Name", "Wide", "Tag", "Note" FROM "PrefetchStr" ORDER BY "Id")"sv);
        while (cursor.FetchRow())
        {
            // Sequenced ascending reads (MS SQL requires per-row SQLGetData in column order).
            auto const id = cursor.GetColumn<std::int64_t>(1);
            auto name = cursor.GetColumn<SqlAnsiString<32>>(2);
            auto wide = cursor.GetColumn<std::u16string>(3);
            auto tag = cursor.GetColumn<SqlTrimmedFixedString<8>>(4);
            auto note = cursor.GetNullableColumn<std::string>(5);
            // name (SqlAnsiString<32>) and tag (SqlTrimmedFixedString<8>) are trivially copyable, so they
            // are passed by value; only the heap-backed wide string and optional are moved.
            rows.emplace_back(id, name, std::move(wide), tag, std::move(note));
        }
        return rows;
    };

    auto const expected = [&] {
        auto reference = MakePerRowStatement();
        return readRows(reference);
    }();

    std::vector<Row> actual;
    {
        PrefetchProbe probe;
        LoggerSwap const swap { probe };
        actual = readRows(stmt);
    }
    // Byte-identical to the trusted per-row path on every backend (prefetched where the driver allows,
    // per-row where a column — e.g. narrow CHAR on PostgreSQL — opts out; correctness holds regardless).
    CHECK(actual == expected);
    REQUIRE(actual.size() == rowCount);
}

TEST_CASE_METHOD(SqlTestFixture, "Prefetch: GUID column round-trips", "[prefetch]")
{
    auto stmt = SqlStatement {};
    stmt.Connection().SetDefaultPrefetchDepth(TestPrefetchDepth);
    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.CreateTable("PrefetchGuid")
            .PrimaryKey("Id", SqlColumnTypeDefinitions::Bigint {})
            .Column("Uid", SqlColumnTypeDefinitions::Guid {});
    });

    constexpr std::size_t rowCount = TestPrefetchDepth + 3;
    stmt.Prepare(stmt.Query("PrefetchGuid").Insert().Set("Id", SqlWildcard).Set("Uid", SqlWildcard));
    for (auto const i: std::views::iota(std::size_t { 1 }, rowCount + 1))
    {
        auto const parsedUid = SqlGuid::TryParse(std::format("12345678-1234-1234-1234-{:012}", i));
        REQUIRE(parsedUid.has_value());
        if (parsedUid.has_value()) // explicit guard so the optional access is statically checked
            (void) stmt.Execute(static_cast<std::int64_t>(i), *parsedUid);
    }

    auto readGuids = [&](SqlStatement& source) {
        std::vector<std::pair<std::int64_t, SqlGuid>> rows;
        auto cursor = source.ExecuteDirect(R"(SELECT "Id", "Uid" FROM "PrefetchGuid" ORDER BY "Id")"sv);
        while (cursor.FetchRow())
        {
            auto const id = cursor.GetColumn<std::int64_t>(1);
            auto const uid = cursor.GetColumn<SqlGuid>(2);
            rows.emplace_back(id, uid);
        }
        return rows;
    };

    auto const expected = [&] {
        auto reference = MakePerRowStatement();
        return readGuids(reference);
    }();
    std::vector<std::pair<std::int64_t, SqlGuid>> actual;
    {
        PrefetchProbe probe;
        LoggerSwap const swap { probe };
        actual = readGuids(stmt);
    }
    CHECK(actual == expected);
    REQUIRE(actual.size() == rowCount);
}

TEST_CASE_METHOD(SqlTestFixture, "Prefetch: RowArrayCursor bulk-reads fixed-width CHAR/NCHAR columns", "[prefetch]")
{
    // Fixed-width text (CHAR(N)/NCHAR(N)) is excluded from the transparent automatic prefetch gate
    // (PrefetchableSqlType in SqlStatement.cpp) and always falls back to per-row SQLGetData there.
    // RowArrayCursor — the actual bulk block-fetch mechanism reachable via ExecuteBatchFetch — binds
    // and reads fixed-width text directly, independent of that gate. This pins that a bulk fetch of
    // CHAR(N)/NCHAR(N) columns, including NULLs and the server's right-padding, is byte-identical to
    // the trusted per-row reference.
    auto stmt = SqlStatement {};

    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.CreateTable("PrefetchFixedWidth")
            .PrimaryKey("Id", SqlColumnTypeDefinitions::Bigint {})
            .Column("Code", SqlColumnTypeDefinitions::Char { 8 })       // narrow fixed-width, right-padded
            .Column("WideCode", SqlColumnTypeDefinitions::NChar { 8 }); // wide fixed-width, right-padded
    });

    constexpr std::size_t rowCount = TestPrefetchDepth + 6;
    stmt.Prepare(stmt.Query("PrefetchFixedWidth")
                     .Insert()
                     .Set("Id", SqlWildcard)
                     .Set("Code", SqlWildcard)
                     .Set("WideCode", SqlWildcard));
    for (auto const i: std::views::iota(std::size_t { 1 }, rowCount + 1))
    {
        // Every 4th row is NULL in both fixed-width columns; the rest carry a short value shorter
        // than the column's declared width, so the server's right-padding is actually exercised.
        if (i % 4 == 0)
            (void) stmt.Execute(static_cast<std::int64_t>(i), std::optional<std::string> {}, std::optional<std::string> {});
        else
            (void) stmt.Execute(static_cast<std::int64_t>(i),
                                std::optional<std::string> { std::format("C{}", i % 100) },
                                std::optional<std::string> { std::format("W{}", i % 100) });
    }

    // Trusted reference: the classic per-row SQLGetData path. SqlTrimmedFixedString strips the
    // server's CHAR(N) right-padding on the narrow column; the wide column is read as plain
    // std::string (already-proven UTF-16 -> UTF-8 conversion) and trimmed manually here to mirror
    // RowArrayCursor::GetString's own trailing-space trim below, since SqlTrimmedWideFixedString's
    // GetColumn does not currently apply its trim (a separate, pre-existing gap outside this test's
    // scope — see SqlDataBinder<Utf16StringType>::GetColumn in BasicStringBinder.hpp).
    using Row = std::tuple<std::int64_t, std::optional<std::string>, std::optional<std::string>>;
    auto readPerRow = [&]() {
        std::vector<Row> rows;
        auto reference = MakePerRowStatement();
        auto cursor =
            reference.ExecuteDirect(R"(SELECT "Id", "Code", "WideCode" FROM "PrefetchFixedWidth" ORDER BY "Id")"sv);
        while (cursor.FetchRow())
        {
            auto const id = cursor.GetColumn<std::int64_t>(1);
            auto const code = cursor.GetNullableColumn<SqlTrimmedFixedString<8>>(2);
            auto const wideCode = TrimRight(cursor.GetNullableColumn<std::string>(3));
            rows.emplace_back(
                id, code.has_value() ? std::optional<std::string> { code->ToStringView() } : std::nullopt, wideCode);
        }
        return rows;
    };
    auto const expected = readPerRow();
    REQUIRE(expected.size() == rowCount);

    // RowArrayCursor directly — the bulk block-fetch mechanism under test.
    std::vector<Row> actual;
    {
        auto cursor = stmt.ExecuteBatchFetch(R"(SELECT "Id", "Code", "WideCode" FROM "PrefetchFixedWidth" ORDER BY "Id")"sv,
                                             TestPrefetchDepth);

        // Confirm the columns actually bound as text (Char/WChar), not silently as something else.
        // Which of Char/WChar a driver picks for a given declared SQL type is driver-specific and does
        // not affect correctness (RowArrayCursor::GetString normalizes both to UTF-8) — e.g. SQLite3's
        // ODBC driver reports CHAR(N) as WChar on Windows but as Char on Linux, and NCHAR(N) the other
        // way around on Linux vs Windows. Accept either for both columns; only the byte-identity check
        // below actually matters.
        auto const isTextlike = [](RowArrayCursor::BoundType type) {
            return type == RowArrayCursor::BoundType::Char || type == RowArrayCursor::BoundType::WChar;
        };
        CHECK(isTextlike(cursor.ColumnBoundType(2)));
        CHECK(isTextlike(cursor.ColumnBoundType(3)));

        std::size_t blocks = 0;
        for (auto rowsInBlock = cursor.FetchArray(); rowsInBlock > 0; rowsInBlock = cursor.FetchArray())
        {
            ++blocks;
            for (auto const row: std::views::iota(std::size_t { 0 }, rowsInBlock))
            {
                auto const id = cursor.GetI64(row, 1).value();
                // Trim the CHAR(8)/NCHAR(8) driver padding to compare against the trimmed reference.
                auto code = TrimRight(cursor.GetString(row, 2));
                auto wideCode = TrimRight(cursor.GetString(row, 3));
                actual.emplace_back(id, std::move(code), std::move(wideCode));
            }
        }
        CHECK(blocks >= 2); // crossed at least one block boundary — genuinely block-fetched, not one row
    }

    REQUIRE(actual.size() == rowCount);
    CHECK(actual == expected); // byte-identical to the trusted per-row path, including NULLs and padding
}

TEST_CASE_METHOD(SqlTestFixture,
                 "Prefetch: RowArrayCursor bulk-reads a fixed-width value that fills its full capacity",
                 "[prefetch]")
{
    // Regression sibling to #485 (BasicStringBinder::OutputColumn off-by-one on BufferLength) for the
    // RowArrayCursor bulk path: a value with NO trailing padding — it fills CHAR(N)/NCHAR(N) exactly —
    // must not lose its last character to a buffer sized for the terminator alone.
    auto stmt = SqlStatement {};

    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.CreateTable("PrefetchFixedFull")
            .PrimaryKey("Id", SqlColumnTypeDefinitions::Bigint {})
            .Column("Code", SqlColumnTypeDefinitions::Char { 8 })
            .Column("WideCode", SqlColumnTypeDefinitions::NChar { 8 });
    });

    constexpr std::size_t rowCount = TestPrefetchDepth + 6;
    stmt.Prepare(stmt.Query("PrefetchFixedFull")
                     .Insert()
                     .Set("Id", SqlWildcard)
                     .Set("Code", SqlWildcard)
                     .Set("WideCode", SqlWildcard));
    for (auto const i: std::views::iota(std::size_t { 1 }, rowCount + 1))
    {
        // Every value is exactly 8 chars — fills CHAR(8)/NCHAR(8) with no padding at all.
        auto const code = std::format("{:08}", i % 100'000'000);
        (void) stmt.Execute(static_cast<std::int64_t>(i), code, code);
    }

    auto readPerRow = [&]() {
        std::vector<std::tuple<std::int64_t, std::string, std::string>> rows;
        auto reference = MakePerRowStatement();
        auto cursor = reference.ExecuteDirect(R"(SELECT "Id", "Code", "WideCode" FROM "PrefetchFixedFull" ORDER BY "Id")"sv);
        while (cursor.FetchRow())
        {
            auto const id = cursor.GetColumn<std::int64_t>(1);
            auto const code = cursor.GetColumn<SqlTrimmedFixedString<8>>(2);
            auto const wideCode = *TrimRight(cursor.GetColumn<std::string>(3));
            rows.emplace_back(id, std::string { code.ToStringView() }, wideCode);
        }
        return rows;
    };
    auto const expected = readPerRow();
    REQUIRE(expected.size() == rowCount);

    std::vector<std::tuple<std::int64_t, std::string, std::string>> actual;
    {
        auto cursor = stmt.ExecuteBatchFetch(R"(SELECT "Id", "Code", "WideCode" FROM "PrefetchFixedFull" ORDER BY "Id")"sv,
                                             TestPrefetchDepth);
        for (auto rowsInBlock = cursor.FetchArray(); rowsInBlock > 0; rowsInBlock = cursor.FetchArray())
            for (auto const row: std::views::iota(std::size_t { 0 }, rowsInBlock))
                actual.emplace_back(
                    cursor.GetI64(row, 1).value(), cursor.GetString(row, 2).value(), cursor.GetString(row, 3).value());
    }

    REQUIRE(actual.size() == rowCount);
    for (std::size_t i = 0; i < rowCount; ++i)
    {
        CHECK(std::get<0>(actual[i]) == std::get<0>(expected[i]));
        CHECK(std::get<1>(actual[i]) == std::get<1>(expected[i])); // full 8 chars, not truncated to 7
        CHECK(std::get<2>(actual[i]) == std::get<2>(expected[i]));
        CHECK(std::get<1>(actual[i]).size() == 8);
        CHECK(std::get<2>(actual[i]).size() == 8);
    }
}

TEST_CASE_METHOD(SqlTestFixture,
                 "Prefetch: RowArrayCursor distinguishes NULL from empty string in fixed-width columns",
                 "[prefetch]")
{
    // NULL and "" are distinct SQL values; a bulk fetch must not collapse one into the other (e.g. by
    // treating an all-blank buffer as NULL, or a NULL indicator as an empty-but-non-null string).
    auto stmt = SqlStatement {};

    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.CreateTable("PrefetchFixedEmptyVsNull")
            .PrimaryKey("Id", SqlColumnTypeDefinitions::Bigint {})
            .Column("Code", SqlColumnTypeDefinitions::Char { 8 })
            .Column("WideCode", SqlColumnTypeDefinitions::NChar { 8 });
    });

    constexpr std::size_t rowCount = TestPrefetchDepth + 6;
    stmt.Prepare(stmt.Query("PrefetchFixedEmptyVsNull")
                     .Insert()
                     .Set("Id", SqlWildcard)
                     .Set("Code", SqlWildcard)
                     .Set("WideCode", SqlWildcard));
    for (auto const i: std::views::iota(std::size_t { 1 }, rowCount + 1))
    {
        // Every 3rd row is NULL, every other 3rd row is an empty string, the rest carry a short value.
        if (i % 3 == 0)
            (void) stmt.Execute(static_cast<std::int64_t>(i), std::optional<std::string> {}, std::optional<std::string> {});
        else if (i % 3 == 1)
            (void) stmt.Execute(static_cast<std::int64_t>(i), std::string {}, std::string {});
        else
            (void) stmt.Execute(static_cast<std::int64_t>(i), std::format("C{}", i), std::format("W{}", i));
    }

    auto cursor = stmt.ExecuteBatchFetch(
        R"(SELECT "Id", "Code", "WideCode" FROM "PrefetchFixedEmptyVsNull" ORDER BY "Id")"sv, TestPrefetchDepth);

    std::size_t rowsSeen = 0;
    for (auto rowsInBlock = cursor.FetchArray(); rowsInBlock > 0; rowsInBlock = cursor.FetchArray())
    {
        for (auto const row: std::views::iota(std::size_t { 0 }, rowsInBlock))
        {
            auto const id = cursor.GetI64(row, 1).value();
            ++rowsSeen;

            if (id % 3 == 0)
            {
                // NULL: the indicator must say so, and GetString must not fabricate a value.
                CHECK(cursor.IsCellNull(row, 2));
                CHECK(cursor.IsCellNull(row, 3));
                CHECK_FALSE(cursor.GetString(row, 2).has_value());
                CHECK_FALSE(cursor.GetString(row, 3).has_value());
            }
            else if (id % 3 == 1)
            {
                // Empty string, server-padded to CHAR(8)/NCHAR(8): NOT null, and trims down to "".
                CHECK_FALSE(cursor.IsCellNull(row, 2));
                CHECK_FALSE(cursor.IsCellNull(row, 3));
                CHECK(TrimRight(cursor.GetString(row, 2)) == std::optional<std::string> { "" });
                CHECK(TrimRight(cursor.GetString(row, 3)) == std::optional<std::string> { "" });
            }
            else
            {
                CHECK(TrimRight(cursor.GetString(row, 2)) == std::optional<std::string> { std::format("C{}", id) });
                CHECK(TrimRight(cursor.GetString(row, 3)) == std::optional<std::string> { std::format("W{}", id) });
            }
        }
    }
    CHECK(rowsSeen == rowCount);
}

TEST_CASE_METHOD(SqlTestFixture,
                 "Prefetch: RowArrayCursor bulk-reads fixed-width columns within a single block",
                 "[prefetch]")
{
    // The other fixed-width tests deliberately cross a block boundary; this pins the same correctness
    // for a result set that fits in a single FetchArray() call (arrayDepth > rowCount), so the
    // single-block code paths (no continuation, no second SQLGetData) are exercised too.
    auto stmt = SqlStatement {};

    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.CreateTable("PrefetchFixedSingleBlock")
            .PrimaryKey("Id", SqlColumnTypeDefinitions::Bigint {})
            .Column("Code", SqlColumnTypeDefinitions::Char { 8 });
    });

    constexpr std::size_t rowCount = 5; // well under TestPrefetchDepth
    stmt.Prepare(stmt.Query("PrefetchFixedSingleBlock").Insert().Set("Id", SqlWildcard).Set("Code", SqlWildcard));
    for (auto const i: std::views::iota(std::size_t { 1 }, rowCount + 1))
        (void) stmt.Execute(static_cast<std::int64_t>(i), std::format("C{}", i));

    auto cursor =
        stmt.ExecuteBatchFetch(R"(SELECT "Id", "Code" FROM "PrefetchFixedSingleBlock" ORDER BY "Id")"sv, TestPrefetchDepth);

    auto const rowsInFirstBlock = cursor.FetchArray();
    REQUIRE(rowsInFirstBlock == rowCount); // the whole result set fit in one block
    for (auto const row: std::views::iota(std::size_t { 0 }, rowsInFirstBlock))
    {
        auto const id = cursor.GetI64(row, 1).value();
        CHECK(TrimRight(cursor.GetString(row, 2)) == std::optional<std::string> { std::format("C{}", id) });
    }
    CHECK(cursor.FetchArray() == 0); // no more rows
}

// Opt-in micro-benchmark: per-row SQLFetch vs transparent block-prefetch over a large result set.
// Run with: LightweightTest "[prefetchbench]" (hidden by the leading '.').
TEST_CASE_METHOD(SqlTestFixture, "Prefetch benchmark", "[.][prefetchbench]")
{
    constexpr std::size_t rowCount = 200'000;

    auto stmt = SqlStatement {};
    CreateNumericTable(stmt);
    {
        auto txn = SqlTransaction { stmt.Connection() };
        FillNumericTable(stmt, rowCount);
        txn.Commit();
    }

    auto checksum = [](std::vector<NumericRow> const& rows) {
        std::int64_t sum = 0;
        for (auto const& row: rows)
            sum += row.id + row.big + row.mid;
        return sum;
    };

    auto perRow = MakePerRowStatement();
    auto const slow = checksum(ReadNumeric(perRow));

    auto fast = SqlStatement {};
    fast.Connection().SetDefaultPrefetchDepth(PrefetchDepthDefault);
    auto const quick = checksum(ReadNumeric(fast));

    CHECK(slow == quick);
}
