// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace Lup2DbTool
{

/// @brief Trims leading and trailing whitespace from a string.
inline std::string Trim(std::string_view str)
{
    auto start = str.find_first_not_of(" \t\r\n");
    if (start == std::string_view::npos)
        return {};
    auto end = str.find_last_not_of(" \t\r\n");
    return std::string(str.substr(start, end - start + 1));
}

/// @brief Converts a string to uppercase.
inline std::string ToUpper(std::string_view str)
{
    std::string result(str);
    std::ranges::transform(result, result.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return result;
}

/// @brief Removes surrounding quotes (single, double, or brackets) from a string.
inline std::string RemoveQuotes(std::string_view str)
{
    auto trimmed = Trim(str);
    if (trimmed.size() >= 2)
    {
        if ((trimmed.front() == '"' && trimmed.back() == '"') || (trimmed.front() == '\'' && trimmed.back() == '\'')
            || (trimmed.front() == '[' && trimmed.back() == ']'))
        {
            return trimmed.substr(1, trimmed.size() - 2);
        }
    }
    return trimmed;
}

/// @brief Checks if a string starts with a prefix (case-insensitive).
inline bool StartsWithIgnoreCase(std::string_view str, std::string_view prefix)
{
    if (str.size() < prefix.size())
        return false;
    return std::ranges::equal(str.substr(0, prefix.size()), prefix, [](char a, char b) {
        return std::toupper(static_cast<unsigned char>(a)) == std::toupper(static_cast<unsigned char>(b));
    });
}

} // namespace Lup2DbTool
