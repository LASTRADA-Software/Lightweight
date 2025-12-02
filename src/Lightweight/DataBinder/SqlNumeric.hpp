// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../SqlColumnTypeDefinitions.hpp"
#include "../SqlError.hpp"
#include "Primitives.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <compare>
#include <concepts>
#include <cstring>
#include <source_location>

namespace Lightweight
{

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
    /// Number of total digits
    static constexpr auto Precision = ThePrecision;

    /// Number of digits after the decimal point
    static constexpr auto Scale = TheScale;

    static constexpr auto ColumnType = SqlColumnTypeDefinitions::Decimal { .precision = Precision, .scale = TheScale };

    static_assert(Precision <= SQL_MAX_NUMERIC_LEN);
    static_assert(Scale < Precision);

    SQL_NUMERIC_STRUCT sqlValue {};

    // Cached native value for drivers that do not support SQL_NUMERIC_STRUCT directly (e.g., SQLite).
    double nativeValue {};

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
    LIGHTWEIGHT_FORCE_INLINE constexpr void assign(std::floating_point auto inputValue) noexcept
    {
        nativeValue = static_cast<decltype(nativeValue)>(inputValue);

        sqlValue = {};
        sqlValue.sign = std::signbit(inputValue) ? 0 : 1;
        sqlValue.precision = static_cast<SQLCHAR>(Precision);
        sqlValue.scale = static_cast<SQLSCHAR>(Scale);

        auto const unscaledValue = std::roundl(static_cast<long double>(std::abs(inputValue) * std::powl(10.0L, Scale)));
#if defined(LIGHTWEIGHT_INT128_T)
        auto const num = static_cast<LIGHTWEIGHT_INT128_T>(unscaledValue);
        std::memcpy(sqlValue.val, &num, sizeof(num));
#else
        auto const num = static_cast<int64_t>(unscaledValue);
        std::memcpy(sqlValue.val, &num, sizeof(num));
#endif
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
        if (nativeValue != 0.0)
        {
            auto const unscaledValue = std::roundl(nativeValue * std::powl(10.0L, Scale));
#if defined(LIGHTWEIGHT_INT128_T)
            return static_cast<LIGHTWEIGHT_INT128_T>(unscaledValue);
#else
            return static_cast<int64_t>(unscaledValue);
#endif
        }

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
        return static_cast<float>(ToUnscaledValue()) / std::powf(10, sqlValue.scale);
    }

    /// Converts the numeric to a double precision floating point value.
    [[nodiscard]] constexpr LIGHTWEIGHT_FORCE_INLINE double ToDouble() const noexcept
    {
        return static_cast<double>(ToUnscaledValue()) / std::pow(10, sqlValue.scale);
    }

    /// Converts the numeric to a long double precision floating point value.
    [[nodiscard]] constexpr LIGHTWEIGHT_FORCE_INLINE long double ToLongDouble() const noexcept
    {
        return static_cast<long double>(ToUnscaledValue()) / std::pow(10, sqlValue.scale);
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
        return std::format("{:.{}f}", ToLongDouble(), Scale);
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

        return SQLBindParameter(stmt,
                                column,
                                SQL_PARAM_INPUT,
                                SQL_C_NUMERIC,
                                SQL_NUMERIC,
                                value.sqlValue.precision,
                                value.sqlValue.scale,
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
// clang-format off

} // namespace Lightweight

template <Lightweight::SqlNumericType Type>
struct std::formatter<Type>: std::formatter<std::string>
{
    template <typename FormatContext>
    auto format(Type const& value, FormatContext& ctx)
    {
        return formatter<std::string>::format(value.ToString(), ctx);
    }
};
