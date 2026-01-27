// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../SqlConnection.hpp"
#include "Common.hpp"
#include "SqlBackup.hpp"

#include <atomic>
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
    TableInfo const* tableInfo;
    std::optional<size_t> displayTotal;
};

/// Error information from FetchNextRestoreChunk.
struct FetchChunkError
{
    std::string tableName; // Empty if table could not be determined
    std::string message;
};

/// Context for restore operations, shared between restore workers.
struct RestoreContext
{
    SqlConnectionString connectionString;
    std::string schema;
    std::map<std::string, TableInfo> const& tableMap;
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
};

/// Increments the chunk counter and reports completion status.
///
/// @param ctx The restore context.
/// @param tableName The name of the table being restored.
/// @param success Whether the chunk was restored successfully.
void IncrementChunkCounter(RestoreContext& ctx, std::string const& tableName, bool success);

/// Fetches the next chunk from the restore queue.
///
/// Handles dequeuing from the work queue, reading zip entry content,
/// path parsing to extract table name, and checksum verification.
///
/// @param ctx The restore context.
/// @return The chunk info on success, std::nullopt if queue is empty, or error details on failure.
std::expected<std::optional<RestoreChunkInfo>, FetchChunkError> FetchNextRestoreChunk(RestoreContext& ctx);

/// Restores chunk data to the database with retry logic.
///
/// Handles MSSQL identity insert handling, batch insertion with transaction management,
/// SQLite intermediate commits, retry logic for transient errors, and progress tracking.
///
/// @param ctx The restore context.
/// @param workerConn The database connection.
/// @param chunk The chunk data to restore.
/// @param batchCapacity The batch size for bulk inserts.
/// @return true if chunk was restored successfully, false if errors occurred.
bool RestoreChunkData(RestoreContext& ctx, SqlConnection& workerConn, RestoreChunkInfo const& chunk, size_t batchCapacity);

/// Worker function that processes chunks from the restore queue.
///
/// This function processes restore chunks from the shared queue, using the helper functions
/// FetchNextRestoreChunk() for I/O operations and RestoreChunkData() for database operations.
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
void RestoreIndexes(SqlConnectionString const& connectionString,
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
void ApplyDatabaseConstraints(SqlConnectionString const& connectionString,
                              std::string const& schema,
                              std::map<std::string, TableInfo> const& tableMap,
                              ProgressManager& progress);

/// Recreates the database schema by dropping and creating tables from the backup metadata.
///
/// This function creates tables in dependency order for SQLite (to satisfy FK constraints on CREATE),
/// or alphabetically for other databases (where FKs are added via ALTER TABLE later).
///
/// @param connectionString The connection string for the target database.
/// @param schema The database schema name to create tables in.
/// @param tableMap Map of table names to their metadata from the backup.
/// @param progress Progress manager for reporting errors and status updates.
/// @return Set of table names that were successfully created. Tables that fail to create
///         (e.g., due to type incompatibilities) are excluded from the returned set,
///         allowing the caller to skip data restoration for those tables.
std::set<std::string> RecreateDatabaseSchema(SqlConnectionString const& connectionString,
                                             std::string const& schema,
                                             std::map<std::string, TableInfo> const& tableMap,
                                             ProgressManager& progress);

} // namespace Lightweight::SqlBackup::detail
