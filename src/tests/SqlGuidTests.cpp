// SPDX-License-Identifier: Apache-2.0

#include <Lightweight/DataBinder/SqlGuid.hpp>

#include <catch2/catch_test_macros.hpp>

#include <format>
#include <optional>
#include <stdexcept>
#include <string_view>

using namespace Lightweight;

// ================================================================================================
// SqlGuid::TryParse — accepts canonical 36-character UUIDs
// ================================================================================================

TEST_CASE("SqlGuid::TryParse accepts a canonical version-4 UUID", "[SqlGuid]")
{
    auto const guid = SqlGuid::TryParse("550E8400-E29B-41D4-A716-446655440000");
    REQUIRE(guid.has_value());
    if (guid.has_value())
        CHECK(static_cast<bool>(*guid));
}

TEST_CASE("SqlGuid::TryParse accepts each documented version (1..5)", "[SqlGuid]")
{
    for (char v: { '1', '2', '3', '4', '5' })
    {
        auto const text = std::format("550E8400-E29B-{}1D4-A716-446655440000", v);
        INFO("input: " << text);
        CHECK(SqlGuid::TryParse(text).has_value());
    }
}

TEST_CASE("SqlGuid::TryParse rejects wrong length", "[SqlGuid]")
{
    CHECK_FALSE(SqlGuid::TryParse("").has_value());
    CHECK_FALSE(SqlGuid::TryParse("550E8400-E29B-41D4-A716-44665544000").has_value());   // 35
    CHECK_FALSE(SqlGuid::TryParse("550E8400-E29B-41D4-A716-4466554400000").has_value()); // 37
}

TEST_CASE("SqlGuid::TryParse rejects malformed dashes", "[SqlGuid]")
{
    CHECK_FALSE(SqlGuid::TryParse("550E8400xE29B-41D4-A716-446655440000").has_value());
    CHECK_FALSE(SqlGuid::TryParse("550E8400-E29Bx41D4-A716-446655440000").has_value());
    CHECK_FALSE(SqlGuid::TryParse("550E8400-E29B-41D4xA716-446655440000").has_value());
    CHECK_FALSE(SqlGuid::TryParse("550E8400-E29B-41D4-A716x446655440000").has_value());
}

TEST_CASE("SqlGuid::TryParse rejects invalid version digits", "[SqlGuid]")
{
    CHECK_FALSE(SqlGuid::TryParse("550E8400-E29B-01D4-A716-446655440000").has_value()); // version 0
    CHECK_FALSE(SqlGuid::TryParse("550E8400-E29B-61D4-A716-446655440000").has_value()); // version 6
    CHECK_FALSE(SqlGuid::TryParse("550E8400-E29B-X1D4-A716-446655440000").has_value()); // non-digit
}

TEST_CASE("SqlGuid::TryParse rejects non-hex variant digit", "[SqlGuid]")
{
    CHECK_FALSE(SqlGuid::TryParse("550E8400-E29B-41D4-G716-446655440000").has_value());
}

TEST_CASE("SqlGuid::TryParse rejects non-hex bytes inside the payload", "[SqlGuid]")
{
    CHECK_FALSE(SqlGuid::TryParse("ZZZZZZZZ-E29B-41D4-A716-446655440000").has_value());
}

// ================================================================================================
// Comparison operators and emptiness
// ================================================================================================

namespace
{
// Local helper: parse and `REQUIRE` the optional has a value, returning it.
// The explicit `if`-with-throw wrapper is what clang-tidy's
// `bugprone-unchecked-optional-access` analysis recognizes as a check —
// Catch2's `REQUIRE` is a macro it cannot reason about.
SqlGuid RequireParsed(std::string_view text)
{
    auto const parsed = SqlGuid::TryParse(text);
    REQUIRE(parsed.has_value());
    if (!parsed.has_value())
        throw std::runtime_error("REQUIRE failed but flow continued"); // unreachable
    return *parsed;
}
} // namespace

TEST_CASE("SqlGuid: default-constructed compares equal to itself, unequal to a parsed value", "[SqlGuid]")
{
    SqlGuid const empty {};
    auto const parsed = RequireParsed("550E8400-E29B-41D4-A716-446655440000");
    CHECK(empty == empty);
    CHECK(empty != parsed);
    CHECK_FALSE(static_cast<bool>(empty));
    CHECK(static_cast<bool>(parsed));
    CHECK(!empty);
    CHECK_FALSE(!parsed);
}

TEST_CASE("SqlGuid: total ordering via <=>", "[SqlGuid]")
{
    auto const a = RequireParsed("00000000-0000-1000-8000-000000000001");
    auto const b = RequireParsed("00000000-0000-1000-8000-000000000002");
    CHECK(a < b);
    CHECK(b > a);
    CHECK_FALSE(a == b);
}

// ================================================================================================
// std::formatter<SqlGuid> — emits the canonical 36-character upper-case representation
// ================================================================================================

TEST_CASE("std::formatter<SqlGuid> round-trips through TryParse", "[SqlGuid]")
{
    auto const original = RequireParsed("550E8400-E29B-41D4-A716-446655440000");
    auto const formatted = std::format("{}", original);
    CHECK(formatted == "550E8400-E29B-41D4-A716-446655440000");

    auto const reparsed = SqlGuid::TryParse(formatted);
    REQUIRE(reparsed.has_value());
    if (reparsed.has_value())
        CHECK(*reparsed == original);
}

TEST_CASE("std::formatter<SqlGuid> produces uppercase hex", "[SqlGuid]")
{
    auto const original = RequireParsed("aabbccdd-eeff-1122-8899-001122334455");
    auto const formatted = std::format("{}", original);
    CHECK(formatted == "AABBCCDD-EEFF-1122-8899-001122334455");
}

// ================================================================================================
// SqlGuid::UnsafeParse — same parse contract, no nullopt; fills with sentinel on failure
// ================================================================================================

TEST_CASE("SqlGuid::UnsafeParse returns the same bytes as TryParse for a valid input", "[SqlGuid]")
{
    constexpr std::string_view text { "550E8400-E29B-41D4-A716-446655440000" };
    auto const safe = RequireParsed(text);
    constexpr SqlGuid unsafe = SqlGuid::UnsafeParse(text);
    CHECK(safe == unsafe);
}

// ================================================================================================
// SqlGuid::Create produces unique non-empty values
// ================================================================================================

TEST_CASE("SqlGuid::Create yields non-empty, non-equal values across calls", "[SqlGuid]")
{
    auto const a = SqlGuid::Create();
    auto const b = SqlGuid::Create();
    CHECK(static_cast<bool>(a));
    CHECK(static_cast<bool>(b));
    CHECK(a != b);
}
