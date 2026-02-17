// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <algorithm>
#include <string_view>

namespace Lightweight
{

/// @brief Helper class, used to represent a real SQL column names as template arguments.
///
/// @see Field, BelongsTo
template <size_t N>
struct SqlRealName
{
    /// The length of the name string (excluding null terminator).
    static constexpr size_t length = (N > 0) ? (N - 1) : 0;

    /// Returns the length of the name string.
    [[nodiscard]] constexpr size_t size() const noexcept
    {
        return length;
    }

    constexpr ~SqlRealName() noexcept = default;
    constexpr SqlRealName() noexcept = default;
    /// Default copy constructor.
    constexpr SqlRealName(SqlRealName const&) noexcept = default;
    /// Default move constructor.
    constexpr SqlRealName(SqlRealName&&) noexcept = default;
    /// Default copy assignment operator.
    constexpr SqlRealName& operator=(SqlRealName const&) noexcept = default;
    /// Default move assignment operator.
    constexpr SqlRealName& operator=(SqlRealName&&) noexcept = default;

    /// Constructs a SqlRealName from a character array literal.
    constexpr SqlRealName(char const (&str)[N]) noexcept
    {
        std::copy_n(str, N, value);
    }

    /// The underlying character buffer storing the name.
    char value[N] {};

    /// Returns true if the name is empty.
    [[nodiscard]] constexpr bool empty() const noexcept
    {
        return length == 0;
    }

    /// Returns a pointer to the beginning of the name string.
    [[nodiscard]] constexpr char const* begin() const noexcept
    {
        return value;
    }

    /// Returns a pointer past the end of the name string.
    [[nodiscard]] constexpr char const* end() const noexcept
    {
        return value + length;
    }

    /// Three-way comparison operator.
    [[nodiscard]] constexpr auto operator<=>(SqlRealName const&) const = default;

    /// Returns the name as a string_view.
    // NOLINTNEXTLINE(readability-identifier-naming)
    [[nodiscard]] constexpr std::string_view sv() const noexcept
    {
        return { value, length };
    }

    [[nodiscard]] constexpr operator std::string_view() const noexcept
    {
        return { value, length };
    }
};

} // namespace Lightweight
