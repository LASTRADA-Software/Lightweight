// SPDX-License-Identifier: Apache-2.0

#include <Lightweight/DataBinder/SqlDynamicNumeric.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>

using namespace Lightweight;

TEST_CASE("SqlDynamicNumeric::FromString parses plain decimal literals", "[SqlDynamicNumeric]")
{
    SECTION("integral literal scaled up to the column scale")
    {
        auto const value = SqlDynamicNumeric::FromString("7", 19, 4);
        REQUIRE(value.has_value());
        CHECK(value.value().unscaledValue == 70000);
        CHECK(value.value().scale == 4);
        CHECK(value.value().precision == 19);
    }

    SECTION("fractional literal with fewer digits than the scale")
    {
        auto const value = SqlDynamicNumeric::FromString("1.5", 19, 4);
        REQUIRE(value.has_value());
        CHECK(value.value().unscaledValue == 15000);
    }

    SECTION("fractional literal filling the scale exactly")
    {
        auto const value = SqlDynamicNumeric::FromString("99.5000", 19, 4);
        REQUIRE(value.has_value());
        CHECK(value.value().unscaledValue == 995000);
    }

    SECTION("negative literal")
    {
        auto const value = SqlDynamicNumeric::FromString("-12.34", 19, 2);
        REQUIRE(value.has_value());
        CHECK(value.value().unscaledValue == -1234);
    }

    SECTION("explicit plus sign")
    {
        auto const value = SqlDynamicNumeric::FromString("+12.34", 19, 2);
        REQUIRE(value.has_value());
        CHECK(value.value().unscaledValue == 1234);
    }

    SECTION("leading decimal point")
    {
        auto const value = SqlDynamicNumeric::FromString(".5", 19, 2);
        REQUIRE(value.has_value());
        CHECK(value.value().unscaledValue == 50);
    }

    SECTION("surrounding whitespace is ignored")
    {
        auto const value = SqlDynamicNumeric::FromString("  42.25  ", 19, 2);
        REQUIRE(value.has_value());
        CHECK(value.value().unscaledValue == 4225);
    }

    SECTION("zero at scale 0")
    {
        auto const value = SqlDynamicNumeric::FromString("0", 19, 0);
        REQUIRE(value.has_value());
        CHECK(value.value().unscaledValue == 0);
    }
}

TEST_CASE("SqlDynamicNumeric::FromString rejects what it cannot represent exactly", "[SqlDynamicNumeric]")
{
    // Silently dropping digits is what the double-based predecessor did; refusing is the point.
    CHECK_FALSE(SqlDynamicNumeric::FromString("1.234", 19, 2).has_value());
    CHECK_FALSE(SqlDynamicNumeric::FromString("", 19, 2).has_value());
    CHECK_FALSE(SqlDynamicNumeric::FromString("   ", 19, 2).has_value());
    CHECK_FALSE(SqlDynamicNumeric::FromString("-", 19, 2).has_value());
    CHECK_FALSE(SqlDynamicNumeric::FromString("1.2.3", 19, 2).has_value());
    CHECK_FALSE(SqlDynamicNumeric::FromString("abc", 19, 2).has_value());
    CHECK_FALSE(SqlDynamicNumeric::FromString("1e5", 19, 2).has_value());
    CHECK_FALSE(SqlDynamicNumeric::FromString("1,5", 19, 2).has_value());

    SECTION("overflowing the 64-bit unscaled carrier")
    {
        CHECK_FALSE(SqlDynamicNumeric::FromString("99999999999999999999999", 19, 0).has_value());
        // Fits unscaled, but not once scaled up by 10^4.
        CHECK_FALSE(SqlDynamicNumeric::FromString("9223372036854775", 19, 4).has_value());
    }
}

TEST_CASE("SqlDynamicNumeric::ToString round-trips the exact decimal text", "[SqlDynamicNumeric]")
{
    auto const render = [](std::int64_t unscaled, std::uint8_t scale) {
        return SqlDynamicNumeric { .unscaledValue = unscaled, .precision = 19, .scale = scale }.ToString();
    };

    CHECK(render(995000, 4) == "99.5000");
    CHECK(render(-1234, 2) == "-12.34");
    CHECK(render(70000, 4) == "7.0000");
    CHECK(render(7, 0) == "7");
    CHECK(render(0, 4) == "0.0000");
    CHECK(render(0, 0) == "0");
    // Fewer digits than the scale must gain a leading zero rather than losing the integral part.
    CHECK(render(5, 4) == "0.0005");
    CHECK(render(-5, 4) == "-0.0005");
}

TEST_CASE("SqlDynamicNumeric keeps precision a double would lose", "[SqlDynamicNumeric]")
{
    // A DECIMAL(19, 4) money value beyond the 2^53 exactly-representable range of a double.
    // Reading this column as double — what SqlVariant did before — cannot return it unchanged.
    constexpr auto exactCents = std::int64_t { 92233720368547 };
    auto const value = SqlDynamicNumeric::FromString("9223372036.8547", 19, 4);
    REQUIRE(value.has_value());
    CHECK(value.value().unscaledValue == exactCents);
    CHECK(value.value().ToString() == "9223372036.8547");
}

TEST_CASE("SqlDynamicNumeric compares by mathematical value, not representation", "[SqlDynamicNumeric]")
{
    auto const oneAndAHalfScale1 = SqlDynamicNumeric { .unscaledValue = 15, .precision = 19, .scale = 1 };
    auto const oneAndAHalfScale4 = SqlDynamicNumeric { .unscaledValue = 15000, .precision = 19, .scale = 4 };
    auto const two = SqlDynamicNumeric { .unscaledValue = 20000, .precision = 19, .scale = 4 };

    CHECK(oneAndAHalfScale1 == oneAndAHalfScale4);
    CHECK(oneAndAHalfScale4 == oneAndAHalfScale1);
    CHECK(oneAndAHalfScale1 != two);

    SECTION("negative values rescale correctly")
    {
        auto const minusHalfScale1 = SqlDynamicNumeric { .unscaledValue = -5, .precision = 19, .scale = 1 };
        auto const minusHalfScale3 = SqlDynamicNumeric { .unscaledValue = -500, .precision = 19, .scale = 3 };
        CHECK(minusHalfScale1 == minusHalfScale3);
    }
}

TEST_CASE("SqlDynamicNumeric::ToDouble approximates the value", "[SqlDynamicNumeric]")
{
    auto const value = SqlDynamicNumeric { .unscaledValue = 995000, .precision = 19, .scale = 4 };
    CHECK(value.ToDouble() == 99.5);

    auto const negative = SqlDynamicNumeric { .unscaledValue = -1234, .precision = 19, .scale = 2 };
    CHECK(negative.ToDouble() == -12.34);
}

TEST_CASE("SqlDynamicNumeric formats through std::format", "[SqlDynamicNumeric]")
{
    auto const value = SqlDynamicNumeric { .unscaledValue = 995000, .precision = 19, .scale = 4 };
    CHECK(std::format("{}", value) == "99.5000");
}
