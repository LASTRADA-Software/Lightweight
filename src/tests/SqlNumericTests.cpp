// SPDX-License-Identifier: Apache-2.0

#include <Lightweight/DataBinder/SqlNumeric.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <compare>
#include <format>

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
