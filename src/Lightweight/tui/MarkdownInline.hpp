// SPDX-License-Identifier: Apache-2.0
// Vendored from endo/src/tui/MarkdownInline.hpp.

#pragma once

#include <optional>
#include <string_view>

namespace tui
{

// NOLINTBEGIN(readability-identifier-naming)
// Vendored upstream API names (camelCase) preserved verbatim — keeps re-syncs mechanical.

/// @brief Result of finding a CommonMark inline code span.
struct InlineCodeSpan
{
    std::string_view content; ///< The code content between backtick sequences.
    std::size_t endPos;       ///< Position in the text after the closing backtick sequence.
};

/// @brief Applies CommonMark space stripping to inline code content.
[[nodiscard]] constexpr auto stripInlineCodeSpaces(std::string_view content) noexcept -> std::string_view
{
    if (content.size() >= 2 && content.front() == ' ' && content.back() == ' ')
    {
        auto allSpaces = true;
        for (auto ch: content)
        {
            if (ch != ' ')
            {
                allSpaces = false;
                break;
            }
        }
        if (!allSpaces)
            return content.substr(1, content.size() - 2);
    }
    return content;
}

/// @brief Finds the end of a CommonMark inline code span starting at pos.
[[nodiscard]] constexpr auto findInlineCodeEnd(std::string_view text, std::size_t pos) -> std::optional<InlineCodeSpan>
{
    auto const tickStart = pos;
    while (pos < text.size() && text[pos] == '`')
        ++pos;
    auto const tickCount = pos - tickStart;

    auto searchPos = pos;
    while (searchPos < text.size())
    {
        auto const closeStart = text.find('`', searchPos);
        if (closeStart == std::string_view::npos)
            break;

        auto closeEnd = closeStart;
        while (closeEnd < text.size() && text[closeEnd] == '`')
            ++closeEnd;

        if (closeEnd - closeStart == tickCount)
        {
            auto const content = text.substr(pos, closeStart - pos);
            return InlineCodeSpan { .content = stripInlineCodeSpaces(content), .endPos = closeEnd };
        }
        searchPos = closeEnd;
    }

    return std::nullopt;
}

// NOLINTEND(readability-identifier-naming)

} // namespace tui
