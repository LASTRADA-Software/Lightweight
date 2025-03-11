// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Core.hpp"
#include "UnicodeConverter.hpp"

#include <cassert>
#include <memory>
#include <utility>

/// @brief Magic number, which is used to determine the optimal maximum size of a column.
///
/// Columns may be larger than this value, but this is the optimal maximum size for performance,
/// and usually also means that values are stored in the same row as the rest of the data, or not.
constexpr std::size_t SqlOptimalMaxColumnSize = 4000;

namespace detail
{

template <auto CType, typename ArrayType>
static SQLRETURN GetArrayData(SQLHSTMT stmt, SQLUSMALLINT column, ArrayType* result, SQLLEN* indicator) noexcept
{
    using CharType = typename ArrayType::value_type;

    *indicator = 0;

    // Resize the string to the size of the data
    // Get the data (take care of SQL_NULL_DATA and SQL_NO_TOTAL)
    auto sqlResult = SQLGetData(
        stmt, column, CType, (SQLPOINTER) result->data(), (SQLLEN) (result->size() * sizeof(CharType)), indicator);

    if (sqlResult == SQL_SUCCESS || sqlResult == SQL_NO_DATA)
    {
        // Data has been read successfully on first call to SQLGetData, or there is no data to read.
        if (*indicator == SQL_NULL_DATA)
            result->clear();
        else
            result->resize(*indicator / sizeof(CharType));
        return sqlResult;
    }

    if (sqlResult == SQL_SUCCESS_WITH_INFO && *indicator > static_cast<SQLLEN>(result->size()))
    {
        // We have a truncation and the server knows how much data is left.
        auto const totalCharCount = *indicator / sizeof(CharType);
        auto const charsWritten = result->size() - 1;
        result->resize(totalCharCount + 1);
        auto* bufferCont = result->data() + charsWritten;
        auto const bufferCharsAvailable = (totalCharCount + 1) - charsWritten;
        sqlResult = SQLGetData(stmt, column, CType, bufferCont, bufferCharsAvailable * sizeof(CharType), indicator);
        if (SQL_SUCCEEDED(sqlResult))
            result->resize(charsWritten + (*indicator / sizeof(CharType)));
        return sqlResult;
    }

    size_t writeIndex = 0;
    while (sqlResult == SQL_SUCCESS_WITH_INFO && *indicator == SQL_NO_TOTAL)
    {
        // We have a truncation and the server does not know how much data is left.
        writeIndex += result->size() - 1;
        result->resize(result->size() * 2);
        auto* const bufferStart = result->data() + writeIndex;
        size_t const bufferCharsAvailable = result->size() - writeIndex;
        sqlResult = SQLGetData(stmt, column, CType, bufferStart, bufferCharsAvailable, indicator);
    }
    return sqlResult;
}

template <typename Utf16StringType>
SQLRETURN GetColumnUtf16(SQLHSTMT stmt,
                         SQLUSMALLINT column,
                         Utf16StringType* result,
                         SQLLEN* indicator,
                         SqlDataBinderCallback const& /*cb*/) noexcept
{
    if constexpr (requires { Utf16StringType::Capacity; })
        result->resize(Utf16StringType::Capacity);
    else if (result->size() == 0)
        result->resize(255);

    return GetArrayData<SQL_C_WCHAR>(stmt, column, result, indicator);
}

template <typename StringType>
SQLRETURN OutputColumnNonUtf16Unicode(
    SQLHSTMT stmt, SQLUSMALLINT column, StringType* result, SQLLEN* indicator, SqlDataBinderCallback& cb) noexcept
{
    using CharType = typename StringType::value_type;

    auto u16String = std::make_shared<std::u16string>();
    if (!result->empty())
        u16String->resize(result->size());
    else
        u16String->resize(255);

    cb.PlanPostProcessOutputColumn([stmt, column, result, indicator, u16String = u16String]() {
        if (*indicator == SQL_NULL_DATA)
            u16String->clear();
        else if (*indicator == SQL_NO_TOTAL)
            ; // We don't know the size of the data
        else if (*indicator <= static_cast<SQLLEN>(u16String->size() * sizeof(char16_t)))
            u16String->resize(*indicator / sizeof(char16_t));
        else
        {
            auto const totalCharsRequired = static_cast<SQLLEN>(*indicator / sizeof(char16_t));
            *indicator += sizeof(char16_t); // Add space to hold the null terminator
            u16String->resize(totalCharsRequired);
            auto const sqlResult = SQLGetData(stmt, column, SQL_C_WCHAR, u16String->data(), *indicator, indicator);
            (void) sqlResult;
            assert(SQL_SUCCEEDED(sqlResult));
            assert(*indicator == static_cast<SQLLEN>(totalCharsRequired * sizeof(char16_t)));
        }

        if constexpr (sizeof(typename StringType::value_type) == 1)
            *result = ToUtf8(*u16String);
        else if constexpr (sizeof(typename StringType::value_type) == 4)
        {
            // *result = ToUtf32(*u16String);
            auto const u32String = ToUtf32(*u16String);
            *result = StringType {
                (CharType const*) u32String.data(),
                (CharType const*) u32String.data() + u32String.size(),
            };
        }
    });

    return SQLBindCol(stmt,
                      column,
                      SQL_C_WCHAR,
                      static_cast<SQLPOINTER>(u16String->data()),
                      static_cast<SQLLEN>(u16String->size() * sizeof(char16_t)),
                      indicator);
}

} // namespace detail

// SqlDataBinder<> specialization for ANSI character strings
template <typename AnsiStringType>
    requires SqlBasicStringBinderConcept<AnsiStringType, char>
struct SqlDataBinder<AnsiStringType>
{
    using ValueType = AnsiStringType;
    using CharType = char;
    using StringTraits = SqlBasicStringOperations<AnsiStringType>;

    static constexpr auto ColumnType = StringTraits::ColumnType;

    static LIGHTWEIGHT_FORCE_INLINE SQLRETURN InputParameter(SQLHSTMT stmt,
                                                             SQLUSMALLINT column,
                                                             AnsiStringType const& value,
                                                             SqlDataBinderCallback& /*cb*/) noexcept
    {
        return SQLBindParameter(stmt,
                                column,
                                SQL_PARAM_INPUT,
                                SQL_C_CHAR,
                                SQL_VARCHAR,
                                StringTraits::Size(&value),
                                0,
                                (SQLPOINTER) StringTraits::Data(&value),
                                sizeof(AnsiStringType),
                                nullptr);
    }

    static LIGHTWEIGHT_FORCE_INLINE SQLRETURN OutputColumn(SQLHSTMT stmt,
                                                           SQLUSMALLINT column,
                                                           AnsiStringType* result,
                                                           SQLLEN* indicator,
                                                           SqlDataBinderCallback& cb) noexcept
    {
        if constexpr (requires { AnsiStringType::Capacity; })
            StringTraits::Resize(result, AnsiStringType::Capacity);
        else if (StringTraits::Size(result) == 0)
            StringTraits::Resize(result, 255);

        if constexpr (requires { StringTraits::PostProcessOutputColumn(result, *indicator); })
            cb.PlanPostProcessOutputColumn(
                [indicator, result]() { StringTraits::PostProcessOutputColumn(result, *indicator); });
        else
            cb.PlanPostProcessOutputColumn(
                [stmt, column, indicator, result]() { PostProcessOutputColumn(stmt, column, result, indicator); });

        return SQLBindCol(stmt,
                          column,
                          SQL_C_CHAR,
                          (SQLPOINTER) StringTraits::Data(result),
                          (SQLLEN) StringTraits::Size(result),
                          indicator);
    }

    static void PostProcessOutputColumn(SQLHSTMT stmt, SQLUSMALLINT column, AnsiStringType* result, SQLLEN* indicator)
    {
        // Now resize the string to the actual length of the data
        // NB: If the indicator is greater than the buffer size, we have a truncation.
        if (*indicator == SQL_NO_TOTAL)
        {
            // We have a truncation and the server does not know how much data is left.
            StringTraits::Resize(result, StringTraits::Size(result) - 1);
        }
        else if (*indicator == SQL_NULL_DATA)
        {
            // We have a NULL value
            StringTraits::Resize(result, 0);
        }
        else if (*indicator <= static_cast<SQLLEN>(StringTraits::Size(result)))
        {
            StringTraits::Resize(result, static_cast<size_t>(*indicator));
        }
        else
        {
            // We have a truncation and the server knows how much data is left.
            // Extend the buffer and fetch the rest via SQLGetData.

            auto const totalCharsRequired = *indicator;
            StringTraits::Resize(result, totalCharsRequired + 1);
            auto const sqlResult =
                SQLGetData(stmt, column, SQL_C_CHAR, StringTraits::Data(result), totalCharsRequired + 1, indicator);
            (void) sqlResult;
            assert(SQL_SUCCEEDED(sqlResult));
            assert(*indicator == totalCharsRequired);
            StringTraits::Resize(result, totalCharsRequired);
        }
    }

    // NOLINTNEXTLINE(readability-function-cognitive-complexity)
    static SQLRETURN GetColumn(SQLHSTMT stmt,
                               SQLUSMALLINT column,
                               AnsiStringType* result,
                               SQLLEN* indicator,
                               SqlDataBinderCallback const& /*cb*/) noexcept
    {
        if constexpr (requires { AnsiStringType::Capacity; })
        {
            StringTraits::Resize(result, AnsiStringType::Capacity);
            SQLRETURN const rv =
                SQLGetData(stmt, column, SQL_C_CHAR, StringTraits::Data(result), AnsiStringType::Capacity, indicator);
            if (rv == SQL_SUCCESS || rv == SQL_NO_DATA)
            {
                if (*indicator == SQL_NULL_DATA)
                    StringTraits::Resize(result, 0);
                else if (*indicator != SQL_NO_TOTAL)
                    StringTraits::Resize(result, (std::min)(AnsiStringType::Capacity, static_cast<size_t>(*indicator)));
            }
            if constexpr (requires { StringTraits::PostProcessOutputColumn(result, *indicator); })
                StringTraits::PostProcessOutputColumn(result, *indicator);
            return rv;
        }
        else
        {
            StringTraits::Reserve(result, 15);
            size_t writeIndex = 0;
            *indicator = 0;
            while (true)
            {
                auto* const bufferStart = StringTraits::Data(result) + writeIndex;
                size_t const bufferSize = StringTraits::Size(result) - writeIndex;
                SQLRETURN const rv = SQLGetData(stmt, column, SQL_C_CHAR, bufferStart, bufferSize, indicator);
                switch (rv)
                {
                    case SQL_SUCCESS:
                    case SQL_NO_DATA:
                        // last successive call
                        if (*indicator != SQL_NULL_DATA)
                        {
                            StringTraits::Resize(result, writeIndex + *indicator);
                            *indicator = StringTraits::Size(result);
                        }
                        return SQL_SUCCESS;
                    case SQL_SUCCESS_WITH_INFO: {
                        // more data pending
                        if (*indicator == SQL_NO_TOTAL)
                        {
                            // We have a truncation and the server does not know how much data is left.
                            writeIndex += bufferSize - 1;
                            StringTraits::Resize(result, (2 * writeIndex) + 1);
                        }
                        else if (std::cmp_greater_equal(*indicator, bufferSize))
                        {
                            // We have a truncation and the server knows how much data is left.
                            writeIndex += bufferSize - 1;
                            StringTraits::Resize(result, writeIndex + *indicator);
                        }
                        else
                        {
                            // We have no truncation and the server knows how much data is left.
                            StringTraits::Resize(result, writeIndex + *indicator - 1);
                            return SQL_SUCCESS;
                        }
                        break;
                    }
                    default:
                        if constexpr (requires { StringTraits::PostProcessOutputColumn(result, *indicator); })
                            StringTraits::PostProcessOutputColumn(result, *indicator);
                        return rv;
                }
            }
        }
    }

    static LIGHTWEIGHT_FORCE_INLINE std::string_view Inspect(AnsiStringType const& value) noexcept
    {
        return { StringTraits::Data(&value), StringTraits::Size(&value) };
    }
};

// SqlDataBinder<> specialization for UTF-16 strings
template <typename Utf16StringType>
    requires(SqlBasicStringBinderConcept<Utf16StringType, char16_t>
             || (SqlBasicStringBinderConcept<Utf16StringType, unsigned short>)
             || (SqlBasicStringBinderConcept<Utf16StringType, wchar_t> && sizeof(wchar_t) == 2))
struct SqlDataBinder<Utf16StringType>
{
    using ValueType = Utf16StringType;
    using CharType = std::remove_cvref_t<decltype(std::declval<Utf16StringType>()[0])>;
    using StringTraits = SqlBasicStringOperations<Utf16StringType>;

    static constexpr auto ColumnType = StringTraits::ColumnType;

    static constexpr auto CType = SQL_C_WCHAR;

    static SQLRETURN InputParameter(SQLHSTMT stmt,
                                    SQLUSMALLINT column,
                                    Utf16StringType const& value,
                                    SqlDataBinderCallback& cb) noexcept
    {
        switch (cb.ServerType())
        {
            case SqlServerType::POSTGRESQL: {
                // PostgreSQL only supports UTF-8 as Unicode encoding
                auto u8String =
                    std::make_shared<std::u8string>(ToUtf8(detail::SqlViewHelper<Utf16StringType>::View(value)));
                cb.PlanPostExecuteCallback([u8String = u8String]() {}); // Keep the string alive
                return SQLBindParameter(stmt,
                                        column,
                                        SQL_PARAM_INPUT,
                                        SQL_C_CHAR,
                                        SQL_VARCHAR,
                                        u8String->size(),
                                        0,
                                        (SQLPOINTER) u8String->data(),
                                        0,
                                        nullptr);
            }
            case SqlServerType::ORACLE:
            case SqlServerType::MYSQL:
            case SqlServerType::SQLITE: // We assume UTF-16 for SQLite
            case SqlServerType::MICROSOFT_SQL:
            case SqlServerType::UNKNOWN: {
                using CharType = StringTraits::CharType;
                auto const* data = StringTraits::Data(&value);
                auto const sizeInBytes = StringTraits::Size(&value) * sizeof(CharType);
                auto const charCount = StringTraits::Size(&value);
                auto const sqlType =
                    static_cast<SQLSMALLINT>(charCount > SqlOptimalMaxColumnSize ? SQL_WLONGVARCHAR : SQL_WVARCHAR);
                return SQLBindParameter(stmt,
                                        column,
                                        SQL_PARAM_INPUT,
                                        CType,
                                        sqlType,
                                        charCount,
                                        0,
                                        (SQLPOINTER) data,
                                        sizeInBytes,
                                        nullptr);
            }
        }
        std::unreachable();
    }

    static SQLRETURN OutputColumn(SQLHSTMT stmt,
                                  SQLUSMALLINT column,
                                  Utf16StringType* result,
                                  SQLLEN* indicator,
                                  SqlDataBinderCallback& cb) noexcept
    {
        if constexpr (requires { Utf16StringType::Capacity; })
            StringTraits::Resize(result, Utf16StringType::Capacity);
        else if (StringTraits::Size(result) == 0)
            StringTraits::Resize(result, 255);

        if constexpr (requires { StringTraits::PostProcessOutputColumn(result, *indicator); })
        {
            cb.PlanPostProcessOutputColumn(
                [indicator, result]() { StringTraits::PostProcessOutputColumn(result, *indicator); });
        }
        else
        {
            cb.PlanPostProcessOutputColumn([stmt, column, indicator, result]() {
                // Now resize the string to the actual length of the data
                // NB: If the indicator is greater than the buffer size, we have a truncation.
                if (*indicator == SQL_NULL_DATA)
                    StringTraits::Resize(result, 0);
                else if (*indicator == SQL_NO_TOTAL)
                    ; // We don't know the size of the data
                else if (*indicator <= static_cast<SQLLEN>(result->size() * sizeof(char16_t)))
                    result->resize(*indicator / sizeof(char16_t));
                else
                {
                    auto const totalCharsRequired = static_cast<SQLLEN>(*indicator / sizeof(char16_t));
                    *indicator += sizeof(char16_t); // Add space to hold the null terminator
                    result->resize(totalCharsRequired);
                    auto const sqlResult = SQLGetData(stmt, column, SQL_C_WCHAR, result->data(), *indicator, indicator);
                    (void) sqlResult;
                    assert(SQL_SUCCEEDED(sqlResult));
                    assert(*indicator == static_cast<SQLLEN>(totalCharsRequired * sizeof(char16_t)));
                }
            });
        }
        return SQLBindCol(stmt,
                          column,
                          CType,
                          (SQLPOINTER) StringTraits::Data(result),
                          (SQLLEN) StringTraits::Size(result),
                          indicator);
    }

    static SQLRETURN GetColumn(SQLHSTMT stmt,
                               SQLUSMALLINT column,
                               Utf16StringType* result,
                               SQLLEN* indicator,
                               SqlDataBinderCallback const& cb) noexcept
    {
        return detail::GetColumnUtf16(stmt, column, result, indicator, cb);
    }

    static LIGHTWEIGHT_FORCE_INLINE std::string Inspect(Utf16StringType const& value) noexcept
    {
        auto u8String = ToUtf8(detail::SqlViewHelper<Utf16StringType>::View(value));
        return std::string(reinterpret_cast<char const*>(u8String.data()), u8String.size());
    }
};

// SqlDataBinder<> specialization for UTF-32 strings
template <typename Utf32StringType>
    requires(SqlBasicStringBinderConcept<Utf32StringType, char32_t>
             || (SqlBasicStringBinderConcept<Utf32StringType, uint32_t>)
             || (SqlBasicStringBinderConcept<Utf32StringType, wchar_t> && sizeof(wchar_t) == 4))
struct SqlDataBinder<Utf32StringType>
{
    using ValueType = Utf32StringType;
    using CharType = typename Utf32StringType::value_type;
    using StringTraits = SqlBasicStringOperations<Utf32StringType>;

    static constexpr auto ColumnType = StringTraits::ColumnType;

    static SQLRETURN InputParameter(SQLHSTMT stmt,
                                    SQLUSMALLINT column,
                                    Utf32StringType const& value,
                                    SqlDataBinderCallback& cb) noexcept
    {
        switch (cb.ServerType())
        {
            case SqlServerType::POSTGRESQL: {
                // PostgreSQL only supports UTF-8 as Unicode encoding
                auto u8String =
                    std::make_shared<std::u8string>(ToUtf8(detail::SqlViewHelper<Utf32StringType>::View(value)));
                cb.PlanPostExecuteCallback([u8String = u8String]() {}); // Keep the string alive
                return SQLBindParameter(stmt,
                                        column,
                                        SQL_PARAM_INPUT,
                                        SQL_C_CHAR,
                                        SQL_VARCHAR,
                                        u8String->size(),
                                        0,
                                        (SQLPOINTER) u8String->data(),
                                        0,
                                        nullptr);
            }
            case SqlServerType::ORACLE:
            case SqlServerType::MYSQL:
            case SqlServerType::SQLITE: // We assume UTF-16 for SQLite
            case SqlServerType::MICROSOFT_SQL:
            case SqlServerType::UNKNOWN: {
                auto u16String =
                    std::make_shared<std::u16string>(ToUtf16(detail::SqlViewHelper<Utf32StringType>::View(value)));
                cb.PlanPostExecuteCallback([u8String = u16String]() {}); // Keep the string alive
                auto const* data = u16String->data();
                auto const charCount = u16String->size();
                auto const sizeInBytes = u16String->size() * sizeof(char16_t);
                auto const CType = SQLSMALLINT { SQL_C_WCHAR };
                auto const sqlType = static_cast<SQLSMALLINT>(charCount > SqlOptimalMaxColumnSize ? SQL_WLONGVARCHAR : SQL_WVARCHAR);
                return SQLBindParameter(stmt,
                                        column,
                                        SQL_PARAM_INPUT,
                                        CType,
                                        sqlType,
                                        charCount,
                                        0,
                                        (SQLPOINTER) data,
                                        sizeInBytes,
                                        nullptr);
            }
        }
        std::unreachable();
    }

    static SQLRETURN OutputColumn(SQLHSTMT stmt,
                                  SQLUSMALLINT column,
                                  Utf32StringType* result,
                                  SQLLEN* indicator,
                                  SqlDataBinderCallback& cb) noexcept
    {
        return detail::OutputColumnNonUtf16Unicode<Utf32StringType>(stmt, column, result, indicator, cb);
    }

    static SQLRETURN GetColumn(SQLHSTMT stmt,
                               SQLUSMALLINT column,
                               Utf32StringType* result,
                               SQLLEN* indicator,
                               SqlDataBinderCallback const& cb) noexcept
    {
        auto u16String = std::u16string {};
        auto const sqlResult = detail::GetColumnUtf16(stmt, column, &u16String, indicator, cb);
        if (!SQL_SUCCEEDED(sqlResult))
            return sqlResult;

        auto const u32String = ToUtf32(u16String);
        StringTraits::Resize(result, u32String.size());
        std::copy_n((CharType const*) u32String.data(), u32String.size(), StringTraits::Data(result));

        return sqlResult;
    }

    static LIGHTWEIGHT_FORCE_INLINE std::string Inspect(Utf32StringType const& value) noexcept
    {
        auto u8String = ToUtf8(detail::SqlViewHelper<Utf32StringType>::View(value));
        return std::string(reinterpret_cast<char const*>(u8String.data()), u8String.size());
    }
};

// SqlDataBinder<> specialization for UTF-8 strings
template <typename Utf8StringType>
    requires SqlBasicStringBinderConcept<Utf8StringType, char8_t>
struct SqlDataBinder<Utf8StringType>
{
    using ValueType = Utf8StringType;
    using CharType = char8_t;
    using StringTraits = SqlBasicStringOperations<Utf8StringType>;

    static constexpr auto ColumnType = StringTraits::ColumnType;

    static SQLRETURN InputParameter(SQLHSTMT stmt,
                                    SQLUSMALLINT column,
                                    Utf8StringType const& value,
                                    SqlDataBinderCallback& cb) noexcept
    {
        switch (cb.ServerType())
        {
            case SqlServerType::POSTGRESQL: {
                // PostgreSQL only supports UTF-8 as Unicode encoding
                return SQLBindParameter(stmt,
                                        column,
                                        SQL_PARAM_INPUT,
                                        SQL_C_CHAR,
                                        SQL_VARCHAR,
                                        value.size(),
                                        0,
                                        (SQLPOINTER) value.data(),
                                        0,
                                        nullptr);
            }
            case SqlServerType::ORACLE:
            case SqlServerType::MYSQL:
            case SqlServerType::SQLITE: // We assume UTF-16 for SQLite
            case SqlServerType::MICROSOFT_SQL:
            case SqlServerType::UNKNOWN: {
                auto u16String =
                    std::make_shared<std::u16string>(ToUtf16(detail::SqlViewHelper<Utf8StringType>::View(value)));
                cb.PlanPostExecuteCallback([u16String = u16String]() {}); // Keep the string alive

                auto const CType = SQL_C_WCHAR;
                auto const charCount = u16String->size();
                auto const byteCount = u16String->size() * sizeof(char16_t);
                auto const sqlType = static_cast<SQLSMALLINT>(charCount > SqlOptimalMaxColumnSize ? SQL_WLONGVARCHAR : SQL_WVARCHAR);
                return SQLBindParameter(stmt,
                                        column,
                                        SQL_PARAM_INPUT,
                                        CType,
                                        sqlType,
                                        charCount,
                                        0,
                                        (SQLPOINTER) u16String->data(),
                                        byteCount,
                                        nullptr);
            }
        }
        std::unreachable();
    }

    static SQLRETURN OutputColumn(SQLHSTMT stmt,
                                  SQLUSMALLINT column,
                                  Utf8StringType* result,
                                  SQLLEN* indicator,
                                  SqlDataBinderCallback& cb) noexcept
    {
        return detail::OutputColumnNonUtf16Unicode<Utf8StringType>(stmt, column, result, indicator, cb);
    }

    static SQLRETURN GetColumn(SQLHSTMT stmt,
                               SQLUSMALLINT column,
                               Utf8StringType* result,
                               SQLLEN* indicator,
                               SqlDataBinderCallback const& cb) noexcept
    {
        auto u16String = std::u16string {};
        u16String.resize(result->size());
        auto const sqlReturn = detail::GetColumnUtf16(stmt, column, &u16String, indicator, cb);
        if (SQL_SUCCEEDED(sqlReturn))
            *result = ToUtf8(u16String);
        return sqlReturn;
    }

    static LIGHTWEIGHT_FORCE_INLINE std::string Inspect(Utf8StringType const& value) noexcept
    {
        // Pass data as-is
        return std::string(reinterpret_cast<char const*>(value.data()), value.size());
    }
};
