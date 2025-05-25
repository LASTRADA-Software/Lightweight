// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Core.hpp"
#include "UnicodeConverter.hpp"

#include <concepts>
#include <memory>
#include <string_view>

template <>
struct SqlDataBinder<std::basic_string_view<char>>
{
    static constexpr SQLSMALLINT CType = SQL_C_CHAR;
    static constexpr SQLSMALLINT SqlType = SQL_VARCHAR;

    static LIGHTWEIGHT_FORCE_INLINE SQLRETURN InputParameter(SQLHSTMT stmt,
                                                             SQLUSMALLINT column,
                                                             std::basic_string_view<char> value,
                                                             SqlDataBinderCallback& /*cb*/) noexcept
    {
        return SQLBindParameter(
            stmt, column, SQL_PARAM_INPUT, CType, SqlType, value.size(), 0, (SQLPOINTER) value.data(), 0, nullptr);
    }

    static LIGHTWEIGHT_FORCE_INLINE std::string_view Inspect(std::basic_string_view<char> value) noexcept
    {
        return { value.data(), value.size() };
    }
};

template <typename Char16Type>
    requires std::same_as<Char16Type, char16_t> || (std::same_as<Char16Type, wchar_t> && sizeof(wchar_t) == 2)
struct SqlDataBinder<std::basic_string_view<Char16Type>>
{
    static constexpr SQLSMALLINT CType = SQL_C_WCHAR;
    static constexpr SQLSMALLINT SqlType = SQL_WVARCHAR;

    static LIGHTWEIGHT_FORCE_INLINE SQLRETURN InputParameter(SQLHSTMT stmt,
                                                             SQLUSMALLINT column,
                                                             std::basic_string_view<Char16Type> value,
                                                             SqlDataBinderCallback& /*cb*/) noexcept
    {
        return SQLBindParameter(
            stmt, column, SQL_PARAM_INPUT, CType, SqlType, value.size(), 0, (SQLPOINTER) value.data(), 0, nullptr);
    }

    static LIGHTWEIGHT_FORCE_INLINE std::string Inspect(std::basic_string_view<Char16Type> value) noexcept
    {
        auto u8String = ToUtf8(value);
        return std::string((char const*) u8String.data(), u8String.size());
    }
};

template <typename Char32Type>
    requires std::same_as<Char32Type, char32_t> || (std::same_as<Char32Type, wchar_t> && sizeof(wchar_t) == 4)
struct SqlDataBinder<std::basic_string_view<Char32Type>>
{
    static constexpr SQLSMALLINT CType = SQL_C_WCHAR;
    static constexpr SQLSMALLINT SqlType = SQL_WVARCHAR;

    static LIGHTWEIGHT_FORCE_INLINE SQLRETURN InputParameter(SQLHSTMT stmt,
                                                             SQLUSMALLINT column,
                                                             std::basic_string_view<Char32Type> value,
                                                             SqlDataBinderCallback& cb) noexcept
    {
        auto u16String = std::make_shared<std::u16string>(ToUtf16(value));
        cb.PlanPostExecuteCallback([u16String = u16String]() {}); // Keep the string alive
        auto const* data = u16String->data();
        auto const sizeInBytes = u16String->size() * sizeof(Char32Type);
        return SQLBindParameter(
            stmt, column, SQL_PARAM_INPUT, CType, SqlType, sizeInBytes, 0, (SQLPOINTER) data, 0, nullptr);
    }

    static LIGHTWEIGHT_FORCE_INLINE std::string Inspect(std::basic_string_view<Char32Type> value) noexcept
    {
        auto u8String = ToUtf8(value);
        return std::string((char const*) u8String.data(), u8String.size());
    }
};

template <>
struct SqlDataBinder<std::u8string_view>
{
    static constexpr SQLSMALLINT CType = SQL_C_WCHAR;
    static constexpr SQLSMALLINT SqlType = SQL_WVARCHAR;

    static LIGHTWEIGHT_FORCE_INLINE SQLRETURN InputParameter(SQLHSTMT stmt,
                                                             SQLUSMALLINT column,
                                                             std::u8string_view value,
                                                             SqlDataBinderCallback& cb) noexcept
    {
        auto u16String = std::make_shared<std::u16string>(ToUtf16(value));
        cb.PlanPostExecuteCallback([u16String = u16String]() {}); // Keep the string alive
        auto const* data = u16String->data();
        auto const sizeInBytes = u16String->size() * sizeof(char8_t);
        return SQLBindParameter(
            stmt, column, SQL_PARAM_INPUT, CType, SqlType, sizeInBytes, 0, (SQLPOINTER) data, 0, nullptr);
    }

    static LIGHTWEIGHT_FORCE_INLINE std::string Inspect(std::basic_string_view<char8_t> value) noexcept
    {
        return std::string { (char const*) value.data(), value.size() };
    }
};
