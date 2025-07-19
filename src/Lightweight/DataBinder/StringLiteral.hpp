// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Core.hpp"
#include "UnicodeConverter.hpp"

#include <concepts>

namespace Lightweight
{

template <std::size_t N>
struct SqlDataBinder<char[N]>
{
    static LIGHTWEIGHT_FORCE_INLINE SQLRETURN InputParameter(SQLHSTMT stmt,
                                                             SQLUSMALLINT column,
                                                             char const* value,
                                                             SqlDataBinderCallback& /*cb*/) noexcept
    {
        return SQLBindParameter(
            stmt, column, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_VARCHAR, N - 1, 0, (SQLPOINTER) value, 0, nullptr);
    }

    static LIGHTWEIGHT_FORCE_INLINE std::string_view Inspect(char const* value) noexcept
    {
        return { value, N * sizeof(char) };
    }
};

template <typename T, std::size_t N>
    requires(std::same_as<T, char16_t> || (std::same_as<T, wchar_t> && sizeof(wchar_t) == 2))
struct SqlDataBinder<T[N]>
{
    static LIGHTWEIGHT_FORCE_INLINE SQLRETURN InputParameter(SQLHSTMT stmt,
                                                             SQLUSMALLINT column,
                                                             T const* value,
                                                             SqlDataBinderCallback& /*cb*/) noexcept
    {
        return SQLBindParameter(stmt,
                                column,
                                SQL_PARAM_INPUT,
                                SQL_C_WCHAR,
                                SQL_WVARCHAR,
                                (N - 1) * sizeof(T),
                                0,
                                (SQLPOINTER) value,
                                0,
                                nullptr);
    }

    static LIGHTWEIGHT_FORCE_INLINE std::string Inspect(T const* value) noexcept
    {
        auto u8String = ToUtf8(std::basic_string_view<T> { value, N });
        return std::string((char const*) u8String.data(), u8String.size());
    }
};

} // namespace Lightweight
