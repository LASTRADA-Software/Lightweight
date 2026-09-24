// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../SqlColumnTypeDefinitions.hpp"
#include "Core.hpp"

#include <algorithm>
#include <array>
#include <memory>
#include <span>
#include <string>

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

    /// Binds a contiguous (column-wise) or row-strided (row-wise) array of values as an input parameter.
    ///
    /// @param indicators Optional per-row NULL/length indicator array. Passed straight to ODBC as the
    /// StrLen_or_IndPtr; defaults to nullptr (no NULLs). For row-wise binding the driver strides it by
    /// the statement's SQL_ATTR_PARAM_BIND_TYPE, matching the value stride.
    static LIGHTWEIGHT_FORCE_INLINE SQLRETURN BatchInputParameter(SQLHSTMT stmt,
                                                                  SQLUSMALLINT column,
                                                                  T const* values,
                                                                  size_t /*rowCount*/,
                                                                  SqlDataBinderCallback& /*cb*/,
                                                                  SQLLEN* indicators = nullptr) noexcept
    {
        return SQLBindParameter(
            stmt, column, SQL_PARAM_INPUT, TheCType, TheSqlType, 0, 0, (SQLPOINTER) values, sizeof(T), indicators);
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

    static LIGHTWEIGHT_API SQLRETURN BatchInputParameter(SQLHSTMT stmt,
                                                         SQLUSMALLINT column,
                                                         Int64Type const* values,
                                                         size_t rowCount,
                                                         SqlDataBinderCallback& cb,
                                                         SQLLEN* indicators = nullptr) noexcept;

    static LIGHTWEIGHT_API SQLRETURN OutputColumn(
        SQLHSTMT stmt, SQLUSMALLINT column, Int64Type* result, SQLLEN* indicator, SqlDataBinderCallback& cb) noexcept;

    static LIGHTWEIGHT_API SQLRETURN GetColumn(
        SQLHSTMT stmt, SQLUSMALLINT column, Int64Type* result, SQLLEN* indicator, SqlDataBinderCallback const& cb) noexcept;

    static LIGHTWEIGHT_FORCE_INLINE std::string Inspect(Int64Type value)
    {
        return std::to_string(value);
    }
};

/// Binds a single @c char as a one-character @c CHAR(1) value.
///
/// Unlike the numeric primitives, @c char maps to a character type (@c SQL_C_CHAR / @c SQL_CHAR), whose
/// ODBC contract differs from fixed-width data in three ways, each of which this binder honours:
/// - On input, the column size is the length in characters and must not be zero (MS SQL Server rejects
///   zero with HY104 "Invalid precision value").
/// - On input, the value is not NUL-terminated, so its length must be passed through the indicator —
///   without one the driver reads the value as a NUL-terminated string, past the end of the @c char.
/// - On output, the driver NUL-terminates what it writes, so the buffer needs room for one more byte
///   than the value; a one-byte buffer receives nothing.
template <>
struct SqlDataBinder<char>
{
    /// The column type a @c char maps to.
    static constexpr SqlColumnTypeDefinition ColumnType = SqlColumnTypeDefinitions::Char { 1 };

    /// Binds a single @c char as an input parameter.
    ///
    /// @param stmt The ODBC statement handle.
    /// @param column The 1-based parameter index.
    /// @param value The character to bind; it must outlive the statement's execution.
    /// @param cb Provides the length indicator, which must also outlive the execution.
    /// @return The ODBC return code of @c SQLBindParameter.
    static LIGHTWEIGHT_FORCE_INLINE SQLRETURN InputParameter(SQLHSTMT stmt,
                                                             SQLUSMALLINT column,
                                                             char const& value,
                                                             SqlDataBinderCallback& cb) noexcept
    {
        auto* const indicator = cb.ProvideInputIndicator();
        *indicator = CharLength;
        return SQLBindParameter(
            stmt, column, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, ColumnSize, 0, (SQLPOINTER) &value, 0, indicator);
    }

    /// Binds a contiguous (column-wise) array of characters as an input parameter.
    ///
    /// @param stmt The ODBC statement handle.
    /// @param column The 1-based parameter index.
    /// @param values The first of @p rowCount characters; they must outlive the statement's execution.
    /// @param rowCount The number of rows in the batch.
    /// @param cb Provides the per-row length indicators when @p indicators is not given.
    /// @param indicators Optional per-row indicator array (the length, or @c SQL_NULL_DATA). When null,
    ///                   every row is bound as one character.
    /// @return The ODBC return code of @c SQLBindParameter.
    static LIGHTWEIGHT_FORCE_INLINE SQLRETURN BatchInputParameter(SQLHSTMT stmt,
                                                                  SQLUSMALLINT column,
                                                                  char const* values,
                                                                  size_t rowCount,
                                                                  SqlDataBinderCallback& cb,
                                                                  SQLLEN* indicators = nullptr) noexcept
    {
        if (!indicators)
        {
            indicators = cb.ProvideInputIndicators(rowCount);
            std::ranges::fill(std::span { indicators, rowCount }, CharLength);
        }
        return SQLBindParameter(stmt,
                                column,
                                SQL_PARAM_INPUT,
                                SQL_C_CHAR,
                                SQL_CHAR,
                                ColumnSize,
                                0,
                                (SQLPOINTER) values,
                                sizeof(char),
                                indicators);
    }

    /// Binds @p result as the output column @p column, filled in on every fetch.
    ///
    /// The driver writes into a NUL-terminated staging buffer, which a post-process callback copies into
    /// @p result; a NULL leaves @p result untouched (@c std::optional<char> resets itself on it).
    ///
    /// @param stmt The ODBC statement handle.
    /// @param column The 1-based column index.
    /// @param result Receives the character; it must outlive the fetches.
    /// @param indicator Receives the length, or @c SQL_NULL_DATA.
    /// @param cb Holds the staging buffer and runs the copy after each fetch.
    /// @return The ODBC return code of @c SQLBindCol.
    static SQLRETURN OutputColumn(
        SQLHSTMT stmt, SQLUSMALLINT column, char* result, SQLLEN* indicator, SqlDataBinderCallback& cb) noexcept
    {
        auto buffer = std::make_shared<Buffer>();
        auto const sqlReturn = SQLBindCol(stmt, column, SQL_C_CHAR, buffer->data(), BufferSize, indicator);
        cb.PlanPostProcessOutputColumn([buffer, result, indicator]() {
            if (*indicator != SQL_NULL_DATA)
                *result = buffer->front();
        });
        return sqlReturn;
    }

    /// Reads the character in column @p column of the current row.
    ///
    /// @param stmt The ODBC statement handle.
    /// @param column The 1-based column index.
    /// @param result Receives the character; left untouched on NULL.
    /// @param indicator Receives the length, or @c SQL_NULL_DATA.
    /// @return The ODBC return code of @c SQLGetData.
    static SQLRETURN GetColumn(
        SQLHSTMT stmt, SQLUSMALLINT column, char* result, SQLLEN* indicator, SqlDataBinderCallback const& /*cb*/) noexcept
    {
        auto buffer = Buffer {};
        auto const sqlReturn = SQLGetData(stmt, column, SQL_C_CHAR, buffer.data(), BufferSize, indicator);
        if (SQL_SUCCEEDED(sqlReturn) && *indicator != SQL_NULL_DATA)
            *result = buffer.front();
        return sqlReturn;
    }

    /// @param value The character to render.
    /// @return @p value as a one-character string.
    static LIGHTWEIGHT_FORCE_INLINE std::string Inspect(char value)
    {
        return std::string(1, value);
    }

  private:
    // One character plus the NUL terminator the driver appends to SQL_C_CHAR output.
    using Buffer = std::array<char, 2>;
    static constexpr SQLLEN CharLength = 1;
    static constexpr SQLULEN ColumnSize = 1;
    static constexpr SQLLEN BufferSize = sizeof(Buffer);
};

// clang-format off
template <> struct SqlDataBinder<bool>: SqlSimpleDataBinder<bool, SQL_BIT, SQL_BIT, SqlColumnTypeDefinitions::Bool {}> {};
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
// std::size_t is unsigned, so bind it with the unsigned C type (matching SqlDataBinder<uint64_t> above);
// SQL_C_SBIGINT would sign-corrupt values above INT64_MAX.
template <> struct SqlDataBinder<std::size_t>: SqlSimpleDataBinder<std::size_t, SQL_C_UBIGINT, SQL_BIGINT, SqlColumnTypeDefinitions::Bigint {}> {};
#endif

// These fixed-width primitives bind via a plain SQLBindParameter and are eligible for native row-wise
// batch binding (see SqlIsNativeRowBindableValue in Core.hpp). A single constrained partial
// specialization, keyed on detail::IsAnyOf, covers them all.
//
// char is deliberately absent: row-wise binding strides the value in place, but a char needs a per-row
// length on input and a NUL-terminated staging buffer on output (see SqlDataBinder<char>), so records
// carrying one take the per-row paths instead.
template <typename T>
    requires detail::IsAnyOf<T, bool, int8_t, uint8_t, int16_t, uint16_t, int32_t, uint32_t, int64_t, uint64_t,
                             float, double
#if !defined(_WIN32) && !defined(__APPLE__)
                             , long long, unsigned long long
#endif
#if defined(__APPLE__)
                             , std::size_t
#endif
                             >
inline constexpr bool SqlIsNativeRowBindableValue<T> = true;
// clang-format on

} // namespace Lightweight
