// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../SqlColumnTypeDefinitions.hpp"
#include "Core.hpp"

#include <chrono>
#include <format>

/// Represents a date to efficiently write to or read from a database.
///
/// @ingroup DataTypes
struct SqlDate
{
    SQL_DATE_STRUCT sqlValue {};

    constexpr SqlDate() noexcept = default;
    constexpr SqlDate(SqlDate&&) noexcept = default;
    constexpr SqlDate& operator=(SqlDate&&) noexcept = default;
    constexpr SqlDate(SqlDate const&) noexcept = default;
    constexpr SqlDate& operator=(SqlDate const&) noexcept = default;
    constexpr ~SqlDate() noexcept = default;

    /// Returns the current date.
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE constexpr std::chrono::year_month_day value() const noexcept
    {
        return ConvertToNative(sqlValue);
    }

    LIGHTWEIGHT_FORCE_INLINE constexpr bool operator==(SqlDate const& other) const noexcept
    {
        return sqlValue.year == other.sqlValue.year && sqlValue.month == other.sqlValue.month
               && sqlValue.day == other.sqlValue.day;
    }

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

    static LIGHTWEIGHT_FORCE_INLINE constexpr SQL_DATE_STRUCT ConvertToSqlValue(
        std::chrono::year_month_day value) noexcept
    {
        return SQL_DATE_STRUCT {
            .year = (SQLSMALLINT) (int) value.year(),
            .month = (SQLUSMALLINT) (unsigned) value.month(),
            .day = (SQLUSMALLINT) (unsigned) value.day(),
        };
    }

    static LIGHTWEIGHT_FORCE_INLINE constexpr std::chrono::year_month_day ConvertToNative(
        SQL_DATE_STRUCT const& value) noexcept
    {
        return std::chrono::year_month_day { std::chrono::year { value.year },
                                             std::chrono::month { static_cast<unsigned>(value.month) },
                                             std::chrono::day { static_cast<unsigned>(value.day) } };
    }
};

template <>
struct std::formatter<SqlDate>: std::formatter<std::string>
{
    auto format(SqlDate const& value, std::format_context& ctx) const -> std::format_context::iterator
    {
        return std::formatter<std::string>::format(
            std::format("{:04}-{:02}-{:02}", value.sqlValue.year, value.sqlValue.month, value.sqlValue.day), ctx);
    }
};

template <>
struct SqlDataBinder<SqlDate>
{
    static constexpr auto ColumnType = SqlColumnTypeDefinitions::Date {};

    static LIGHTWEIGHT_FORCE_INLINE SQLRETURN InputParameter(SQLHSTMT stmt,
                                                             SQLUSMALLINT column,
                                                             SqlDate const& value,
                                                             SqlDataBinderCallback& /*cb*/) noexcept
    {
        return SQLBindParameter(stmt,
                                column,
                                SQL_PARAM_INPUT,
                                SQL_C_TYPE_DATE,
                                SQL_TYPE_DATE,
                                0,
                                0,
                                (SQLPOINTER) &value.sqlValue,
                                0,
                                nullptr);
    }

    static LIGHTWEIGHT_FORCE_INLINE SQLRETURN OutputColumn(
        SQLHSTMT stmt, SQLUSMALLINT column, SqlDate* result, SQLLEN* indicator, SqlDataBinderCallback& /*cb*/) noexcept
    {
        return SQLBindCol(stmt, column, SQL_C_TYPE_DATE, &result->sqlValue, sizeof(result->sqlValue), indicator);
    }

    static LIGHTWEIGHT_FORCE_INLINE SQLRETURN GetColumn(SQLHSTMT stmt,
                                                        SQLUSMALLINT column,
                                                        SqlDate* result,
                                                        SQLLEN* indicator,
                                                        SqlDataBinderCallback const& /*cb*/) noexcept
    {
        return SQLGetData(stmt, column, SQL_C_TYPE_DATE, &result->sqlValue, sizeof(result->sqlValue), indicator);
    }

    static LIGHTWEIGHT_FORCE_INLINE std::string Inspect(SqlDate const& value) noexcept
    {
        return std::format("{}", value);
    }
};
