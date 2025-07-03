// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../SqlColumnTypeDefinitions.hpp"
#include "../SqlError.hpp"
#include "../SqlLogger.hpp"
#include "Primitives.hpp"

#include <cmath>
#include <compare>
#include <concepts>
#include <cstring>
#include <print>
#include <source_location>

// clang-cl doesn't support __int128_t but defines __SIZEOF_INT128__
// and also since it pretends to be MSVC, it also defines _MSC_VER
// clang-format off
#if defined(__SIZEOF_INT128__) && !defined(_MSC_VER)
    #define LIGHTWEIGHT_INT128_T __int128_t
    static_assert(sizeof(__int128_t) == sizeof(SQL_NUMERIC_STRUCT::val));
#endif
// clang-format on

/// Represents a fixed-point number with a given precision and scale.
///
/// Precision is *exactly* the total number of digits in the number,
/// including the digits after the decimal point.
///
/// Scale is the number of digits after the decimal point.
///
/// @ingroup DataTypes
template <std::size_t ThePrecision, std::size_t TheScale>
struct SqlNumeric
{
    static constexpr auto ColumnType = SqlColumnTypeDefinitions::Decimal { .precision = ThePrecision, .scale = TheScale };

    /// Number of total digits
    static constexpr auto Precision = ThePrecision;

    /// Number of digits after the decimal point
    static constexpr auto Scale = TheScale;

    static_assert(TheScale < SQL_MAX_NUMERIC_LEN);
    static_assert(Scale <= Precision);

    /// The value is stored as a string to avoid floating point precision issues.
    SQL_NUMERIC_STRUCT sqlValue {};

    constexpr SqlNumeric() noexcept = default;
    constexpr SqlNumeric(SqlNumeric&&) noexcept = default;
    constexpr SqlNumeric& operator=(SqlNumeric&&) noexcept = default;
    constexpr SqlNumeric(SqlNumeric const&) noexcept = default;
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
    LIGHTWEIGHT_FORCE_INLINE constexpr void assign(std::floating_point auto value) noexcept
    {
#if defined(LIGHTWEIGHT_INT128_T)
        auto const num = static_cast<LIGHTWEIGHT_INT128_T>(value * std::pow(10, Scale));
        *((LIGHTWEIGHT_INT128_T*) sqlValue.val) = num;
#else
        auto const num = static_cast<int64_t>(value * std::pow(10, Scale));
        std::memset(sqlValue.val, 0, sizeof(sqlValue.val));
        *((int64_t*) sqlValue.val) = num;
#endif

        sqlValue.sign = num >= 0; // 1 == positive, 0 == negative
        sqlValue.precision = Precision;
        sqlValue.scale = Scale;
    }

    /// Assigns a floating point value to the numeric.
    LIGHTWEIGHT_FORCE_INLINE constexpr SqlNumeric& operator=(std::floating_point auto value) noexcept
    {
        assign(value);
        return *this;
    }

    /// Converts the numeric to an unscaled integer value.
    [[nodiscard]] constexpr LIGHTWEIGHT_FORCE_INLINE auto ToUnscaledValue() const noexcept
    {
        auto const sign = sqlValue.sign ? 1 : -1;
#if defined(LIGHTWEIGHT_INT128_T)
        return sign * *reinterpret_cast<LIGHTWEIGHT_INT128_T const*>(sqlValue.val);
#else
        return sign * *reinterpret_cast<int64_t const*>(sqlValue.val);
#endif
    }

    /// Converts the numeric to a floating point value.
    [[nodiscard]] constexpr LIGHTWEIGHT_FORCE_INLINE float ToFloat() const noexcept
    {
        return float(ToUnscaledValue()) / std::powf(10, Scale);
    }

    /// Converts the numeric to a double precision floating point value.
    [[nodiscard]] constexpr LIGHTWEIGHT_FORCE_INLINE double ToDouble() const noexcept
    {
        return double(ToUnscaledValue()) / std::pow(10, Scale);
    }

    /// Converts the numeric to a long double precision floating point value.
    [[nodiscard]] constexpr LIGHTWEIGHT_FORCE_INLINE long double ToLongDouble() const noexcept
    {
        return static_cast<long double>(ToUnscaledValue()) / std::pow(10, Scale);
    }

    /// Converts the numeric to a floating point value.
    [[nodiscard]] constexpr LIGHTWEIGHT_FORCE_INLINE explicit operator float() const noexcept
    {
        return ToFloat();
    }

    /// Converts the numeric to a double precision floating point value.
    [[nodiscard]] constexpr LIGHTWEIGHT_FORCE_INLINE explicit operator double() const noexcept
    {
        return ToDouble();
    }

    /// Converts the numeric to a long double precision floating point value.
    [[nodiscard]] constexpr LIGHTWEIGHT_FORCE_INLINE explicit operator long double() const noexcept
    {
        return ToLongDouble();
    }

    /// Converts the numeric to a string.
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE std::string ToString() const
    {
        return std::format("{:.{}f}", ToFloat(), Scale);
    }

    [[nodiscard]] constexpr LIGHTWEIGHT_FORCE_INLINE std::weak_ordering operator<=>(SqlNumeric const& other) const noexcept
    {
        return ToDouble() <=> other.ToDouble();
    }

    template <std::size_t OtherPrecision, std::size_t OtherScale>
    [[nodiscard]] constexpr LIGHTWEIGHT_FORCE_INLINE bool operator==(
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

        throw SqlException(SqlErrorInfo::fromStatementHandle(stmt), sourceLocation);
    }

    static LIGHTWEIGHT_FORCE_INLINE SQLRETURN InputParameter(SQLHSTMT stmt, SQLUSMALLINT column, ValueType const& value, SqlDataBinderCallback& /*cb*/) noexcept
    {
        auto* mut = const_cast<ValueType*>(&value);
        mut->sqlValue.precision = Precision;
        mut->sqlValue.scale = Scale;
        RequireSuccess(stmt, SQLBindParameter(stmt, column, SQL_PARAM_INPUT, SQL_C_NUMERIC, SQL_NUMERIC, Precision, Scale, (SQLPOINTER) &value.sqlValue, 0, nullptr));
        return SQL_SUCCESS;
    }

    static LIGHTWEIGHT_FORCE_INLINE SQLRETURN OutputColumn(SQLHSTMT stmt, SQLUSMALLINT column, ValueType* result, SQLLEN* indicator, SqlDataBinderCallback& /*unused*/) noexcept
    {
        SQLHDESC hDesc {};
        RequireSuccess(stmt, SQLGetStmtAttr(stmt, SQL_ATTR_APP_ROW_DESC, (SQLPOINTER) &hDesc, 0, nullptr));
        RequireSuccess(stmt, SQLSetDescField(hDesc, (SQLSMALLINT) column, SQL_DESC_PRECISION, (SQLPOINTER) Precision, 0)); // NOLINT(performance-no-int-to-ptr)
        RequireSuccess(stmt, SQLSetDescField(hDesc, (SQLSMALLINT) column, SQL_DESC_SCALE, (SQLPOINTER) Scale, 0)); // NOLINT(performance-no-int-to-ptr)

        return SQLBindCol(stmt, column, SQL_C_NUMERIC, &result->sqlValue, sizeof(ValueType), indicator);
    }

    static LIGHTWEIGHT_FORCE_INLINE SQLRETURN GetColumn(SQLHSTMT stmt, SQLUSMALLINT column, ValueType* result, SQLLEN* indicator, SqlDataBinderCallback const& /*cb*/) noexcept
    {
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
// clang-format off


template <SqlNumericType Type>
struct std::formatter<Type>: std::formatter<std::string>
{
    template <typename FormatContext>
    auto format(Type const& value, FormatContext& ctx)
    {
        return formatter<std::string>::format(value.ToString(), ctx);
    }
};
