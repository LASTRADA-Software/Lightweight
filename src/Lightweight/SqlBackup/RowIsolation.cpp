// SPDX-License-Identifier: Apache-2.0

#include "RowIsolation.hpp"

#include <algorithm>
#include <format>
#include <optional>
#include <ranges>
#include <utility>

namespace Lightweight::detail
{

TableRestoreFailure::TableRestoreFailure(SqlErrorInfo error):
    std::runtime_error(std::format("{}", error)),
    error { std::move(error) }
{
}

namespace
{
    /// One pending unit of work. Pending items always cover the unsettled rows of the current run
    /// contiguously, the leftmost one on top of the stack.
    struct WorkItem
    {
        /// First row of the item.
        std::size_t firstRow {};
        /// Number of rows; always at least 1.
        std::size_t rowCount {};
        /// Set when the item is a single row the driver already reported as failed with this error.
        std::optional<SqlErrorInfo> knownFailure;
        /// For the right half of a bisected item: the error of that item. Set while the left half is
        /// pending; once the left half is inserted, the right half holds the failing row for sure.
        std::optional<SqlErrorInfo> bisectedFailure {};
        /// Whether the item is the left half of a bisected item, whose right half lies below it.
        bool leftHalf = false;
        /// Set on a right half once its left half is inserted: it fails, so it is split untried.
        bool failsForSure = false;

        [[nodiscard]] std::size_t EndRow() const noexcept
        {
            return firstRow + rowCount;
        }
    };

    /// Driver-reported failed rows that lie within [firstRow, endRow), sorted and deduplicated.
    std::vector<std::size_t> ReportedRowsWithin(std::vector<std::size_t> rows, std::size_t firstRow, std::size_t endRow)
    {
        std::erase_if(rows, [&](std::size_t row) { return row < firstRow || row >= endRow; });
        std::ranges::sort(rows);
        auto const duplicates = std::ranges::unique(rows);
        rows.erase(duplicates.begin(), duplicates.end());
        return rows;
    }

    /// Pushes the pieces a failed run is split into, so that the leftmost one is processed first.
    ///
    /// With driver-reported failed rows, the run splits into those rows (already known to fail) and
    /// the sub-runs between them. Without, it is bisected.
    void Split(WorkItem const& item, BatchInsertError const& failure, std::vector<WorkItem>& stack)
    {
        auto const reported = ReportedRowsWithin(failure.reportedFailedRows, item.firstRow, item.EndRow());
        if (reported.empty())
        {
            auto const leftCount = item.rowCount / 2;
            stack.push_back(WorkItem { .firstRow = item.firstRow + leftCount,
                                       .rowCount = item.rowCount - leftCount,
                                       .knownFailure = std::nullopt,
                                       .bisectedFailure = failure.error });
            stack.push_back(WorkItem {
                .firstRow = item.firstRow, .rowCount = leftCount, .knownFailure = std::nullopt, .leftHalf = true });
            return;
        }

        // Build left to right, then push in reverse so that the leftmost piece ends up on top.
        auto pieces = std::vector<WorkItem> {};
        auto next = item.firstRow;
        for (auto const row: reported)
        {
            if (row > next)
                pieces.push_back(WorkItem { .firstRow = next, .rowCount = row - next, .knownFailure = std::nullopt });
            pieces.push_back(WorkItem { .firstRow = row, .rowCount = 1, .knownFailure = failure.error });
            next = row + 1;
        }
        if (next < item.EndRow())
            pieces.push_back(WorkItem { .firstRow = next, .rowCount = item.EndRow() - next, .knownFailure = std::nullopt });
        stack.insert(stack.end(), pieces.rbegin(), pieces.rend());
    }

    /// Inserts the contiguous run [firstRow, endRow) of non-excluded rows, isolating failing rows.
    /// Settles the run's rows strictly in ascending order.
    void InsertRun(std::size_t firstRow,
                   std::size_t endRow,
                   RangeInserter const& tryInsert,
                   FailureClassifier const& classify,
                   RowFailureSink const& onRowFailed,
                   SettledSink const& onSettled)
    {
        auto stack = std::vector<WorkItem> {
            WorkItem { .firstRow = firstRow, .rowCount = endRow - firstRow, .knownFailure = std::nullopt },
        };
        while (!stack.empty())
        {
            auto const item = stack.back();
            stack.pop_back();

            if (item.knownFailure)
            {
                onRowFailed(item.firstRow, *item.knownFailure);
                onSettled(item.EndRow());
                continue;
            }

            // The rows of the bisected item that failed are all in this half: splitting it right away
            // saves an insert that fails. A single row is still tried, so that it is never reported
            // failed without a failure of its own.
            if (item.failsForSure && item.rowCount > 1)
            {
                Split(item, BatchInsertError { .error = *item.bisectedFailure, .reportedFailedRows = {} }, stack);
                continue;
            }

            auto result = tryInsert(item.firstRow, item.rowCount);
            if (result)
            {
                onSettled(item.EndRow());
                // Nothing was pushed since, so the right half is on top of the stack.
                if (item.leftHalf && !stack.empty() && stack.back().bisectedFailure)
                    stack.back().failsForSure = true;
                continue;
            }

            switch (classify(result.error().error))
            {
                case FailureDisposition::Propagate:
                    throw SqlException(std::move(result.error().error));
                case FailureDisposition::FailTable:
                    throw TableRestoreFailure(std::move(result.error().error));
                case FailureDisposition::IsolateRows:
                    break;
            }

            if (item.rowCount == 1)
            {
                onRowFailed(item.firstRow, result.error().error);
                onSettled(item.EndRow());
            }
            else
                Split(item, result.error(), stack);
        }
    }
} // namespace

void InsertIsolatingFailures(std::size_t rowCount,
                             std::vector<bool> const& excluded,
                             RangeInserter const& tryInsert,
                             FailureClassifier const& classify,
                             RowFailureSink const& onRowFailed,
                             ExcludedRowSink const& onExcludedRow,
                             SettledSink const& onSettled)
{
    auto const isExcluded = [&](std::size_t row) {
        return row < excluded.size() && excluded[row];
    };

    auto row = std::size_t { 0 };
    while (row < rowCount)
    {
        if (isExcluded(row))
        {
            onExcludedRow(row);
            ++row;
            onSettled(row);
            continue;
        }

        auto const rest = std::views::iota(row, rowCount);
        auto const nextExcluded = std::ranges::find_if(rest, isExcluded);
        auto const runEnd = nextExcluded == rest.end() ? rowCount : *nextExcluded;
        InsertRun(row, runEnd, tryInsert, classify, onRowFailed, onSettled);
        row = runEnd;
    }
}

} // namespace Lightweight::detail
