// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <algorithm>
#include <string_view>

/// @brief Helper class, used to represent a real SQL column names as template arguments.
///
/// @see Field, BelongsTo
template <size_t N>
struct SqlRealName
{
    static constexpr size_t length = (N > 0) ? (N - 1) : 0;

    [[nodiscard]] constexpr size_t size() const noexcept
    {
        return length;
    }

    constexpr ~SqlRealName() noexcept = default;
    constexpr SqlRealName() noexcept = default;
    constexpr SqlRealName(SqlRealName const&) noexcept = default;
    constexpr SqlRealName(SqlRealName&&) noexcept = default;
    constexpr SqlRealName& operator=(SqlRealName const&) noexcept = default;
    constexpr SqlRealName& operator=(SqlRealName&&) noexcept = default;

    constexpr SqlRealName(char const (&str)[N]) noexcept
    {
        std::copy_n(str, N, value);
    }

    char value[N] {};

    [[nodiscard]] constexpr bool empty() const noexcept
    {
        return length == 0;
    }

    [[nodiscard]] constexpr char const* begin() const noexcept
    {
        return value;
    }

    [[nodiscard]] constexpr char const* end() const noexcept
    {
        return value + length;
    }

    [[nodiscard]] constexpr auto operator<=>(const SqlRealName&) const = default;

    [[nodiscard]] constexpr std::string_view sv() const noexcept
    {
        return { value, length };
    }

    [[nodiscard]] constexpr operator std::string_view() const noexcept
    {
        return { value, length };
    }
};

