// SPDX-License-Identifier: Apache-2.0

#include <Lightweight/DataBinder/SqlNumeric.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <compare>
#include <cstdint>
#include <cstring>
#include <format>
#include <limits>
#include <ranges>
#include <string>

using namespace Lightweight;

// ================================================================================================
// SqlNumeric construction & conversion (no DB required)
// ================================================================================================

TEST_CASE("SqlNumeric default-constructs to zero", "[SqlNumeric]")
{
    SqlNumeric<10, 2> const n;
    CHECK_THAT(n.ToDouble(), Catch::Matchers::WithinAbs(0.0, 1e-9));
    CHECK(n.ToString() == "0.00");
}

TEST_CASE("SqlNumeric round-trips positive values across float/double/long-double", "[SqlNumeric]")
{
    SqlNumeric<10, 2> const n { 12345.67 };
    CHECK_THAT(n.ToDouble(), Catch::Matchers::WithinAbs(12345.67, 1e-3));
    CHECK_THAT(n.ToFloat(), Catch::Matchers::WithinAbs(12345.67F, 1e-2F));
    CHECK_THAT(static_cast<double>(n.ToLongDouble()), Catch::Matchers::WithinAbs(12345.67, 1e-3));
}

TEST_CASE("SqlNumeric explicit conversion operators", "[SqlNumeric]")
{
    SqlNumeric<10, 2> const n { 42.5 };
    CHECK_THAT(static_cast<double>(n), Catch::Matchers::WithinAbs(42.5, 1e-9));
    CHECK_THAT(static_cast<float>(n), Catch::Matchers::WithinAbs(42.5F, 1e-6F));
    CHECK_THAT(static_cast<double>(static_cast<long double>(n)), Catch::Matchers::WithinAbs(42.5, 1e-9));
}

TEST_CASE("SqlNumeric formats with the configured scale", "[SqlNumeric]")
{
    CHECK(SqlNumeric<5, 2>(1.5).ToString() == "1.50");
    CHECK(SqlNumeric<6, 3>(1.5).ToString() == "1.500");
    CHECK(SqlNumeric<10, 4>(0.1).ToString() == "0.1000");
    CHECK(SqlNumeric<15, 0>(42.0).ToString() == "42");
}

TEST_CASE("SqlNumeric handles negative values", "[SqlNumeric]")
{
    SqlNumeric<10, 2> const n { -987.65 };
    CHECK_THAT(n.ToDouble(), Catch::Matchers::WithinAbs(-987.65, 1e-3));
    CHECK(n.ToString() == "-987.65");
}

TEST_CASE("SqlNumeric assign() overwrites previous value", "[SqlNumeric]")
{
    SqlNumeric<10, 2> n { 1.0 };
    n.assign(99.99);
    CHECK_THAT(n.ToDouble(), Catch::Matchers::WithinAbs(99.99, 1e-3));

    n = -5.5;
    CHECK_THAT(n.ToDouble(), Catch::Matchers::WithinAbs(-5.5, 1e-3));
}

TEST_CASE("SqlNumeric copy preserves value", "[SqlNumeric]")
{
    SqlNumeric<10, 2> const src { 3.14 };
    SqlNumeric<10, 2> const copy = src;
    CHECK_THAT(copy.ToDouble(), Catch::Matchers::WithinAbs(3.14, 1e-3));

    SqlNumeric<10, 2> assigned;
    assigned = src;
    CHECK_THAT(assigned.ToDouble(), Catch::Matchers::WithinAbs(3.14, 1e-3));
}

TEST_CASE("SqlNumeric operator<=> orders by numeric value", "[SqlNumeric]")
{
    SqlNumeric<10, 2> const a { 1.0 };
    SqlNumeric<10, 2> const b { 2.0 };
    SqlNumeric<10, 2> const c { 1.0 };

    CHECK((a <=> b) == std::partial_ordering::less);
    CHECK((b <=> a) == std::partial_ordering::greater);
    CHECK((a <=> c) == std::partial_ordering::equivalent);
    CHECK(a < b);
    CHECK(b > a);
    CHECK_FALSE(a < c);
}

TEST_CASE("std::formatter<SqlNumeric> renders the same as ToString()", "[SqlNumeric]")
{
    SqlNumeric<10, 2> const n { 42.5 };
    CHECK(std::format("{}", n) == n.ToString());
    CHECK(std::format("{}", n) == "42.50");
}

TEST_CASE("SqlNumeric operator== across heterogeneous precision/scale", "[SqlNumeric]")
{
    SqlNumeric<10, 2> const a { 5.5 };
    SqlNumeric<8, 3> const b { 5.5 };
    SqlNumeric<10, 2> const c { 5.6 };
    CHECK(a == b);
    CHECK_FALSE(a == c);
}

TEST_CASE("SqlNumeric ColumnType matches template arguments", "[SqlNumeric]")
{
    constexpr auto col = SqlNumeric<15, 4>::ColumnType;
    CHECK(col.precision == 15);
    CHECK(col.scale == 4);
}

TEST_CASE("SqlMaxNumericPrecision is the width this implementation can carry", "[SqlNumeric]")
{
    // SQL_MAX_NUMERIC_LEN is the mantissa size in *bytes* (16), not a digit count, so the historical
    // `Precision <= SQL_MAX_NUMERIC_LEN` was a category error that rejected DECIMAL(18, 2). But the
    // mantissa's 38-digit capacity is not the bound either — see the derivation on
    // SqlMaxNumericPrecision. Two things narrow it, and the bound is the tighter of the two:
    //
    //   - the unscaled carrier: `int64_t` (18 digits) without a 128-bit integer type,
    //   - the readable width: at most a 64-bit magnitude (19 digits), which is what the widest
    //     `long double` in use — the 80-bit x87 one — renders exactly. Where `long double` is
    //     narrower still (MSVC, Clang on Apple Silicon) ToString() is correspondingly narrower;
    //     that is an accessor property, not a representability one, and is asserted separately.
    STATIC_CHECK(detail::DecimalDigitsForBits(63) == 18);  // int64_t magnitude
    STATIC_CHECK(detail::DecimalDigitsForBits(64) == 19);  // 64-bit magnitude / x87 significand
    STATIC_CHECK(detail::DecimalDigitsForBits(127) == 38); // __int128 magnitude — deliberately NOT the bound

#if defined(LIGHTWEIGHT_INT128_T)
    STATIC_CHECK(SqlMaxNumericPrecision == 19);

    // MS SQL Server's `money` is DECIMAL(19, 4); here it must be expressible.
    STATIC_CHECK(SqlNumeric<19, 4>::Precision == 19);
    STATIC_CHECK(SqlNumeric<19, 4>::Scale == 4);
    STATIC_CHECK(SqlNumeric<19, 4>::ColumnType.precision == 19);
#else
    // Without a 128-bit integer the unscaled value is carried by an `int64_t`, whose magnitude
    // holds 18 digits. `SqlNumeric<19, 4>` at the `money` maximum would need an unscaled
    // 9223372036854775808 — one past INT64_MAX — and the out-of-range float-to-int conversion is
    // undefined behaviour that flips the sign on x86-64. Rejecting it at compile time is the point.
    STATIC_CHECK(SqlMaxNumericPrecision == 18);
#endif

    // DECIMAL(18, s), the widest column that works on every supported toolchain.
    STATIC_CHECK(SqlNumeric<18, 4>::Precision == 18);

    SqlNumeric<SqlMaxNumericPrecision, 4> const widest { 1234567890.1234 };
    CHECK(widest.ToString() == "1234567890.1234");
}

TEST_CASE("SqlNumeric carries every digit up to SqlMaxNumericPrecision", "[SqlNumeric]")
{
    // The bound's whole purpose: at SqlMaxNumericPrecision the worst case — an all-nines mantissa,
    // the largest value the precision admits — must still survive the unscaled carrier. One digit
    // more and it does not, which is why the bound is where it is. Values are injected through the
    // SQL_NUMERIC_STRUCT constructor because that is the state a native SQL_C_NUMERIC fetch leaves
    // behind (nativeValue == 0, mantissa verbatim); going through `assign()` would measure the
    // `double` argument instead.
    constexpr auto precision = SqlMaxNumericPrecision;

    auto allNines = std::uint64_t { 0 };
    for ([[maybe_unused]] auto const digit: std::views::iota(std::size_t { 0 }, precision))
        allNines = (allNines * 10) + 9;

    auto raw = SQL_NUMERIC_STRUCT {};
    raw.precision = static_cast<SQLCHAR>(precision);
    raw.scale = 0;
    raw.sign = 1;
    std::memcpy(static_cast<void*>(raw.val), &allNines, sizeof(allNines));

    SqlNumeric<precision, 0> const widest { raw };
    auto const expected = std::string(precision, '9');

    // The carrier's guarantee, and the one the bound is derived from: unconditional, on every
    // platform, independent of any floating-point type. The unscaled value is a `__int128` where
    // one exists, which std::format does not handle; at `precision <= 19` digits its magnitude
    // always fits a std::uint64_t, so narrow it there.
    CHECK(std::format("{}", static_cast<std::uint64_t>(widest.ToUnscaledValue())) == expected);

    // ToString() is a distinctly weaker, toolchain-dependent guarantee, because it — like every
    // accessor but ToUnscaledValue() — divides through `long double` and is then rendered by the
    // standard library's formatter. How many digits survive depends on *both*: the width of that
    // type (53 bits on MSVC and on Clang for Apple Silicon, 64 on the x87 80-bit one) and the
    // formatter's own long-double support, which in practice narrows to `double` on some
    // toolchains even where the type is wider. The portable promise — and the only one
    // docs/data-binder.md makes — is `std::numeric_limits<double>::digits10` significant digits,
    // so assert exactly that and nothing stronger. The strong claim lives on ToUnscaledValue()
    // above, where it actually holds.
    constexpr auto portableDigits = static_cast<std::size_t>(std::numeric_limits<double>::digits10);
    auto relativeTolerance = 1.0;
    for ([[maybe_unused]] auto const digit: std::views::iota(std::size_t { 1 }, portableDigits))
        relativeTolerance /= 10.0;
    CHECK_THAT(std::stod(widest.ToString()), Catch::Matchers::WithinRel(static_cast<double>(allNines), relativeTolerance));
}

TEST_CASE("SqlNumeric supports a purely fractional Scale == Precision", "[SqlNumeric]")
{
    // `DECIMAL(4, 4)` is legal SQL: four digits, all of them after the decimal point, i.e. the
    // value range [0.0000, 0.9999]. No conversion path needs an integral digit.
    STATIC_CHECK(SqlNumeric<4, 4>::Precision == 4);
    STATIC_CHECK(SqlNumeric<4, 4>::Scale == 4);
    STATIC_CHECK(SqlNumeric<4, 4>::ColumnType.precision == 4);
    STATIC_CHECK(SqlNumeric<4, 4>::ColumnType.scale == 4);

    SqlNumeric<4, 4> const fraction { 0.1234 };
    CHECK(fraction.ToString() == "0.1234");
    CHECK(static_cast<long long>(fraction.ToUnscaledValue()) == 1234);
    CHECK_THAT(fraction.ToDouble(), Catch::Matchers::WithinAbs(0.1234, 1e-9));
    CHECK_THAT(fraction.ToFloat(), Catch::Matchers::WithinAbs(0.1234F, 1e-6F));

    // The boundaries of the range, and the negative half.
    CHECK(SqlNumeric<4, 4>(0.9999).ToString() == "0.9999");
    CHECK(SqlNumeric<4, 4>(0.0).ToString() == "0.0000");
    CHECK(SqlNumeric<4, 4>(-0.1234).ToString() == "-0.1234");

    // Ordering and equality keep working across the fractional-only form.
    CHECK(SqlNumeric<4, 4>(0.1234) < SqlNumeric<4, 4>(0.5678));
    CHECK(SqlNumeric<4, 4>(0.1234) == SqlNumeric<8, 4>(0.1234));

    // Scale == Precision at the widest supported precision is fine too.
    STATIC_CHECK(SqlNumeric<SqlMaxNumericPrecision, SqlMaxNumericPrecision>::Scale == SqlMaxNumericPrecision);
}

TEST_CASE("SqlNumeric ToUnscaledValue scales by 10^Scale", "[SqlNumeric]")
{
    SqlNumeric<10, 2> const n { 1.23 };
    auto const scaled = n.ToUnscaledValue();
    CHECK(static_cast<long long>(scaled) == 123);

    SqlNumeric<10, 4> const m { 0.0001 };
    CHECK(static_cast<long long>(m.ToUnscaledValue()) == 1);
}

TEST_CASE("SqlNumericType concept matches SqlNumeric and rejects others", "[SqlNumeric]")
{
    STATIC_CHECK(SqlNumericType<SqlNumeric<10, 2>>);
    STATIC_CHECK(SqlNumericType<SqlNumeric<5, 1>>);
    STATIC_CHECK_FALSE(SqlNumericType<int>);
    STATIC_CHECK_FALSE(SqlNumericType<double>);
}

TEST_CASE("SqlDataBinder<SqlNumeric>::Inspect returns the same as ToString()", "[SqlNumeric]")
{
    SqlNumeric<10, 2> const n { 7.25 };
    CHECK(SqlDataBinder<SqlNumeric<10, 2>>::Inspect(n) == n.ToString());
}
