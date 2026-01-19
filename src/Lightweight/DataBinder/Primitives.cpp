// SPDX-License-Identifier: Apache-2.0

#include "BasicStringBinder.hpp"
#include "Primitives.hpp"
#include "SqlFixedString.hpp"

#include <array>
#include <charconv>

namespace Lightweight
{

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
        &SqlSimpleDataBinder<Int64Type, TheCType, SQL_BIGINT, SqlColumnTypeDefinitions::Bigint {}>::InputParameter, // SQLite
        &SqlSimpleDataBinder<Int64Type, TheCType, SQL_BIGINT, SqlColumnTypeDefinitions::Bigint {}>::InputParameter, // MySQL
    };
    // clang-format on
    auto const index = static_cast<size_t>(cb.ServerType());
    return map[index](stmt, column, value, cb);
}

template <typename Int64Type, SQLSMALLINT TheCType>
SQLRETURN Int64DataBinderHelper<Int64Type, TheCType>::BatchInputParameter(
    SQLHSTMT stmt, SQLUSMALLINT column, Int64Type const* values, size_t rowCount, SqlDataBinderCallback& cb) noexcept
{
    // clang-format off
    static constexpr auto map = std::array {
        &SqlSimpleDataBinder<Int64Type, TheCType, SQL_BIGINT, SqlColumnTypeDefinitions::Bigint {}>::BatchInputParameter, // Unknown
        &SqlSimpleDataBinder<Int64Type, TheCType, SQL_BIGINT, SqlColumnTypeDefinitions::Bigint {}>::BatchInputParameter, // Microsoft SQL
        &SqlSimpleDataBinder<Int64Type, TheCType, SQL_BIGINT, SqlColumnTypeDefinitions::Bigint {}>::BatchInputParameter, // PostgreSQL
        &SqlSimpleDataBinder<Int64Type, TheCType, SQL_BIGINT, SqlColumnTypeDefinitions::Bigint {}>::BatchInputParameter, // SQLite
        &SqlSimpleDataBinder<Int64Type, TheCType, SQL_BIGINT, SqlColumnTypeDefinitions::Bigint {}>::BatchInputParameter, // MySQL
    };
    // clang-format on
    auto const index = static_cast<size_t>(cb.ServerType());
    return map[index](stmt, column, values, rowCount, cb);
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
