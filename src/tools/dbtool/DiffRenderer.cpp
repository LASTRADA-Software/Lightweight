// SPDX-License-Identifier: Apache-2.0

#include "DiffRenderer.hpp"

#include <Lightweight/SqlColumnTypeDefinitions.hpp>
#include <Lightweight/tui/MarkdownTable.hpp>
#include <Lightweight/tui/SgrBuilder.hpp>
#include <Lightweight/tui/Style.hpp>

#include <charconv>
#include <cstdlib>
#include <format>
#include <functional>
#include <ostream>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace Lightweight::Tools
{

namespace
{

    using SqlSchema::ColumnDiff;
    using SqlSchema::DiffKind;
    using SqlSchema::RowDiff;
    using SqlSchema::SchemaDiff;
    using SqlSchema::TableDataDiff;
    using SqlSchema::TableDiff;

    /// Resolves the effective max table width: the user value, or `$COLUMNS`, or 120.
    [[nodiscard]] int ResolveMaxWidth(int requested) noexcept
    {
        if (requested > 0)
            return requested;
        // NOLINTNEXTLINE(concurrency-mt-unsafe) — startup, single-threaded.
        if (auto const* cols = std::getenv("COLUMNS"))
        {
            auto const view = std::string_view { cols };
            auto value = 0;
            auto const result = std::from_chars(view.data(), view.data() + view.size(), value);
            if (result.ec == std::errc {} && value > 20)
                return value;
        }
        return 120;
    }

    /// Color palette for diff entries. RGB picked to look reasonable on both light and
    /// dark backgrounds without depending on a specific 256-color scheme.
    struct Palette
    {
        tui::Style added;   ///< OnlyInB rows / cells (additions).
        tui::Style removed; ///< OnlyInA rows / cells (removals).
        tui::Style changed; ///< Changed rows / cells.
        tui::Style header;  ///< Section headers and table headers.
        tui::Style dim;     ///< Box-drawing borders.
    };

    [[nodiscard]] Palette MakePalette()
    {
        auto const green = tui::RgbColor { .r = 64, .g = 200, .b = 100 };
        auto const red = tui::RgbColor { .r = 230, .g = 80, .b = 80 };
        auto const yellow = tui::RgbColor { .r = 220, .g = 180, .b = 60 };
        auto const cyan = tui::RgbColor { .r = 90, .g = 180, .b = 220 };
        auto added = tui::Style {};
        added.fg = green;
        auto removed = tui::Style {};
        removed.fg = red;
        auto changed = tui::Style {};
        changed.fg = yellow;
        auto header = tui::Style {};
        header.fg = cyan;
        header.bold = true;
        auto dim = tui::Style {};
        dim.dim = true;
        return Palette {
            .added = added,
            .removed = removed,
            .changed = changed,
            .header = header,
            .dim = dim,
        };
    }

    [[nodiscard]] std::string DiffKindLabel(DiffKind k)
    {
        switch (k)
        {
            case DiffKind::OnlyInA:
                return "only in A";
            case DiffKind::OnlyInB:
                return "only in B";
            case DiffKind::Changed:
                return "changed";
        }
        return "?";
    }

    [[nodiscard]] tui::Style const& StyleFor(DiffKind k, Palette const& p)
    {
        switch (k)
        {
            case DiffKind::OnlyInA:
                return p.removed;
            case DiffKind::OnlyInB:
                return p.added;
            case DiffKind::Changed:
                return p.changed;
        }
        return p.changed;
    }

    /// Wraps @p text in the SGR sequence for @p style, followed by reset. Returns the
    /// raw text when colors are disabled.
    [[nodiscard]] std::string Colorize(std::string_view text, tui::Style const& style, bool useColor)
    {
        if (!useColor)
            return std::string { text };
        auto const open = tui::buildSgrSequence(style);
        if (open.empty())
            return std::string { text };
        return std::format("{}{}{}", open, text, tui::sgrReset());
    }

    /// Renders a parsed table to @p out with bordered, colored cells.
    /// @p cellStyle returns the style for cell `(row, col)`; nullptr cells use default.
    void EmitBorderedTable(std::ostream& out,
                           tui::ParsedTable const& table,
                           Palette const& palette,
                           bool useColor,
                           int maxWidth,
                           std::function<tui::Style const*(std::size_t, std::size_t)> cellStyle)
    {
        auto widths = tui::computeColumnWidths(table);
        tui::constrainColumnWidths(widths, maxWidth);

        auto const drawSeparator = [&]() {
            auto sep = std::string { "+" };
            for (auto const w: widths)
                sep += std::string(static_cast<std::size_t>(w + 2), '-') + "+";
            out << Colorize(sep, palette.dim, useColor) << '\n';
        };

        auto const drawRow = [&](std::vector<std::string> const& cells,
                                 std::function<tui::Style const*(std::size_t)> const& styleOfCell) {
            out << Colorize("|", palette.dim, useColor);
            for (auto const& [col, w]: std::views::enumerate(widths))
            {
                auto const colSize = static_cast<std::size_t>(col);
                auto const text =
                    std::cmp_less(col, cells.size()) ? tui::truncateToDisplayWidth(cells[colSize], w) : std::string {};
                auto const alignment =
                    std::cmp_less(col, table.alignments.size()) ? table.alignments[colSize] : tui::TableAlignment::Left;
                auto const padded = tui::alignCell(text, w, alignment);
                auto const* st = styleOfCell(colSize);
                out << ' ';
                out << (st ? Colorize(padded, *st, useColor) : padded);
                out << ' ';
                out << Colorize("|", palette.dim, useColor);
            }
            out << '\n';
        };

        drawSeparator();
        drawRow(table.headers, [&](std::size_t) { return &palette.header; });
        drawSeparator();
        for (auto const& [r, row]: std::views::enumerate(table.rows))
            drawRow(row, [&, r = r](std::size_t c) { return cellStyle(static_cast<std::size_t>(r), c); });
        drawSeparator();
    }

    void EmitSectionHeader(std::ostream& out, std::string_view title, Palette const& p, bool useColor)
    {
        out << '\n' << Colorize(title, p.header, useColor) << '\n';
    }

    /// Stringifies a canonical column-type variant for human-readable diff output.
    [[nodiscard]] std::string FormatCanonicalType(SqlColumnTypeDefinition const& type)
    {
        using namespace SqlColumnTypeDefinitions;
        return std::visit(
            [](auto const& t) -> std::string {
                using T = std::decay_t<decltype(t)>;
                if constexpr (std::is_same_v<T, Bigint>)
                    return "Bigint";
                else if constexpr (std::is_same_v<T, Binary>)
                    return std::format("Binary({})", t.size);
                else if constexpr (std::is_same_v<T, Bool>)
                    return "Bool";
                else if constexpr (std::is_same_v<T, Char>)
                    return std::format("Char({})", t.size);
                else if constexpr (std::is_same_v<T, Date>)
                    return "Date";
                else if constexpr (std::is_same_v<T, DateTime>)
                    return "DateTime";
                else if constexpr (std::is_same_v<T, Decimal>)
                    return std::format("Decimal({},{})", t.precision, t.scale);
                else if constexpr (std::is_same_v<T, Guid>)
                    return "Guid";
                else if constexpr (std::is_same_v<T, Integer>)
                    return "Integer";
                else if constexpr (std::is_same_v<T, NChar>)
                    return std::format("NChar({})", t.size);
                else if constexpr (std::is_same_v<T, NVarchar>)
                    return std::format("NVarchar({})", t.size);
                else if constexpr (std::is_same_v<T, Real>)
                    return std::format("Real({})", t.precision);
                else if constexpr (std::is_same_v<T, Smallint>)
                    return "Smallint";
                else if constexpr (std::is_same_v<T, Text>)
                    return std::format("Text({})", t.size);
                else if constexpr (std::is_same_v<T, Time>)
                    return "Time";
                else if constexpr (std::is_same_v<T, Timestamp>)
                    return "Timestamp";
                else if constexpr (std::is_same_v<T, Tinyint>)
                    return "Tinyint";
                else if constexpr (std::is_same_v<T, VarBinary>)
                    return std::format("VarBinary({})", t.size);
                else if constexpr (std::is_same_v<T, Varchar>)
                    return std::format("Varchar({})", t.size);
                else
                    static_assert(sizeof(T) == 0, "Unhandled SqlColumnTypeDefinition alternative");
            },
            type);
    }

    [[nodiscard]] std::string ColumnSummary(SqlSchema::Column const& c)
    {
        return std::format("{} [{}]", c.dialectDependantTypeString, FormatCanonicalType(c.type));
    }

    /// Builds a one-line description of a column diff (for the schema-diff "details" cell).
    ///
    /// When the canonical (logical) type differs, the description includes both the
    /// dialect type string and the canonical variant so the reason is visible.
    [[nodiscard]] std::string DescribeColumnDiff(ColumnDiff const& cd)
    {
        switch (cd.kind)
        {
            case DiffKind::OnlyInA:
                return cd.a ? std::format("only in A: {}", ColumnSummary(*cd.a)) : "only in A";
            case DiffKind::OnlyInB:
                return cd.b ? std::format("only in B: {}", ColumnSummary(*cd.b)) : "only in B";
            case DiffKind::Changed: {
                auto details = std::string { "changed: " };
                for (auto const& [i, f]: std::views::enumerate(cd.changedFields))
                {
                    if (i != 0)
                        details += ", ";
                    details += f;
                }
                if (cd.a && cd.b)
                    details += std::format(" ({} → {})", ColumnSummary(*cd.a), ColumnSummary(*cd.b));
                return details;
            }
        }
        return {};
    }

    /// Builds a single-line "details" cell for a TableDiff at the table level.
    [[nodiscard]] std::string DescribeTableDiff(TableDiff const& td)
    {
        if (td.kind != DiffKind::Changed)
            return "";
        auto parts = std::vector<std::string> {};
        if (!td.columns.empty())
            parts.emplace_back(std::format("{} column(s)", td.columns.size()));
        if (!td.primaryKeyDiffs.empty())
            parts.emplace_back("PK changed");
        if (!td.foreignKeyDiffs.empty())
            parts.emplace_back(std::format("{} FK", td.foreignKeyDiffs.size()));
        if (!td.indexDiffs.empty())
            parts.emplace_back(std::format("{} index", td.indexDiffs.size()));
        auto out = std::string {};
        for (auto const& [i, p]: std::views::enumerate(parts))
        {
            if (i != 0)
                out += ", ";
            out += p;
        }
        return out;
    }

    void RenderSchemaDiff(std::ostream& out, SchemaDiff const& diff, Palette const& palette, bool useColor, int maxWidth)
    {
        EmitSectionHeader(out, "Schema diff", palette, useColor);

        if (diff.Empty())
        {
            out << Colorize("  (schemas match)", palette.dim, useColor) << '\n';
            return;
        }

        // Top-level table: one row per TableDiff plus inline column rows under "Changed".
        auto pt = tui::ParsedTable {};
        pt.headers = { "Table", "Status", "Details" };
        pt.alignments = { tui::TableAlignment::Left, tui::TableAlignment::Left, tui::TableAlignment::Left };
        pt.columnCount = 3;

        // Track which rows correspond to which DiffKind for cell coloring.
        auto rowKinds = std::vector<DiffKind> {};

        // Resolve a display label for the table that surfaces a schema label when one or both
        // sides have one, but doesn't crowd output when they're the same. Tables are paired by
        // name across engines, so `dbo`/`public`/`""` may meet on the same row.
        auto qualifyName = [](TableDiff const& td) -> std::string {
            auto const& sa = td.schemaA;
            auto const& sb = td.schemaB;
            if (!sa.empty() && !sb.empty() && sa != sb)
                return std::format("{}.{} | {}.{}", sa, td.name, sb, td.name);
            auto const& schema = !sa.empty() ? sa : sb;
            return schema.empty() ? td.name : std::format("{}.{}", schema, td.name);
        };

        for (auto const& td: diff.tables)
        {
            pt.rows.push_back({ qualifyName(td), DiffKindLabel(td.kind), DescribeTableDiff(td) });
            rowKinds.push_back(td.kind);

            for (auto const& cd: td.columns)
            {
                pt.rows.push_back({ std::format("  └ {}", cd.name), DiffKindLabel(cd.kind), DescribeColumnDiff(cd) });
                rowKinds.push_back(cd.kind);
            }
            for (auto const& pk: td.primaryKeyDiffs)
            {
                pt.rows.push_back({ std::string { "  └ " }, "changed", pk });
                rowKinds.push_back(DiffKind::Changed);
            }
            for (auto const& fk: td.foreignKeyDiffs)
            {
                pt.rows.push_back({ std::string { "  └ FK" }, "changed", fk });
                rowKinds.push_back(DiffKind::Changed);
            }
            for (auto const& idx: td.indexDiffs)
            {
                pt.rows.push_back({ std::string { "  └ index" }, "changed", idx });
                rowKinds.push_back(DiffKind::Changed);
            }
        }

        EmitBorderedTable(out, pt, palette, useColor, maxWidth, [&](std::size_t r, std::size_t /*c*/) -> tui::Style const* {
            return r < rowKinds.size() ? &StyleFor(rowKinds[r], palette) : nullptr;
        });
    }

    [[nodiscard]] std::string JoinPk(std::vector<std::string> const& pk)
    {
        auto out = std::string {};
        for (auto const& [i, v]: std::views::enumerate(pk))
        {
            if (i != 0)
                out += "/";
            out += v;
        }
        return out;
    }

    void RenderTableDataDiff(std::ostream& out, TableDataDiff const& d, Palette const& palette, bool useColor, int maxWidth)
    {
        EmitSectionHeader(out,
                          std::format("Data diff: {} (A: {} rows, B: {} rows{})",
                                      d.tableName,
                                      d.aRowCount,
                                      d.bRowCount,
                                      d.truncated ? ", truncated" : ""),
                          palette,
                          useColor);

        if (d.skipReason.has_value())
        {
            out << Colorize(std::format("  (skipped: {})", *d.skipReason), palette.dim, useColor) << '\n';
            return;
        }
        if (d.rows.empty())
        {
            out << Colorize("  (data matches)", palette.dim, useColor) << '\n';
            return;
        }

        auto pt = tui::ParsedTable {};
        pt.headers = { "PK", "Status", "Column", "A", "B" };
        pt.alignments = { tui::TableAlignment::Left,
                          tui::TableAlignment::Left,
                          tui::TableAlignment::Left,
                          tui::TableAlignment::Left,
                          tui::TableAlignment::Left };
        pt.columnCount = 5;

        auto rowKinds = std::vector<DiffKind> {};
        for (auto const& r: d.rows)
        {
            if (r.kind == DiffKind::Changed && !r.changedCells.empty())
            {
                for (auto const& [col, va, vb]: r.changedCells)
                {
                    pt.rows.push_back({ JoinPk(r.primaryKey), DiffKindLabel(r.kind), col, va, vb });
                    rowKinds.push_back(DiffKind::Changed);
                }
            }
            else
            {
                pt.rows.push_back({ JoinPk(r.primaryKey), DiffKindLabel(r.kind), "-", "-", "-" });
                rowKinds.push_back(r.kind);
            }
        }

        EmitBorderedTable(out, pt, palette, useColor, maxWidth, [&](std::size_t r, std::size_t /*c*/) -> tui::Style const* {
            return r < rowKinds.size() ? &StyleFor(rowKinds[r], palette) : nullptr;
        });
    }

} // namespace

void RenderDiff(std::ostream& out,
                SqlSchema::SchemaDiff const& schemaDiff,
                std::vector<TableDataDiff> const& dataDiffs,
                DiffRenderOptions const& options)
{
    auto const palette = MakePalette();
    auto const maxWidth = ResolveMaxWidth(options.maxWidth);

    RenderSchemaDiff(out, schemaDiff, palette, options.useColor, maxWidth);
    for (auto const& d: dataDiffs)
        RenderTableDataDiff(out, d, palette, options.useColor, maxWidth);
}

} // namespace Lightweight::Tools
