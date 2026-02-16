// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../SqlColumnTypeDefinitions.hpp"
#include "Core.hpp"

#include <chrono>
#include <format>

// clang-format off
#if !defined(SQL_SS_TIME2)
// This is a Microsoft-specific extension to ODBC.
// It is supported by at lesat the following drivers:
// - SQL Server 2008 and later
// - MariaDB and MySQL ODBC drivers

#define SQL_SS_TIME2 (-154)

struct SQL_SS_TIME2_STRUCT
{
    SQLUSMALLINT hour;
    SQLUSMALLINT minute;
    SQLUSMALLINT second;
    SQLUINTEGER fraction;
};

static_assert(
    sizeof(SQL_SS_TIME2_STRUCT) == 12,
    "SQL_SS_TIME2_STRUCT size must be padded 12 bytes, as per ODBC extension spec."
);

#endif
// clang-format on

namespace Lightweight
{

/// Stores the time (of the day) to efficiently write to or read from a database.
///
/// @ingroup DataTypes
struct SqlTime
{
    /// The native C++ type for time representation.
    using native_type = std::chrono::hh_mm_ss<std::chrono::microseconds>;

#if defined(SQL_SS_TIME2)
    /// The SQL type used for ODBC binding.
    using sql_type = SQL_SS_TIME2_STRUCT;
#else
    /// The SQL type used for ODBC binding.
    using sql_type = SQL_TIME_STRUCT;
#endif

    /// The underlying SQL value for ODBC binding.
    sql_type sqlValue {};

    /// Default constructor.
    constexpr SqlTime() noexcept = default;
    /// Move constructor.
    constexpr SqlTime(SqlTime&&) noexcept = default;
    /// Move assignment operator.
    constexpr SqlTime& operator=(SqlTime&&) noexcept = default;
    /// Copy constructor.
    constexpr SqlTime(SqlTime const&) noexcept = default;
    /// Copy assignment operator.
    constexpr SqlTime& operator=(SqlTime const&) noexcept = default;
    constexpr ~SqlTime() noexcept = default;

    /// Returns the native time value.
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE constexpr native_type value() const noexcept
    {
        return ConvertToNative(sqlValue);
    }

    /// Equality comparison operator.
    LIGHTWEIGHT_FORCE_INLINE constexpr bool operator==(SqlTime const& other) const noexcept
    {
        return value().to_duration().count() == other.value().to_duration().count();
    }

    /// Inequality comparison operator.
    LIGHTWEIGHT_FORCE_INLINE constexpr bool operator!=(SqlTime const& other) const noexcept
    {
        return !(*this == other);
    }

    /// Constructs a SqlTime from a native time value.
    LIGHTWEIGHT_FORCE_INLINE constexpr SqlTime(native_type value) noexcept:
        sqlValue { SqlTime::ConvertToSqlValue(value) }
    {
    }

    /// Constructs a SqlTime from hours, minutes, seconds, and optional microseconds.
    LIGHTWEIGHT_FORCE_INLINE constexpr SqlTime(std::chrono::hours hour,
                                               std::chrono::minutes minute,
                                               std::chrono::seconds second,
                                               std::chrono::microseconds micros = {}) noexcept:
        SqlTime(native_type { hour + minute + second + micros })
    {
    }

    /// Converts a native time value to the SQL representation.
    static LIGHTWEIGHT_FORCE_INLINE constexpr sql_type ConvertToSqlValue(native_type value) noexcept
    {
        return sql_type {
            .hour = (SQLUSMALLINT) value.hours().count(),
            .minute = (SQLUSMALLINT) value.minutes().count(),
            .second = (SQLUSMALLINT) value.seconds().count(),
#if defined(SQL_SS_TIME2)
            .fraction = (SQLUINTEGER) value.subseconds().count(),
#endif
        };
    }

    /// Converts a SQL time value to the native representation.
    static LIGHTWEIGHT_FORCE_INLINE constexpr native_type ConvertToNative(sql_type const& value) noexcept
    {
        // clang-format off
        return native_type { std::chrono::hours { (int) value.hour }
                             + std::chrono::minutes { (unsigned) value.minute }
                             + std::chrono::seconds { (unsigned) value.second }
#if defined(SQL_SS_TIME2)
                             + std::chrono::microseconds { value.fraction }
#endif

        };
        // clang-format on
    }
};

template <>
struct SqlDataBinder<SqlTime>
{
    static constexpr auto ColumnType = SqlColumnTypeDefinitions::Time {};

    static LIGHTWEIGHT_FORCE_INLINE SQLRETURN InputParameter(SQLHSTMT stmt,
                                                             SQLUSMALLINT column,
                                                             SqlTime const& value,
                                                             SqlDataBinderCallback& /*cb*/) noexcept
    {
        return SQLBindParameter(
            stmt, column, SQL_PARAM_INPUT, SQL_C_TYPE_TIME, SQL_TYPE_TIME, 0, 0, (SQLPOINTER) &value.sqlValue, 0, nullptr);
    }

    static LIGHTWEIGHT_FORCE_INLINE SQLRETURN OutputColumn(
        SQLHSTMT stmt, SQLUSMALLINT column, SqlTime* result, SQLLEN* indicator, SqlDataBinderCallback& /*cb*/) noexcept
    {
        return SQLBindCol(stmt, column, SQL_C_TYPE_TIME, &result->sqlValue, sizeof(result->sqlValue), indicator);
    }

    static LIGHTWEIGHT_FORCE_INLINE SQLRETURN GetColumn(
        SQLHSTMT stmt, SQLUSMALLINT column, SqlTime* result, SQLLEN* indicator, SqlDataBinderCallback const& /*cb*/) noexcept
    {
        return SQLGetData(stmt, column, SQL_C_TYPE_TIME, &result->sqlValue, sizeof(result->sqlValue), indicator);
    }

    static LIGHTWEIGHT_FORCE_INLINE std::string Inspect(SqlTime const& value) noexcept
    {
        return std::format("{:02}:{:02}:{:02}.{:06}",
                           value.sqlValue.hour,
                           value.sqlValue.minute,
                           value.sqlValue.second,
                           value.sqlValue.fraction);
    }
};

} // namespace Lightweight

template <>
struct std::formatter<Lightweight::SqlTime>: std::formatter<std::string>
{
    auto format(Lightweight::SqlTime const& value, std::format_context& ctx) const -> std::format_context::iterator
    {
        return std::formatter<std::string>::format(std::format("{:02}:{:02}:{:02}.{:06}",
                                                               value.sqlValue.hour,
                                                               value.sqlValue.minute,
                                                               value.sqlValue.second,
                                                               value.sqlValue.fraction),
                                                   ctx);
    }
};
