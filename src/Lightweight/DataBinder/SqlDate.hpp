// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../SqlColumnTypeDefinitions.hpp"
#include "Core.hpp"

#include <chrono>
#include <format>

namespace Lightweight
{

/// Represents a date to efficiently write to or read from a database.
///
/// @ingroup DataTypes
struct SqlDate
{
    /// Holds the underlying SQL date structure.
    SQL_DATE_STRUCT sqlValue {};

    /// Default constructor.
    constexpr SqlDate() noexcept = default;
    /// Default move constructor.
    constexpr SqlDate(SqlDate&&) noexcept = default;
    /// Default move assignment operator.
    constexpr SqlDate& operator=(SqlDate&&) noexcept = default;
    /// Default copy constructor.
    constexpr SqlDate(SqlDate const&) noexcept = default;
    /// Default copy assignment operator.
    constexpr SqlDate& operator=(SqlDate const&) noexcept = default;
    constexpr ~SqlDate() noexcept = default;

    /// Returns the current date.
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE constexpr std::chrono::year_month_day value() const noexcept
    {
        return ConvertToNative(sqlValue);
    }

    /// Equality comparison operator.
    LIGHTWEIGHT_FORCE_INLINE constexpr bool operator==(SqlDate const& other) const noexcept
    {
        return sqlValue.year == other.sqlValue.year && sqlValue.month == other.sqlValue.month
               && sqlValue.day == other.sqlValue.day;
    }

    /// Inequality comparison operator.
    LIGHTWEIGHT_FORCE_INLINE constexpr bool operator!=(SqlDate const& other) const noexcept
    {
        return !(*this == other);
    }

    /// Constructs a date from individual std::chrono::year_month_day.
    constexpr SqlDate(std::chrono::year_month_day value) noexcept:
        sqlValue { SqlDate::ConvertToSqlValue(value) }
    {
    }

    /// Constructs a date from individual components.
    constexpr SqlDate(std::chrono::year year, std::chrono::month month, std::chrono::day day) noexcept:
        SqlDate(std::chrono::year_month_day { year, month, day })
    {
    }

    /// Returns the current date.
    [[nodiscard]] static LIGHTWEIGHT_FORCE_INLINE SqlDate Today() noexcept
    {
        return SqlDate { std::chrono::year_month_day {
            std::chrono::floor<std::chrono::days>(std::chrono::system_clock::now()),
        } };
    }

    /// Converts a std::chrono::year_month_day to the underlying SQL date structure.
    static LIGHTWEIGHT_FORCE_INLINE constexpr SQL_DATE_STRUCT ConvertToSqlValue(std::chrono::year_month_day value) noexcept
    {
        return SQL_DATE_STRUCT {
            .year = (SQLSMALLINT) (int) value.year(),
            .month = (SQLUSMALLINT) (unsigned) value.month(),
            .day = (SQLUSMALLINT) (unsigned) value.day(),
        };
    }

    /// Converts a SQL date structure to std::chrono::year_month_day.
    static LIGHTWEIGHT_FORCE_INLINE constexpr std::chrono::year_month_day ConvertToNative(
        SQL_DATE_STRUCT const& value) noexcept
    {
        return std::chrono::year_month_day { std::chrono::year { value.year },
                                             std::chrono::month { static_cast<unsigned>(value.month) },
                                             std::chrono::day { static_cast<unsigned>(value.day) } };
    }
};

} // namespace Lightweight

template <>
struct std::formatter<Lightweight::SqlDate>: std::formatter<std::string>
{
    auto format(Lightweight::SqlDate const& value, std::format_context& ctx) const -> std::format_context::iterator
    {
        return std::formatter<std::string>::format(
            std::format("{:04}-{:02}-{:02}", value.sqlValue.year, value.sqlValue.month, value.sqlValue.day), ctx);
    }
};

namespace Lightweight
{

template <>
struct SqlDataBinder<SqlDate>
{
    static constexpr auto ColumnType = SqlColumnTypeDefinitions::Date {};

    // The legacy Microsoft "SQL Server" ODBC driver (SQLSRV32.DLL) is ODBC 2.x-only
    // and rejects the ODBC 3.x SQL_C_TYPE_DATE / SQL_TYPE_DATE type codes with
    // HYC00 ("Optional feature not implemented"). Fall back to the equivalent
    // ODBC 2.x codes when that driver is detected; modern drivers (ODBC Driver
    // 17/18) accept both forms.
    static LIGHTWEIGHT_FORCE_INLINE SQLRETURN InputParameter(SQLHSTMT stmt,
                                                             SQLUSMALLINT column,
                                                             SqlDate const& value,
                                                             SqlDataBinderCallback& cb) noexcept
    {
        auto const cType = static_cast<SQLSMALLINT>(cb.IsLegacyMicrosoftSqlServerDriver() ? SQL_C_DATE : SQL_C_TYPE_DATE);
        auto const sqlType = static_cast<SQLSMALLINT>(cb.IsLegacyMicrosoftSqlServerDriver() ? SQL_DATE : SQL_TYPE_DATE);
        return SQLBindParameter(
            stmt, column, SQL_PARAM_INPUT, cType, sqlType, 0, 0, (SQLPOINTER) &value.sqlValue, 0, nullptr);
    }

    static LIGHTWEIGHT_FORCE_INLINE SQLRETURN
    OutputColumn(SQLHSTMT stmt, SQLUSMALLINT column, SqlDate* result, SQLLEN* indicator, SqlDataBinderCallback& cb) noexcept
    {
        auto const cType = static_cast<SQLSMALLINT>(cb.IsLegacyMicrosoftSqlServerDriver() ? SQL_C_DATE : SQL_C_TYPE_DATE);
        return SQLBindCol(stmt, column, cType, &result->sqlValue, sizeof(result->sqlValue), indicator);
    }

    static LIGHTWEIGHT_FORCE_INLINE SQLRETURN GetColumn(
        SQLHSTMT stmt, SQLUSMALLINT column, SqlDate* result, SQLLEN* indicator, SqlDataBinderCallback const& cb) noexcept
    {
        auto const cType = static_cast<SQLSMALLINT>(cb.IsLegacyMicrosoftSqlServerDriver() ? SQL_C_DATE : SQL_C_TYPE_DATE);
        return SQLGetData(stmt, column, cType, &result->sqlValue, sizeof(result->sqlValue), indicator);
    }

    static LIGHTWEIGHT_FORCE_INLINE std::string Inspect(SqlDate const& value) noexcept
    {
        return std::format("{}", value);
    }
};

} // namespace Lightweight
