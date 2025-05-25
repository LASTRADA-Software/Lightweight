// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Core.hpp"
#include "SqlNullValue.hpp"

#include <format>
#include <optional>

template <typename T>
struct SqlDataBinder<std::optional<T>>
{
    using OptionalValue = std::optional<T>;

    static constexpr auto ColumnType = SqlDataBinder<T>::ColumnType;

    static LIGHTWEIGHT_FORCE_INLINE SQLRETURN InputParameter(SQLHSTMT stmt,
                                                             SQLUSMALLINT column,
                                                             OptionalValue const& value,
                                                             SqlDataBinderCallback& cb) noexcept
    {
        if (value.has_value())
            return SqlDataBinder<T>::InputParameter(stmt, column, *value, cb);
        else
            return SqlDataBinder<SqlNullType>::InputParameter(stmt, column, SqlNullValue, cb);
    }

    static LIGHTWEIGHT_FORCE_INLINE SQLRETURN OutputColumn(
        SQLHSTMT stmt, SQLUSMALLINT column, OptionalValue* result, SQLLEN* indicator, SqlDataBinderCallback& cb) noexcept
    {
        if (!result)
            return SQL_ERROR;

        auto const sqlReturn = SqlDataBinder<T>::OutputColumn(stmt, column, &result->emplace(), indicator, cb);
        cb.PlanPostProcessOutputColumn([result, indicator]() {
            if (indicator && *indicator == SQL_NULL_DATA)
                *result = std::nullopt;
        });
        return sqlReturn;
    }

    static LIGHTWEIGHT_FORCE_INLINE SQLRETURN GetColumn(SQLHSTMT stmt,
                                                        SQLUSMALLINT column,
                                                        OptionalValue* result,
                                                        SQLLEN* indicator,
                                                        SqlDataBinderCallback const& cb) noexcept
    {
        auto const sqlReturn = SqlDataBinder<T>::GetColumn(stmt, column, &result->emplace(), indicator, cb);
        if (indicator && *indicator == SQL_NULL_DATA)
            *result = std::nullopt;
        return sqlReturn;
    }

    static LIGHTWEIGHT_FORCE_INLINE std::string Inspect(OptionalValue const& value) noexcept
    {
        if (!value)
            return "NULL";
        else
            return std::string(SqlDataBinder<T>::Inspect(*value));
    }
};
