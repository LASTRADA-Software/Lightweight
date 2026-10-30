// SPDX-License-Identifier: Apache-2.0

#include "RestoreReportFormat.hpp"

#include <format>
#include <ranges>

namespace Lightweight::Tools
{

namespace
{
    /// @param values The values.
    /// @return The values joined with ", ".
    std::string JoinKeys(std::vector<std::string> const& values)
    {
        auto joined = std::string {};
        for (auto const& value: values)
            joined += (joined.empty() ? "" : ", ") + value;
        return joined;
    }
} // namespace

std::vector<std::string> DescribeTableRestoreFailures(SqlBackup::TableRestoreReport const& report)
{
    auto lines = std::vector<std::string> {};

    for (auto const& range: report.failedRowRanges | std::views::take(MaxListedRowRanges))
        lines.push_back(range.firstRow == range.lastRow
                            ? std::format("{} row {}", range.chunkPath, range.firstRow)
                            : std::format("{} rows {}–{}", range.chunkPath, range.firstRow, range.lastRow));
    if (report.failedRowRanges.size() > MaxListedRowRanges)
        lines.push_back(
            "... and "
            + SqlBackup::CountOf(report.failedRowRanges.size() - MaxListedRowRanges, "more row range", "more row ranges"));
    if (report.rowsBeyondRangeCap > 0)
        lines.push_back(
            "... and "
            + SqlBackup::CountOf(report.rowsBeyondRangeCap, "more failed row not listed", "more failed rows not listed"));

    for (auto const& reason: report.reasons)
    {
        auto line = std::format(
            "{} x {}{}{}", reason.occurrences, reason.sqlState, reason.sqlState.empty() ? "" : " ", reason.message);
        if (!reason.sampleKeys.empty())
            line += std::format(" (e.g. key {})", JoinKeys(reason.sampleKeys));
        lines.push_back(std::move(line));
    }
    if (report.occurrencesBeyondReasonCap > 0)
        lines.push_back("... and "
                        + SqlBackup::CountOf(report.occurrencesBeyondReasonCap,
                                             "more failure with other reasons",
                                             "more failures with other reasons"));

    for (auto const& column: report.valueFailures)
        lines.push_back(std::format("column {}: {} set to NULL ({})",
                                    column.column,
                                    SqlBackup::CountOf(column.cellsSetToNull, "cell", "cells"),
                                    column.sampleReason));

    if (report.tableFailure)
        lines.push_back(std::format("table failed: {}", *report.tableFailure));
    if (report.unreadableChunks > 0)
        lines.push_back(std::format("{} unreadable", SqlBackup::CountOf(report.unreadableChunks, "chunk", "chunks")));
    return lines;
}

std::vector<std::string> DescribeRestoreReport(SqlBackup::RestoreReport const& report)
{
    if (!report.HasFailures())
        return {};

    auto lines = std::vector<std::string> { "Restore failures:" };
    for (auto const& table: report.tables)
    {
        lines.push_back(std::format("  {}: {}", table.tableName, table.FailureSummary()));
        for (auto const& detail: DescribeTableRestoreFailures(table))
            lines.push_back(std::format("    {}", detail));
    }
    if (report.WorkerStopped())
        lines.emplace_back(
            "  A restore worker stopped early after losing its connection; the rest of its work was not done.");
    for (auto const& table: report.incompleteTables)
        lines.push_back(std::format("  incomplete: {}", table));
    if (report.unattributedChunkErrors > 0)
        lines.push_back(
            report.unattributedChunkErrors == 1
                ? std::string { "  1 unreadable archive entry belongs to no known table" }
                : std::format("  {} unreadable archive entries belong to no known table", report.unattributedChunkErrors));
    lines.push_back(std::format("Total: {} failed in {}, {} set to NULL",
                                SqlBackup::CountOf(report.TotalFailedRows(), "row", "rows"),
                                SqlBackup::CountOf(report.tables.size(), "table", "tables"),
                                SqlBackup::CountOf(report.NulledCellCount(), "cell", "cells")));
    return lines;
}

std::string JoinedReportText(SqlBackup::RestoreReport const& report)
{
    auto text = std::string {};
    for (auto const& line: DescribeRestoreReport(report))
        text += (text.empty() ? "" : "\n") + line;
    return text;
}

} // namespace Lightweight::Tools
