// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "../Api.hpp"
#include "../DataBinder/SqlRawColumn.hpp" // For SqlRawColumn
#include "../SqlSchema.hpp"
#include "../SqlServerType.hpp"
#include "RowIsolation.hpp"
#include "SqlBackupFormats.hpp"

#include <cstddef>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Lightweight::detail
{

struct BatchColumn;

/// Executes and commits @p rowCount rows given as @p slice (one slice per column, each holding
/// exactly those rows), or rolls them back and returns the error. The rows of
/// BatchInsertError::reportedFailedRows are indices into the slice.
using SliceInserter =
    std::function<std::expected<void, BatchInsertError>(std::span<SqlRawColumn const> slice, std::size_t rowCount)>;

/// The callbacks through which a BatchManager inserts rows and reports what it could not insert.
/// All are optional except @c tryInsert.
struct BatchManagerHooks
{
    /// Inserts a contiguous range of the pending rows.
    SliceInserter tryInsert;

    /// Classifies a failed insert; when empty, every error is FailureDisposition::Propagate.
    FailureClassifier classify;

    /// Called for each row that is not inserted, in row order: @p rowInManager is 1-based and counts
    /// every row the manager was given (skipped rows included); @p sqlState is empty for a row
    /// excluded because of a value that does not convert; @p sampleKey names the row by its primary
    /// key columns (by all columns if it has none), at most 200 characters.
    std::function<void(std::size_t rowInManager, std::string_view sqlState, std::string_view message, std::string sampleKey)>
        onRowFailed;

    /// Called for each value that does not convert and was inserted as NULL into the nullable
    /// @p column, once its row is committed; never for a row that is not inserted.
    std::function<void(std::string_view column, std::string_view reason)> onCellNulled;

    /// Called with BatchManager::SettledRows() whenever it advances.
    std::function<void(std::size_t settledRows)> onSettled;
};

#if defined(_MSC_VER)
    #pragma warning(push)
    #pragma warning(disable : 4251) // STL types in DLL interface
#endif

/// A batch manager that manages multiple batch columns for a statement.
struct LIGHTWEIGHT_API BatchManager
{
    /// Create a new BatchManager that inserts and reports through @p hooks.
    ///
    /// @param hooks The callbacks that insert rows and receive what could not be inserted.
    /// @param colDecls The column declarations for the statement.
    /// @param capacity The capacity of the batch manager.
    /// @param serverType The server type for database-specific bindings.
    /// @param sourceServerType The DBMS the data was backed up from. When it pads CHAR(n) values and
    ///        @p serverType does not, the trailing pad of CHAR/NCHAR values is stripped.
    explicit BatchManager(BatchManagerHooks hooks,
                          std::vector<SqlColumnDeclaration> const& colDecls,
                          size_t capacity,
                          SqlServerType serverType = SqlServerType::UNKNOWN,
                          SqlServerType sourceServerType = SqlServerType::UNKNOWN);

    ~BatchManager();

    BatchManager(BatchManager const&) = delete;
    BatchManager& operator=(BatchManager const&) = delete;
    BatchManager(BatchManager&&) noexcept;
    BatchManager& operator=(BatchManager&&) noexcept;

    /// Push a row to the batch manager.
    ///
    /// A value that cannot be converted to its column's type becomes NULL in a nullable column
    /// (reported via BatchManagerHooks::onCellNulled); in a NOT NULL column it excludes the row,
    /// which Flush() reports via BatchManagerHooks::onRowFailed instead of inserting it.
    void PushRow(std::vector<SqlBackup::BackupValue> const& row);

    /// Push a batch of data (from a chunk) to the batch manager; values that do not convert are
    /// handled as by PushRow().
    void PushBatch(SqlBackup::ColumnBatch const& batch);

    /// Push a batch of data (from a chunk) to the batch manager, leaving out its first rows.
    ///
    /// @param batch The rows to push.
    /// @param skipRows The number of leading rows of @p batch not to insert, e.g. because an earlier
    ///        attempt already settled them. They count toward the row numbers and SettledRows().
    void PushBatch(SqlBackup::ColumnBatch const& batch, std::size_t skipRows);

    /// Flush the current batch to the database, isolating rows the database rejects.
    ///
    /// @throws SqlException for an insert error classified FailureDisposition::Propagate, and
    ///         TableRestoreFailure for one classified FailureDisposition::FailTable; the rows not yet
    ///         inserted are discarded either way.
    void Flush();

    /// The resume watermark: the length of the gap-free prefix of the rows given to this manager
    /// (skipped rows included) that are each inserted, excluded or reported failed.
    /// @return The number of leading rows that need no further attempt.
    [[nodiscard]] std::size_t SettledRows() const noexcept;

    // private:
    [[nodiscard]] std::unique_ptr<BatchColumn> CreateColumn(SqlColumnDeclaration const& col) const;

    /// Tells whether CHAR/NCHAR values lose their trailing pad: the source DBMS pads them and the
    /// target does not, so the pad would otherwise stay in the data and break comparisons.
    /// @return True if the pad is stripped; false if either DBMS is unknown.
    [[nodiscard]] bool TrimsFixedCharPadding() const;

    /// Clears every column of the rows not yet flushed.
    void DiscardPendingRows();

    /// Appends @p cellCount cells to column @p columnIndex via @p push, which is called with the
    /// first cell and the number of cells to append. A cell that does not convert is appended as
    /// NULL and recorded (see RecordValueFailure()), and @p push resumes after it.
    /// @param columnIndex The index of the column in `columns`.
    /// @param cellCount The number of cells to append.
    /// @param push Appends cells [first, first + count) of the pushed range to the column.
    void PushIntoColumn(size_t columnIndex,
                        size_t cellCount,
                        std::function<void(BatchColumn& column, size_t first, size_t count)> const& push);

    /// Handles the value of column @p columnIndex in pending row @p pendingRow that did not convert.
    /// @param columnIndex The index of the column in `columns`.
    /// @param pendingRow The zero-based index of the row among the pending rows.
    /// @param reason Why the value does not convert.
    void RecordValueFailure(size_t columnIndex, size_t pendingRow, std::string_view reason);

    /// Names pending row @p row for a failure report by its primary key (all columns without one).
    /// @param rawColumns The raw columns of the pending rows.
    /// @param row The zero-based index of the row among the pending rows.
    /// @return The cells, joined by ", ", cut to 200 characters.
    [[nodiscard]] std::string DescribeRowKey(std::vector<SqlRawColumn> const& rawColumns, size_t row) const;

    /// Settles @p count rows that directly follow the pending ones without inserting them.
    /// @param count The number of rows to skip.
    void SkipRows(size_t count);

    /// Advances the watermark to @p settledEnd, given that the rows [@p firstRow, @p settledEnd) are
    /// settled, provided every row before @p firstRow is settled already; reports it via
    /// BatchManagerHooks::onSettled.
    /// @param firstRow The first of the newly settled rows, counted as SettledRows() counts.
    /// @param settledEnd The row after the last of them.
    void ExtendWatermark(size_t firstRow, size_t settledEnd);

    size_t rowCount = 0;
    /// Rows given to the manager before the pending ones (flushed, failed or skipped).
    size_t firstPendingRow = 0;
    /// See SettledRows().
    size_t settledRows = 0;
    size_t capacity = 1000;
    BatchManagerHooks hooks;
    /// Per column: a value that does not convert excludes its row instead of becoming NULL.
    std::vector<bool> columnRequired;
    /// The indices of the primary key columns, which name a failed row.
    std::vector<size_t> keyColumns;
    /// Per pending row: excluded because a NOT NULL value does not convert. Sized on demand.
    std::vector<bool> excludedRows;
    /// Per pending row, parallel to `excludedRows`: why the row is excluded.
    std::vector<std::optional<std::string>> exclusionReasons;

    /// A value set to NULL in a nullable column of a pending row.
    struct NulledCell
    {
        /// The zero-based index of the row among the pending rows.
        size_t pendingRow {};
        /// The index of the column in `columns`.
        size_t columnIndex {};
        /// Why the value does not convert.
        std::string reason;
    };

    /// The pending rows' nulled cells, reported via BatchManagerHooks::onCellNulled only once their
    /// row is inserted.
    std::vector<NulledCell> nulledCells;
    std::vector<std::unique_ptr<BatchColumn>> columns;
    SqlServerType serverType = SqlServerType::UNKNOWN;
    /// The DBMS the backup was taken from; UNKNOWN when the archive does not name a known one.
    SqlServerType sourceServerType = SqlServerType::UNKNOWN;
    /// The column names, parallel to `columns`, for failure reports.
    std::vector<std::string> columnNames;
};

#if defined(_MSC_VER)
    #pragma warning(pop)
#endif

} // namespace Lightweight::detail
