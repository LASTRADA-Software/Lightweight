// SPDX-License-Identifier: Apache-2.0

#include <Lightweight/DataBinder/SqlBinary.hpp>
#include <Lightweight/DataBinder/SqlDynamicNumeric.hpp>
#include <Lightweight/DataBinder/SqlVariant.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <charconv>
#include <cstdint>
#include <limits>
#include <string>

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

TEST_CASE("SqlDynamicNumeric::ToDouble scales in a single division", "[SqlDynamicNumeric]")
{
    // Dividing by ten `scale` times rounds at every step: these three land on
    // 0.12345678900000004, 0.12345599999999998 and 0.12345678000000002 respectively, so a caller
    // comparing against the literal it inserted would see a mismatch.
    auto const asDouble = [](std::int64_t unscaled, std::uint8_t scale) {
        return SqlDynamicNumeric { .unscaledValue = unscaled, .precision = 19, .scale = scale }.ToDouble();
    };

    CHECK(asDouble(123456789, 9) == 0.123456789);
    CHECK(asDouble(123456, 6) == 0.123456);
    CHECK(asDouble(12345678, 8) == 0.12345678);
    CHECK(asDouble(-123456789, 9) == -0.123456789);
}

TEST_CASE("SqlDynamicNumeric scales correctly at the widest scale", "[SqlDynamicNumeric]")
{
    // The int64 power table must stop at 10^18, the largest power of ten that fits. Sizing it one
    // entry longer made the generator saturate, so index 19 silently held 10^18 and every
    // DECIMAL(19, 19) converted ten times too large.
    STATIC_REQUIRE(Lightweight::detail::PowersOfTen.back() == 1'000'000'000'000'000'000LL);
    STATIC_REQUIRE(Lightweight::detail::DoublePowersOfTen.back() == 1.0e19);

    auto const allFractional = SqlDynamicNumeric { .unscaledValue = 1234567890123456789LL, .precision = 19, .scale = 19 };
    CHECK_THAT(allFractional.ToDouble(), Catch::Matchers::WithinRel(0.1234567890123456789, 1e-15));

    SECTION("a rescale that cannot fit reports failure rather than a wrong answer")
    {
        // 10^19 overflows int64, so nothing but zero survives the shift.
        auto const deepest = SqlDynamicNumeric { .unscaledValue = 5, .precision = 19, .scale = 19 };
        auto const whole = SqlDynamicNumeric { .unscaledValue = 5, .precision = 19, .scale = 0 };
        CHECK(deepest != whole);

        auto const zeroDeep = SqlDynamicNumeric { .unscaledValue = 0, .precision = 19, .scale = 19 };
        auto const zeroWhole = SqlDynamicNumeric { .unscaledValue = 0, .precision = 19, .scale = 0 };
        CHECK(zeroDeep == zeroWhole);
    }
}

TEST_CASE("SqlDynamicNumeric::operator== survives INT64_MIN", "[SqlDynamicNumeric]")
{
    // Negating INT64_MIN to strip the sign before rescaling is undefined behaviour, and a hard
    // failure under UBSan. Rescaling it can only overflow, so the comparison must simply say "not
    // equal" rather than wrap or trap.
    auto const extreme = SqlDynamicNumeric { .unscaledValue = INT64_MIN, .precision = 19, .scale = 1 };
    auto const other = SqlDynamicNumeric { .unscaledValue = -5, .precision = 19, .scale = 2 };

    CHECK(extreme != other);
    CHECK(extreme == extreme);
}

TEST_CASE("detail::FractionDigitsOf recovers the scale a driver did not declare", "[SqlDynamicNumeric]")
{
    using Lightweight::detail::FractionDigitsOf;

    // A padded literal must score the same as the trimmed one: ParseUnscaledDecimal trims, so
    // scoring "12.34  " as zero would make the recovered scale reject digits that are really there.
    CHECK(FractionDigitsOf("12.34  ") == 2);
    CHECK(FractionDigitsOf("  12.34") == 2);
    CHECK(FractionDigitsOf("\t-0.500\n") == 3);

    CHECK(FractionDigitsOf("99.50") == 2);
    CHECK(FractionDigitsOf("0.123456789") == 9);
    CHECK(FractionDigitsOf("7") == 0);
    CHECK(FractionDigitsOf("-12.5") == 1);
    CHECK(FractionDigitsOf("") == 0);
    // Not a plain decimal literal — no scale can be claimed from it.
    CHECK(FractionDigitsOf("1.2e5") == 0);
    CHECK(FractionDigitsOf("abc") == 0);
}

TEST_CASE("SqlVariant accessors degrade instead of aborting on the new alternatives", "[SqlDynamicNumeric][SqlVariant]")
{
    // Get<T>() and ValueOr<T>() are noexcept and reach std::get, so a type mismatch would abort the
    // process rather than throw. DECIMAL and BINARY columns changed which alternative they fill, so
    // these conversions are what keeps existing callers working.
    SECTION("a decimal reads back as a floating-point value")
    {
        auto v = SqlVariant { SqlDynamicNumeric { .unscaledValue = 9950, .precision = 19, .scale = 2 } };
        CHECK_THAT(v.ValueOr<double>(0.0), Catch::Matchers::WithinAbs(99.50, 1e-9));
        CHECK_THAT(v.Get<double>(), Catch::Matchers::WithinAbs(99.50, 1e-9));
        CHECK(v.ValueOr<std::string>({}) == "99.50");
    }

    SECTION("binary bytes remain reachable as a string")
    {
        auto v = SqlVariant { SqlBinary { 0x41, 0x42, 0x43 } };
        CHECK(v.ValueOr<std::string>({}) == "ABC");
        CHECK(v.Get<std::string>() == "ABC");
        auto const view = v.TryGetStringView();
        REQUIRE(view.has_value());
        CHECK(view.value() == "ABC");
    }

    SECTION("asking for an alternative the variant does not hold yields the fallback, not a crash")
    {
        auto const v = SqlVariant { SqlDynamicNumeric { .unscaledValue = 1, .precision = 19, .scale = 0 } };
        CHECK(v.ValueOr<SqlGuid>(SqlGuid {}) == SqlGuid {});
    }
}

TEST_CASE("A padded or signed literal parses the same both ways", "[SqlDynamicNumeric]")
{
    // SqlVariant reads a DECIMAL exactly and, when it does not fit, falls back to std::from_chars on
    // the same literal. from_chars skips no leading whitespace and rejects a leading '+', while the
    // exact parser accepts both — so a driver that pads or signs its output could make the fallback
    // refuse a literal the exact path would have taken. ReadLiteral trims once for both; this pins
    // the parsers' agreement on the forms that difference would have split.
    auto const acceptedExactly = [](std::string_view text) {
        return SqlDynamicNumeric::FromString(text, 19, 2).has_value();
    };
    auto const acceptedApproximately = [](std::string_view text) {
        auto const signless = text.starts_with('+') ? text.substr(1) : text;
        auto value = 0.0;
        return std::from_chars(signless.data(), signless.data() + signless.size(), value).ec == std::errc {};
    };

    for (auto const& text: { "12.34", "+12.34", "-12.34", "0.50" })
    {
        INFO("literal: " << text);
        CHECK(acceptedExactly(text));
        CHECK(acceptedApproximately(text));
    }

    // Padding is removed before either parser sees the text, so both still accept it.
    CHECK(acceptedExactly("  12.34  "));
    CHECK(acceptedApproximately(detail::TrimAsciiWhitespace("  12.34  ")));
}

TEST_CASE("SqlDynamicNumeric formats through std::format", "[SqlDynamicNumeric]")
{
    auto const value = SqlDynamicNumeric { .unscaledValue = 995000, .precision = 19, .scale = 4 };
    CHECK(std::format("{}", value) == "99.5000");
}
