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

/// @brief Returns true if `token` is a SQL reserved word that must NOT be quoted/treated
/// as an identifier when canonicalising a free-form SQL fragment.
inline bool IsSqlReservedWord(std::string_view token)
{
    auto const upper = ToUpper(token);
    static constexpr std::string_view kKeywords[] = {
        "AND",    "OR",     "NOT",    "NULL",   "IS",    "IN",      "BETWEEN",  "LIKE",   "EXISTS",  "ANY",
        "ALL",    "TRUE",   "FALSE",  "CASE",   "WHEN",  "THEN",    "ELSE",     "END",    "AS",      "DISTINCT",
        "ASC",    "DESC",   "FROM",   "WHERE",  "JOIN",  "ON",      "USING",    "GROUP",  "BY",      "ORDER",
        "HAVING", "LIMIT",  "OFFSET", "SELECT", "CAST",  "CONVERT", "COALESCE", "NULLIF", "DEFAULT", "SOME",
        "INTO",   "VALUES", "INNER",  "LEFT",   "RIGHT", "OUTER",   "FULL",     "CROSS",  "UNION",
    };
    return std::ranges::any_of(kKeywords, [&](auto const& kw) { return kw == upper; });
}

namespace detail
{
    /// Consume a string literal (single- or double-quoted) starting at `i` and
    /// append the verbatim characters to `out`. Advances `i` past the closing quote.
    inline void AppendQuotedRun(std::string_view src, size_t& i, std::string& out)
    {
        char const quote = src[i];
        out.push_back(src[i]);
        ++i;
        while (i < src.size())
        {
            out.push_back(src[i]);
            if (src[i] == quote)
            {
                ++i;
                return;
            }
            ++i;
        }
    }

    inline void AppendNumberRun(std::string_view src, size_t& i, std::string& out)
    {
        while (i < src.size() && (std::isdigit(static_cast<unsigned char>(src[i])) || src[i] == '.'))
        {
            out.push_back(src[i]);
            ++i;
        }
    }

    /// Detects whether the run starting at `i` begins a numeric literal (handling
    /// optional leading sign / decimal-point only when not glued to an identifier).
    inline bool LooksLikeNumberAt(std::string_view src, size_t i)
    {
        char const c = src[i];
        if (std::isdigit(static_cast<unsigned char>(c)))
            return true;
        if (c != '+' && c != '-' && c != '.')
            return false;
        if (i + 1 >= src.size() || !std::isdigit(static_cast<unsigned char>(src[i + 1])))
            return false;
        return i == 0 || !std::isalnum(static_cast<unsigned char>(src[i - 1]));
    }

    inline void AppendBarewordCanonical(std::string_view src, size_t& i, std::string& out)
    {
        size_t const start = i;
        while (i < src.size() && (std::isalnum(static_cast<unsigned char>(src[i])) || src[i] == '_'))
            ++i;
        auto const token = src.substr(start, i - start);
        if (IsSqlReservedWord(token))
        {
            out.append(ToUpper(token));
            return;
        }
        out.push_back('"');
        out.append(ToUpper(token));
        out.push_back('"');
    }
} // namespace detail

/// @brief Rewrites a free-form SQL fragment so every bareword identifier is
/// double-quoted and uppercased, while leaving operators, numbers, string literals
/// and reserved words intact.
///
/// Used wherever lup2dbtool needs to embed source-SQL text inside generated code
/// (UPDATE SET right-hand-sides, IN/EXISTS subquery bodies, etc.). The lup2dbtool
/// emitter canonicalises every identifier to UPPERCASE so PostgreSQL — which is
/// case-sensitive on quoted names — finds the columns regardless of how the source
/// SQL spelt them. This helper applies the same convention to embedded fragments.
inline std::string CanonicalizeIdentifiersInSql(std::string_view expr)
{
    std::string result;
    result.reserve(expr.size());
    size_t i = 0;
    while (i < expr.size())
    {
        char const c = expr[i];
        if (c == '\'' || c == '"')
            detail::AppendQuotedRun(expr, i, result);
        else if (detail::LooksLikeNumberAt(expr, i))
            detail::AppendNumberRun(expr, i, result);
        else if (std::isalpha(static_cast<unsigned char>(c)) || c == '_')
            detail::AppendBarewordCanonical(expr, i, result);
        else
        {
            result.push_back(c);
            ++i;
        }
    }
    return result;
}

/// @brief Collapses runs of whitespace into single spaces and trims leading/trailing whitespace.
inline std::string NormalizeWhitespace(std::string_view str)
{
    std::string result;
    result.reserve(str.size());
    bool lastWasSpace = false;
    for (char c: str)
    {
        if (std::isspace(static_cast<unsigned char>(c)))
        {
            if (!lastWasSpace)
            {
                result += ' ';
                lastWasSpace = true;
            }
        }
        else
        {
            result += c;
            lastWasSpace = false;
        }
    }
    return Trim(result);
}

} // namespace Lup2DbTool
