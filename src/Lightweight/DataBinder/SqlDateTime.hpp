// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../SqlColumnTypeDefinitions.hpp"
#include "Core.hpp"

#include <chrono>
#include <format>

/// Represents a date and time to efficiently write to or read from a database.
///
/// @see SqlDate, SqlTime
struct LIGHTWEIGHT_API SqlDateTime
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

    constexpr bool operator==(SqlDateTime const& other) const noexcept
    {
        return value() == other.value();
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
            .fraction = (SQLUINTEGER) (std::chrono::duration_cast<std::chrono::nanoseconds>(time.to_duration()).count() / 100) * 100,
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

    LIGHTWEIGHT_FORCE_INLINE constexpr operator native_type() const noexcept
    {
        return value();
    }

    static LIGHTWEIGHT_FORCE_INLINE SQL_TIMESTAMP_STRUCT constexpr ConvertToSqlValue(native_type value) noexcept
    {
        using namespace std::chrono;
        auto const totalDays = floor<days>(value);
        auto const ymd = year_month_day { totalDays };
        auto const hms = hh_mm_ss<duration_type> {
            std::chrono::duration_cast<duration_type>(floor<nanoseconds>(value - totalDays)) };
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
            .fraction = (SQLUINTEGER) (((std::chrono::duration_cast<std::chrono::nanoseconds>(hms.to_duration()).count() % 1'000'000'000llu) / 100) * 100)
        };
        // clang-format on
    }

    static native_type constexpr ConvertToNative(SQL_TIMESTAMP_STRUCT const& time) noexcept
    {
        // clang-format off
        using namespace std::chrono;
        auto const ymd = year_month_day { year { time.year } / month { time.month } / day { time.day } };
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
    [[nodiscard]] constexpr native_type value() const noexcept
    {
        return ConvertToNative(sqlValue);
    }

    SqlDateTime& operator+=(duration_type duration) noexcept
    {
        *this = SqlDateTime { value() + duration };
        return *this;
    }

    SqlDateTime& operator-=(duration_type duration) noexcept
    {
        *this = SqlDateTime { value() - duration };
        return *this;
    }

    friend SqlDateTime operator+(SqlDateTime dateTime, duration_type duration) noexcept
    {
        auto tmp = dateTime.value() + duration;
        return SqlDateTime(tmp);
        //return SqlDateTime { dateTime.value() + duration };
    }

    friend SqlDateTime operator-(SqlDateTime dateTime, duration_type duration) noexcept
    {
        return SqlDateTime { dateTime.value() - duration };
    }

    SQL_TIMESTAMP_STRUCT sqlValue {};
};

template <>
struct std::formatter<SqlDateTime>: std::formatter<std::string>
{
    auto format(SqlDateTime const& value, std::format_context& ctx) const -> std::format_context::iterator
    {
        return std::formatter<std::string>::format(std::format("{:04}-{:02}-{:02} {:02}:{:02}:{:02}.{:09}",
                                                               value.sqlValue.year,
                                                               value.sqlValue.month,
                                                               value.sqlValue.day,
                                                               value.sqlValue.hour,
                                                               value.sqlValue.minute,
                                                               value.sqlValue.second,
                                                               value.sqlValue.fraction),
                                                   ctx);
    }
};

template <>
struct LIGHTWEIGHT_API SqlDataBinder<SqlDateTime::native_type>
{
    static SQLRETURN GetColumn(SQLHSTMT stmt,
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
                                                             SqlDataBinderCallback& /*cb*/) noexcept
    {
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

    static LIGHTWEIGHT_FORCE_INLINE SQLRETURN OutputColumn(SQLHSTMT stmt,
                                                           SQLUSMALLINT column,
                                                           SqlDateTime* result,
                                                           SQLLEN* indicator,
                                                           SqlDataBinderCallback& /*cb*/) noexcept
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
