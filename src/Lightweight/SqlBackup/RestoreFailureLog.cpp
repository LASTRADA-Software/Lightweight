// SPDX-License-Identifier: Apache-2.0

#include "RestoreFailureLog.hpp"

#include <algorithm>
#include <cctype>
#include <ranges>

namespace Lightweight::SqlBackup::detail
{

namespace
{
    /// Collapses every run of digits in @p message into a single '#'.
    std::string CollapseDigitRuns(std::string_view message)
    {
        std::string result;
        result.reserve(message.size());
        for (auto const c: message)
        {
            auto const isDigit = std::isdigit(static_cast<unsigned char>(c)) != 0;
            if (!isDigit || result.empty() || result.back() != '#')
                result.push_back(isDigit ? '#' : c);
        }
        return result;
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
    auto const reasonKey = std::pair { std::string(sqlState), CollapseDigitRuns(message) };

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

    // Reasons.
    auto const reasonIt = state.reasonIndex.find(reasonKey);
    if (reasonIt != state.reasonIndex.end())
    {
        auto& reason = report.reasons[reasonIt->second];
        ++reason.occurrences;
        if (reason.sampleKeys.size() < MaxSampleKeysPerReason)
            reason.sampleKeys.push_back(std::move(sampleKey));
    }
    else if (report.reasons.size() < MaxReportedReasons)
    {
        state.reasonIndex.emplace(reasonKey, report.reasons.size());
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

void RestoreFailureLog::RecordTableFailure(std::string_view table, std::string_view reason, std::uint64_t rowCount)
{
    auto const lock = std::scoped_lock { _mutex };
    auto& report = StateFor(table).report;
    if (!report.tableFailure.has_value())
        report.tableFailure = std::string(reason);
    report.failedRowCount += rowCount;
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
        result.totalFailedRows += state.report.failedRowCount;
        result.tables.push_back(state.report);
    }
    return result;
}

} // namespace Lightweight::SqlBackup::detail
