// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "../Api.hpp"
#include "../DataBinder/SqlRawColumn.hpp" // For SqlRawColumn
#include "../SqlSchema.hpp"
#include "../SqlServerType.hpp"
#include "SqlBackupFormats.hpp"

#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace Lightweight::detail
{

struct BatchColumn;

/// Thrown by BatchManager::PushRow() and BatchManager::PushBatch() for a non-NULL backup value that
/// cannot be converted to its column's type. The message names the table, the column, the row
/// (1-based, counted from the BatchManager's first row) and the value.
struct BackupValueConversionError: std::runtime_error
{
    using std::runtime_error::runtime_error;
};

#if defined(_MSC_VER)
    #pragma warning(push)
    #pragma warning(disable : 4251) // STL types in DLL interface
#endif

/// A batch manager that manages multiple batch columns for a statement.
struct LIGHTWEIGHT_API BatchManager
{
    using BatchExecutor = std::function<void(std::vector<SqlRawColumn> const&, size_t)>;

    /// Create a new BatchManager for the given statement and column declarations.
    ///
    /// @param executor The callback to execute the batch.
    /// @param colDecls The column declarations for the statement.
    /// @param capacity The capacity of the batch manager.
    /// @param serverType The server type for database-specific bindings.
    /// @param tableName The table being restored, named in the error for a value that does not convert.
    explicit BatchManager(BatchExecutor executor,
                          std::vector<SqlColumnDeclaration> const& colDecls,
                          size_t capacity,
                          SqlServerType serverType = SqlServerType::UNKNOWN,
                          std::string tableName = {});

    ~BatchManager();

    BatchManager(BatchManager const&) = delete;
    BatchManager& operator=(BatchManager const&) = delete;
    BatchManager(BatchManager&&) noexcept;
    BatchManager& operator=(BatchManager&&) noexcept;

    /// Push a row to the batch manager.
    ///
    /// @throws BackupValueConversionError when a non-NULL value cannot be converted to its column's
    ///         type. The rows not yet flushed are discarded.
    void PushRow(std::vector<SqlBackup::BackupValue> const& row);

    /// Push a batch of data (from a chunk) to the batch manager.
    ///
    /// @throws BackupValueConversionError when a non-NULL value cannot be converted to its column's
    ///         type. The rows not yet flushed are discarded.
    void PushBatch(SqlBackup::ColumnBatch const& batch);

    /// Flush the current batch to the database.
    void Flush();

    // private:
    [[nodiscard]] std::unique_ptr<BatchColumn> CreateColumn(SqlColumnDeclaration const& col) const;

    /// Clears every column of the rows not yet flushed.
    void DiscardPendingRows();

    /// Runs @p push on column @p columnIndex, turning a value it cannot convert into an error that
    /// names the table, the column and the value, after discarding the rows not yet flushed.
    /// @param columnIndex The index of the column in `columns`.
    /// @param push Appends values to the column.
    void PushIntoColumn(size_t columnIndex, std::function<void(BatchColumn&)> const& push);

    size_t rowCount = 0;
    /// Rows flushed so far, to locate a rejected row in error messages.
    size_t rowsFlushed = 0;
    size_t capacity = 1000;
    BatchExecutor executor;
    std::vector<std::unique_ptr<BatchColumn>> columns;
    SqlServerType serverType = SqlServerType::UNKNOWN;
    /// The table being restored, for error messages; may be empty.
    std::string tableName;
    /// The column names, parallel to `columns`, for error messages.
    std::vector<std::string> columnNames;
};

#if defined(_MSC_VER)
    #pragma warning(pop)
#endif

} // namespace Lightweight::detail
