// SPDX-License-Identifier: Apache-2.0

#include "RestoreReportFormat.hpp"

#include <algorithm>
#include <format>
#include <numeric>
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

std::uint64_t CountNulledCells(SqlBackup::TableRestoreReport const& report)
{
    return std::ranges::fold_left(
        report.valueFailures, std::uint64_t { 0 }, [](std::uint64_t sum, SqlBackup::ColumnValueFailures const& column) {
            return sum + column.cellsSetToNull;
        });
}

std::vector<std::string> DescribeTableRestoreFailures(SqlBackup::TableRestoreReport const& report)
{
    auto lines = std::vector<std::string> {};

    for (auto const& range: report.failedRowRanges | std::views::take(MaxListedRowRanges))
        lines.push_back(range.firstRow == range.lastRow
                            ? std::format("{} row {}", range.chunkPath, range.firstRow)
                            : std::format("{} rows {}–{}", range.chunkPath, range.firstRow, range.lastRow));
    if (report.failedRowRanges.size() > MaxListedRowRanges)
        lines.push_back(std::format("... and {} more row ranges", report.failedRowRanges.size() - MaxListedRowRanges));
    if (report.rowsBeyondRangeCap > 0)
        lines.push_back(std::format("... and {} more failed rows not listed", report.rowsBeyondRangeCap));

    for (auto const& reason: report.reasons)
    {
        auto line = std::format(
            "{} x {}{}{}", reason.occurrences, reason.sqlState, reason.sqlState.empty() ? "" : " ", reason.message);
        if (!reason.sampleKeys.empty())
            line += std::format(" (e.g. key {})", JoinKeys(reason.sampleKeys));
        lines.push_back(std::move(line));
    }
    if (report.occurrencesBeyondReasonCap > 0)
        lines.push_back(std::format("... and {} more failures with other reasons", report.occurrencesBeyondReasonCap));

    for (auto const& column: report.valueFailures)
        lines.push_back(
            std::format("column {}: {} cells set to NULL ({})", column.column, column.cellsSetToNull, column.sampleReason));

    if (report.tableFailure)
        lines.push_back(std::format("table failed: {}", *report.tableFailure));
    if (report.unreadableChunks > 0)
        lines.push_back(std::format("{} chunks unreadable", report.unreadableChunks));
    return lines;
}

std::string SummarizeTableRestoreFailures(SqlBackup::TableRestoreReport const& report)
{
    return std::format("{} rows failed, {} cells set to NULL", report.failedRowCount, CountNulledCells(report));
}

std::vector<std::string> DescribeRestoreReport(SqlBackup::RestoreReport const& report)
{
    if (!report.HasFailures())
        return {};

    auto lines = std::vector<std::string> { "Restore failures:" };
    auto nulledCells = std::uint64_t { 0 };
    for (auto const& table: report.tables)
    {
        lines.push_back(std::format("  {}: {}", table.tableName, SummarizeTableRestoreFailures(table)));
        for (auto const& detail: DescribeTableRestoreFailures(table))
            lines.push_back(std::format("    {}", detail));
        nulledCells += CountNulledCells(table);
    }
    if (report.workerStopped)
        lines.emplace_back(
            "  A restore worker stopped early after losing its connection; the rest of its work was not done.");
    for (auto const& table: report.incompleteTables)
        lines.push_back(std::format("  incomplete: {}", table));
    if (report.unattributedChunkErrors > 0)
        lines.push_back(
            std::format("  {} unreadable archive entries belong to no known table", report.unattributedChunkErrors));
    lines.push_back(std::format("Total: {} rows failed in {} tables, {} cells set to NULL",
                                report.totalFailedRows,
                                report.tables.size(),
                                nulledCells));
    return lines;
}

} // namespace Lightweight::Tools
