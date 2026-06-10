// SPDX-License-Identifier: Apache-2.0

#include "Utils.hpp"

#include <Lightweight/Lightweight.hpp>

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

using namespace Lightweight;
using namespace std::string_view_literals;

namespace
{

// One materialized result row, as collected from either the bulk array-fetch path or the trusted
// single-row SQLGetData path. Comparing the two proves the array-fetch primitive matches.
struct Row
{
    std::optional<std::int64_t> id;
    std::optional<double> ratio;
    std::optional<std::string> name;

    bool operator==(Row const&) const = default;
};

void CreateBatchFetchTable(SqlStatement& stmt)
{
    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.CreateTable("BatchFetch")
            .PrimaryKey("Id", SqlColumnTypeDefinitions::Bigint {})
            .Column("Ratio", SqlColumnTypeDefinitions::Real {})
            .Column("Name", SqlColumnTypeDefinitions::Varchar { 64 });
    });
}

// Inserts 10 rows of known values, with one NULL in each nullable column.
void FillBatchFetchTable(SqlStatement& stmt)
{
    stmt.Prepare(
        stmt.Query("BatchFetch").Insert().Set("Id", SqlWildcard).Set("Ratio", SqlWildcard).Set("Name", SqlWildcard));
    for (auto const i: std::views::iota(1, 11))
    {
        // Row 4 has a NULL Ratio, row 7 has a NULL Name.
        std::vector<SqlVariant> args;
        args.emplace_back(static_cast<std::int64_t>(i * 100));
        if (i == 4)
            args.emplace_back(SqlNullValue);
        else
            args.emplace_back(1.5 * static_cast<double>(i));
        if (i == 7)
            args.emplace_back(SqlNullValue);
        else
            args.emplace_back(std::format("name-{}", i));
        (void) stmt.ExecuteWithVariants(args);
    }
}

constexpr auto kSelectQuery = R"(SELECT "Id", "Ratio", "Name" FROM "BatchFetch" ORDER BY "Id")"sv;

// Reads a text column the way the backup's single-row path does: PostgreSQL goes through the
// wide (UTF-16) representation because psqlODBC converts SQL_C_CHAR output to the client's
// narrow codepage (losing non-ASCII characters on Windows); other DBMS read SQL_C_CHAR verbatim.
std::optional<std::string> ReadTextColumn(SqlResultCursor& cursor, SQLUSMALLINT column, SqlServerType serverType)
{
    if (serverType == SqlServerType::POSTGRESQL)
    {
        auto const u16 = cursor.GetNullableColumn<std::u16string>(column);
        if (!u16)
            return std::nullopt;
        auto const u8 = ToUtf8(*u16);
        return std::string { reinterpret_cast<char const*>(u8.data()), u8.size() };
    }
    return cursor.GetNullableColumn<std::string>(column);
}

// Reads the same query through the trusted single-row path.
std::vector<Row> ReadViaSingleRow(SqlStatement& stmt)
{
    auto const serverType = stmt.Connection().ServerType();
    std::vector<Row> rows;
    auto cursor = stmt.ExecuteDirect(std::string { kSelectQuery });
    while (cursor.FetchRow())
    {
        rows.push_back(Row {
            .id = cursor.GetNullableColumn<std::int64_t>(1),
            .ratio = cursor.GetNullableColumn<double>(2),
            .name = ReadTextColumn(cursor, 3, serverType),
        });
    }
    return rows;
}

} // namespace

TEST_CASE_METHOD(SqlTestFixture, "RowArrayCursor matches the single-row path", "[batchfetch]")
{
    auto stmt = SqlStatement {};
    CreateBatchFetchTable(stmt);
    FillBatchFetchTable(stmt);

    auto const expected = [&] {
        SqlStatement single {};
        return ReadViaSingleRow(single);
    }();
    REQUIRE(expected.size() == 10);

    std::vector<Row> actual;
    std::vector<std::size_t> fetchCounts;
    {
        auto cursor = stmt.ExecuteBatchFetch(kSelectQuery, 4);
        CHECK(cursor.ColumnCount() == 3);
        CHECK(cursor.ArrayDepth() == 4);

        while (auto const fetched = cursor.FetchArray())
        {
            fetchCounts.push_back(fetched);
            for (auto const r: std::views::iota(std::size_t { 0 }, fetched))
            {
                actual.push_back(Row {
                    .id = cursor.GetI64(r, 1),
                    .ratio = cursor.GetF64(r, 2),
                    .name = cursor.GetString(r, 3),
                });
            }
        }
    }

    // 10 rows at arrayDepth 4 -> blocks of 4, 4, 2, then 0 (loop exits).
    CHECK(fetchCounts == std::vector<std::size_t> { 4, 4, 2 });

    REQUIRE(actual.size() == expected.size());
    CHECK(actual == expected);

    // Explicitly assert the planted NULLs survived the round-trip.
    REQUIRE(actual.size() == 10);
    CHECK_FALSE(actual[3].ratio.has_value()); // row 4 (Id=400): NULL Ratio
    CHECK_FALSE(actual[6].name.has_value());  // row 7 (Id=700): NULL Name
    CHECK(actual[0].id == 100);
    CHECK(actual[9].name == std::optional<std::string> { "name-10" });
}

TEST_CASE_METHOD(SqlTestFixture, "RowArrayCursor handles arrayDepth larger than the row count", "[batchfetch]")
{
    auto stmt = SqlStatement {};
    CreateBatchFetchTable(stmt);
    FillBatchFetchTable(stmt);

    auto cursor = stmt.ExecuteBatchFetch(kSelectQuery, 32);
    auto const first = cursor.FetchArray();
    CHECK(first == 10);
    CHECK(cursor.FetchArray() == 0);
}

TEST_CASE_METHOD(SqlTestFixture, "RowArrayCursor rejects arrayDepth of zero", "[batchfetch]")
{
    auto stmt = SqlStatement {};
    CreateBatchFetchTable(stmt);

    auto const _ = ScopedSqlNullLogger {};
    CHECK_THROWS_AS(stmt.ExecuteBatchFetch(kSelectQuery, 0), std::invalid_argument);
}

TEST_CASE_METHOD(SqlTestFixture, "RowArrayCursor leaves the statement reusable", "[batchfetch]")
{
    auto stmt = SqlStatement {};
    CreateBatchFetchTable(stmt);
    FillBatchFetchTable(stmt);

    {
        auto cursor = stmt.ExecuteBatchFetch(kSelectQuery, 4);
        std::size_t total = 0;
        while (auto const fetched = cursor.FetchArray())
            total += fetched;
        CHECK(total == 10);
    }

    // After the cursor's destructor reset the row-array attributes, the same statement handle must
    // round-trip an ordinary single-row query correctly (no leaked SQL_ATTR_ROW_ARRAY_SIZE).
    auto const count = stmt.ExecuteDirectScalar<int>(R"(SELECT COUNT(*) FROM "BatchFetch")");
    REQUIRE(count.has_value());
    CHECK((count.has_value() && count.value() == 10));
}

// Belt-and-suspenders: the cursor owns the statement's array-binding state for its lifetime and must
// never be relocated (a move would dangle the SQL_ATTR_ROWS_FETCHED_PTR binding -> use-after-free).
static_assert(!std::is_move_constructible_v<RowArrayCursor>);
static_assert(!std::is_move_assignable_v<RowArrayCursor>);
static_assert(!std::is_copy_constructible_v<RowArrayCursor>);
static_assert(!std::is_copy_assignable_v<RowArrayCursor>);

TEST_CASE_METHOD(SqlTestFixture, "RowArrayCursor bounds-checks rowInBatch against the fetched count", "[batchfetch]")
{
    auto stmt = SqlStatement {};
    CreateBatchFetchTable(stmt);
    FillBatchFetchTable(stmt);

    auto cursor = stmt.ExecuteBatchFetch(kSelectQuery, 4);
    auto const fetched = cursor.FetchArray();
    REQUIRE(fetched == 4);

    // The last valid row is rowInBatch == fetched - 1; reading at the count itself must throw.
    CHECK_NOTHROW(cursor.GetI64(fetched - 1, 1));
    CHECK_THROWS_AS(cursor.GetI64(fetched, 1), std::out_of_range);
    CHECK_THROWS_AS(cursor.GetF64(fetched, 2), std::out_of_range);
    CHECK_THROWS_AS(cursor.GetString(fetched, 3), std::out_of_range);

    // After the result set is exhausted (FetchArray returns 0), every accessor is out of range.
    while (cursor.FetchArray())
        ;
    CHECK_THROWS_AS(cursor.GetI64(0, 1), std::out_of_range);
}

TEST_CASE_METHOD(SqlTestFixture, "RowArrayCursor reads NVARCHAR cells as UTF-8 text", "[batchfetch]")
{
    auto stmt = SqlStatement {};
    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.CreateTable("BatchFetchN")
            .PrimaryKey("Id", SqlColumnTypeDefinitions::Bigint {})
            .Column("Name", SqlColumnTypeDefinitions::NVarchar { 50 });
    });

    auto const names = std::vector<std::string> {
        "Müller-Straße",
        "Žluťoučký kůň",
        "plain ascii",
    };
    stmt.Prepare(stmt.Query("BatchFetchN").Insert().Set("Id", SqlWildcard).Set("Name", SqlWildcard));
    for (auto const i: std::views::iota(std::size_t { 0 }, names.size()))
    {
        // Bind as UTF-16 so the stored value is encoding-exact on every DBMS/driver.
        auto const u16 = ToUtf16(std::u8string_view { reinterpret_cast<char8_t const*>(names[i].data()), names[i].size() });
        (void) stmt.Execute(static_cast<std::int64_t>(i + 1), std::u16string { u16 });
    }
    (void) stmt.ExecuteDirect(R"(INSERT INTO "BatchFetchN" ("Id", "Name") VALUES (4, NULL))");

    constexpr auto query = R"(SELECT "Id", "Name" FROM "BatchFetchN" ORDER BY "Id")"sv;

    // Reference: the single-row u16string -> UTF-8 read the backup's unicode-text branches use
    // (on PostgreSQL the backup reads ALL text this way; see ReadTextColumn).
    auto const expected = [&] {
        SqlStatement single {};
        auto cursor = single.ExecuteDirect(std::string { query });
        std::vector<std::optional<std::string>> rows;
        while (cursor.FetchRow())
        {
            auto const u16 = cursor.GetNullableColumn<std::u16string>(2);
            if (u16)
            {
                auto const u8 = ToUtf8(*u16);
                rows.emplace_back(std::string { reinterpret_cast<char const*>(u8.data()), u8.size() });
            }
            else
                rows.emplace_back(std::nullopt);
        }
        return rows;
    }();
    REQUIRE(expected.size() == names.size() + 1);

    std::vector<std::optional<std::string>> actual;
    {
        auto cursor = stmt.ExecuteBatchFetch(query, 8);
        while (auto const fetched = cursor.FetchArray())
            for (auto const r: std::views::iota(std::size_t { 0 }, fetched))
                actual.push_back(cursor.GetString(r, 2));
    }

    // The array path must match the single-row NVarchar read byte-for-byte (including the NULL)...
    REQUIRE(actual.size() == expected.size());
    CHECK(actual == expected);
    CHECK_FALSE(actual.back().has_value());

    // ...and the bytes are the original UTF-8 on every driver (MSSQL and psqlODBC report the
    // column wide; SQLite stores UTF-8 and reports text wide too).
    for (auto const i: std::views::iota(std::size_t { 0 }, names.size()))
    {
        auto const& cell = actual[i];
        REQUIRE(cell.has_value());
        CHECK((cell.has_value() && cell.value() == names[i]));
    }
}

TEST_CASE_METHOD(SqlTestFixture, "RowArrayCursor reads DATE and TIMESTAMP cells natively", "[batchfetch]")
{
    auto stmt = SqlStatement {};
    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.CreateTable("BatchFetchTemporal")
            .PrimaryKey("Id", SqlColumnTypeDefinitions::Bigint {})
            .Column("D", SqlColumnTypeDefinitions::Date {})
            .Column("Ts", SqlColumnTypeDefinitions::DateTime {});
    });
    (void) stmt.ExecuteDirect(
        R"(INSERT INTO "BatchFetchTemporal" ("Id", "D", "Ts") VALUES (1, '2025-08-21', '2025-08-21 14:30:45.123'))");
    (void) stmt.ExecuteDirect(
        R"(INSERT INTO "BatchFetchTemporal" ("Id", "D", "Ts") VALUES (2, '1999-12-31', '2000-01-01 00:00:00'))");
    (void) stmt.ExecuteDirect(R"(INSERT INTO "BatchFetchTemporal" ("Id", "D", "Ts") VALUES (3, NULL, NULL))");

    constexpr auto query = R"(SELECT "Id", "D", "Ts" FROM "BatchFetchTemporal" ORDER BY "Id")"sv;

    // Single-row reference: the same native reads + std::format the backup's temporal branches use.
    struct TemporalRow
    {
        std::optional<std::string> d;
        std::optional<std::string> ts;

        bool operator==(TemporalRow const&) const = default;
    };
    auto const expected = [&] {
        SqlStatement single {};
        auto cursor = single.ExecuteDirect(std::string { query });
        std::vector<TemporalRow> rows;
        while (cursor.FetchRow())
        {
            auto const d = cursor.GetNullableColumn<SqlDate>(2);
            auto const ts = cursor.GetNullableColumn<SqlDateTime>(3);
            rows.push_back(TemporalRow {
                .d = d ? std::optional { std::format("{}", *d) } : std::nullopt,
                .ts = ts ? std::optional { std::format("{}", *ts) } : std::nullopt,
            });
        }
        return rows;
    }();
    REQUIRE(expected.size() == 3);

    std::vector<TemporalRow> actual;
    {
        auto cursor = stmt.ExecuteBatchFetch(query, 8);
        while (auto const fetched = cursor.FetchArray())
            for (auto const r: std::views::iota(std::size_t { 0 }, fetched))
            {
                auto const d = cursor.GetDate(r, 2);
                auto const ts = cursor.GetTimestamp(r, 3);
                actual.push_back(TemporalRow {
                    .d = d ? std::optional { std::format("{}", *d) } : std::nullopt,
                    .ts = ts ? std::optional { std::format("{}", *ts) } : std::nullopt,
                });
            }
    }

    REQUIRE(actual.size() == expected.size());
    CHECK(actual == expected);
    CHECK_FALSE(actual.back().d.has_value());
    CHECK_FALSE(actual.back().ts.has_value());
    // The sub-second fraction survives identically on both paths.
    auto const& actualTs = actual.front().ts;
    auto const& expectedTs = expected.front().ts;
    REQUIRE(actualTs.has_value());
    REQUIRE(expectedTs.has_value());
    CHECK((actualTs.has_value() && expectedTs.has_value() && actualTs.value() == expectedTs.value()));
}

TEST_CASE_METHOD(SqlTestFixture, "RowArrayCursor reads GUID cells natively on MSSQL/PG", "[batchfetch]")
{
    auto stmt = SqlStatement {};
    if (stmt.Connection().ServerType() == SqlServerType::SQLITE)
        return; // SQLite stores GUIDs as text (TryParse single-row path); no SQL_GUID to array-bind.

    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.CreateTable("BatchFetchGuid")
            .PrimaryKey("Id", SqlColumnTypeDefinitions::Bigint {})
            .Column("G", SqlColumnTypeDefinitions::Guid {});
    });

    auto const guids = std::vector<SqlGuid> {
        SqlGuid::UnsafeParse("01234567-89ab-cdef-0123-456789abcdef"),
        SqlGuid::UnsafeParse("fedcba98-7654-3210-fedc-ba9876543210"),
    };
    stmt.Prepare(stmt.Query("BatchFetchGuid").Insert().Set("Id", SqlWildcard).Set("G", SqlWildcard));
    for (auto const i: std::views::iota(std::size_t { 0 }, guids.size()))
        (void) stmt.Execute(static_cast<std::int64_t>(i + 1), guids[i]);
    (void) stmt.ExecuteDirect(R"(INSERT INTO "BatchFetchGuid" ("Id", "G") VALUES (3, NULL))");

    constexpr auto query = R"(SELECT "Id", "G" FROM "BatchFetchGuid" ORDER BY "Id")"sv;

    // Single-row reference: the same SqlGuid read + to_string the backup's Guid branch uses.
    auto const expected = [&] {
        SqlStatement single {};
        auto cursor = single.ExecuteDirect(std::string { query });
        std::vector<std::optional<std::string>> rows;
        while (cursor.FetchRow())
        {
            auto const g = cursor.GetNullableColumn<SqlGuid>(2);
            rows.emplace_back(g ? std::optional { to_string(*g) } : std::nullopt);
        }
        return rows;
    }();
    REQUIRE(expected.size() == 3);

    std::vector<std::optional<std::string>> actual;
    {
        auto cursor = stmt.ExecuteBatchFetch(query, 8);
        while (auto const fetched = cursor.FetchArray())
            for (auto const r: std::views::iota(std::size_t { 0 }, fetched))
            {
                auto const g = cursor.GetGuid(r, 2);
                actual.push_back(g ? std::optional { to_string(*g) } : std::nullopt);
            }
    }

    REQUIRE(actual.size() == expected.size());
    CHECK(actual == expected);
    CHECK_FALSE(actual.back().has_value());
}

TEST_CASE_METHOD(SqlTestFixture, "RowArrayCursor adapts its depth to the per-cursor memory budget", "[batchfetch]")
{
    auto stmt = SqlStatement {};
    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.CreateTable("BatchFetchWide")
            .PrimaryKey("Id", SqlColumnTypeDefinitions::Bigint {})
            .Column("C0", SqlColumnTypeDefinitions::Varchar { 4000 })
            .Column("C1", SqlColumnTypeDefinitions::Varchar { 4000 })
            .Column("C2", SqlColumnTypeDefinitions::Varchar { 4000 })
            .Column("C3", SqlColumnTypeDefinitions::Varchar { 4000 })
            .Column("C4", SqlColumnTypeDefinitions::Varchar { 4000 })
            .Column("C5", SqlColumnTypeDefinitions::Varchar { 4000 })
            .Column("C6", SqlColumnTypeDefinitions::Varchar { 4000 })
            .Column("C7", SqlColumnTypeDefinitions::Varchar { 4000 });
    });
    (void) stmt.ExecuteDirect(
        R"(INSERT INTO "BatchFetchWide" ("Id", "C0", "C1", "C2", "C3", "C4", "C5", "C6", "C7")
           VALUES (1, 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h'))");
    (void) stmt.ExecuteDirect(
        R"(INSERT INTO "BatchFetchWide" ("Id", "C0", "C1", "C2", "C3", "C4", "C5", "C6", "C7")
           VALUES (2, 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p'))");

    {
        // Eight varchar(4000) columns bind at 4000*4+1 bytes each (~128 KB per row), so the
        // requested depth of 512 (~64 MB of buffers) must be clamped down to fit the per-cursor
        // memory budget, while staying at or above the minimum that keeps bulk fetch progressing.
        auto cursor = stmt.ExecuteBatchFetch(R"(SELECT "Id", "C0", "C7" FROM "BatchFetchWide" ORDER BY "Id")", 512);
        CHECK(cursor.ArrayDepth() < 512);
        CHECK(cursor.ArrayDepth() >= RowArrayCursor::MinArrayDepth);

        // The clamped cursor still reads data correctly.
        auto const fetched = cursor.FetchArray();
        REQUIRE(fetched == 2);
        CHECK(cursor.GetI64(0, 1) == 1);
        CHECK(cursor.GetString(0, 2) == std::optional<std::string> { "a" });
        CHECK(cursor.GetString(1, 3) == std::optional<std::string> { "p" });
    }

    // A narrow table is unaffected: the budget allows far more than a small requested depth.
    CreateBatchFetchTable(stmt);
    FillBatchFetchTable(stmt);
    auto narrow = stmt.ExecuteBatchFetch(kSelectQuery, 4);
    CHECK(narrow.ArrayDepth() == 4);
}

TEST_CASE_METHOD(SqlTestFixture, "RowArrayCursor round-trips multibyte UTF-8 strings", "[batchfetch]")
{
    auto stmt = SqlStatement {};

    // The BatchFetch "Name" column is VARCHAR (see CreateBatchFetchTable). MSSQL VARCHAR uses byte
    // semantics over the server codepage (CP1252), so arbitrary UTF-8 multibyte input is lossily
    // converted on insert and cannot round-trip byte-for-byte — the same limitation that makes the
    // DataBinder "max-width umlaut value" section SKIP() MSSQL. SQLite and PostgreSQL (UTF-8 server)
    // store the bytes verbatim, which is exactly the narrow SQL_C_CHAR octet-sizing this test pins.
    if (stmt.Connection().ServerType() == SqlServerType::MICROSOFT_SQL)
        SKIP();

    CreateBatchFetchTable(stmt);

    // Non-ASCII payloads whose UTF-8 byte length exceeds their character count. If the CHAR buffer
    // were sized by character count (as it was before the octet-length fix) these would truncate.
    auto const names = std::vector<std::string> {
        "Müller-Straße",
        "Žluťoučký kůň",
        "naïve café",
    };

    stmt.Prepare(
        stmt.Query("BatchFetch").Insert().Set("Id", SqlWildcard).Set("Ratio", SqlWildcard).Set("Name", SqlWildcard));
    for (auto const i: std::views::iota(std::size_t { 0 }, names.size()))
    {
        std::vector<SqlVariant> args;
        args.emplace_back(static_cast<std::int64_t>(i + 1));
        args.emplace_back(1.5 * static_cast<double>(i + 1));
        args.emplace_back(names[i]);
        (void) stmt.ExecuteWithVariants(args);
    }

    // Trusted single-row path establishes the reference bytes for each row.
    auto const expected = [&] {
        SqlStatement single {};
        return ReadViaSingleRow(single);
    }();
    REQUIRE(expected.size() == names.size());

    std::vector<Row> actual;
    {
        auto cursor = stmt.ExecuteBatchFetch(kSelectQuery, 8);
        while (auto const fetched = cursor.FetchArray())
            for (auto const r: std::views::iota(std::size_t { 0 }, fetched))
                actual.push_back(Row {
                    .id = cursor.GetI64(r, 1),
                    .ratio = cursor.GetF64(r, 2),
                    .name = cursor.GetString(r, 3),
                });
    }

    REQUIRE(actual.size() == expected.size());
    // The array path must produce byte-identical strings to the single-row path.
    CHECK(actual == expected);
    for (auto const i: std::views::iota(std::size_t { 0 }, names.size()))
    {
        auto const& name = actual[i].name;
        REQUIRE(name.has_value());
        CHECK((name.has_value() && name.value() == names[i]));
    }
}
