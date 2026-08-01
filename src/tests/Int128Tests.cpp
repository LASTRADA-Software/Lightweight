// SPDX-License-Identifier: Apache-2.0

#include <Lightweight/DataBinder/Int128.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>

using namespace Lightweight;
using Lightweight::detail::Int128Soft;
using Lightweight::detail::ToDecimalString;

// ================================================================================================
// detail::Int128Soft — the software 128-bit carrier.
//
// These tests exercise `Int128Soft` *directly and unconditionally*, on every toolchain, rather than
// only where it happens to be what `Int128` aliases. Without that, the software path — the one that
// exists specifically for MSVC and clang-cl — would go completely untested on the Linux/macOS CI
// legs that do have a native __int128_t, and a regression in it would only ever surface on Windows.
// Where a native type is available the tests below additionally cross-check the two implementations
// agree.
// ================================================================================================

namespace
{

/// The largest unscaled value MS SQL Server's `money` (DECIMAL(19, 4)) can hold: 922337203685477.5807
/// scaled by 10^4. This is the value that motivated the whole change — it is one past INT64_MAX, so
/// the previous `int64_t` carrier turned it into INT64_MIN and rendered it sign-flipped.
constexpr auto MoneyMaxUnscaled = std::uint64_t { 9'223'372'036'854'775'808ULL };

} // namespace

TEST_CASE("Int128Soft default-constructs to zero", "[Int128]")
{
    constexpr auto zero = Int128Soft {};
    STATIC_CHECK(zero.low == 0);
    STATIC_CHECK(zero.high == 0);
    STATIC_CHECK_FALSE(zero.IsNegative());
    CHECK(ToDecimalString(zero) == "0");
}

TEST_CASE("Int128Soft widens from 64-bit integers", "[Int128]")
{
    // Unsigned widens by zero-extension...
    constexpr auto positive = Int128Soft { std::uint64_t { 42 } };
    STATIC_CHECK(positive.low == 42);
    STATIC_CHECK(positive.high == 0);

    // ...signed by sign-extension, so a negative value fills the high word with ones.
    constexpr auto negative = Int128Soft { std::int64_t { -42 } };
    STATIC_CHECK(negative.high == ~std::uint64_t { 0 });
    STATIC_CHECK(negative.IsNegative());

    CHECK(ToDecimalString(positive) == "42");
    CHECK(ToDecimalString(negative) == "-42");

    // The boundaries of the 64-bit types must survive widening intact.
    CHECK(ToDecimalString(Int128Soft { std::numeric_limits<std::int64_t>::max() }) == "9223372036854775807");
    CHECK(ToDecimalString(Int128Soft { std::numeric_limits<std::int64_t>::min() }) == "-9223372036854775808");
    CHECK(ToDecimalString(Int128Soft { std::numeric_limits<std::uint64_t>::max() }) == "18446744073709551615");
}

TEST_CASE("Int128Soft negation round-trips", "[Int128]")
{
    constexpr auto value = Int128Soft { std::uint64_t { 12345 } };
    STATIC_CHECK(-(-value) == value);
    STATIC_CHECK((-value).IsNegative());
    STATIC_CHECK(+value == value);
    CHECK(ToDecimalString(-value) == "-12345");

    // Negating zero stays zero rather than producing a negative zero representation.
    STATIC_CHECK(-Int128Soft {} == Int128Soft {});

    // Crossing the 64-bit boundary: negating a value whose low word is zero must borrow correctly.
    constexpr auto highOnly = Int128Soft { std::uint64_t { 0 }, std::uint64_t { 1 } }; // 2^64
    CHECK(ToDecimalString(highOnly) == "18446744073709551616");
    CHECK(ToDecimalString(-highOnly) == "-18446744073709551616");
    STATIC_CHECK(-(-highOnly) == highOnly);
}

TEST_CASE("Int128Soft orders signed values correctly", "[Int128]")
{
    constexpr auto negative = Int128Soft { std::int64_t { -5 } };
    constexpr auto zero = Int128Soft {};
    constexpr auto small = Int128Soft { std::uint64_t { 5 } };
    constexpr auto large = Int128Soft { std::uint64_t { 0 }, std::uint64_t { 1 } }; // 2^64

    // A negative value is less than every non-negative one, even though its *unsigned* bit pattern
    // is the larger — the ordering must consult the sign bit first.
    STATIC_CHECK(negative < zero);
    STATIC_CHECK(negative < small);
    STATIC_CHECK(negative < large);
    STATIC_CHECK(zero < small);
    STATIC_CHECK(small < large);
    STATIC_CHECK(large > small);

    // Two negatives compare by magnitude in the right direction.
    STATIC_CHECK(Int128Soft { std::int64_t { -10 } } < Int128Soft { std::int64_t { -5 } });

    STATIC_CHECK(small == Int128Soft { std::uint64_t { 5 } });
    STATIC_CHECK(small != large);
}

TEST_CASE("Int128Soft converts from floating point without overflow", "[Int128]")
{
    // The motivating case: `money`'s maximum unscaled value is one past INT64_MAX. An int64_t
    // carrier made this UB (sign-flipped on x86-64); the 128-bit carrier represents it exactly.
    auto const moneyMax = Int128Soft { 9'223'372'036'854'775'808.0L };
    CHECK(ToDecimalString(moneyMax) == "9223372036854775808");
    CHECK_FALSE(moneyMax.IsNegative());

    // ...and its negative counterpart.
    auto const moneyMin = Int128Soft { -9'223'372'036'854'775'808.0L };
    CHECK(ToDecimalString(moneyMin) == "-9223372036854775808");
    CHECK(moneyMin.IsNegative());

    // Small values, both signs, and truncation toward zero.
    CHECK(ToDecimalString(Int128Soft { 42.0 }) == "42");
    CHECK(ToDecimalString(Int128Soft { -42.0 }) == "-42");
    CHECK(ToDecimalString(Int128Soft { 42.9 }) == "42");
    CHECK(ToDecimalString(Int128Soft { -42.9 }) == "-42");
    CHECK(ToDecimalString(Int128Soft { 0.0 }) == "0");
    CHECK(ToDecimalString(Int128Soft { 0.9 }) == "0");
}

TEST_CASE("Int128Soft saturates rather than invoking UB on out-of-range floats", "[Int128]")
{
    // An out-of-range float-to-integer conversion is undefined behaviour; the whole reason this type
    // exists is that the old carrier performed one silently. Beyond the representable range the
    // result must be the clamped extreme, and — critically — must keep the sign of the input rather
    // than flipping it the way the int64_t overflow did.
    auto const huge = Int128Soft { 1.0e40L };
    CHECK_FALSE(huge.IsNegative());
    CHECK(ToDecimalString(huge) == "170141183460469231731687303715884105727"); // 2^127 - 1

    auto const hugeNegative = Int128Soft { -1.0e40L };
    CHECK(hugeNegative.IsNegative());
    CHECK(ToDecimalString(hugeNegative) == "-170141183460469231731687303715884105728"); // -2^127

    // Infinity saturates the same way.
    CHECK(Int128Soft { std::numeric_limits<long double>::infinity() }.IsNegative() == false);
    CHECK(Int128Soft { -std::numeric_limits<long double>::infinity() }.IsNegative());

    // NaN has no integer image; zero is the defined answer rather than garbage.
    CHECK(Int128Soft { std::numeric_limits<long double>::quiet_NaN() } == Int128Soft {});
}

TEST_CASE("Int128Soft converts to floating point", "[Int128]")
{
    CHECK_THAT(Int128Soft { std::uint64_t { 42 } }.To<double>(), Catch::Matchers::WithinAbs(42.0, 1e-9));
    CHECK_THAT(Int128Soft { std::int64_t { -42 } }.To<double>(), Catch::Matchers::WithinAbs(-42.0, 1e-9));
    CHECK_THAT(static_cast<double>(Int128Soft { std::uint64_t { 7 } }), Catch::Matchers::WithinAbs(7.0, 1e-9));
    CHECK_THAT(static_cast<float>(Int128Soft { std::uint64_t { 7 } }), Catch::Matchers::WithinAbs(7.0F, 1e-6F));

    // Values above 2^64 must combine the two words, not just read the low one.
    auto const twoPow64 = Int128Soft { std::uint64_t { 0 }, std::uint64_t { 1 } };
    CHECK_THAT(twoPow64.To<double>(), Catch::Matchers::WithinRel(18446744073709551616.0, 1e-15));
    CHECK_THAT(static_cast<double>(-twoPow64), Catch::Matchers::WithinRel(-18446744073709551616.0, 1e-15));

    // The round trip through `long double` for a value at the precision bound. 2^63 is a power of
    // two, hence exactly representable in every floating-point format, so this holds even where
    // `long double` is narrow (MSVC) or emulated at 53 bits (the CI SQLite3 leg runs under Valgrind).
    auto const moneyMax = Int128Soft { 9'223'372'036'854'775'808.0L };
    CHECK(static_cast<double>(moneyMax.To<long double>()) == 9223372036854775808.0);
}

TEST_CASE("Int128Soft narrows to 64-bit like a native __int128_t does", "[Int128]")
{
    // Narrowing truncates to the low word, matching `static_cast<int64_t>` on the native type.
    constexpr auto value = Int128Soft { std::uint64_t { 0xDEAD'BEEF }, std::uint64_t { 0xFFFF } };
    STATIC_CHECK(static_cast<std::uint64_t>(value) == 0xDEAD'BEEFULL);
    STATIC_CHECK(static_cast<std::int64_t>(value) == 0xDEAD'BEEFLL);
    STATIC_CHECK(static_cast<std::int32_t>(Int128Soft { std::uint64_t { 1234 } }) == 1234);
}

TEST_CASE("Int128Soft renders decimal across the full 128-bit range", "[Int128]")
{
    // Rendering divides by 10^19 repeatedly, so the interesting cases are around chunk boundaries
    // and at the extremes where the padding/trimming of the most significant chunk matters.
    CHECK(ToDecimalString(Int128Soft { std::uint64_t { 1 } }) == "1");
    CHECK(ToDecimalString(Int128Soft { std::uint64_t { 10 } }) == "10");

    // Exactly one chunk (10^19 - 1), then one past it, where a second chunk appears and the first
    // must be zero-padded to 19 digits.
    CHECK(ToDecimalString(Int128Soft { std::uint64_t { 9'999'999'999'999'999'999ULL } }) == "9999999999999999999");
    CHECK(ToDecimalString(Int128Soft { std::uint64_t { 10'000'000'000'000'000'000ULL } }) == "10000000000000000000");

    // A value with interior zeros, which the zero-padding of a non-final chunk must preserve.
    // 10^20 == 5 * 2^20 * 10^... — construct it directly from the word pair instead:
    // 100000000000000000000 = 0x5_6BC7_5E2D_6310_0000
    auto const tenPow20 = Int128Soft { std::uint64_t { 0x6BC7'5E2D'6310'0000ULL }, std::uint64_t { 0x5 } };
    CHECK(ToDecimalString(tenPow20) == "100000000000000000000");

    // The extremes of the type.
    auto const maxValue = Int128Soft { ~std::uint64_t { 0 }, ~std::uint64_t { 0 } >> 1 };
    CHECK(ToDecimalString(maxValue) == "170141183460469231731687303715884105727"); // 2^127 - 1

    auto const minValue = Int128Soft { std::uint64_t { 0 }, std::uint64_t { 1 } << 63 };
    CHECK(ToDecimalString(minValue) == "-170141183460469231731687303715884105728"); // -2^127
}

TEST_CASE("Int128Soft byte layout matches SQL_NUMERIC_STRUCT::val", "[Int128]")
{
    // The carrier is memcpy'd straight into and out of the ODBC struct's little-endian mantissa, so
    // its representation has to be exactly that: 16 bytes, low word first.
    STATIC_CHECK(sizeof(Int128Soft) == 16);

    auto const value = Int128Soft { MoneyMaxUnscaled };

    auto bytes = std::array<unsigned char, 16> {};
    std::memcpy(bytes.data(), &value, sizeof(value));

    // 9223372036854775808 == 2^63 — the low word's top bit set, everything else clear.
    CHECK(bytes[7] == 0x80);
    for (auto const index: { 0, 1, 2, 3, 4, 5, 6, 8, 9, 10, 11, 12, 13, 14, 15 })
        CHECK(bytes[static_cast<std::size_t>(index)] == 0x00);

    // ...and the reverse direction reconstructs it.
    auto restored = Int128Soft {};
    std::memcpy(&restored, bytes.data(), sizeof(restored));
    CHECK(restored == value);
    CHECK(ToDecimalString(restored) == "9223372036854775808");
}

TEST_CASE("Int128 is 128 bits wide on every toolchain", "[Int128]")
{
    // The property the review comment asked for: the carrier's width — and therefore the widest
    // declarable SqlNumeric precision — does not depend on which compiler built the executable.
    STATIC_CHECK(sizeof(Int128) == 16);

    CHECK(detail::Int128ToString(static_cast<Int128>(9'223'372'036'854'775'808.0L)) == "9223372036854775808");
    CHECK(detail::Int128ToString(static_cast<Int128>(-9'223'372'036'854'775'808.0L)) == "-9223372036854775808");
    CHECK(detail::Int128ToString(Int128 {}) == "0");
}

#if defined(LIGHTWEIGHT_HAVE_NATIVE_INT128)
TEST_CASE("Int128Soft agrees with the native __int128_t", "[Int128]")
{
    // Where both implementations exist, the software one is only correct if it is indistinguishable
    // from the native one. Cross-check the operations SqlNumeric actually performs over a spread of
    // values, including the ones that overflow 64 bits.
    auto const check = [](long double input) {
        auto const soft = Int128Soft { input };
        auto const native = static_cast<__int128_t>(input);

        // Same decimal rendering...
        CHECK(ToDecimalString(soft) == detail::Int128ToString(native));

        // ...same floating-point image. Compared via `double` rather than `long double`: the CI
        // SQLite3 leg runs the suite under Valgrind, which does not emulate the x87 80-bit
        // `long double` faithfully. Under it the *native* `__int128_t -> long double` conversion
        // (`__floattixf`) returns 0 for small negative values — verified by reproducing this exact
        // failure for -1 and -42 under `valgrind` locally — so a `long double` comparison fails on
        // the reference side while the shim is correct. `double` is exact for every probe value
        // below (both sides round identically, even at 1e30) and is emulated faithfully.
        CHECK(soft.To<double>() == static_cast<double>(native));

        // ...same 16-byte little-endian representation, which is what gets memcpy'd into ODBC.
        auto softBytes = std::array<unsigned char, 16> {};
        auto nativeBytes = std::array<unsigned char, 16> {};
        std::memcpy(softBytes.data(), &soft, sizeof(soft));
        std::memcpy(nativeBytes.data(), &native, sizeof(native));
        CHECK(softBytes == nativeBytes);

        // ...and the same sign and negation behaviour.
        CHECK(soft.IsNegative() == (native < 0));
        CHECK(ToDecimalString(-soft) == detail::Int128ToString(static_cast<__int128_t>(-native)));
    };

    check(0.0L);
    check(1.0L);
    check(-1.0L);
    check(42.0L);
    check(-42.0L);
    check(9'223'372'036'854'775'807.0L);  // INT64_MAX
    check(9'223'372'036'854'775'808.0L);  // money's max unscaled — one past INT64_MAX
    check(-9'223'372'036'854'775'808.0L); // INT64_MIN
    check(1.0e19L);
    check(-1.0e19L);
    check(1.0e30L);
    check(-1.0e30L);
}
#endif
