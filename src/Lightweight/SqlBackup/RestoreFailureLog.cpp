// SPDX-License-Identifier: Apache-2.0

#include "RestoreFailureLog.hpp"

#include <algorithm>
#include <cctype>
#include <ranges>

namespace Lightweight::SqlBackup::detail
{

namespace
{
    /// Passes each character of @p message with every run of digits collapsed into a single '#' to
    /// @p emit, which returns false to stop.
    /// @return false if @p emit stopped.
    template <typename Emit>
    bool ForEachCollapsedChar(std::string_view message, Emit emit)
    {
        auto last = char { 0 };
        for (auto const c: message)
        {
            auto const isDigit = std::isdigit(static_cast<unsigned char>(c)) != 0;
            if (isDigit && last == '#')
                continue;
            last = isDigit ? '#' : c;
            if (!emit(last))
                return false;
        }
        return true;
    }

    /// Collapses every run of digits in @p message into a single '#'.
    std::string CollapseDigitRuns(std::string_view message)
    {
        std::string result;
        result.reserve(message.size());
        ForEachCollapsedChar(message, [&](char c) {
            result.push_back(c);
            return true;
        });
        return result;
    }

    /// Tests, without allocating, whether @p message collapses (CollapseDigitRuns()) to @p collapsed.
    bool CollapsesTo(std::string_view message, std::string_view collapsed)
    {
        auto matched = std::size_t { 0 };
        auto const matches = ForEachCollapsedChar(message, [&](char c) {
            if (matched == collapsed.size() || collapsed[matched] != c)
                return false;
            ++matched;
            return true;
        });
        return matches && matched == collapsed.size();
    }
} // namespace

RestoreFailureLog::TableState& RestoreFailureLog::StateFor(std::string_view table)
{
    auto it = _tables.find(table);
    if (it == _tables.end())
    {
        it = _tables.emplace(std::string(table), TableState {}).first;
        it->second.report.tableName = std::string(table);
    }
    return it->second;
}

void RestoreFailureLog::RecordRowFailure(std::string_view table,
                                         std::string_view chunkPath,
                                         std::uint64_t rowInChunk,
                                         std::string_view sqlState,
                                         std::string_view message,
                                         std::string sampleKey)
{
    auto const lock = std::scoped_lock { _mutex };
    auto& state = StateFor(table);
    auto& report = state.report;
    ++report.failedRowCount;

    // Row ranges: extend the chunk's most recent range if adjacent, else open a new one while capacity remains.
    auto const lastIt = state.lastRangeOfChunk.find(chunkPath);
    if (lastIt != state.lastRangeOfChunk.end() && report.failedRowRanges[lastIt->second].lastRow + 1 == rowInChunk)
        report.failedRowRanges[lastIt->second].lastRow = rowInChunk;
    else if (report.failedRowRanges.size() < MaxReportedRowRanges)
    {
        report.failedRowRanges.push_back(
            FailedRowRange { .chunkPath = std::string(chunkPath), .firstRow = rowInChunk, .lastRow = rowInChunk });
        state.lastRangeOfChunk.insert_or_assign(std::string(chunkPath), report.failedRowRanges.size() - 1);
    }
    else
        ++report.rowsBeyondRangeCap;

    // Reasons: matched against the few known ones in place, so that a row allocates no reason key
    // unless it opens a new reason.
    auto const reasonIndices = std::views::iota(std::size_t { 0 }, report.reasons.size());
    auto const known = std::ranges::find_if(reasonIndices, [&](std::size_t index) {
        return report.reasons[index].sqlState == sqlState && CollapsesTo(message, state.reasonKeys[index]);
    });
    if (known != reasonIndices.end())
    {
        auto& reason = report.reasons[*known];
        ++reason.occurrences;
        if (reason.sampleKeys.size() < MaxSampleKeysPerReason)
            reason.sampleKeys.push_back(std::move(sampleKey));
    }
    else if (report.reasons.size() < MaxReportedReasons)
    {
        state.reasonKeys.push_back(CollapseDigitRuns(message));
        report.reasons.push_back(FailureReason {
            .sqlState = std::string(sqlState),
            .message = std::string(message),
            .occurrences = 1,
            .sampleKeys = { std::move(sampleKey) },
        });
    }
    else
        ++report.occurrencesBeyondReasonCap;
}

void RestoreFailureLog::RecordNulledCell(std::string_view table, std::string_view column, std::string_view reason)
{
    auto const lock = std::scoped_lock { _mutex };
    auto& state = StateFor(table);
    auto const it = state.columnIndex.find(column);
    if (it != state.columnIndex.end())
    {
        ++state.report.valueFailures[it->second].cellsSetToNull;
        return;
    }
    state.columnIndex.emplace(std::string(column), state.report.valueFailures.size());
    state.report.valueFailures.push_back(
        ColumnValueFailures { .column = std::string(column), .cellsSetToNull = 1, .sampleReason = std::string(reason) });
}

void RestoreFailureLog::RecordTableFailure(std::string_view table, std::string_view reason)
{
    auto const lock = std::scoped_lock { _mutex };
    auto& report = StateFor(table).report;
    if (!report.tableFailure.has_value())
        report.tableFailure = std::string(reason);
}

void RestoreFailureLog::SetFailedTableRowCount(std::string_view table, std::uint64_t missingRows)
{
    auto const lock = std::scoped_lock { _mutex };
    auto const it = _tables.find(table);
    if (it == _tables.end() || !it->second.report.tableFailure.has_value())
        return;
    it->second.report.failedRowCount = missingRows;
}

void RestoreFailureLog::RecordUnreadableChunk(std::string_view table)
{
    auto const lock = std::scoped_lock { _mutex };
    if (table.empty())
        ++_unattributedChunkErrors;
    else
        ++StateFor(table).report.unreadableChunks;
}

TableRestoreReport RestoreFailureLog::TableSnapshot(std::string_view table) const
{
    auto const lock = std::scoped_lock { _mutex };
    auto const it = _tables.find(table);
    if (it == _tables.end())
    {
        TableRestoreReport empty;
        empty.tableName = std::string(table);
        return empty;
    }
    return it->second.report;
}

RestoreReport RestoreFailureLog::Snapshot() const
{
    auto const lock = std::scoped_lock { _mutex };
    RestoreReport result;
    result.unattributedChunkErrors = _unattributedChunkErrors;
    // std::map iterates in key order, so tables come out sorted by name.
    for (auto const& state: _tables | std::views::values)
    {
        if (!state.report.HasFailures())
            continue;
        result.tables.push_back(state.report);
    }
    return result;
}

} // namespace Lightweight::SqlBackup::detail
