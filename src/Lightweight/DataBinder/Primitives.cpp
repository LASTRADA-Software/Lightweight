// SPDX-License-Identifier: Apache-2.0

#include "BasicStringBinder.hpp"
#include "Primitives.hpp"
#include "SqlFixedString.hpp"

#include <array>

namespace Lightweight
{

template <typename Int64Type, SQLSMALLINT TheCType>
struct OracleInt64DataBinderHelper
{
    static SQLRETURN InputParameter(SQLHSTMT stmt,
                                    SQLUSMALLINT column,
                                    Int64Type const& value,
                                    SqlDataBinderCallback& cb) noexcept
    {
        using StringType = SqlFixedString<21>;
        auto strValue = std::make_shared<StringType>();
        std::to_chars(strValue->data(), strValue->data() + strValue->capacity(), value, 10);
        cb.PlanPostExecuteCallback([strValue]() {}); // Defer the destruction of the string until after the execute
        return SqlDataBinder<StringType>::InputParameter(stmt, column, *strValue, cb);
    }

    static SQLRETURN OutputColumn(
        SQLHSTMT stmt, SQLUSMALLINT column, Int64Type* result, SQLLEN* indicator, SqlDataBinderCallback& cb) noexcept
    {
        using StringType = SqlFixedString<21>;
        auto buffer = std::make_shared<StringType>();
        auto const sqlResult = SqlDataBinder<StringType>::OutputColumn(stmt, column, buffer.get(), indicator, cb);
        if (SQL_SUCCEEDED(sqlResult))
        {
            cb.PlanPostProcessOutputColumn([buffer, result, indicator]() {
                if (*indicator != SQL_NULL_DATA && *indicator != SQL_NO_TOTAL)
                {
                    std::from_chars(buffer->data(), buffer->data() + buffer->size(), *result, 10);
                }
            });
        }
        return sqlResult;
    }

    static SQLRETURN GetColumn(
        SQLHSTMT stmt, SQLUSMALLINT column, Int64Type* result, SQLLEN* indicator, SqlDataBinderCallback const& cb) noexcept
    {
        using StringType = SqlFixedString<21>;
        auto buffer = StringType {};
        auto const sqlResult = SqlDataBinder<StringType>::GetColumn(stmt, column, &buffer, indicator, cb);
        if (SQL_SUCCEEDED(sqlResult) && *indicator != SQL_NULL_DATA && *indicator != SQL_NO_TOTAL)
        {
            std::from_chars(buffer.data(), buffer.data() + buffer.size(), *result, 10);
        }
        return sqlResult;
    }
};

// We use jump tables below to avoid CPU branch prediction misses.

template <typename Int64Type, SQLSMALLINT TheCType>
SQLRETURN Int64DataBinderHelper<Int64Type, TheCType>::InputParameter(SQLHSTMT stmt,
                                                                     SQLUSMALLINT column,
                                                                     Int64Type const& value,
                                                                     SqlDataBinderCallback& cb) noexcept
{
    // clang-format off
    static constexpr auto map = std::array {
        &SqlSimpleDataBinder<Int64Type, TheCType, SQL_BIGINT, SqlColumnTypeDefinitions::Bigint {}>::InputParameter, // Unknown
        &SqlSimpleDataBinder<Int64Type, TheCType, SQL_BIGINT, SqlColumnTypeDefinitions::Bigint {}>::InputParameter, // Microsoft SQL
        &SqlSimpleDataBinder<Int64Type, TheCType, SQL_BIGINT, SqlColumnTypeDefinitions::Bigint {}>::InputParameter, // PostgreSQL
        &OracleInt64DataBinderHelper<Int64Type, TheCType>::InputParameter,
        &SqlSimpleDataBinder<Int64Type, TheCType, SQL_BIGINT, SqlColumnTypeDefinitions::Bigint {}>::InputParameter, // SQLite
        &SqlSimpleDataBinder<Int64Type, TheCType, SQL_BIGINT, SqlColumnTypeDefinitions::Bigint {}>::InputParameter, // MySQL
    };
    // clang-format on
    auto const index = static_cast<size_t>(cb.ServerType());
    return map[index](stmt, column, value, cb);
}

template <typename Int64Type, SQLSMALLINT TheCType>
SQLRETURN Int64DataBinderHelper<Int64Type, TheCType>::OutputColumn(
    SQLHSTMT stmt, SQLUSMALLINT column, Int64Type* result, SQLLEN* indicator, SqlDataBinderCallback& cb) noexcept
{
    // clang-format off
    static constexpr auto map = std::array {
        &SqlSimpleDataBinder<Int64Type, TheCType, SQL_BIGINT, SqlColumnTypeDefinitions::Bigint {}>::OutputColumn, // Unknown
        &SqlSimpleDataBinder<Int64Type, TheCType, SQL_BIGINT, SqlColumnTypeDefinitions::Bigint {}>::OutputColumn, // Microsoft SQL
        &SqlSimpleDataBinder<Int64Type, TheCType, SQL_BIGINT, SqlColumnTypeDefinitions::Bigint {}>::OutputColumn, // PostgreSQL
        &OracleInt64DataBinderHelper<Int64Type, TheCType>::OutputColumn,
        &SqlSimpleDataBinder<Int64Type, TheCType, SQL_BIGINT, SqlColumnTypeDefinitions::Bigint {}>::OutputColumn, // SQLite
        &SqlSimpleDataBinder<Int64Type, TheCType, SQL_BIGINT, SqlColumnTypeDefinitions::Bigint {}>::OutputColumn, // MySQL
    };
    // clang-format on
    auto const index = static_cast<size_t>(cb.ServerType());
    return map[index](stmt, column, result, indicator, cb);
}

template <typename Int64Type, SQLSMALLINT TheCType>
SQLRETURN Int64DataBinderHelper<Int64Type, TheCType>::GetColumn(
    SQLHSTMT stmt, SQLUSMALLINT column, Int64Type* result, SQLLEN* indicator, SqlDataBinderCallback const& cb) noexcept
{
    // clang-format off
    static constexpr auto map = std::array {
        &SqlSimpleDataBinder<Int64Type, TheCType, SQL_BIGINT, SqlColumnTypeDefinitions::Bigint {}>::GetColumn, // Unknown
        &SqlSimpleDataBinder<Int64Type, TheCType, SQL_BIGINT, SqlColumnTypeDefinitions::Bigint {}>::GetColumn, // Microsoft SQL
        &SqlSimpleDataBinder<Int64Type, TheCType, SQL_BIGINT, SqlColumnTypeDefinitions::Bigint {}>::GetColumn, // PostgreSQL
        &OracleInt64DataBinderHelper<Int64Type, TheCType>::GetColumn,
        &SqlSimpleDataBinder<Int64Type, TheCType, SQL_BIGINT, SqlColumnTypeDefinitions::Bigint {}>::GetColumn, // SQLite
        &SqlSimpleDataBinder<Int64Type, TheCType, SQL_BIGINT, SqlColumnTypeDefinitions::Bigint {}>::GetColumn, // MySQL
    };
    // clang-format on
    auto const index = static_cast<size_t>(cb.ServerType());
    return map[index](stmt, column, result, indicator, cb);
}

template struct Int64DataBinderHelper<int64_t, SQL_C_SBIGINT>;
template struct Int64DataBinderHelper<uint64_t, SQL_C_UBIGINT>;

#if !defined(_WIN32) && !defined(__APPLE__)
template struct Int64DataBinderHelper<long long, SQL_C_SBIGINT>;
template struct Int64DataBinderHelper<unsigned long long, SQL_C_UBIGINT>;
#endif

} // namespace Lightweight
