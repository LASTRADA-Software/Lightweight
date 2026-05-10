// SPDX-License-Identifier: Apache-2.0
// Vendored from endo/src/tui/MarkdownTable.hpp.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace tui
{

// NOLINTBEGIN(readability-identifier-naming)
// Vendored upstream API names (camelCase) preserved verbatim — keeps re-syncs mechanical.

/// @brief Column alignment for table cells.
enum class TableAlignment : std::uint8_t
{
    Left,   ///< Left-aligned (default).
    Center, ///< Center-aligned.
    Right,  ///< Right-aligned.
};

/// @brief A fully parsed GFM-style pipe table.
struct ParsedTable
{
    std::vector<std::string> headers;
    std::vector<TableAlignment> alignments;
    std::vector<std::vector<std::string>> rows;
    std::size_t columnCount = 0;
};

[[nodiscard]] auto detectTableRow(std::string_view line) -> bool;
[[nodiscard]] auto detectTableSeparator(std::string_view line) -> bool;
[[nodiscard]] auto splitTableRow(std::string_view line) -> std::vector<std::string>;
[[nodiscard]] auto parseTableAlignments(std::string_view line) -> std::vector<TableAlignment>;
[[nodiscard]] auto computeColumnWidths(ParsedTable const& table) -> std::vector<int>;
[[nodiscard]] auto alignCell(std::string_view text, int width, TableAlignment alignment) -> std::string;
void constrainColumnWidths(std::vector<int>& widths, int maxTableWidth);
[[nodiscard]] auto wrapText(std::string_view text, int maxWidth) -> std::vector<std::string>;
[[nodiscard]] auto stripInlineMarkdown(std::string_view text) -> std::string;
[[nodiscard]] auto inlineDisplayWidth(std::string_view text) -> int;
[[nodiscard]] auto truncateToDisplayWidth(std::string_view text, int maxWidth) -> std::string;

// NOLINTEND(readability-identifier-naming)

} // namespace tui
