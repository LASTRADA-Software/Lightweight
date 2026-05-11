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
