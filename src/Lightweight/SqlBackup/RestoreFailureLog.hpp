// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../Api.hpp"
#include "SqlBackup.hpp"

#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>

namespace Lightweight::SqlBackup::detail
{

/// Thread-safe, size-capped collector of restore failures.
///
/// Memory is bounded by the report caps (ranges, reasons) and by the number of tables and chunks,
/// never by the number of failures.
class LIGHTWEIGHT_API RestoreFailureLog
{
  public:
    /// Records one row that failed to restore.
    /// @param table Table name.
    /// @param chunkPath Archive path of the chunk holding the row.
    /// @param rowInChunk 1-based row number within the chunk.
    /// @param sqlState SQLSTATE of the failure.
    /// @param message Failure message; digit runs are collapsed when grouping reasons.
    /// @param sampleKey Identifying key of the row, kept for the first few occurrences of a reason.
    void RecordRowFailure(std::string_view table,
                          std::string_view chunkPath,
                          std::uint64_t rowInChunk,
                          std::string_view sqlState,
                          std::string_view message,
                          std::string sampleKey);

    /// Records a cell that was set to NULL because its value could not be converted.
    /// @param table Table name.
    /// @param column Column name.
    /// @param reason Why the value was dropped.
    void RecordNulledCell(std::string_view table, std::string_view column, std::string_view reason);

    /// Records that a whole table failed. Its failed row count is settled by SetFailedTableRowCount().
    /// @param table Table name.
    /// @param reason Failure reason (the first one wins).
    void RecordTableFailure(std::string_view table, std::string_view reason);

    /// Sets the failed row count of a table that failed as a whole to @p missingRows, the rows of the
    /// table that did not reach the target. That replaces the count of its individually failed rows,
    /// which are among them. Does nothing for a table without a table failure.
    /// @param table Table name.
    /// @param missingRows The table's rows that are not in the target.
    void SetFailedTableRowCount(std::string_view table, std::uint64_t missingRows);

    /// Records a chunk that could not be read.
    /// @param table Table the chunk belongs to; empty if unknown.
    void RecordUnreadableChunk(std::string_view table);

    /// @param table Table name.
    /// @return Snapshot of the table's report; an empty report if nothing failed.
    [[nodiscard]] TableRestoreReport TableSnapshot(std::string_view table) const;

    /// @return Snapshot of all failures; incompleteTables is left for the caller to set.
    [[nodiscard]] RestoreReport Snapshot() const;

  private:
    struct TableState
    {
        TableRestoreReport report;
        /// Index into report.failedRowRanges of each chunk's most recent range.
        std::map<std::string, std::size_t, std::less<>> lastRangeOfChunk;
        std::map<std::pair<std::string, std::string>, std::size_t> reasonIndex;
        std::map<std::string, std::size_t, std::less<>> columnIndex;
    };

    mutable std::mutex _mutex;
    std::map<std::string, TableState, std::less<>> _tables;
    std::uint64_t _unattributedChunkErrors {};

    TableState& StateFor(std::string_view table);
};

} // namespace Lightweight::SqlBackup::detail
