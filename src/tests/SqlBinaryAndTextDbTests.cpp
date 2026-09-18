// SPDX-License-Identifier: Apache-2.0

#include "Utils.hpp"

#include <Lightweight/DataBinder/SqlBinary.hpp>
#include <Lightweight/DataBinder/SqlDynamicBinary.hpp>
#include <Lightweight/DataBinder/SqlText.hpp>
#include <Lightweight/Lightweight.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <ranges>
#include <string>

using namespace Lightweight;

// ================================================================================================
// SqlBinary round-trips through a VARBINARY column
// ================================================================================================

TEST_CASE_METHOD(SqlTestFixture, "SqlBinary: small payload round-trip", "[SqlBinary]")
{
    auto stmt = SqlStatement {};
    stmt.MigrateDirect([](auto& migration) {
        migration.CreateTable("Bin").RequiredColumn("payload", SqlColumnTypeDefinitions::VarBinary { 64 });
    });

    auto const inputValue = SqlBinary { 0x00, 0x01, 0xFF, 0xAA, 0x55 };
    stmt.Prepare(R"(INSERT INTO "Bin" ("payload") VALUES (?))");
    (void) stmt.Execute(inputValue);

    auto const fetched = stmt.ExecuteDirectScalar<SqlBinary>(R"(SELECT "payload" FROM "Bin")");
    REQUIRE(fetched.has_value());
    if (fetched.has_value())
        CHECK(*fetched == inputValue);
}

TEST_CASE_METHOD(SqlTestFixture, "SqlBinary: empty payload round-trip", "[SqlBinary]")
{
    // Pins the contract: an empty SqlBinary{} binds and round-trips as a zero-byte
    // VARBINARY on every supported DB (either NULL or empty SqlBinary on read).
    auto stmt = SqlStatement {};
    stmt.MigrateDirect([](auto& migration) {
        migration.CreateTable("BinEmpty").Column("payload", SqlColumnTypeDefinitions::VarBinary { 64 });
    });

    auto const empty = SqlBinary {};
    stmt.Prepare(R"(INSERT INTO "BinEmpty" ("payload") VALUES (?))");
    (void) stmt.Execute(empty);

    auto const fetched = stmt.ExecuteDirectScalar<SqlBinary>(R"(SELECT "payload" FROM "BinEmpty")");
    // The driver may surface an empty VARBINARY as either empty SqlBinary or NULL.
    if (fetched.has_value())
        CHECK(fetched->empty());
}

TEST_CASE_METHOD(SqlTestFixture, "SqlBinary: comparison and size accessors", "[SqlBinary]")
{
    auto const a = SqlBinary { 0x10, 0x20, 0x30 };
    auto const b = SqlBinary { 0x10, 0x20, 0x30 };
    auto const c = SqlBinary { 0x10, 0x20 };
    CHECK(a == b);
    CHECK(a > c);
    CHECK(c < a);
    CHECK(a.size() == 3);
}

// ================================================================================================
// SqlDynamicBinary<N> with variable-length payload
// ================================================================================================

TEST_CASE_METHOD(SqlTestFixture, "SqlDynamicBinary: medium payload round-trip", "[SqlBinary]")
{
    auto stmt = SqlStatement {};
    stmt.MigrateDirect([](auto& migration) {
        migration.CreateTable("DynBin").RequiredColumn("payload", SqlColumnTypeDefinitions::VarBinary { 256 });
    });

    std::array<uint8_t, 200> raw {};
    for (auto const i: std::views::iota(size_t { 0 }, raw.size()))
        raw[i] = static_cast<uint8_t>(i % 256);

    SqlDynamicBinary<256> input { raw.data(), raw.data() + raw.size() };

    stmt.Prepare(R"(INSERT INTO "DynBin" ("payload") VALUES (?))");
    (void) stmt.Execute(input);

    auto const fetched = stmt.ExecuteDirectScalar<SqlDynamicBinary<256>>(R"(SELECT "payload" FROM "DynBin")");
    REQUIRE(fetched.has_value());
    if (fetched.has_value())
    {
        REQUIRE(fetched->size() == input.size());
        REQUIRE(*fetched == input);
    }
}

// ================================================================================================
// SqlText round-trips through a TEXT column
// ================================================================================================

TEST_CASE_METHOD(SqlTestFixture, "SqlText: small text round-trip", "[SqlText]")
{
    auto stmt = SqlStatement {};
    stmt.MigrateDirect(
        [](auto& migration) { migration.CreateTable("Txt").RequiredColumn("body", SqlColumnTypeDefinitions::Text {}); });

    auto const inputValue = SqlText { .value = "Hello, SQLite!" };
    stmt.Prepare(R"(INSERT INTO "Txt" ("body") VALUES (?))");
    (void) stmt.Execute(inputValue);

    auto const fetched = stmt.ExecuteDirectScalar<SqlText>(R"(SELECT "body" FROM "Txt")");
    REQUIRE(fetched.has_value());
    if (fetched.has_value())
        CHECK(fetched->value == inputValue.value);
}

TEST_CASE_METHOD(SqlTestFixture, "SqlText: large text (>4 KiB) round-trip", "[SqlText]")
{
    auto stmt = SqlStatement {};
    stmt.MigrateDirect(
        [](auto& migration) { migration.CreateTable("Txt").RequiredColumn("body", SqlColumnTypeDefinitions::Text {}); });

    auto const longContent = MakeLargeText<char>(8 * 1024);
    auto const inputValue = SqlText { .value = std::string { longContent.begin(), longContent.end() } };

    stmt.Prepare(R"(INSERT INTO "Txt" ("body") VALUES (?))");
    (void) stmt.Execute(inputValue);

    auto const fetched = stmt.ExecuteDirectScalar<SqlText>(R"(SELECT "body" FROM "Txt")");
    REQUIRE(fetched.has_value());
    if (fetched.has_value())
    {
        CHECK(fetched->value.size() == inputValue.value.size());
        CHECK(fetched->value == inputValue.value);
    }
}

TEST_CASE("SqlText: comparison operators", "[SqlText]")
{
    SqlText const a { .value = "alpha" };
    SqlText const b { .value = "alpha" };
    SqlText const c { .value = "beta" };
    CHECK(a == b);
    CHECK(a != c);
    CHECK(a < c);
}

// ================================================================================================
// An unsigned TINYINT above 127 read through the variant cursor
// ================================================================================================

TEST_CASE_METHOD(SqlTestFixture, "SqlVariant: an unsigned TINYINT above 127", "[SqlVariant]")
{
    // SQL Server's TINYINT is unsigned 0..255. Read into a signed int8_t it would either wrap
    // 128..255 to negatives (the prefetch path) or make the driver reject the row with 22003 (the
    // per-row path) -- and a rejected column mid-row unwinds through the noexcept cursor into a
    // hard crash. So a value above 127 is the case that separates a faithful read from either.
    auto stmt = SqlStatement {};
    stmt.MigrateDirect(
        [](auto& migration) { migration.CreateTable("Tiny").Column("kind", SqlColumnTypeDefinitions::Tinyint {}); });

    stmt.Prepare(stmt.Query("Tiny").Insert().Set("kind", SqlWildcard));
    (void) stmt.Execute(255);

    stmt.Prepare(stmt.Query("Tiny").Select().Field("kind").All());
    auto cursor = stmt.ExecuteWithVariants({});

    std::size_t rowsRead = 0;
    for (auto& row: SqlVariantRowCursor(std::move(cursor)))
    {
        REQUIRE(row.size() == 1);
        // Whatever integral alternative the variant chose, the value has to survive as 255 rather
        // than come back negative.
        long long const value = std::visit(
            [](auto const& held) -> long long {
                if constexpr (std::is_integral_v<std::remove_cvref_t<decltype(held)>>)
                    return static_cast<long long>(held);
                else
                    return -1;
            },
            row[0].value);
        CHECK(value == 255);
        ++rowsRead;
    }
    CHECK(rowsRead == 1);
}

// ================================================================================================
// A LOB column followed by an out-of-range unsigned TINYINT
// ================================================================================================

TEST_CASE_METHOD(SqlTestFixture, "SqlVariant: a LOB column followed by an unsigned TINYINT", "[SqlVariant][SqlText]")
{
    // The shape of a real row that used to crash: a LOB disables the block-prefetch (the LOB is not
    // a prefetchable type), so the whole row falls to the per-row path -- and there the following
    // TINYINT of 255 hit the 22003 rejection. This pins that a LOB and an out-of-range column read
    // together in one row.
    //
    // A LOB column is needed to disable prefetch. TEXT is a large-object type on every supported
    // dialect, so the table is created with raw DDL. The trailing column is TINYINT on SQL Server --
    // the exact unsigned-0..255 regression -- but PostgreSQL has no TINYINT keyword, so there (and
    // wherever else) it is SMALLINT, which still exercises reading a column after a chunked LOB.
    auto stmt = SqlStatement {};

    auto const* const kindType = stmt.Connection().ServerType() == SqlServerType::MICROSOFT_SQL ? "TINYINT" : "SMALLINT";
    (void) stmt.ExecuteDirect(R"(CREATE TABLE "LobThenTiny" ("note" TEXT, "kind" )" + std::string { kindType } + ")");

    // Longer than the reader's first buffer, so the chunked LOB path is the one taken.
    auto const longNote = std::string(2000, 'x');
    stmt.Prepare(R"(INSERT INTO "LobThenTiny" ("note", "kind") VALUES (?, ?))");
    (void) stmt.Execute(longNote, 255);

    stmt.Prepare(R"(SELECT "note", "kind" FROM "LobThenTiny")");
    auto cursor = stmt.ExecuteWithVariants({});

    std::size_t rowsRead = 0;
    for (auto& row: SqlVariantRowCursor(std::move(cursor)))
    {
        REQUIRE(row.size() == 2);
        CHECK(std::get<std::string>(row[0].value).size() == longNote.size());
        ++rowsRead;
    }
    CHECK(rowsRead == 1);
}
