// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <array>
#include <cmath>
#include <compare>
#include <concepts>
#include <cstdint>
#include <string>

namespace Lightweight
{

// clang-cl doesn't support __int128_t but defines __SIZEOF_INT128__
// and also since it pretends to be MSVC, it also defines _MSC_VER
// clang-format off
#if defined(__SIZEOF_INT128__) && !defined(_MSC_VER)
    #define LIGHTWEIGHT_HAVE_NATIVE_INT128 1
#endif
// clang-format on

namespace detail
{

    /// Signed 128-bit integer with just enough arithmetic to carry a fixed-point unscaled value.
    ///
    /// This exists so that `SqlNumeric`'s unscaled carrier is 128 bits wide on *every* toolchain. Where
    /// the compiler offers `__int128_t` that type is used directly (see `Int128` below); MSVC and
    /// clang-cl have no such type, and this software implementation stands in for it there. Without it
    /// the carrier would fall back to `int64_t`, which holds only 18 decimal digits, and the widest
    /// declarable precision would differ between toolchains — meaning a `ddl2cpp`-generated record for a
    /// `money` column (`DECIMAL(19, 4)`) would compile under GCC/Clang and fail under MSVC. A schema is a
    /// property of the database, not of the compiler that happens to build the client.
    ///
    /// @see Int128, SqlMaxNumericPrecision
    ///
    /// The operation set is deliberately minimal — construction from and conversion to the floating-point
    /// types, negation, comparison, and decimal rendering — because that is all `SqlNumeric` needs. It is
    /// not a general-purpose big integer: there is no multiplication, no division by another `Int128Soft`,
    /// and no bitwise interface beyond what the conversions require.
    ///
    /// Representation is sign-magnitude-free two's complement across a low/high pair, matching the
    /// little-endian layout of `SQL_NUMERIC_STRUCT::val` so that a `std::memcpy` in either direction is a
    /// faithful round-trip.
    struct Int128Soft
    {
        /// Low 64 bits of the two's-complement magnitude.
        std::uint64_t low {};

        /// High 64 bits of the two's-complement magnitude, including the sign bit.
        std::uint64_t high {};

        /// Default-constructs a zero value.
        constexpr Int128Soft() noexcept = default;

        /// Constructs from a signed 64-bit integer, sign-extending into the high word.
        /// @param value Value to widen.
        constexpr Int128Soft(std::int64_t value) noexcept: // NOLINT(google-explicit-constructor)
            low { static_cast<std::uint64_t>(value) },
            high { value < 0 ? ~std::uint64_t { 0 } : std::uint64_t { 0 } }
        {
        }

        /// Constructs from an unsigned 64-bit integer, zero-extending into the high word.
        /// @param value Value to widen.
        constexpr Int128Soft(std::uint64_t value) noexcept: // NOLINT(google-explicit-constructor)
            low { value }
        {
        }

        /// Constructs from an explicit low/high word pair.
        /// @param lowWord Low 64 bits.
        /// @param highWord High 64 bits, including the sign bit.
        constexpr Int128Soft(std::uint64_t lowWord, std::uint64_t highWord) noexcept:
            low { lowWord },
            high { highWord }
        {
        }

        /// Constructs from a floating-point value by truncation toward zero.
        ///
        /// Values whose magnitude is at or beyond 2^127 saturate to the corresponding extreme rather than
        /// invoking the undefined behaviour an out-of-range `static_cast` to an integer type would. That
        /// is a large part of the point of routing through this type: a narrower carrier makes the
        /// conversion of `money`'s maximum out of range, and on x86-64 that silently flips its sign.
        ///
        /// @param value Value to truncate. Must be finite; NaN yields zero.
        template <std::floating_point T>
        constexpr explicit Int128Soft(T value) noexcept
        {
            if (!(value == value)) // NaN — no meaningful integer image.
                return;

            auto const negative = value < T { 0 };
            auto magnitude = negative ? -value : value;

            // 2^127, the first magnitude a signed 128-bit integer cannot represent. Computed by repeated
            // doubling so it stays exact in every floating-point format (2^127 is a power of two, hence
            // representable in all of them, but a literal would depend on the type's width).
            auto limit = T { 1 };
            for (auto bit = 0; bit != 127; ++bit)
                limit *= T { 2 };

            if (magnitude >= limit)
            {
                // Saturate: -2^127 .. 2^127-1.
                low = negative ? std::uint64_t { 0 } : ~std::uint64_t { 0 };
                high = negative ? (std::uint64_t { 1 } << 63) : (~std::uint64_t { 0 } >> 1);
                return;
            }

            // Split into two 64-bit halves. `magnitude / 2^64` truncated is the high word; the remainder
            // is the low word. Both subexpressions are exact: the quotient is < 2^63 and the remainder is
            // recovered by subtraction rather than by a second division.
            auto divisor = T { 1 };
            for (auto bit = 0; bit != 64; ++bit)
                divisor *= T { 2 };

            auto const highPart = std::trunc(magnitude / divisor);
            auto const lowPart = std::trunc(magnitude - (highPart * divisor));

            low = static_cast<std::uint64_t>(lowPart);
            high = static_cast<std::uint64_t>(highPart);

            if (negative)
                *this = -*this;
        }

        /// @return `true` if the value is negative.
        [[nodiscard]] constexpr bool IsNegative() const noexcept
        {
            return (high >> 63) != 0;
        }

        /// Negates the value (two's complement).
        /// @return The additive inverse. `-(-2^127)` saturates to itself, as it does for every two's-complement type.
        [[nodiscard]] constexpr Int128Soft operator-() const noexcept
        {
            auto const invertedLow = ~low;
            auto const invertedHigh = ~high;
            auto const carriedLow = invertedLow + 1;
            auto const carriedHigh = invertedHigh + (carriedLow < invertedLow ? 1 : 0);
            return { carriedLow, carriedHigh };
        }

        /// @return The value unchanged (provided for symmetry with `operator-`).
        [[nodiscard]] constexpr Int128Soft operator+() const noexcept
        {
            return *this;
        }

        /// Converts to a floating-point type.
        ///
        /// Precision is bounded by the target type's significand, exactly as it is for the native
        /// `__int128_t` this stands in for.
        ///
        /// @tparam T Target floating-point type.
        /// @return The value as `T`, rounded to that type's precision.
        template <std::floating_point T>
        [[nodiscard]] constexpr T To() const noexcept
        {
            auto const negative = IsNegative();
            auto const magnitude = negative ? -*this : *this;

            auto scale = T { 1 };
            for (auto bit = 0; bit != 64; ++bit)
                scale *= T { 2 };

            auto const result = (static_cast<T>(magnitude.high) * scale) + static_cast<T>(magnitude.low);
            return negative ? -result : result;
        }

        /// @return The value as a `float`.
        [[nodiscard]] constexpr explicit operator float() const noexcept
        {
            return To<float>();
        }

        /// @return The value as a `double`.
        [[nodiscard]] constexpr explicit operator double() const noexcept
        {
            return To<double>();
        }

        /// @return The value as a `long double`.
        [[nodiscard]] constexpr explicit operator long double() const noexcept
        {
            return To<long double>();
        }

        /// Narrows to a signed 64-bit integer, truncating the high word.
        /// @return The low 64 bits reinterpreted as signed, matching `static_cast<int64_t>` on a native `__int128_t`.
        [[nodiscard]] constexpr explicit operator std::int64_t() const noexcept
        {
            return static_cast<std::int64_t>(low);
        }

        /// Narrows to an unsigned 64-bit integer, truncating the high word.
        /// @return The low 64 bits, matching `static_cast<uint64_t>` on a native `__int128_t`.
        [[nodiscard]] constexpr explicit operator std::uint64_t() const noexcept
        {
            return low;
        }

        /// Narrows to a signed 32-bit integer, truncating.
        [[nodiscard]] constexpr explicit operator std::int32_t() const noexcept
        {
            return static_cast<std::int32_t>(low);
        }

        /// Equality comparison.
        [[nodiscard]] constexpr bool operator==(Int128Soft const& other) const noexcept = default;

        /// Three-way comparison, signed.
        /// @param other Value to compare against.
        /// @return The ordering of `*this` relative to `other`.
        [[nodiscard]] constexpr std::strong_ordering operator<=>(Int128Soft const& other) const noexcept
        {
            if (auto const negative = IsNegative(); negative != other.IsNegative())
                return negative ? std::strong_ordering::less : std::strong_ordering::greater;

            if (high != other.high)
                return high <=> other.high;

            return low <=> other.low;
        }
    };

    /// Renders a software 128-bit integer in base 10.
    ///
    /// `std::format` has no support for 128-bit integers — not even for the native `__int128_t` — so
    /// decimal rendering is done here by schoolbook long division over 32-bit limbs.
    ///
    /// @param value Value to render.
    /// @return Decimal representation, with a leading '-' for negative values.
    [[nodiscard]] inline std::string ToDecimalString(Int128Soft value) noexcept
    {
        if (value == Int128Soft {})
            return "0";

        auto const negative = value.IsNegative();

        // Work on the magnitude as an unsigned 128-bit pair. Negating the most-negative value overflows
        // back to itself, but its two's-complement bit pattern is exactly the unsigned magnitude 2^127,
        // so treating the words as unsigned below is correct for it too.
        auto const magnitude = negative ? -value : value;
        auto low = magnitude.low;
        auto high = magnitude.high;

        // Divide repeatedly by 10^9, peeling off nine digits per pass. The divisor has to stay below
        // 2^32: each step of the long division forms `(remainder << 32) | limb`, and that only fits a
        // std::uint64_t while `remainder < divisor <= 2^32`. (10^19 would be the largest power of ten a
        // std::uint64_t *holds*, but its remainders overflow that shift — the digits then come out
        // scrambled, which is exactly what the round-trip tests catch.)
        constexpr auto chunkDivisor = std::uint64_t { 1'000'000'000ULL };
        constexpr auto chunkDigits = 9;

        auto chunks = std::string {};

        while (high != 0 || low != 0)
        {
            auto remainder = std::uint64_t { 0 };

            auto const limbs = std::array<std::uint32_t, 4> {
                static_cast<std::uint32_t>(high >> 32),
                static_cast<std::uint32_t>(high & 0xFFFF'FFFFULL),
                static_cast<std::uint32_t>(low >> 32),
                static_cast<std::uint32_t>(low & 0xFFFF'FFFFULL),
            };

            auto quotientLimbs = std::array<std::uint32_t, 4> {};

            for (auto index = std::size_t { 0 }; index != limbs.size(); ++index)
            {
                // `remainder < 10^9 < 2^32`, so `(remainder << 32) | limb` stays within 64 bits.
                auto const dividend = (remainder << 32) | limbs[index];
                quotientLimbs[index] = static_cast<std::uint32_t>(dividend / chunkDivisor);
                remainder = dividend % chunkDivisor;
            }

            high = (static_cast<std::uint64_t>(quotientLimbs[0]) << 32) | quotientLimbs[1];
            low = (static_cast<std::uint64_t>(quotientLimbs[2]) << 32) | quotientLimbs[3];

            // Emit this chunk's digits least-significant first; they are zero-padded unless this was the
            // final (most significant) chunk, whose padding is trimmed after the loop.
            for (auto digit = 0; digit != chunkDigits; ++digit)
            {
                chunks.push_back(static_cast<char>('0' + static_cast<char>(remainder % 10)));
                remainder /= 10;
            }
        }

        // Drop the zero padding of the most significant chunk, then reverse into print order.
        while (chunks.size() > 1 && chunks.back() == '0')
            chunks.pop_back();

        if (negative)
            chunks.push_back('-');

        return std::string { chunks.rbegin(), chunks.rend() };
    }

} // namespace detail

/// The unscaled carrier `SqlNumeric` keeps `value * 10^Scale` in: a signed 128-bit integer on every
/// supported toolchain.
///
/// Where the compiler provides `__int128_t` that is used directly, so nothing changes for GCC and
/// Clang. MSVC and clang-cl get `detail::Int128Soft`, a software stand-in with the same width and the
/// same conversion behaviour. Both are 16 bytes and little-endian, matching `SQL_NUMERIC_STRUCT::val`
/// byte for byte.
#if defined(LIGHTWEIGHT_HAVE_NATIVE_INT128)
using Int128 = __int128_t;
#else
using Int128 = detail::Int128Soft;
#endif

static_assert(sizeof(Int128) == 16, "The unscaled carrier must be exactly as wide as SQL_NUMERIC_STRUCT::val.");

namespace detail
{

    /// Renders the unscaled carrier in base 10, whichever of the two implementations it is.
    ///
    /// `std::format` supports neither `__int128_t` nor `Int128Soft`, so both paths are handled here.
    ///
    /// @param value Value to render.
    /// @return Decimal representation, with a leading '-' for negative values.
    [[nodiscard]] inline std::string Int128ToString(Int128 value) noexcept
    {
#if defined(LIGHTWEIGHT_HAVE_NATIVE_INT128)
        auto const negative = value < 0;
        auto magnitude = static_cast<__uint128_t>(negative ? -value : value);

        if (magnitude == 0)
            return "0";

        auto reversed = std::string {};
        while (magnitude != 0)
        {
            reversed.push_back(static_cast<char>('0' + static_cast<char>(magnitude % 10)));
            magnitude /= 10;
        }
        if (negative)
            reversed.push_back('-');

        return std::string { reversed.rbegin(), reversed.rend() };
#else
        return ToDecimalString(value);
#endif
    }

} // namespace detail

} // namespace Lightweight
