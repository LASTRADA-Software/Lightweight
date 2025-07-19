// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../SqlColumnTypeDefinitions.hpp"
#include "Core.hpp"

namespace Lightweight
{

template <typename T, SQLSMALLINT TheCType, SQLINTEGER TheSqlType, auto TheColumnType>
struct SqlSimpleDataBinder
{
    static constexpr SqlColumnTypeDefinition ColumnType = TheColumnType;

    static LIGHTWEIGHT_FORCE_INLINE SQLRETURN InputParameter(SQLHSTMT stmt,
                                                             SQLUSMALLINT column,
                                                             T const& value,
                                                             SqlDataBinderCallback& /*cb*/) noexcept
    {
        return SQLBindParameter(stmt, column, SQL_PARAM_INPUT, TheCType, TheSqlType, 0, 0, (SQLPOINTER) &value, 0, nullptr);
    }

    static LIGHTWEIGHT_FORCE_INLINE SQLRETURN OutputColumn(
        SQLHSTMT stmt, SQLUSMALLINT column, T* result, SQLLEN* indicator, SqlDataBinderCallback& /*unused*/) noexcept
    {
        return SQLBindCol(stmt, column, TheCType, result, 0, indicator);
    }

    static LIGHTWEIGHT_FORCE_INLINE SQLRETURN
    GetColumn(SQLHSTMT stmt, SQLUSMALLINT column, T* result, SQLLEN* indicator, SqlDataBinderCallback const& /*cb*/) noexcept
    {
        return SQLGetData(stmt, column, TheCType, result, 0, indicator);
    }

    static LIGHTWEIGHT_FORCE_INLINE std::string Inspect(T value)
    {
        return std::to_string(value);
    }
};

template <typename Int64Type, SQLSMALLINT TheCType>
struct Int64DataBinderHelper
{
    static constexpr SqlColumnTypeDefinition ColumnType = SqlColumnTypeDefinitions::Bigint {};

    static LIGHTWEIGHT_API SQLRETURN InputParameter(SQLHSTMT stmt,
                                                    SQLUSMALLINT column,
                                                    Int64Type const& value,
                                                    SqlDataBinderCallback& cb) noexcept;

    static LIGHTWEIGHT_API SQLRETURN OutputColumn(
        SQLHSTMT stmt, SQLUSMALLINT column, Int64Type* result, SQLLEN* indicator, SqlDataBinderCallback& cb) noexcept;

    static LIGHTWEIGHT_API SQLRETURN GetColumn(
        SQLHSTMT stmt, SQLUSMALLINT column, Int64Type* result, SQLLEN* indicator, SqlDataBinderCallback const& cb) noexcept;

    static LIGHTWEIGHT_FORCE_INLINE std::string Inspect(Int64Type value)
    {
        return std::to_string(value);
    }
};

// clang-format off
template <> struct SqlDataBinder<bool>: SqlSimpleDataBinder<bool, SQL_BIT, SQL_BIT, SqlColumnTypeDefinitions::Bool {}> {};
template <> struct SqlDataBinder<char>: SqlSimpleDataBinder<char, SQL_C_CHAR, SQL_CHAR, SqlColumnTypeDefinitions::Char {}> {};
template <> struct SqlDataBinder<int8_t>: SqlSimpleDataBinder<int8_t, SQL_C_STINYINT, SQL_TINYINT, SqlColumnTypeDefinitions::Tinyint {}> {};
template <> struct SqlDataBinder<uint8_t>: SqlSimpleDataBinder<uint8_t, SQL_C_UTINYINT, SQL_TINYINT, SqlColumnTypeDefinitions::Tinyint {}> {};
template <> struct SqlDataBinder<int16_t>: SqlSimpleDataBinder<int16_t, SQL_C_SSHORT, SQL_SMALLINT, SqlColumnTypeDefinitions::Smallint {}> {};
template <> struct SqlDataBinder<uint16_t>: SqlSimpleDataBinder<uint16_t, SQL_C_USHORT, SQL_SMALLINT, SqlColumnTypeDefinitions::Smallint {}> {};
template <> struct SqlDataBinder<int32_t>: SqlSimpleDataBinder<int32_t, SQL_C_SLONG, SQL_INTEGER, SqlColumnTypeDefinitions::Integer {}> {};
template <> struct SqlDataBinder<uint32_t>: SqlSimpleDataBinder<uint32_t, SQL_C_ULONG, SQL_INTEGER, SqlColumnTypeDefinitions::Integer {}> {};
template <> struct SqlDataBinder<int64_t>: Int64DataBinderHelper<int64_t, SQL_C_SBIGINT> {};
template <> struct SqlDataBinder<uint64_t>: Int64DataBinderHelper<uint64_t, SQL_C_UBIGINT> {};
//template <> struct SqlDataBinder<uint64_t>: Int64DataBinderHelper<uint64_t, SQL_C_UBIGINT> {};
template <> struct SqlDataBinder<float>: SqlSimpleDataBinder<float, SQL_C_FLOAT, SQL_REAL, SqlColumnTypeDefinitions::Real {}> {};
template <> struct SqlDataBinder<double>: SqlSimpleDataBinder<double, SQL_C_DOUBLE, SQL_DOUBLE, SqlColumnTypeDefinitions::Real {}> {};
#if !defined(_WIN32) && !defined(__APPLE__)
template <> struct SqlDataBinder<long long>: Int64DataBinderHelper<long long, SQL_C_SBIGINT> {};
template <> struct SqlDataBinder<unsigned long long>: Int64DataBinderHelper<unsigned long long, SQL_C_UBIGINT> {};
#endif
#if defined(__APPLE__) // size_t is a different type on macOS
template <> struct SqlDataBinder<std::size_t>: SqlSimpleDataBinder<std::size_t, SQL_C_SBIGINT, SqlColumnTypeDefinitions::Bigint {}> {};
#endif
// clang-format on

} // namespace Lightweight
