// SPDX-License-Identifier: Apache-2.0

#include "Utils.hpp"

#include <Lightweight/DataBinder/SqlBinary.hpp>
#include <Lightweight/Lightweight.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <format>
#include <optional>
#include <ostream>
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

// Unwraps a RowArrayCursor cell known to be non-NULL (e.g. the primary key column, or a text
// column the test never inserts NULL into), REQUIRE-ing that first. The explicit if-with-throw is
// what clang-tidy's bugprone-unchecked-optional-access analysis recognizes as a null check —
// REQUIRE alone is a macro it cannot reason about (see the same pattern in SqlGuidTests.cpp's
// RequireParsed).
std::int64_t RequireI64(RowArrayCursor const& cursor, std::size_t row, SQLUSMALLINT column)
{
    auto const value = cursor.GetI64(row, column);
    REQUIRE(value.has_value());
    if (!value.has_value())
        throw std::runtime_error("REQUIRE failed but flow continued"); // unreachable
    return *value;
}

std::string RequireString(RowArrayCursor const& cursor, std::size_t row, SQLUSMALLINT column)
{
    auto value = cursor.GetString(row, column);
    REQUIRE(value.has_value());
    if (!value.has_value())
        throw std::runtime_error("REQUIRE failed but flow continued"); // unreachable
    return std::move(*value);
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
            .Column("Name", SqlColumnTypeDefinitions::Varchar { .size = 64 })
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
            .Column("Payload", SqlColumnTypeDefinitions::VarBinary { .size = 64 });
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
            .Column("Name", SqlColumnTypeDefinitions::Varchar { .size = 32 })  // narrow text (multibyte UTF-8)
            .Column("Wide", SqlColumnTypeDefinitions::NVarchar { .size = 32 }) // wide (UTF-16) text
            .Column("Tag", SqlColumnTypeDefinitions::Char { 8 })               // CHAR(N): right-padded -> trim parity
            .Column("Note", SqlColumnTypeDefinitions::Varchar { .size = 16 }); // nullable text
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
                auto const id = RequireI64(cursor, row, 1);
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
                    RequireI64(cursor, row, 1), RequireString(cursor, row, 2), RequireString(cursor, row, 3));
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
            auto const id = RequireI64(cursor, row, 1);
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
        auto const id = RequireI64(cursor, row, 1);
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

TEST_CASE_METHOD(SqlTestFixture, "Prefetch: SqlVariantRowCursor reads temporal and GUID columns", "[prefetch]")
{
    using namespace Lightweight::SqlColumnTypeDefinitions;
    constexpr std::size_t rowCount = TestPrefetchDepth + 5;

    auto stmt = SqlStatement {};
    stmt.Connection().SetDefaultPrefetchDepth(TestPrefetchDepth);

    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.DropTableIfExists("prefetch_temporal");
        migration.CreateTable("prefetch_temporal")
            .PrimaryKey("id", Integer {})
            .RequiredColumn("d", Date {})
            .RequiredColumn("ts", DateTime {})
            .RequiredColumn("g", Guid {});
    });

    stmt.Prepare(R"(INSERT INTO "prefetch_temporal" ("id", "d", "ts", "g") VALUES (?, ?, ?, ?))");
    auto const guid = SqlGuid::Create();
    for (auto const i: std::views::iota(std::size_t { 1 }, rowCount + 1))
        (void) stmt.Execute(static_cast<int>(i),
                            SqlDate { std::chrono::year { 2024 }, std::chrono::month { 6 }, std::chrono::day { 15 } },
                            SqlDateTime { std::chrono::year { 2024 },
                                          std::chrono::month { 6 },
                                          std::chrono::day { 15 },
                                          std::chrono::hours { 12 },
                                          std::chrono::minutes { 30 },
                                          std::chrono::seconds { 45 } },
                            guid);

    // Reading DATE / TIMESTAMP / GUID cells through the variant row cursor takes
    // the temporal and GUID arms of the prefetch variant dispatch.
    auto cursor = stmt.ExecuteDirect(R"(SELECT "id", "d", "ts", "g" FROM "prefetch_temporal" ORDER BY "id")");
    std::size_t seen = 0;
    for (auto const& row: SqlVariantRowCursor { std::move(cursor) })
    {
        ++seen;
        REQUIRE(row.size() == 4);
        CHECK_FALSE(row[1].IsNull());
        CHECK_FALSE(row[2].IsNull());
        CHECK_FALSE(row[3].IsNull());
    }
    CHECK(seen == rowCount);
}

// Regression test for #602: a VARBINARY column is not prefetchable, so its presence moves the whole
// result set onto the per-row path, where a GUID read into std::string goes through the string
// binder's growing buffer. That buffer starts at 15 bytes, and MS SQL Server refuses to hand out a
// uniqueidentifier (36 characters) in chunks: it fails the first SQLGetData with 22003 "Numeric
// value out of range" rather than reporting a truncation the loop could grow from.
TEST_CASE_METHOD(SqlTestFixture, "Per-row fallback: a GUID column reads into std::string", "[prefetch],[SqlGuid]")
{
    auto stmt = SqlStatement {};
    stmt.MigrateDirect([](auto& migration) {
        migration.CreateTable("GuidText")
            .RequiredColumn("Id", SqlColumnTypeDefinitions::Integer {})
            .RequiredColumn("G", SqlColumnTypeDefinitions::Guid {})
            .Column("B", SqlColumnTypeDefinitions::VarBinary { .size = 64 });
    });

    auto const guid = SqlGuid::Create();
    stmt.Prepare(R"(INSERT INTO "GuidText" ("Id", "G", "B") VALUES (?, ?, ?))");
    (void) stmt.Execute(1, guid, SqlBinary { 0x01, 0x02 });

    auto cursor = stmt.ExecuteDirect(R"(SELECT "G", "B" FROM "GuidText")");
    REQUIRE(cursor.FetchRow());

    auto const text = cursor.GetColumn<std::string>(1);
    // The textual form differs in case between drivers; the value must not.
    CHECK(SqlGuid::TryParse(text) == guid);
}

// ------------------------------------------------------------------------------------------------
// A user type whose binder composes a built-in binder cannot be rebuilt from the block buffer
// ------------------------------------------------------------------------------------------------

namespace PrefetchTests
{

/// A user type with its own binder, stored as a DOUBLE.
struct Celsius
{
    double degrees {};

    constexpr auto operator<=>(Celsius const&) const noexcept = default;
};

std::ostream& operator<<(std::ostream& os, Celsius const& value)
{
    return os << std::format("Celsius({})", value.degrees);
}

} // namespace PrefetchTests

template <>
struct Lightweight::SqlDataBinder<PrefetchTests::Celsius>
{
    using Celsius = PrefetchTests::Celsius;

    static constexpr auto ColumnType = SqlDataBinder<double>::ColumnType;

    static SQLRETURN InputParameter(SQLHSTMT stmt,
                                    SQLUSMALLINT column,
                                    Celsius const& value,
                                    SqlDataBinderCallback& cb) noexcept
    {
        return SqlDataBinder<double>::InputParameter(stmt, column, value.degrees, cb);
    }

    static SQLRETURN OutputColumn(
        SQLHSTMT stmt, SQLUSMALLINT column, Celsius* result, SQLLEN* indicator, SqlDataBinderCallback& cb) noexcept
    {
        return SqlDataBinder<double>::OutputColumn(stmt, column, &result->degrees, indicator, cb);
    }

    static SQLRETURN GetColumn(
        SQLHSTMT stmt, SQLUSMALLINT column, Celsius* result, SQLLEN* indicator, SqlDataBinderCallback const& cb) noexcept
    {
        return SqlDataBinder<double>::GetColumn(stmt, column, &result->degrees, indicator, cb);
    }

    static std::string Inspect(Celsius const& value) noexcept
    {
        return std::format("{}C", value.degrees);
    }
};

namespace
{

using PrefetchTests::Celsius;

constexpr auto CelsiusValues = std::array { Celsius { 20.5 }, Celsius { -3.25 }, Celsius { 100.0 } };

void CreateCelsiusTable(SqlStatement& stmt)
{
    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.CreateTable("PrefetchCelsius")
            .PrimaryKey("Id", SqlColumnTypeDefinitions::Integer {})
            .RequiredColumn("Degrees", SqlColumnTypeDefinitions::Real {});
    });
    stmt.Prepare(R"(INSERT INTO "PrefetchCelsius" ("Id", "Degrees") VALUES (?, ?))");
    for (auto const index: std::views::iota(std::size_t { 0 }, CelsiusValues.size()))
        (void) stmt.Execute(static_cast<int>(index), CelsiusValues.at(index));
}

} // namespace

TEST_CASE_METHOD(SqlTestFixture, "Prefetch: ExecuteDirectScalar of a custom-binder type", "[prefetch]")
{
    auto stmt = SqlStatement {};
    stmt.Connection().SetDefaultPrefetchDepth(PrefetchDepthDefault);
    CreateCelsiusTable(stmt);

    CHECK(stmt.ExecuteDirectScalar<Celsius>(R"(SELECT "Degrees" FROM "PrefetchCelsius" WHERE "Id" = 0)")
          == std::optional { CelsiusValues[0] });
    CHECK(stmt.ExecuteDirectScalar<Celsius>(R"(SELECT "Degrees" FROM "PrefetchCelsius" ORDER BY "Id" DESC)")
          == std::optional { CelsiusValues[2] });
}

TEST_CASE_METHOD(SqlTestFixture, "Prefetch: GetColumn of a custom-binder type", "[prefetch]")
{
    auto stmt = SqlStatement {};
    stmt.Connection().SetDefaultPrefetchDepth(PrefetchDepthDefault);
    CreateCelsiusTable(stmt);

    auto const query = R"(SELECT "Degrees" FROM "PrefetchCelsius" ORDER BY "Id")"sv;

    SECTION("Read per row while the result set is block-prefetched")
    {
        auto cursor = stmt.ExecuteDirect(query);
        REQUIRE(cursor.FetchRow());
        if (stmt.Connection().SupportsNativeRowArrayFetch())
            // The block buffer cannot rebuild the value; it must say so rather than hand back Celsius {}.
            CHECK_THROWS_AS(cursor.GetColumn<Celsius>(1), SqlException);
        else
            CHECK(cursor.GetColumn<Celsius>(1) == CelsiusValues[0]);
    }

    SECTION("Bound output column")
    {
        auto celsius = Celsius {};
        auto cursor = stmt.ExecuteDirect(query);
        cursor.BindOutputColumns(&celsius);
        auto rows = std::vector<Celsius> {};
        while (cursor.FetchRow())
            rows.push_back(celsius);
        CHECK(rows == std::vector<Celsius>(CelsiusValues.begin(), CelsiusValues.end()));
    }

    SECTION("Prefetch disabled")
    {
        stmt.Connection().SetDefaultPrefetchDepth(1);
        auto cursor = stmt.ExecuteDirect(query);
        auto rows = std::vector<Celsius> {};
        while (cursor.FetchRow())
            rows.push_back(cursor.GetColumn<Celsius>(1));
        CHECK(rows == std::vector<Celsius>(CelsiusValues.begin(), CelsiusValues.end()));
    }
}
TEST_CASE_METHOD(SqlTestFixture,
                 "Prefetch: cross-type GetColumn converts or fails but never returns a default",
                 "[prefetch]")
{
    auto stmt = SqlStatement {};
    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.CreateTable("PrefetchCross")
            .PrimaryKey("Id", SqlColumnTypeDefinitions::Integer {})
            .RequiredColumn("Number", SqlColumnTypeDefinitions::Bigint {})
            .RequiredColumn("Day", SqlColumnTypeDefinitions::Date {})
            .RequiredColumn("Moment", SqlColumnTypeDefinitions::DateTime {});
    });
    using namespace std::chrono;
    auto const theDay = SqlDate { year { 2024 }, month { 1 }, day { 2 } };
    auto const moment = SqlDateTime { year { 2024 }, month { 1 }, day { 2 }, hours { 3 }, minutes { 4 }, seconds { 5 } };
    stmt.Prepare(R"(INSERT INTO "PrefetchCross" ("Id", "Number", "Day", "Moment") VALUES (?, ?, ?, ?))");
    (void) stmt.Execute(1, std::int64_t { 20240102 }, theDay, moment);

    stmt.Connection().SetDefaultPrefetchDepth(TestPrefetchDepth);
    auto const read = [&]<typename T>(std::string_view column) {
        auto cursor = stmt.ExecuteDirect(std::format(R"(SELECT "{}" FROM "PrefetchCross")", column));
        REQUIRE(cursor.FetchRow());
        return cursor.GetColumn<T>(1);
    };

    // Conversions the ODBC rules define: the same value the per-row path reads on every backend.
    CHECK(read.operator()<SqlDate>("Moment") == theDay);
    CHECK(read.operator()<SqlDateTime>("Day")
          == SqlDateTime { year { 2024 }, month { 1 }, day { 2 }, hours { 0 }, minutes { 0 }, seconds { 0 } });
    CHECK(read.operator()<double>("Number") == 20240102.0);

    // Conversions they do not define. The per-row path is driver-specific here (MS SQL Server raises
    // 07006, others convert or report NULL); a block-prefetched read raises 07006 instead of inventing a value.
    if (stmt.Connection().SupportsNativeRowArrayFetch())
    {
        auto const restricted = [&]<typename T>(std::string_view column) {
            try
            {
                (void) read.operator()<T>(column);
                return std::string { "no error" };
            }
            catch (SqlException const& error)
            {
                return error.info().sqlState;
            }
        };
        CHECK(restricted.operator()<double>("Day") == "07006");
        CHECK(restricted.operator()<std::int64_t>("Moment") == "07006");
        CHECK(restricted.operator()<SqlDate>("Number") == "07006");
        CHECK(restricted.operator()<SqlDateTime>("Number") == "07006");
        CHECK(restricted.operator()<SqlGuid>("Day") == "07006");
    }

    // A bound output column learns the column type before the first fetch, so a pairing whose per-row
    // conversion is driver-specific keeps the per-row path and reads exactly what it reads there.
    auto const boundOutcome = [&](std::size_t depth) -> std::string {
        stmt.Connection().SetDefaultPrefetchDepth(depth);
        try
        {
            auto value = std::optional<SqlDateTime> {};
            auto cursor = stmt.ExecuteDirect(R"(SELECT "Number" FROM "PrefetchCross")"sv);
            cursor.BindOutputColumns(&value);
            if (!cursor.FetchRow())
                return std::string { "no row" };
            return value.has_value() ? std::format("{}", *value) : std::string { "NULL" };
        }
        catch (SqlException const& error)
        {
            return std::format("error {}", error.info().sqlState);
        }
    };
    CHECK(boundOutcome(TestPrefetchDepth) == boundOutcome(1));
}

/// A record SqlRowIterator reads with GetColumn, one of whose members has a custom binder. At namespace
/// scope, not in an anonymous namespace: MSVC cannot reflect a type with internal linkage.
struct PrefetchCelsiusRecord
{
    static constexpr std::string_view TableName = "PrefetchCelsius";

    Field<int> Id;
    Field<Celsius> Degrees;
};

TEST_CASE_METHOD(SqlTestFixture, "Prefetch: SqlRowIterator of a record with a custom-binder member", "[prefetch]")
{
    auto stmt = SqlStatement {};
    stmt.Connection().SetDefaultPrefetchDepth(PrefetchDepthDefault);
    CreateCelsiusTable(stmt);

    // The member types are known before the first fetch, so the result set keeps the per-row path
    // rather than failing the read of a value the block buffer cannot rebuild.
    auto degrees = std::vector<Celsius> {};
    for (auto const& record: SqlRowIterator<PrefetchCelsiusRecord> { stmt.Connection() })
        degrees.push_back(record.Degrees.Value());

    auto expected = std::vector<Celsius>(CelsiusValues.begin(), CelsiusValues.end());
    std::ranges::sort(degrees);
    std::ranges::sort(expected);
    CHECK(degrees == expected);
}

/// A record SqlRowIterator reads with GetColumn, whose members do not match their columns' SQL types:
/// a timestamp over a BIGINT column and a string over a REAL column. At namespace scope, not in an
/// anonymous namespace: MSVC cannot reflect a type with internal linkage.
struct PrefetchMismatchRecord
{
    static constexpr std::string_view TableName = "PrefetchMismatch";

    Field<int> Id;
    Field<std::optional<SqlDateTime>> Number;
    Field<std::string> Ratio;
};

TEST_CASE_METHOD(SqlTestFixture,
                 "Prefetch: SqlRowIterator of a record whose members do not match their columns",
                 "[prefetch]")
{
    auto stmt = SqlStatement {};
    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.CreateTable("PrefetchMismatch")
            .PrimaryKey("Id", SqlColumnTypeDefinitions::Integer {})
            .RequiredColumn("Number", SqlColumnTypeDefinitions::Bigint {})
            .RequiredColumn("Ratio", SqlColumnTypeDefinitions::Real {});
    });
    stmt.Prepare(R"(INSERT INTO "PrefetchMismatch" ("Id", "Number", "Ratio") VALUES (?, ?, ?))");
    (void) stmt.Execute(1, std::int64_t { 20240102 }, 0.1F);

    // The per-row path reads each member with the driver's own conversion (or its error); a block-prefetched
    // result set must read the same, so the iterator keeps the per-row path for such a record.
    auto const iterate = [&](std::size_t depth) -> std::string {
        stmt.Connection().SetDefaultPrefetchDepth(depth);
        try
        {
            auto outcome = std::string {};
            for (auto const& record: SqlRowIterator<PrefetchMismatchRecord> { stmt.Connection() })
            {
                auto const& number = record.Number.Value();
                outcome += std::format("{} {} {};",
                                       record.Id.Value(),
                                       number.has_value() ? std::format("{}", *number) : std::string { "NULL" },
                                       record.Ratio.Value());
            }
            return outcome;
        }
        catch (SqlException const& error)
        {
            return std::format("error {}", error.info().sqlState);
        }
    };
    CHECK(iterate(TestPrefetchDepth) == iterate(1));
}

TEST_CASE_METHOD(SqlTestFixture,
                 "Prefetch: a REAL column bound to a string and an INTEGER column bound to a char",
                 "[prefetch]")
{
    auto stmt = SqlStatement {};
    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.CreateTable("PrefetchTextTargets")
            .PrimaryKey("Id", SqlColumnTypeDefinitions::Integer {})
            .RequiredColumn("Ratio", SqlColumnTypeDefinitions::Real {})
            .RequiredColumn("Digit", SqlColumnTypeDefinitions::Integer {});
    });
    stmt.Prepare(R"(INSERT INTO "PrefetchTextTargets" ("Id", "Ratio", "Digit") VALUES (?, ?, ?))");
    (void) stmt.Execute(1, 0.1F, 7);

    // Both pairings read through the column binder on the per-row path: the driver renders the REAL as
    // text, and SqlDataBinder<char> reads the INTEGER as text. Block prefetch must read the same.
    auto const boundRead = [&]<typename T>(std::size_t depth, std::string_view column) -> std::string {
        stmt.Connection().SetDefaultPrefetchDepth(depth);
        try
        {
            auto value = T {};
            auto cursor = stmt.ExecuteDirect(std::format(R"(SELECT "Id", "{}" FROM "PrefetchTextTargets")", column));
            auto id = 0;
            cursor.BindOutputColumns(&id, &value);
            if (!cursor.FetchRow())
                return "no row";
            if constexpr (std::same_as<T, char>)
                return std::format("char {}", static_cast<int>(value));
            else
                return value;
        }
        catch (SqlException const& error)
        {
            return std::format("error {}", error.info().sqlState);
        }
    };
    CHECK(boundRead.operator()<std::string>(TestPrefetchDepth, "Ratio") == boundRead.operator()<std::string>(1, "Ratio"));
    CHECK(boundRead.operator()<char>(TestPrefetchDepth, "Digit") == boundRead.operator()<char>(1, "Digit"));
}

TEST_CASE_METHOD(SqlTestFixture, "Prefetch: GetColumn into a pointer reads a NULL cell as the per-row path", "[prefetch]")
{
    auto stmt = SqlStatement {};
    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.CreateTable("PrefetchNullPointer")
            .PrimaryKey("Id", SqlColumnTypeDefinitions::Integer {})
            .Column("Number", SqlColumnTypeDefinitions::Integer {});
    });
    (void) stmt.ExecuteDirect(R"(INSERT INTO "PrefetchNullPointer" ("Id", "Number") VALUES (1, NULL))");

    // Reads the NULL cell into a target that holds a value already; reports whether the overload said
    // "not NULL" and what the target holds afterwards.
    auto const readNull = [&]<typename T>(std::size_t depth, T preset) -> std::string {
        stmt.Connection().SetDefaultPrefetchDepth(depth);
        auto cursor = stmt.ExecuteDirect(R"(SELECT "Id", "Number" FROM "PrefetchNullPointer")");
        REQUIRE(cursor.FetchRow());
        auto target = std::move(preset);
        auto const notNull = cursor.GetColumn(2, &target);
        return std::format("{} {}", notNull, SqlDataBinder<T>::Inspect(target));
    };
    auto const bothPaths = [&]<typename T>(T const& preset) {
        INFO(SqlDataBinder<T>::Inspect(preset));
        CHECK(readNull(PrefetchDepthDefault, preset) == readNull(std::size_t { 1 }, preset));
        CHECK(readNull(std::size_t { 1 }, preset) == std::format("false {}", SqlDataBinder<T>::Inspect(T {})));
    };
    bothPaths(std::optional<int> { 5 });
    bothPaths(std::optional<std::string> { "x" });
    bothPaths(SqlVariant { 5 });
    bothPaths(5);
    bothPaths(std::string { "x" });
}

TEST_CASE_METHOD(SqlTestFixture, "Prefetch: a DATETIME column read as std::chrono::system_clock::time_point", "[prefetch]")
{
    // time_point has a read-only binder (GetColumn only), so it cannot be a DataMapper Field: the record
    // path does not exist for it. GetColumn and ExecuteDirectScalar do.
    using TimePoint = std::chrono::system_clock::time_point;
    using namespace std::chrono_literals;

    auto stmt = SqlStatement {};
    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.CreateTable("PrefetchTimePoint")
            .PrimaryKey("Id", SqlColumnTypeDefinitions::Integer {})
            .RequiredColumn("Moment", SqlColumnTypeDefinitions::DateTime {});
    });
    auto const expected = TimePoint { std::chrono::sys_days { std::chrono::year { 2024 } / 5 / 6 } + 7h + 8min + 9s };
    stmt.Prepare(R"(INSERT INTO "PrefetchTimePoint" ("Id", "Moment") VALUES (?, ?))");
    (void) stmt.Execute(1, SqlDateTime { expected });

    auto const query = R"(SELECT "Id", "Moment" FROM "PrefetchTimePoint")"sv;
    for (auto const depth: { PrefetchDepthDefault, std::size_t { 1 } })
    {
        INFO("prefetch depth " << depth);
        stmt.Connection().SetDefaultPrefetchDepth(depth);
        {
            auto cursor = stmt.ExecuteDirect(query);
            REQUIRE(cursor.FetchRow());
            CHECK(cursor.GetColumn<TimePoint>(2) == expected);
        }
        {
            auto cursor = stmt.ExecuteDirect(query);
            REQUIRE(cursor.FetchRow());
            auto value = TimePoint {};
            CHECK(cursor.GetColumn(2, &value));
            CHECK(value == expected);
        }
        CHECK(stmt.ExecuteDirectScalar<TimePoint>(R"(SELECT "Moment" FROM "PrefetchTimePoint")")
              == std::optional { expected });
    }
}
