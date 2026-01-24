// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "../Api.hpp"
#include "../DataBinder/SqlRawColumn.hpp" // For SqlRawColumn
#include "../SqlSchema.hpp"
#include "../SqlServerType.hpp"
#include "SqlBackupFormats.hpp"

#include <functional>
#include <memory>
#include <vector>

namespace Lightweight::detail
{

struct BatchColumn;

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
    explicit BatchManager(BatchExecutor executor,
                          std::vector<SqlColumnDeclaration> const& colDecls,
                          size_t capacity,
                          SqlServerType serverType = SqlServerType::UNKNOWN);

    ~BatchManager();

    BatchManager(BatchManager const&) = delete;
    BatchManager& operator=(BatchManager const&) = delete;
    BatchManager(BatchManager&&) = default;
    BatchManager& operator=(BatchManager&&) = default;

    /// Push a row to the batch manager.
    void PushRow(std::vector<SqlBackup::BackupValue> const& row);

    /// Push a batch of data (from a chunk) to the batch manager.
    void PushBatch(SqlBackup::ColumnBatch const& batch);

    /// Flush the current batch to the database.
    void Flush();

    // private:
    std::unique_ptr<BatchColumn> CreateColumn(SqlColumnDeclaration const& col) const;

    size_t rowCount = 0;
    size_t capacity = 1000;
    BatchExecutor executor;
    std::vector<std::unique_ptr<BatchColumn>> columns;
    SqlServerType serverType = SqlServerType::UNKNOWN;
};

} // namespace Lightweight::detail
