// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../SqlColumnTypeDefinitions.hpp"
#include "../SqlError.hpp"
#include "Int128.hpp"
#include "Primitives.hpp"

#include <bit>
#include <cmath>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstring>
#include <format>
#include <source_location>
#include <string>

namespace Lightweight
{

static_assert(sizeof(Int128) == sizeof(SQL_NUMERIC_STRUCT::val));

namespace detail
{

    /// Number of whole decimal digits that fit into a binary magnitude of `bits` bits.
    ///
    /// `floor(bits * log10(2))`, evaluated with integer arithmetic (`log10(2) ~= 0.30103`) so that
    /// it stays usable in a constant expression without pulling in `<cmath>` at compile time.
    ///
    /// @param bits Width of the binary magnitude, excluding any sign bit.
    /// @return Number of decimal digits that magnitude represents without loss.
    [[nodiscard]] constexpr std::size_t DecimalDigitsForBits(std::size_t bits) noexcept
    {
        return (bits * 30103) / 100'000;
    }

} // namespace detail

/// Widest `Precision` a `SqlNumeric` may declare, in decimal digits.
///
/// **This is the same value on every toolchain.** A column's precision is a property of the
/// database schema, so the type that maps it must not depend on which compiler builds the client:
/// a `ddl2cpp`-generated record has to compile everywhere or the generator is useless. `Int128`
/// exists to make that true — it is `__int128_t` where the compiler has one and a software
/// stand-in (`detail::Int128Soft`) where it does not, so the unscaled carrier is 128 bits wide
/// under MSVC and clang-cl as well.
///
/// The bound is deliberately *not* derived from `SQL_MAX_NUMERIC_LEN`. That macro is the size **in
/// bytes** of `SQL_NUMERIC_STRUCT::val` (16, i.e. a 128-bit mantissa), not a decimal precision;
/// comparing a digit count against it is a category error that rejects perfectly ordinary columns
/// such as `DECIMAL(18, 2)`.
///
/// Bounding by the mantissa's theoretical capacity instead (128 bits -> 38 digits) is the same
/// category error in the other direction: 38 is what the ODBC *struct* can hold, not what this
/// implementation can read back. What narrows it is the **readable width**: every accessor except
/// `ToUnscaledValue()` and `ToString()` — that is, `ToFloat`, `ToDouble` and `ToLongDouble` —
/// divides through `long double`, so no more digits can be read back through those than that
/// type's significand holds. The widest one in use is the 80-bit x87 `long double`, whose 64-bit
/// significand gives `DecimalDigitsForBits(64)` == 19; beyond that nothing is readable on *any*
/// platform, e.g. a fetched `SqlNumeric<20, 0>` holding 99999999999999999999 reads back as
/// 100000000000000000000.
///
/// Hence 19, everywhere. `DECIMAL(18, s)` and MS SQL Server's `money` (`DECIMAL(19, 4)`) both
/// compile on every supported toolchain; a wider column must be read as a string.
///
/// NB: 19 is the point past which nothing is readable *anywhere*. It is emphatically not a promise
/// that any given platform delivers 19 digits through the *floating-point* accessors: those are
/// bounded by the width of `long double`, which is 53 bits on MSVC and on Clang for Apple Silicon.
/// The width they guarantee everywhere is `std::numeric_limits<double>::digits10`. Above that,
/// `ToUnscaledValue()` and `ToString()` are the accessors to rely on: neither divides through a
/// floating-point type, so both carry all 19 digits on every toolchain.
/// `docs/data-binder.md` tabulates what each accessor delivers.
///
/// NB: `inline` is load-bearing. At namespace scope `constexpr` implies `const`, hence internal
/// linkage, and an exported template in the module interface (`SqlNumeric`, via its static_assert)
/// may not reference an internal-linkage entity.
inline constexpr std::size_t SqlMaxNumericPrecision =
    detail::DecimalDigitsForBits(64); // 19 — bounded by the `long double` significand every accessor divides through

/// Represents a fixed-point number with a given precision and scale.
///
/// Precision is *exactly* the total number of digits in the number,
/// including the digits after the decimal point.
///
/// Scale is the number of digits after the decimal point, and may be anywhere in `[0, Precision]`.
/// `Scale == Precision` denotes a purely fractional number, e.g. `SqlNumeric<4, 4>` covers
/// `[0.0000, 0.9999]` — the C++ equivalent of SQL's `DECIMAL(4, 4)`.
///
/// @ingroup DataTypes
template <std::size_t ThePrecision, std::size_t TheScale>
struct SqlNumeric
{
    /// Number of total digits
    static constexpr auto Precision = ThePrecision;

    /// Number of digits after the decimal point
    static constexpr auto Scale = TheScale;

    /// The SQL column type definition for this numeric type.
    static constexpr auto ColumnType = SqlColumnTypeDefinitions::Decimal { .precision = Precision, .scale = TheScale };

    static_assert(Precision > 0, "A fixed-point number must have at least one digit.");
    // NB: This bound is 19 on every toolchain, so `SqlNumeric<19, 4>` — what ddl2cpp emits for MS
    // SQL Server's `money` — compiles everywhere. It is the same value under MSVC and clang-cl
    // because `Int128` supplies a software 128-bit carrier where the compiler has no native one;
    // see SqlMaxNumericPrecision for the derivation.
    static_assert(Precision <= SqlMaxNumericPrecision,
                  "Precision exceeds the number of decimal digits this implementation can carry. Read the column as a "
                  "string instead, or narrow the column.");
    // `DECIMAL(p, s)` requires 0 <= s <= p; `s == p` denotes a purely fractional number (e.g.
    // DECIMAL(4, 4) holds [0, 1) with four fractional digits) and is legal in every supported
    // backend. No conversion path here needs an integral digit: the value is kept as the unscaled
    // integer `value * 10^Scale`, and every accessor divides that by `10^Scale` again.
    static_assert(Scale <= Precision, "Scale counts digits after the decimal point and cannot exceed Precision.");

    /// The SQL numeric struct for ODBC binding.
    SQL_NUMERIC_STRUCT sqlValue {};

    /// Cached native value for drivers without SQL_NUMERIC_STRUCT support.
    double nativeValue {};

    /// Default constructor.
    constexpr SqlNumeric() noexcept = default;
    /// Move constructor.
    constexpr SqlNumeric(SqlNumeric&&) noexcept = default;
    /// Move assignment operator.
    constexpr SqlNumeric& operator=(SqlNumeric&&) noexcept = default;
    /// Copy constructor.
    constexpr SqlNumeric(SqlNumeric const&) noexcept = default;
    /// Copy assignment operator.
    constexpr SqlNumeric& operator=(SqlNumeric const&) noexcept = default;
    constexpr ~SqlNumeric() noexcept = default;

    /// Constructs a numeric from a floating point value.
    constexpr SqlNumeric(std::floating_point auto value) noexcept
    {
        assign(value);
    }

    /// Constructs a numeric from a SQL_NUMERIC_STRUCT.
    constexpr explicit SqlNumeric(SQL_NUMERIC_STRUCT const& value) noexcept:
        sqlValue(value)
    {
    }

    // For encoding/decoding purposes, we assume little-endian.
    static_assert(std::endian::native == std::endian::little);

    /// Assigns a value to the numeric.
    LIGHTWEIGHT_FORCE_INLINE constexpr void assign(std::floating_point auto inputValue) noexcept
    {
        nativeValue = static_cast<decltype(nativeValue)>(inputValue);

        sqlValue = {};
        sqlValue.sign = std::signbit(inputValue) ? 0 : 1;
        sqlValue.precision = static_cast<SQLCHAR>(Precision);
        sqlValue.scale = static_cast<SQLSCHAR>(Scale);

        auto const unscaledValue = std::roundl(static_cast<long double>(std::abs(inputValue) * std::powl(10.0L, Scale)));

        // `Int128` is 128 bits wide on every toolchain, so the unscaled value of even the widest
        // declarable precision fits, and the conversion stays in range. `sqlValue.val` is
        // little-endian and exactly this wide (asserted above).
        auto const num = static_cast<Int128>(unscaledValue);
        std::memcpy(sqlValue.val, &num, sizeof(num));
    }

    /// Assigns a floating point value to the numeric.
    LIGHTWEIGHT_FORCE_INLINE constexpr SqlNumeric& operator=(std::floating_point auto value) noexcept
    {
        assign(value);
        return *this;
    }

    /// Converts the numeric to an unscaled integer value.
    ///
    /// Along with `ToString()`, which renders from this value, it is one of the two accessors that
    /// do not divide through a floating-point type, and therefore carries every digit up to
    /// `SqlMaxNumericPrecision` on every toolchain. The floating-point accessors do not.
    ///
    /// @return `value * 10^Scale` as a signed 128-bit integer.
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE Int128 ToUnscaledValue() const noexcept
    {
        if (nativeValue != 0.0)
            return static_cast<Int128>(std::roundl(nativeValue * std::powl(10.0L, Scale)));

        // `sqlValue.val` sits at offset 3 of a 1-aligned struct, so it cannot be dereferenced
        // through a 128-bit pointer without a misaligned load; copy it out instead. Both `Int128`
        // implementations are little-endian 16-byte two's complement, matching the field's layout.
        auto magnitude = Int128 {};
        std::memcpy(&magnitude, sqlValue.val, sizeof(magnitude));

        return sqlValue.sign ? magnitude : -magnitude;
    }

    /// Converts the numeric to a floating point value.
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE float ToFloat() const noexcept
    {
        return static_cast<float>(ToUnscaledValue()) / std::powf(10, sqlValue.scale);
    }

    /// Converts the numeric to a double precision floating point value.
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE double ToDouble() const noexcept
    {
        return static_cast<double>(ToUnscaledValue()) / std::pow(10, sqlValue.scale);
    }

    /// Converts the numeric to a long double precision floating point value.
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE long double ToLongDouble() const noexcept
    {
        return static_cast<long double>(ToUnscaledValue()) / std::pow(10, sqlValue.scale);
    }

    /// Converts the numeric to a floating point value.
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE explicit operator float() const noexcept
    {
        return ToFloat();
    }

    /// Converts the numeric to a double precision floating point value.
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE explicit operator double() const noexcept
    {
        return ToDouble();
    }

    /// Converts the numeric to a long double precision floating point value.
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE explicit operator long double() const noexcept
    {
        return ToLongDouble();
    }

    /// Converts the numeric to a string, exactly.
    ///
    /// Rendered from the unscaled integer rather than by formatting `ToLongDouble()`, so every digit
    /// the carrier holds survives on every toolchain. Formatting through `long double` would drop
    /// the low digits wherever that type is narrow (53 bits on MSVC and on Clang for Apple Silicon)
    /// or wherever the standard library's formatter narrows to `double` regardless.
    ///
    /// @return The value in plain decimal notation with exactly `Scale` fractional digits.
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE std::string ToString() const
    {
        auto const unscaled = ToUnscaledValue();
        auto digits = detail::Int128ToString(unscaled);

        auto const negative = !digits.empty() && digits.front() == '-';
        if (negative)
            digits.erase(digits.begin());

        if constexpr (Scale == 0)
            return negative ? "-" + digits : digits;

        // Left-pad so there is at least one integral digit to the left of the point, which is what a
        // purely fractional type (Scale == Precision, e.g. DECIMAL(4, 4)) always needs.
        if (digits.size() <= Scale)
            digits.insert(digits.begin(), Scale + 1 - digits.size(), '0');

        digits.insert(digits.size() - Scale, 1, '.');

        return negative ? "-" + digits : digits;
    }

    /// Three-way comparison operator.
    ///
    /// Comparing two `SqlNumeric` values yields `std::partial_ordering` because the
    /// underlying conversion to `double` admits NaN inputs. In practice every value
    /// produced by this type is finite, so the result is totally ordered.
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE std::partial_ordering operator<=>(SqlNumeric const& other) const noexcept
    {
        return ToDouble() <=> other.ToDouble();
    }

    /// Equality comparison operator.
    template <std::size_t OtherPrecision, std::size_t OtherScale>
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE bool operator==(
        SqlNumeric<OtherPrecision, OtherScale> const& other) const noexcept
    {
        return ToFloat() == other.ToFloat();
    }
};

template <typename T>
concept SqlNumericType = requires(T t) {
    { T::Precision } -> std::convertible_to<std::size_t>;
    { T::Scale } -> std::convertible_to<std::size_t>;
} && std::same_as<T, SqlNumeric<T::Precision, T::Scale>>;

// clang-format off
template <std::size_t Precision, std::size_t Scale>
struct SqlDataBinder<SqlNumeric<Precision, Scale>>
{
    using ValueType = SqlNumeric<Precision, Scale>;

    static constexpr auto ColumnType = SqlColumnTypeDefinitions::Decimal { .precision = Precision, .scale = Scale };

    static void RequireSuccess(SQLHSTMT stmt, SQLRETURN error, std::source_location sourceLocation = std::source_location::current())
    {
        if (SQL_SUCCEEDED(error))
            return;

        throw SqlException(SqlErrorInfo::FromStatementHandle(stmt), sourceLocation);
    }

    static constexpr bool NativeNumericSupportIsBroken(SqlServerType serverType) noexcept
    {
        // SQLite's ODBC driver does not support SQL_NUMERIC_STRUCT (it's all just floating point numbers).
        // Microsoft SQL Server's ODBC driver also has issues (keeps reporting out of bound, on Linux at least).
        return serverType == SqlServerType::SQLITE || serverType == SqlServerType::MICROSOFT_SQL;
    }

    static LIGHTWEIGHT_FORCE_INLINE SQLRETURN InputParameter(SQLHSTMT stmt,
                                                             SQLUSMALLINT column,
                                                             ValueType const& value,
                                                             SqlDataBinderCallback& cb) noexcept
    {
        if (NativeNumericSupportIsBroken(cb.ServerType()))
        {
            return SQLBindParameter(stmt,
                                    column,
                                    SQL_PARAM_INPUT,
                                    SQL_C_DOUBLE,
                                    SQL_DOUBLE,
                                    0,
                                    0,
                                    (SQLPOINTER) &value.nativeValue,
                                    sizeof(value.nativeValue),
                                    nullptr);
        }

        // Bind with the type's compile-time Precision/Scale rather than value.sqlValue.precision/scale:
        // the latter is 0 for a default-constructed (never-assigned) value. On the native row-wise batch
        // path a single bind descriptor (taken from row 0) governs the whole array, so a default-constructed
        // row 0 would otherwise mis-bind every row. The template constants are correct for every value of
        // SqlNumeric<Precision, Scale> by definition (assign() always sets these same values).
        return SQLBindParameter(stmt,
                                column,
                                SQL_PARAM_INPUT,
                                SQL_C_NUMERIC,
                                SQL_NUMERIC,
                                static_cast<SQLULEN>(Precision),
                                static_cast<SQLSMALLINT>(Scale),
                                (SQLPOINTER) &value,
                                sizeof(value),
                                nullptr);
    }


    static LIGHTWEIGHT_FORCE_INLINE SQLRETURN OutputColumn(
        SQLHSTMT stmt, SQLUSMALLINT column, ValueType* result, SQLLEN* indicator, SqlDataBinderCallback& cb) noexcept
    {
        if (NativeNumericSupportIsBroken(cb.ServerType()))
        {
            result->sqlValue = { .precision = Precision, .scale = Scale, .sign = 0, .val = {} };
            return SQLBindCol(stmt, column, SQL_C_DOUBLE, &result->nativeValue, sizeof(result->nativeValue), indicator);
        }

        SQLHDESC hDesc {};
        RequireSuccess(stmt, SQLGetStmtAttr(stmt, SQL_ATTR_APP_ROW_DESC, (SQLPOINTER) &hDesc, 0, nullptr));
        RequireSuccess(stmt, SQLSetDescField(hDesc, (SQLSMALLINT) column, SQL_DESC_PRECISION, (SQLPOINTER) Precision, 0)); // NOLINT(performance-no-int-to-ptr)
        RequireSuccess(stmt, SQLSetDescField(hDesc, (SQLSMALLINT) column, SQL_DESC_SCALE, (SQLPOINTER) Scale, 0)); // NOLINT(performance-no-int-to-ptr)

        return SQLBindCol(stmt, column, SQL_C_NUMERIC, &result->sqlValue, sizeof(ValueType), indicator);
    }

    static LIGHTWEIGHT_FORCE_INLINE SQLRETURN GetColumn(SQLHSTMT stmt, SQLUSMALLINT column, ValueType* result, SQLLEN* indicator, SqlDataBinderCallback const& cb) noexcept
    {
        if (NativeNumericSupportIsBroken(cb.ServerType()))
        {
            result->sqlValue = { .precision = Precision, .scale = Scale, .sign = 0, .val = {} };
            return SQLGetData(stmt, column, SQL_C_DOUBLE, &result->nativeValue, sizeof(result->nativeValue), indicator);
        }

        SQLHDESC hDesc {};
        RequireSuccess(stmt, SQLGetStmtAttr(stmt, SQL_ATTR_APP_ROW_DESC, (SQLPOINTER) &hDesc, 0, nullptr));
        RequireSuccess(stmt, SQLSetDescField(hDesc, (SQLSMALLINT) column, SQL_DESC_PRECISION, (SQLPOINTER) Precision, 0)); // NOLINT(performance-no-int-to-ptr)
        RequireSuccess(stmt, SQLSetDescField(hDesc, (SQLSMALLINT) column, SQL_DESC_SCALE, (SQLPOINTER) Scale, 0)); // NOLINT(performance-no-int-to-ptr)

        return SQLGetData(stmt, column, SQL_C_NUMERIC, &result->sqlValue, sizeof(ValueType), indicator);
    }

    static LIGHTWEIGHT_FORCE_INLINE std::string Inspect(ValueType const& value) noexcept
    {
        return value.ToString();
    }
};

// SqlNumeric binds a fixed-width inline struct and is row-wise batchable for non-nullable columns. It
// is flagged as numeric so the std::optional batch path excludes it (its contained value is not bound
// at a uniform offset/representation across backends).
template <std::size_t ThePrecision, std::size_t TheScale>
inline constexpr bool SqlIsNativeRowBindableValue<SqlNumeric<ThePrecision, TheScale>> = true;
template <std::size_t ThePrecision, std::size_t TheScale>
inline constexpr bool SqlIsNumericValue<SqlNumeric<ThePrecision, TheScale>> = true;
// clang-format off

} // namespace Lightweight

template <Lightweight::SqlNumericType Type>
struct std::formatter<Type>: std::formatter<std::string>
{
    template <typename FormatContext>
    auto format(Type const& value, FormatContext& ctx) const
    {
        return formatter<std::string>::format(value.ToString(), ctx);
    }
};
