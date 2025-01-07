// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Core.hpp"

/// Helper binder type to indicate NULL values in SQL queries.
struct SqlNullType
{
    SQLLEN sqlValue = SQL_NULL_DATA;
};

/// Used to indicate a NULL value in a SQL query.
constexpr auto SqlNullValue = SqlNullType {};

template <>
struct SqlDataBinder<SqlNullType>
{
    static LIGHTWEIGHT_FORCE_INLINE SQLRETURN InputParameter(SQLHSTMT stmt,
                                                             SQLUSMALLINT column,
                                                             SqlNullType const& value,
                                                             SqlDataBinderCallback& cb) noexcept
    {
        // This is generally ignored for NULL values, but MS SQL Server requires a non-zero value
        // when the underlying type is e.g. an INT.

        // For MS SQL Server, we need to determine the actual SQL type of the column to bind the parameter correctly,
        // because requires a correctly convertible SQL type for NULL values.
        // e.g. MS SQL Server cannot convert a VARCHAR NULL to a binary column type (e.g. BLOB).
        SQLSMALLINT const sqlType = [stmt, column, serverType = cb.ServerType()]() -> SQLSMALLINT {
            if (serverType == SqlServerType::MICROSOFT_SQL)
            {
                SQLSMALLINT columnType {};
                auto const sqlReturn = SQLDescribeParam(stmt, column, &columnType, nullptr, nullptr, nullptr);
                if (SQL_SUCCEEDED(sqlReturn))
                    return columnType;
            }
            return SQL_C_CHAR;
        }();

        return SQLBindParameter(stmt,
                                column,
                                SQL_PARAM_INPUT,
                                SQL_C_CHAR,
                                sqlType,
                                10,
                                0,
                                nullptr,
                                0,
                                &const_cast<SqlNullType&>(value).sqlValue);
    }

    static LIGHTWEIGHT_FORCE_INLINE std::string_view Inspect(SqlNullType const& /*value*/) noexcept
    {
        return "NULL";
    }
};
