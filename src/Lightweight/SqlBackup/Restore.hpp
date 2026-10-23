// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../SqlConnection.hpp"
#include "Common.hpp"
#include "SqlBackup.hpp"

#include <atomic>
#include <cstdint>
#include <deque>
#include <expected>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>

#if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wnullability-extension"
#endif
#include <zip.h>
#if defined(__clang__)
    #pragma clang diagnostic pop
#endif

namespace Lightweight::SqlBackup::detail
{

/// Data required to restore a single chunk to the database.
struct RestoreChunkInfo
{
    std::string tableName;
    std::string chunkPath;
    std::vector<uint8_t> content;
    TableInfo const* tableInfo {};
    std::optional<size_t> displayTotal;
    bool isEndOfStream {}; ///< True when queue is empty and no more data is available.
    /// True when the chunk belongs to a table that already failed: the worker counts it as failed
    /// without reading it.
    bool skipForFailedTable {};
};

/// Error information from FetchNextRestoreChunk.
struct FetchChunkError
{
    std::string tableName; // Empty if table could not be determined
    std::string message;
};

/// The tables a chunk of which failed to restore, shared by all restore workers.
struct FailedTables
{
    /// Records @p tableName as failed.
    /// @param tableName The table.
    void Add(std::string const& tableName)
    {
        auto const lock = std::scoped_lock(_mutex);
        _names.insert(tableName);
    }

    /// Tests whether @p tableName has failed.
    /// @param tableName The table.
    /// @return `true` once a chunk of the table failed.
    [[nodiscard]] bool Contains(std::string const& tableName) const
    {
        auto const lock = std::scoped_lock(_mutex);
        return _names.contains(tableName);
    }

    /// Returns a copy of the failed table names.
    /// @return The names of the tables a chunk of which failed.
    [[nodiscard]] std::set<std::string> Snapshot() const
    {
        auto const lock = std::scoped_lock(_mutex);
        return _names;
    }

  private:
    mutable std::mutex _mutex;
    std::set<std::string> _names;
};

/// The result of restoring one chunk.
enum class ChunkRestoreOutcome : std::uint8_t
{
    /// The chunk was restored.
    Restored,
    /// The chunk failed; only its table is affected, and the worker goes on with the other tables.
    TableFailed,
    /// The chunk failed in a way that left the worker's connection unusable: the worker stops.
    WorkerAborted,
};

/// Returns the schema a table is restored into: the one the archive records for it, else @p defaultSchema.
/// @param info The table.
/// @param defaultSchema The schema the restore targets.
[[nodiscard]] inline std::string TargetSchema(TableInfo const& info, std::string const& defaultSchema)
{
    return info.schema.empty() ? defaultSchema : info.schema;
}

/// Returns a table's name in the database: the name the archive records, else its key in the table map.
/// @param key The table's key in the table map.
/// @param info The table.
[[nodiscard]] inline std::string TargetTableName(std::string const& key, TableInfo const& info)
{
    return info.name.empty() ? key : info.name;
}

/// Context for restore operations, shared between restore workers.
struct RestoreContext
{
    SqlConnectionString connectionString;
    std::string schema;
    std::map<std::string, TableInfo> const& tableMap;
    /// The archive folder of each table's chunks (see @ref ChunkDirectory) -> the table's key in @ref tableMap.
    std::map<std::string, std::string> const& chunkTables;
    std::deque<ZipEntryInfo>& dataQueue;
    zip_t* zip;
    std::mutex& queueMutex;
    std::mutex& fileMutex;
    ProgressManager& progress;
    std::map<std::string, std::shared_ptr<std::atomic<size_t>>> tableProgress;
    std::map<std::string, std::shared_ptr<std::atomic<size_t>>> chunksProcessed; // Per-table processed chunk count
    std::map<std::string, size_t> totalChunks;                                   // Per-table total chunk count
    std::map<std::string, std::string> const* checksums; // entryName -> expected SHA-256 hash (optional)
    RetrySettings const& retrySettings;
    RestoreSettings restoreSettings;
    /// Tables whose remaining chunks are skipped because one of their chunks failed.
    FailedTables& failedTables;
};

/// Increments the chunk counter and reports completion status.
///
/// @param ctx The restore context.
/// @param tableName The name of the table being restored.
/// @param success Whether the chunk was restored successfully.
void IncrementChunkCounter(RestoreContext& ctx, std::string const& tableName, bool success);

/// A table whose restore did not deliver every chunk.
struct IncompleteTable
{
    /// The table's key in @ref RestoreContext::tableMap.
    std::string tableName;
    /// How many of the table's chunks were attempted.
    size_t chunksProcessed {};
    /// How many chunks the archive holds for the table.
    size_t totalChunks {};
};

/// Returns the tables that did not receive all of their data.
///
/// A table is incomplete when one of its chunks failed, or when a worker that stopped never
/// attempted its last chunk. Call it after all restore workers have finished.
///
/// @param ctx The restore context.
/// @return One entry per incomplete table, ordered by table name.
[[nodiscard]] LIGHTWEIGHT_API std::vector<IncompleteTable> FindIncompleteTables(RestoreContext const& ctx);

/// Fetches the next chunk from the restore queue.
///
/// Handles dequeuing from the work queue, reading zip entry content,
/// path parsing to extract table name, and checksum verification.
///
/// @param ctx The restore context.
/// @return The chunk info on success (with isEndOfStream=true when queue is empty), or error details on failure.
std::expected<RestoreChunkInfo, FetchChunkError> FetchNextRestoreChunk(RestoreContext& ctx);

/// Restores chunk data to the database with retry logic.
///
/// Handles MSSQL identity insert handling, batch insertion with transaction management,
/// SQLite intermediate commits, retry logic for transient errors, and progress tracking.
///
/// @param ctx The restore context.
/// @param workerConn The database connection.
/// @param chunk The chunk data to restore.
/// @param batchCapacity The batch size for bulk inserts.
/// @return Whether the chunk was restored, failed only its table, or failed the worker.
ChunkRestoreOutcome RestoreChunkData(RestoreContext& ctx,
                                     SqlConnection& workerConn,
                                     RestoreChunkInfo const& chunk,
                                     size_t batchCapacity);

/// Worker function that processes chunks from the restore queue.
///
/// This function processes restore chunks from the shared queue, using the helper functions
/// FetchNextRestoreChunk() for I/O operations and RestoreChunkData() for database operations.
/// Once a chunk of a table fails, the remaining chunks of that table are skipped, but the worker
/// keeps draining the queue so chunks belonging to other tables are not abandoned. Only a failure
/// that leaves the worker's connection unusable stops it; the chunks it leaves queued are reported
/// after all workers have finished.
///
/// @param ctx The restore context containing queue, progress tracking, and settings.
/// @param workerConn The database connection for this worker.
void RestoreWorker(RestoreContext ctx, SqlConnection& workerConn);

/// Restores indexes for all tables after data has been restored.
///
/// This function creates indexes that were backed up in the metadata.
/// It is called after FK constraints are restored to ensure indexes are created
/// on the final table structure.
///
/// @param connectionString The connection string to use.
/// @param schema The schema name.
/// @param tableMap Map of table names to their metadata including indexes.
/// @param progress Progress manager for reporting status.
LIGHTWEIGHT_API void RestoreIndexes(SqlConnectionString const& connectionString,
                                    std::string const& schema,
                                    std::map<std::string, TableInfo> const& tableMap,
                                    ProgressManager& progress);

/// Applies foreign key constraints to all tables after data has been restored.
///
/// This function is called after data restoration to add FK constraints that
/// were not included in the initial CREATE TABLE statements.
///
/// @param connectionString The connection string to use.
/// @param schema The schema name.
/// @param tableMap Map of table names to their metadata including foreign keys.
/// @param progress Progress manager for reporting status.
LIGHTWEIGHT_API void ApplyDatabaseConstraints(SqlConnectionString const& connectionString,
                                              std::string const& schema,
                                              std::map<std::string, TableInfo> const& tableMap,
                                              ProgressManager& progress);

/// Recreates the database schema by dropping and creating tables from the backup metadata.
///
/// This function creates tables in dependency order for SQLite (to satisfy FK constraints on CREATE),
/// or alphabetically for other databases (where FKs are added via ALTER TABLE later). The schemas
/// that a multi-schema archive records are created first unless they exist.
///
/// @param connectionString The connection string for the target database.
/// @param schema The database schema name to create tables in.
/// @param tableMap Map of table names to their metadata from the backup.
/// @param progress Progress manager for reporting errors and status updates.
/// @return Set of table names that were successfully created. Tables that fail to create
///         (e.g., due to type incompatibilities) are excluded from the returned set,
///         allowing the caller to skip data restoration for those tables.
LIGHTWEIGHT_API std::set<std::string> RecreateDatabaseSchema(SqlConnectionString const& connectionString,
                                                             std::string const& schema,
                                                             std::map<std::string, TableInfo> const& tableMap,
                                                             ProgressManager& progress);

} // namespace Lightweight::SqlBackup::detail
