// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../SqlColumnTypeDefinitions.hpp"
#include "Core.hpp"

#include <chrono>
#include <format>
#include <string_view>

#include <sql.h>
#include <sqlext.h>
#include <sqltypes.h>

/// Represents a date and time to efficiently write to or read from a database.
///
/// @see SqlDate, SqlTime
/// @ingroup DataTypes
struct SqlDateTime
{
    using native_type = std::chrono::system_clock::time_point;
    using duration_type = std::chrono::system_clock::duration;

    /// Returns the current date and time.
    [[nodiscard]] static LIGHTWEIGHT_FORCE_INLINE SqlDateTime Now() noexcept
    {
        return SqlDateTime { std::chrono::system_clock::now() };
    }

    constexpr SqlDateTime() noexcept = default;
    constexpr SqlDateTime(SqlDateTime&&) noexcept = default;
    constexpr SqlDateTime& operator=(SqlDateTime&&) noexcept = default;
    constexpr SqlDateTime(SqlDateTime const&) noexcept = default;
    constexpr SqlDateTime& operator=(SqlDateTime const& other) noexcept = default;
    constexpr ~SqlDateTime() noexcept = default;

    constexpr std::weak_ordering operator<=>(SqlDateTime const& other) const noexcept
    {
        return value() <=> other.value();
    }
    constexpr bool operator==(SqlDateTime const& other) const noexcept
    {
        return (*this <=> other) == std::weak_ordering::equivalent;
    }

    constexpr bool operator!=(SqlDateTime const& other) const noexcept
    {
        return !(*this == other);
    }

    /// Constructs a date and time from individual components.
    LIGHTWEIGHT_FORCE_INLINE constexpr SqlDateTime(std::chrono::year_month_day ymd,
                                                   std::chrono::hh_mm_ss<duration_type> time) noexcept:
        sqlValue {
            .year = (SQLSMALLINT) (int) ymd.year(),
            .month = (SQLUSMALLINT) (unsigned) ymd.month(),
            .day = (SQLUSMALLINT) (unsigned) ymd.day(),
            .hour = (SQLUSMALLINT) time.hours().count(),
            .minute = (SQLUSMALLINT) time.minutes().count(),
            .second = (SQLUSMALLINT) time.seconds().count(),
            .fraction =
                (SQLUINTEGER) (std::chrono::duration_cast<std::chrono::nanoseconds>(time.to_duration()).count() / 100) * 100,
        }
    {
    }

    /// Constructs a date and time from individual components.
    LIGHTWEIGHT_FORCE_INLINE constexpr SqlDateTime(
        std::chrono::year year,
        std::chrono::month month,
        std::chrono::day day,
        std::chrono::hours hour,
        std::chrono::minutes minute,
        std::chrono::seconds second,
        std::chrono::nanoseconds nanosecond = std::chrono::nanoseconds(0)) noexcept:
        sqlValue {
            .year = (SQLSMALLINT) (int) year,
            .month = (SQLUSMALLINT) (unsigned) month,
            .day = (SQLUSMALLINT) (unsigned) day,
            .hour = (SQLUSMALLINT) hour.count(),
            .minute = (SQLUSMALLINT) minute.count(),
            .second = (SQLUSMALLINT) second.count(),
            .fraction = (SQLUINTEGER) (nanosecond.count() / 100) * 100,
        }
    {
    }

    /// Constructs a date and time from a time point.
    LIGHTWEIGHT_FORCE_INLINE constexpr SqlDateTime(std::chrono::system_clock::time_point value) noexcept:
        sqlValue { ConvertToSqlValue(value) }
    {
    }

    /// Returns the year of this date-time object.
    [[nodiscard]] constexpr LIGHTWEIGHT_FORCE_INLINE std::chrono::year year() const noexcept
    {
        return std::chrono::year(static_cast<int>(sqlValue.year));
    }

    /// Returns the month of this date-time object.
    [[nodiscard]] constexpr LIGHTWEIGHT_FORCE_INLINE std::chrono::month month() const noexcept
    {
        return std::chrono::month(static_cast<unsigned>(sqlValue.month));
    }

    /// Returns the day of this date-time object.
    [[nodiscard]] constexpr LIGHTWEIGHT_FORCE_INLINE std::chrono::day day() const noexcept
    {
        return std::chrono::day(static_cast<unsigned>(sqlValue.day));
    }

    /// Returns the hour of this date-time object.
    [[nodiscard]] constexpr LIGHTWEIGHT_FORCE_INLINE std::chrono::hours hour() const noexcept
    {
        return std::chrono::hours(static_cast<unsigned>(sqlValue.hour));
    }

    /// Returns the minute of this date-time object.
    [[nodiscard]] constexpr LIGHTWEIGHT_FORCE_INLINE std::chrono::minutes minute() const noexcept
    {
        return std::chrono::minutes(static_cast<unsigned>(sqlValue.minute));
    }

    /// Returns the second of this date-time object.
    [[nodiscard]] constexpr LIGHTWEIGHT_FORCE_INLINE std::chrono::seconds second() const noexcept
    {
        return std::chrono::seconds(static_cast<unsigned>(sqlValue.second));
    }

    /// Returns the nanosecond of this date-time object.
    [[nodiscard]] constexpr LIGHTWEIGHT_FORCE_INLINE std::chrono::nanoseconds nanosecond() const noexcept
    {
        return std::chrono::nanoseconds(static_cast<unsigned>(sqlValue.fraction));
    }

    LIGHTWEIGHT_FORCE_INLINE constexpr operator native_type() const noexcept
    {
        return value();
    }

    static LIGHTWEIGHT_FORCE_INLINE SQL_TIMESTAMP_STRUCT constexpr ConvertToSqlValue(native_type value) noexcept
    {
        using namespace std::chrono;
        auto const totalDays = floor<days>(value);
        auto const ymd = year_month_day { totalDays };
        auto const hms =
            hh_mm_ss<duration_type> { std::chrono::duration_cast<duration_type>(floor<nanoseconds>(value - totalDays)) };
        return ConvertToSqlValue(ymd, hms);
    }

    static LIGHTWEIGHT_FORCE_INLINE SQL_TIMESTAMP_STRUCT constexpr ConvertToSqlValue(
        std::chrono::year_month_day ymd, std::chrono::hh_mm_ss<duration_type> hms) noexcept
    {
        // clang-format off
        // NB: The fraction field is in 100ns units.
        return SQL_TIMESTAMP_STRUCT {
            .year = (SQLSMALLINT) (int) ymd.year(),
            .month = (SQLUSMALLINT) (unsigned) ymd.month(),
            .day = (SQLUSMALLINT) (unsigned) ymd.day(),
            .hour = (SQLUSMALLINT) hms.hours().count(),
            .minute = (SQLUSMALLINT) hms.minutes().count(),
            .second = (SQLUSMALLINT) hms.seconds().count(),
            .fraction = (SQLUINTEGER) (((std::chrono::duration_cast<std::chrono::nanoseconds>(hms.to_duration()).count() % 1'000'000'000LLU) / 100) * 100)
        };
        // clang-format on
    }

    static LIGHTWEIGHT_FORCE_INLINE native_type constexpr ConvertToNative(SQL_TIMESTAMP_STRUCT const& time) noexcept
    {
        // clang-format off
        using namespace std::chrono;
        auto const ymd = year_month_day { std::chrono::year { time.year } / std::chrono::month { time.month } / std::chrono::day { time.day } };
        auto const hms = hh_mm_ss<duration_type> {
            duration_cast<duration_type>(
                hours { time.hour }
                + minutes { time.minute }
                + seconds { time.second }
                + nanoseconds { time.fraction }
            )
        };
        return sys_days { ymd } + hms.to_duration();
        // clang-format on
    }

    /// Returns the current date and time.
    [[nodiscard]] constexpr LIGHTWEIGHT_FORCE_INLINE native_type value() const noexcept
    {
        return ConvertToNative(sqlValue);
    }

    LIGHTWEIGHT_FORCE_INLINE SqlDateTime& operator+=(duration_type duration) noexcept
    {
        *this = SqlDateTime { value() + duration };
        return *this;
    }

    LIGHTWEIGHT_FORCE_INLINE SqlDateTime& operator-=(duration_type duration) noexcept
    {
        *this = SqlDateTime { value() - duration };
        return *this;
    }

    friend LIGHTWEIGHT_FORCE_INLINE SqlDateTime operator+(SqlDateTime dateTime, duration_type duration) noexcept
    {
        auto tmp = dateTime.value() + duration;
        return { tmp };
    }

    friend LIGHTWEIGHT_FORCE_INLINE SqlDateTime operator-(SqlDateTime dateTime, duration_type duration) noexcept
    {
        return SqlDateTime { dateTime.value() - duration };
    }

    SQL_TIMESTAMP_STRUCT sqlValue {};
};

template <>
struct std::formatter<SqlDateTime>: std::formatter<std::string>
{
    LIGHTWEIGHT_FORCE_INLINE auto format(SqlDateTime const& value, std::format_context& ctx) const
        -> std::format_context::iterator
    {
        // This can be used manually to format the date and time inside sql query builder,
        // that is why we need to use iso standard 8601, also millisecond precision is used for the formatting
        //
        // internally fraction is stored in nanoseconds, here we need to get it in milliseconds
        auto const milliseconds = value.sqlValue.fraction / 1'000'000;
        return std::formatter<std::string>::format(std::format("{:04}-{:02}-{:02}T{:02}:{:02}:{:02}.{:03}",
                                                               value.sqlValue.year,
                                                               value.sqlValue.month,
                                                               value.sqlValue.day,
                                                               value.sqlValue.hour,
                                                               value.sqlValue.minute,
                                                               value.sqlValue.second,
                                                               milliseconds),
                                                   ctx);
    }
};

template <>
struct SqlDataBinder<SqlDateTime::native_type>
{
    static LIGHTWEIGHT_FORCE_INLINE SQLRETURN GetColumn(SQLHSTMT stmt,
                                                        SQLUSMALLINT column,
                                                        SqlDateTime::native_type* result,
                                                        SQLLEN* indicator,
                                                        SqlDataBinderCallback const& /*cb*/) noexcept
    {
        SQL_TIMESTAMP_STRUCT sqlValue {};
        auto const rc = SQLGetData(stmt, column, SQL_C_TYPE_TIMESTAMP, &sqlValue, sizeof(sqlValue), indicator);
        if (SQL_SUCCEEDED(rc))
            *result = SqlDateTime::ConvertToNative(sqlValue);
        return rc;
    }
};

template <>
struct LIGHTWEIGHT_API SqlDataBinder<SqlDateTime>
{
    static constexpr auto ColumnType = SqlColumnTypeDefinitions::DateTime {};

    static LIGHTWEIGHT_FORCE_INLINE SQLRETURN InputParameter(SQLHSTMT stmt,
                                                             SQLUSMALLINT column,
                                                             SqlDateTime const& value,
                                                             [[maybe_unused]] SqlDataBinderCallback const& cb) noexcept
    {
#if defined(_WIN32) || defined(_WIN64)
        // Microsoft Windows also chips with SQLSRV32.DLL, which is legacy, but seems to be used sometimes.
        // See: https://learn.microsoft.com/en-us/sql/connect/connect-history
        using namespace std::string_view_literals;
        if (cb.ServerType() == SqlServerType::MICROSOFT_SQL && cb.DriverName() == "SQLSRV32.DLL"sv)
        {
            struct
            {
                SQLSMALLINT sqlType { SQL_TYPE_TIMESTAMP };
                SQLULEN paramSize { 23 };
                SQLSMALLINT decimalDigits { 3 };
                SQLSMALLINT nullable {};
            } hints;
            auto const sqlDescribeParamResult =
                SQLDescribeParam(stmt, column, &hints.sqlType, &hints.paramSize, &hints.decimalDigits, &hints.nullable);
            if (SQL_SUCCEEDED(sqlDescribeParamResult))
            {
                return SQLBindParameter(stmt,
                                        column,
                                        SQL_PARAM_INPUT,
                                        SQL_C_TIMESTAMP,
                                        hints.sqlType,
                                        hints.paramSize,
                                        hints.decimalDigits,
                                        (SQLPOINTER) &value.sqlValue,
                                        sizeof(value),
                                        nullptr);
            }
        }
#endif

        return SQLBindParameter(stmt,
                                column,
                                SQL_PARAM_INPUT,
                                SQL_C_TIMESTAMP,
                                SQL_TYPE_TIMESTAMP,
                                27,
                                7,
                                (SQLPOINTER) &value.sqlValue,
                                sizeof(value),
                                nullptr);
    }

    static LIGHTWEIGHT_FORCE_INLINE SQLRETURN OutputColumn(
        SQLHSTMT stmt, SQLUSMALLINT column, SqlDateTime* result, SQLLEN* indicator, SqlDataBinderCallback& /*cb*/) noexcept
    {
        // TODO: handle indicator to check for NULL values
        *indicator = sizeof(result->sqlValue);
        return SQLBindCol(stmt, column, SQL_C_TYPE_TIMESTAMP, &result->sqlValue, 0, indicator);
    }

    static LIGHTWEIGHT_FORCE_INLINE SQLRETURN GetColumn(SQLHSTMT stmt,
                                                        SQLUSMALLINT column,
                                                        SqlDateTime* result,
                                                        SQLLEN* indicator,
                                                        SqlDataBinderCallback const& /*cb*/) noexcept
    {
        return SQLGetData(stmt, column, SQL_C_TYPE_TIMESTAMP, &result->sqlValue, sizeof(result->sqlValue), indicator);
    }

    static LIGHTWEIGHT_FORCE_INLINE std::string Inspect(SqlDateTime const& value) noexcept
    {
        return std::format("{}", value);
    }
};
