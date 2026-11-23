// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../SqlColumnTypeDefinitions.hpp"
#include "BasicStringBinder.hpp"
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

/// Binds a @c bool as @c SQL_BIT. A read goes through a value the statement stages, never straight into the
/// @c bool, and applies the rule of the block prefetch, so a read gives the same with and without it: a number
/// reads 0 as @c false and 1 as @c true, -1 as @c true where the driver reports a boolean true so
/// (@c SqlQueryFormatter::BooleanTrueMayBeMinusOne, PostgreSQL), and fails with SQLSTATE 22003 otherwise; a
/// floating-point number reads as its integral part where it is in [0, 2). The C type follows the column's
/// described SQL type: @c SQL_C_SBIGINT for an integer or bit column (the drivers write the number itself, where
/// @c SQL_C_BIT gets PostgreSQL's low byte and SQLite's "positive is 1"), @c SQL_C_DOUBLE for a floating-point or
/// decimal column (SQLite answers a fraction through @c SQL_C_SBIGINT as NULL), and @c SQL_C_BIT for text and any
/// other type (the drivers parse 'true' only there). A NULL read into a bound @c bool sets it to @c false, as
/// the block prefetch does.
template <>
struct SqlDataBinder<bool>
{
    /// The column type the DataMapper creates for @c bool.
    static constexpr SqlColumnTypeDefinition ColumnType = SqlColumnTypeDefinitions::Bool {};

    /// Binds @p value as an input parameter.
    /// @return The driver's result.
    static LIGHTWEIGHT_FORCE_INLINE SQLRETURN InputParameter(SQLHSTMT stmt,
                                                             SQLUSMALLINT column,
                                                             bool const& value,
                                                             SqlDataBinderCallback& /*cb*/) noexcept
    {
        return SQLBindParameter(stmt, column, SQL_PARAM_INPUT, SQL_C_BIT, SQL_BIT, 0, 0, (SQLPOINTER) &value, 0, nullptr);
    }

    /// Binds a contiguous (column-wise) or row-strided (row-wise) array of values as an input parameter.
    /// @return The driver's result.
    static LIGHTWEIGHT_FORCE_INLINE SQLRETURN BatchInputParameter(SQLHSTMT stmt,
                                                                  SQLUSMALLINT column,
                                                                  bool const* values,
                                                                  size_t /*rowCount*/,
                                                                  SqlDataBinderCallback& /*cb*/,
                                                                  SQLLEN* indicators = nullptr) noexcept
    {
        return SQLBindParameter(
            stmt, column, SQL_PARAM_INPUT, SQL_C_BIT, SQL_BIT, 0, 0, (SQLPOINTER) values, sizeof(bool), indicators);
    }

    /// Binds @p result as an output column, through the statement's staging of @p column.
    /// @return The driver's result.
    static LIGHTWEIGHT_FORCE_INLINE SQLRETURN
    OutputColumn(SQLHSTMT stmt, SQLUSMALLINT column, bool* result, SQLLEN* indicator, SqlDataBinderCallback& cb) noexcept
    {
        auto const source = SourceOf(stmt, column, cb);
        auto* const staged =
            detail::PlanStagedOutputColumn<&ConvertStaged>(cb,
                                                           SqlOutputColumnStaging {
                                                               .value = {},
                                                               .target = result,
                                                               .indicator = indicator,
                                                               .column = column,
                                                               .kind = std::to_underlying(source),
                                                               .booleanTrueMayBeMinusOne = cb.BooleanTrueMayBeMinusOne(),
                                                           });
        return SQLBindCol(stmt, column, CTypeOf(source), staged, 0, indicator);
    }

    /// Reads column @p column of the current row into @p result.
    /// @return The driver's result.
    /// @throws SqlException 22003 for a value that stands for neither @c false nor @c true.
    static LIGHTWEIGHT_FORCE_INLINE SQLRETURN
    GetColumn(SQLHSTMT stmt, SQLUSMALLINT column, bool* result, SQLLEN* indicator, SqlDataBinderCallback const& cb)
    {
        auto const source = SourceOf(stmt, column, cb);
        auto staging = SqlOutputColumnStaging {
            .value = {},
            .target = result,
            .indicator = indicator,
            .column = column,
            .kind = std::to_underlying(source),
            .booleanTrueMayBeMinusOne = cb.BooleanTrueMayBeMinusOne(),
        };
        auto const rv = SQLGetData(stmt, column, CTypeOf(source), staging.value.data(), 0, indicator);
        if (SQL_SUCCEEDED(rv) && *indicator != SQL_NULL_DATA)
            *result = FromStaged(staging);
        return rv;
    }

    /// @return @p value as text.
    static LIGHTWEIGHT_FORCE_INLINE std::string Inspect(bool value)
    {
        return std::to_string(value);
    }

  private:
    /// The C type a @c bool column is read through.
    enum class Source : std::uint8_t
    {
        Bit,           //!< @c SQL_C_BIT, for text and any type not listed below.
        Integer,       //!< @c SQL_C_SBIGINT, for an integer or bit column.
        FloatingPoint, //!< @c SQL_C_DOUBLE, for a floating-point or decimal column.
    };

    /// The ODBC C type of each @c Source.
    static constexpr auto SourceCTypes = std::array<SQLSMALLINT, 3> { SQL_C_BIT, SQL_C_SBIGINT, SQL_C_DOUBLE };

    /// @return The ODBC C type of @p source.
    static constexpr SQLSMALLINT CTypeOf(Source source) noexcept
    {
        return SourceCTypes[std::to_underlying(source)];
    }

    /// The source a column of SQL type @p type is read through.
    static constexpr Source SourceOfSqlType(std::optional<SQLSMALLINT> type) noexcept
    {
        switch (type.value_or(SQL_UNKNOWN_TYPE))
        {
            case SQL_BIT:
            case SQL_TINYINT:
            case SQL_SMALLINT:
            case SQL_INTEGER:
            case SQL_BIGINT:
                return Source::Integer;
            case SQL_REAL:
            case SQL_FLOAT:
            case SQL_DOUBLE:
            case SQL_NUMERIC:
            case SQL_DECIMAL:
                return Source::FloatingPoint;
            default:
                return Source::Bit;
        }
    }

    /// The source result column @p column is read through, by the SQL type the driver describes for it (the
    /// statement's, asked once per result set and column).
    static Source SourceOf(SQLHSTMT stmt, SQLUSMALLINT column, SqlDataBinderCallback const& cb) noexcept
    {
        auto type = cb.DescribeResultColumnType(column);
        if (!type)
            type = detail::DescribeResultColumnSqlType(stmt, column);
        return SourceOfSqlType(type);
    }

    /// Stores the staged value of a bound column into its @c bool: @c false for NULL.
    static void ConvertStaged(SqlOutputColumnStaging const& staging)
    {
        *static_cast<bool*>(staging.target) = *staging.indicator != SQL_NULL_DATA && FromStaged(staging);
    }

    /// The @c bool the staged value stands for, by the C type it was read through.
    /// @throws SqlException 22003 for a value that stands for neither @c false nor @c true.
    static bool FromStaged(SqlOutputColumnStaging const& staging)
    {
        switch (static_cast<Source>(staging.kind))
        {
            case Source::Integer: {
                auto value = std::int64_t {};
                std::memcpy(&value, staging.value.data(), sizeof(value));
                return detail::BoolFromInteger(value, staging.column, staging.booleanTrueMayBeMinusOne);
            }
            case Source::FloatingPoint: {
                auto value = 0.0;
                std::memcpy(&value, staging.value.data(), sizeof(value));
                return detail::BoolFromFloatingPoint(value, staging.column);
            }
            case Source::Bit:
                break;
        }
        // SQL_C_BIT: the byte 0xFF is a boolean true reported as -1.
        auto const byte = std::to_integer<unsigned char>(staging.value[0]);
        auto const value =
            byte == std::numeric_limits<unsigned char>::max() ? std::int64_t { -1 } : static_cast<std::int64_t>(byte);
        return detail::BoolFromInteger(value, staging.column, staging.booleanTrueMayBeMinusOne);
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
///
/// Where the connection converts narrow text with the process code page
/// (@c detail::NarrowTextBinding::WideFromProcessCodePage), the character goes through @c SQL_C_WCHAR instead.
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
        if (auto const binding = cb.NarrowTextBindingMode(); binding == detail::NarrowTextBinding::WideFromProcessCodePage)
            return detail::BindNarrowTextAsUtf16(stmt, column, std::string_view { &value, 1 }, binding, cb);

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
    ///                   every row is bound as one character, converted where the connection converts narrow
    ///                   text; given indicators keep the narrow bind.
    /// @return The ODBC return code of @c SQLBindParameter.
    static LIGHTWEIGHT_FORCE_INLINE SQLRETURN BatchInputParameter(SQLHSTMT stmt,
                                                                  SQLUSMALLINT column,
                                                                  char const* values,
                                                                  size_t rowCount,
                                                                  SqlDataBinderCallback& cb,
                                                                  SQLLEN* indicators = nullptr) noexcept
    {
        if (auto const binding = cb.NarrowTextBindingMode();
            !indicators && binding == detail::NarrowTextBinding::WideFromProcessCodePage)
            return BatchInputParameterAsUtf16(stmt, column, values, rowCount, binding, cb);

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

    /// Binds a column-wise array of characters as @c SQL_C_WCHAR, converted to UTF-16 as @p binding prescribes.
    static SQLRETURN BatchInputParameterAsUtf16(SQLHSTMT stmt,
                                                SQLUSMALLINT column,
                                                char const* values,
                                                size_t rowCount,
                                                detail::NarrowTextBinding binding,
                                                SqlDataBinderCallback& cb) noexcept
    {
        // One UTF-16 code unit per character (a single byte of an ANSI code page), plus a terminator per row.
        constexpr auto rowStride = size_t { 2 };
        auto buffer = std::make_shared<std::u16string>(rowCount * rowStride, u'\0');
        cb.PlanPostExecuteCallback([buffer = buffer]() {}); // keep buffer alive
        auto* const indicators = cb.ProvideInputIndicators(rowCount);
        for (auto const rowIndex: std::views::iota(size_t { 0 }, rowCount))
        {
            auto const wide = detail::NarrowTextToUtf16(std::string_view { values + rowIndex, 1 }, binding);
            (*buffer)[rowIndex * rowStride] = wide.empty() ? u'?' : wide.front();
            indicators[rowIndex] = static_cast<SQLLEN>(sizeof(char16_t));
        }
        return SQLBindParameter(stmt,
                                column,
                                SQL_PARAM_INPUT,
                                SQL_C_WCHAR,
                                SQL_WCHAR,
                                ColumnSize,
                                0,
                                (SQLPOINTER) buffer->data(),
                                static_cast<SQLLEN>(rowStride * sizeof(char16_t)),
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
        if (auto const binding = cb.NarrowTextBindingMode(); binding == detail::NarrowTextBinding::WideFromProcessCodePage)
        {
            auto wideBuffer = std::make_shared<WideBuffer>();
            auto const sqlReturn = SQLBindCol(stmt, column, SQL_C_WCHAR, wideBuffer->data(), WideBufferSize, indicator);
            cb.PlanPostProcessOutputColumn([wideBuffer, result, indicator, binding]() {
                if (*indicator != SQL_NULL_DATA)
                    *result = ToNarrowCharacter(*wideBuffer, *indicator, binding);
            });
            return sqlReturn;
        }

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
    /// @param cb Tells how the connection binds narrow text.
    /// @return The ODBC return code of @c SQLGetData.
    static SQLRETURN GetColumn(
        SQLHSTMT stmt, SQLUSMALLINT column, char* result, SQLLEN* indicator, SqlDataBinderCallback const& cb) noexcept
    {
        if (auto const binding = cb.NarrowTextBindingMode(); binding == detail::NarrowTextBinding::WideFromProcessCodePage)
        {
            auto wideBuffer = WideBuffer {};
            auto const sqlReturn = SQLGetData(stmt, column, SQL_C_WCHAR, wideBuffer.data(), WideBufferSize, indicator);
            if (SQL_SUCCEEDED(sqlReturn) && *indicator != SQL_NULL_DATA)
                *result = ToNarrowCharacter(wideBuffer, *indicator, binding);
            return sqlReturn;
        }

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
        return { value };
    }

  private:
    // One character plus the NUL terminator the driver appends to SQL_C_CHAR output.
    using Buffer = std::array<char, 2>;
    // The same for SQL_C_WCHAR output; a character outside the BMP is a surrogate pair.
    using WideBuffer = std::array<char16_t, 3>;
    static constexpr SQLLEN WideBufferSize = sizeof(WideBuffer);

    /// The first narrow character of the UTF-16 text in @p buffer, whose length in bytes @p indicator gives.
    static char ToNarrowCharacter(WideBuffer const& buffer, SQLLEN indicator, detail::NarrowTextBinding binding)
    {
        auto const length = indicator == SQL_NO_TOTAL
                                ? buffer.size() - 1
                                : (std::min) (buffer.size() - 1, static_cast<size_t>(indicator) / sizeof(char16_t));
        auto const narrow = detail::Utf16ToNarrowText(std::u16string_view { buffer.data(), length }, binding);
        return narrow.empty() ? '\0' : narrow.front();
    }
    static constexpr SQLLEN CharLength = 1;
    static constexpr SQLULEN ColumnSize = 1;
    static constexpr SQLLEN BufferSize = sizeof(Buffer);
};

// clang-format off
template <> struct SqlDataBinder<int8_t>: SqlSimpleDataBinder<int8_t, SQL_C_STINYINT, SQL_TINYINT, SqlColumnTypeDefinitions::Tinyint {}> {};
template <> struct SqlDataBinder<uint8_t>: SqlSimpleDataBinder<uint8_t, SQL_C_UTINYINT, SQL_TINYINT, SqlColumnTypeDefinitions::Tinyint {}> {};
template <> struct SqlDataBinder<int16_t>: SqlSimpleDataBinder<int16_t, SQL_C_SSHORT, SQL_SMALLINT, SqlColumnTypeDefinitions::Smallint {}> {};
template <> struct SqlDataBinder<uint16_t>: SqlSimpleDataBinder<uint16_t, SQL_C_USHORT, SQL_SMALLINT, SqlColumnTypeDefinitions::Smallint {}> {};
template <> struct SqlDataBinder<int32_t>: SqlSimpleDataBinder<int32_t, SQL_C_SLONG, SQL_INTEGER, SqlColumnTypeDefinitions::Integer {}> {};
template <> struct SqlDataBinder<uint32_t>: SqlSimpleDataBinder<uint32_t, SQL_C_ULONG, SQL_INTEGER, SqlColumnTypeDefinitions::Integer {}> {};
template <> struct SqlDataBinder<int64_t>: Int64DataBinderHelper<int64_t, SQL_C_SBIGINT> {};
template <> struct SqlDataBinder<uint64_t>: Int64DataBinderHelper<uint64_t, SQL_C_UBIGINT> {};
//template <> struct SqlDataBinder<uint64_t>: Int64DataBinderHelper<uint64_t, SQL_C_UBIGINT> {};
template <> struct SqlDataBinder<float>: SqlSimpleDataBinder<float, SQL_C_FLOAT, SQL_REAL, SqlColumnTypeDefinitions::Real { .precision = 24 }> {};
template <> struct SqlDataBinder<double>: SqlSimpleDataBinder<double, SQL_C_DOUBLE, SQL_DOUBLE, SqlColumnTypeDefinitions::Real { .precision = 53 }> {};
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
