// SPDX-License-Identifier: Apache-2.0
// Vendored from endo/src/tui/MarkdownTable.cpp.

#include "MarkdownInline.hpp"
#include "MarkdownTable.hpp"
#include "Unicode.hpp"

#if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wold-style-cast"
#endif
#include <libunicode/utf8_grapheme_segmenter.h>
#if defined(__clang__)
    #pragma clang diagnostic pop
#endif

#include <algorithm>

namespace tui
{

// NOLINTBEGIN(readability-identifier-naming, readability-function-cognitive-complexity)
// Vendored upstream API names (camelCase) preserved verbatim — keeps re-syncs mechanical.

namespace
{
    auto trim(std::string_view sv) -> std::string_view
    {
        auto const start = sv.find_first_not_of(" \t");
        if (start == std::string_view::npos)
            return {};
        auto const end = sv.find_last_not_of(" \t");
        return sv.substr(start, end - start + 1);
    }

    auto displayWidth(std::string_view text) -> int
    {
        auto width = 0;
        auto segmenter = unicode::utf8_grapheme_segmenter(text);
        for (auto const& cluster: segmenter)
            width += graphemeClusterWidth(cluster);
        return width;
    }
} // namespace

auto detectTableRow(std::string_view line) -> bool
{
    auto const trimmed = trim(line);
    return !trimmed.empty() && trimmed.front() == '|';
}

auto detectTableSeparator(std::string_view line) -> bool
{
    auto const trimmed = trim(line);
    if (trimmed.empty() || trimmed.front() != '|')
        return false;

    auto pos = std::size_t { 1 };
    auto foundCell = false;

    while (pos < trimmed.size())
    {
        auto const pipePos = trimmed.find('|', pos);
        auto const cellEnd = (pipePos != std::string_view::npos) ? pipePos : trimmed.size();
        auto const cell = trim(trimmed.substr(pos, cellEnd - pos));

        if (!cell.empty())
        {
            auto cellPos = std::size_t { 0 };
            if (cellPos < cell.size() && cell[cellPos] == ':')
                ++cellPos;

            auto const dashStart = cellPos;
            while (cellPos < cell.size() && cell[cellPos] == '-')
                ++cellPos;

            if (cellPos == dashStart)
                return false;

            if (cellPos < cell.size() && cell[cellPos] == ':')
                ++cellPos;

            if (cellPos != cell.size())
                return false;

            foundCell = true;
        }

        if (pipePos == std::string_view::npos)
            break;
        pos = pipePos + 1;
    }

    return foundCell;
}

auto splitTableRow(std::string_view line) -> std::vector<std::string>
{
    auto const trimmed = trim(line);
    auto result = std::vector<std::string> {};

    if (trimmed.empty() || trimmed.front() != '|')
        return result;

    auto content = trimmed.substr(1);
    if (!content.empty() && content.back() == '|')
        content.remove_suffix(1);

    auto pos = std::size_t { 0 };
    while (pos <= content.size())
    {
        auto const pipePos = content.find('|', pos);
        auto const cellEnd = (pipePos != std::string_view::npos) ? pipePos : content.size();
        auto const cell = trim(content.substr(pos, cellEnd - pos));
        result.emplace_back(cell);
        if (pipePos == std::string_view::npos)
            break;
        pos = pipePos + 1;
    }

    return result;
}

auto parseTableAlignments(std::string_view line) -> std::vector<TableAlignment>
{
    auto const cells = splitTableRow(line);
    auto result = std::vector<TableAlignment> {};
    result.reserve(cells.size());

    for (auto const& cell: cells)
    {
        auto const trimmed = trim(cell);
        if (trimmed.empty())
        {
            result.push_back(TableAlignment::Left);
            continue;
        }

        auto const leftColon = trimmed.front() == ':';
        auto const rightColon = trimmed.back() == ':';

        if (leftColon && rightColon)
            result.push_back(TableAlignment::Center);
        else if (rightColon)
            result.push_back(TableAlignment::Right);
        else
            result.push_back(TableAlignment::Left);
    }

    return result;
}

auto computeColumnWidths(ParsedTable const& table) -> std::vector<int>
{
    auto widths = std::vector<int>(table.columnCount, 3);

    for (std::size_t col = 0; col < table.columnCount; ++col)
    {
        if (col < table.headers.size())
            widths[col] = std::max(widths[col], inlineDisplayWidth(table.headers[col]));

        for (auto const& row: table.rows)
        {
            if (col < row.size())
                widths[col] = std::max(widths[col], inlineDisplayWidth(row[col]));
        }
    }

    return widths;
}

auto alignCell(std::string_view text, int width, TableAlignment alignment) -> std::string
{
    auto const textWidth = displayWidth(text);
    auto const padding = std::max(0, width - textWidth);

    switch (alignment)
    {
        case TableAlignment::Right: {
            auto result = std::string(static_cast<std::size_t>(padding), ' ');
            result.append(text);
            return result;
        }
        case TableAlignment::Center: {
            auto const leftPad = padding / 2;
            auto const rightPad = padding - leftPad;
            auto result = std::string(static_cast<std::size_t>(leftPad), ' ');
            result.append(text);
            result.append(static_cast<std::size_t>(rightPad), ' ');
            return result;
        }
        case TableAlignment::Left:
        default: {
            auto result = std::string(text);
            result.append(static_cast<std::size_t>(padding), ' ');
            return result;
        }
    }
}

void constrainColumnWidths(std::vector<int>& widths, int maxTableWidth)
{
    if (widths.empty() || maxTableWidth <= 0)
        return;

    auto const columnCount = static_cast<int>(widths.size());
    auto const overhead = 1 + (columnCount * 3);
    auto const availableContent = maxTableWidth - overhead;

    if (availableContent <= 0)
        return;

    auto totalContent = 0;
    for (auto w: widths)
        totalContent += w;

    if (totalContent <= availableContent)
        return;

    auto constexpr minWidth = 3;
    auto excess = totalContent - availableContent;

    auto indices = std::vector<std::size_t>(widths.size());
    for (std::size_t i = 0; i < indices.size(); ++i)
        indices[i] = i;
    std::ranges::sort(indices, [&](auto a, auto b) { return widths[a] > widths[b]; });

    auto i = std::size_t { 0 };
    while (i < indices.size() && excess > 0)
    {
        auto const tierWidth = widths[indices[i]];
        auto j = i + 1;
        while (j < indices.size() && widths[indices[j]] == tierWidth)
            ++j;
        auto const tierCount = static_cast<int>(j - i);

        auto const nextTierWidth = (j < indices.size()) ? widths[indices[j]] : minWidth;
        auto const target = std::max(nextTierWidth, minWidth);
        auto const reductionPerCol = tierWidth - target;
        auto const maxTierReduction = reductionPerCol * tierCount;

        if (maxTierReduction >= excess)
        {
            auto const perCol = excess / tierCount;
            auto remainder = excess % tierCount;
            for (auto k = i; k < j; ++k)
            {
                auto const extra = (remainder > 0) ? 1 : 0;
                widths[indices[k]] -= perCol + extra;
                if (remainder > 0)
                    --remainder;
            }
            excess = 0;
        }
        else
        {
            for (auto k = i; k < j; ++k)
                widths[indices[k]] = target;
            excess -= maxTierReduction;

            if (target <= minWidth)
                break;

            continue;
        }
        i = j;
    }
}

auto wrapText(std::string_view text, int maxWidth) -> std::vector<std::string>
{
    if (maxWidth <= 0)
        return { std::string(text) };

    auto const textW = inlineDisplayWidth(text);
    if (textW <= maxWidth)
        return { std::string(text) };

    auto lines = std::vector<std::string> {};
    auto currentLine = std::string {};
    auto currentWidth = 0;
    auto pos = std::size_t { 0 };

    while (pos < text.size())
    {
        auto const wordStart = text.find_first_not_of(' ', pos);
        if (wordStart == std::string_view::npos)
            break;

        auto const wordEnd = text.find(' ', wordStart);
        auto const word = text.substr(
            wordStart, wordEnd == std::string_view::npos ? std::string_view::npos : wordEnd - wordStart);
        auto const wordWidth = inlineDisplayWidth(word);

        if (currentLine.empty() && wordWidth > maxWidth)
        {
            auto segmenter = unicode::utf8_grapheme_segmenter(word);
            auto chunk = std::string {};
            auto chunkWidth = 0;
            for (auto const& cluster: segmenter)
            {
                auto const clusterW = graphemeClusterWidth(cluster);
                if (chunkWidth + clusterW > maxWidth && !chunk.empty())
                {
                    lines.push_back(std::move(chunk));
                    chunk.clear();
                    chunkWidth = 0;
                }
                for (auto ch: cluster)
                    chunk += static_cast<char>(ch);
                chunkWidth += clusterW;
            }
            if (!chunk.empty())
            {
                currentLine = std::move(chunk);
                currentWidth = chunkWidth;
            }
        }
        else if (currentLine.empty())
        {
            currentLine = std::string(word);
            currentWidth = wordWidth;
        }
        else if (currentWidth + 1 + wordWidth <= maxWidth)
        {
            currentLine += ' ';
            currentLine += word;
            currentWidth += 1 + wordWidth;
        }
        else
        {
            lines.push_back(std::move(currentLine));
            currentLine = std::string(word);
            currentWidth = wordWidth;
        }

        pos = (wordEnd == std::string_view::npos) ? text.size() : wordEnd;
    }

    if (!currentLine.empty())
        lines.push_back(std::move(currentLine));

    if (lines.empty())
        lines.emplace_back();

    return lines;
}

auto stripInlineMarkdown(std::string_view text) -> std::string
{
    auto result = std::string {};
    result.reserve(text.size());
    auto pos = std::size_t { 0 };

    while (pos < text.size())
    {
        if (text[pos] == '`')
        {
            if (auto const span = findInlineCodeEnd(text, pos))
            {
                result.append(span->content);
                pos = span->endPos;
                continue;
            }
        }

        if (pos + 1 < text.size() && text[pos] == '*' && text[pos + 1] == '*')
        {
            auto const endBold = text.find("**", pos + 2);
            if (endBold != std::string_view::npos)
            {
                result.append(text.substr(pos + 2, endBold - pos - 2));
                pos = endBold + 2;
                continue;
            }
        }

        if (text[pos] == '*')
        {
            auto const endItalic = text.find('*', pos + 1);
            if (endItalic != std::string_view::npos)
            {
                result.append(text.substr(pos + 1, endItalic - pos - 1));
                pos = endItalic + 1;
                continue;
            }
        }

        if (pos + 1 < text.size() && text[pos] == '_' && text[pos + 1] == '_')
        {
            auto const endBold = text.find("__", pos + 2);
            if (endBold != std::string_view::npos)
            {
                result.append(text.substr(pos + 2, endBold - pos - 2));
                pos = endBold + 2;
                continue;
            }
        }

        if (text[pos] == '[')
        {
            auto const endBracket = text.find(']', pos + 1);
            if (endBracket != std::string_view::npos && endBracket + 1 < text.size()
                && text[endBracket + 1] == '(')
            {
                auto const endParen = text.find(')', endBracket + 2);
                if (endParen != std::string_view::npos)
                {
                    result.append(text.substr(pos + 1, endBracket - pos - 1));
                    pos = endParen + 1;
                    continue;
                }
            }
        }

        auto const nextSpecial = text.find_first_of("`*_[", pos + 1);
        auto const end = (nextSpecial != std::string_view::npos) ? nextSpecial : text.size();
        result.append(text.substr(pos, end - pos));
        pos = end;
    }

    return result;
}

auto inlineDisplayWidth(std::string_view text) -> int
{
    return displayWidth(stripInlineMarkdown(text));
}

auto truncateToDisplayWidth(std::string_view text, int maxWidth) -> std::string
{
    if (maxWidth <= 0)
        return {};

    auto result = std::string {};
    auto width = 0;
    auto segmenter = unicode::utf8_grapheme_segmenter(text);
    for (auto const& cluster: segmenter)
    {
        auto const clusterW = graphemeClusterWidth(cluster);
        if (width + clusterW > maxWidth)
        {
            if (maxWidth >= 1 && !result.empty())
            {
                while (width > maxWidth - 1 && !result.empty())
                {
                    result.pop_back();
                    width = displayWidth(result);
                }
                result += "\xe2\x80\xa6";
            }
            return result;
        }
        for (auto ch: cluster)
            result += static_cast<char>(ch);
        width += clusterW;
    }
    return result;
}

// NOLINTEND(readability-identifier-naming, readability-function-cognitive-complexity)

} // namespace tui
