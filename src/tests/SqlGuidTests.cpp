// SPDX-License-Identifier: Apache-2.0

#include <Lightweight/DataBinder/SqlGuid.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <format>
#include <optional>
#include <stdexcept>
#include <string>
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

TEST_CASE("SqlGuid::TryParse accepts any version nibble", "[SqlGuid]")
{
    // SQL Server accepts any 8-4-4-4-12 hex value, and older Lightweight builds stored GUIDs whose
    // version nibble lies outside 1..5 (byte-swapped Data3); those must still parse.
    for (char const version: { '0', '6', '7', '9', 'A', 'E', 'F', 'e' })
    {
        auto text = std::string { "550E8400-E29B-01D4-A716-446655440000" };
        text[14] = version;
        INFO(text);
        CHECK(SqlGuid::TryParse(text).has_value());
    }
}

TEST_CASE("SqlGuid::TryParse rejects a non-hex version nibble", "[SqlGuid]")
{
    CHECK_FALSE(SqlGuid::TryParse("550E8400-E29B-X1D4-A716-446655440000").has_value());
}

TEST_CASE("SqlGuid::TryParse round-trips a GUID with a non-RFC version nibble byte for byte", "[SqlGuid]")
{
    constexpr std::string_view text { "FCC64492-FF82-E040-9C3A-12AB34CD56EF" };
    auto const guid = SqlGuid::TryParse(text);
    REQUIRE(guid.has_value());
    if (!guid.has_value())
        return;

    // Canonical order: data[i] is the i-th hex pair of the text, so no group is reordered.
    uint8_t const expected[16] = {
        0xFC, 0xC6, 0x44, 0x92, 0xFF, 0x82, 0xE0, 0x40, 0x9C, 0x3A, 0x12, 0xAB, 0x34, 0xCD, 0x56, 0xEF,
    };
    CHECK(std::ranges::equal(guid->data, expected));
    CHECK(to_string(*guid) == text);

    constexpr SqlGuid unsafe = SqlGuid::UnsafeParse(text);
    CHECK(unsafe == *guid);
}

TEST_CASE("SqlGuid::TryParse rejects non-hex variant digit", "[SqlGuid]")
{
    CHECK_FALSE(SqlGuid::TryParse("550E8400-E29B-41D4-G716-446655440000").has_value());
}

TEST_CASE("SqlGuid::TryParse rejects non-hex bytes inside the payload", "[SqlGuid]")
{
    CHECK_FALSE(SqlGuid::TryParse("ZZZZZZZZ-E29B-41D4-A716-446655440000").has_value());
}

TEST_CASE("SqlGuid::TryParse rejects a hex pair whose second character is not hex", "[SqlGuid]")
{
    CHECK_FALSE(SqlGuid::TryParse("550E840G-E29B-41D4-A716-446655440000").has_value());
    CHECK_FALSE(SqlGuid::TryParse("550E8400-E29B-41D4-A716-44665544000+").has_value());
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

// ================================================================================================
// detail::SwapGuidWireByteOrder — converts between SqlGuid's canonical (textual) byte order and
// the native SQL_C_GUID/Win32 GUID wire layout used at the ODBC boundary.
//
// Regression coverage for https://github.com/LASTRADA-Software/Lightweight/issues/596.
// ================================================================================================

TEST_CASE("detail::SwapGuidWireByteOrder converts canonical order to the native GUID wire order", "[SqlGuid]")
{
    // Textbook example: text group N maps to Data1=0x01020304, Data2=0x0506, Data3=0x4708,
    // Data4={0x09..0x10} (Data3's leading nibble is fixed to a valid version digit so TryParse()
    // accepts the string). The native (Win32 GUID / SQL_C_GUID) in-memory layout stores Data1,
    // Data2, and Data3 in little-endian byte order, which is the byte-reverse of how each group
    // reads left to right in the canonical text; Data4 is a plain byte array and is identical in
    // both representations.
    auto guid = RequireParsed("01020304-0506-4708-090a-0b0c0d0e0f10");
    uint8_t const canonical[16] = {
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x47, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10,
    };
    uint8_t const wire[16] = {
        0x04, 0x03, 0x02, 0x01, 0x06, 0x05, 0x08, 0x47, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10,
    };
    CHECK(std::ranges::equal(guid.data, canonical));

    detail::SwapGuidWireByteOrder(guid.data);
    CHECK(std::ranges::equal(guid.data, wire));
}

TEST_CASE("detail::SwapGuidWireByteOrder is self-inverse", "[SqlGuid]")
{
    auto const original = RequireParsed("AABBCCDD-EEFF-1122-8899-001122334455");
    auto roundTripped = original;
    detail::SwapGuidWireByteOrder(roundTripped.data);
    detail::SwapGuidWireByteOrder(roundTripped.data);
    CHECK(roundTripped == original);
}
