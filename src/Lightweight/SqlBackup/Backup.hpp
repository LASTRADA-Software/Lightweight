// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../SqlConnection.hpp"
#include "../SqlSchema.hpp"
#include "../SqlStatement.hpp"
#include "../ThreadSafeQueue.hpp"
#include "ChunkPlanner.hpp"
#include "SqlBackup.hpp"
#include "WorkerChunkArchive.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

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

/// Context for backup operations, shared between backup workers.
struct BackupContext
{
    zip_t* zip;
    std::mutex& zipMutex;
    ProgressManager& progress;
    SqlConnectionString const& connectionString;
    std::string const& schema;
    std::map<std::string, std::string>* checksums; // entryName -> SHA-256 hash
    std::mutex* checksumMutex;
    RetrySettings const& retrySettings;
    BackupSettings const& backupSettings;
};

/// Builds a SELECT query with ORDER BY for deterministic results.
///
/// This is critical for:
/// 1. MS SQL Server which requires ORDER BY when using OFFSET
/// 2. Deterministic results for resumption after transient errors
///
/// For MS SQL Server, DECIMAL columns are wrapped in CONVERT(VARCHAR, ...) to preserve
/// full precision, as the ODBC driver loses precision when reading DECIMAL as SQL_C_CHAR.
///
/// @param formatter The SQL query formatter for the database.
/// @param serverType The type of database server.
/// @param schema The schema name.
/// @param tableName The table name.
/// @param columns The table columns.
/// @param primaryKeys The primary key columns.
/// @param offset The row offset for pagination.
/// @return The SQL SELECT query string.
std::string BuildSelectQueryWithOffset(SqlQueryFormatter const& formatter,
                                       SqlServerType serverType,
                                       std::string_view schema,
                                       std::string const& tableName,
                                       std::vector<SqlSchema::Column> const& columns,
                                       std::vector<std::string> const& primaryKeys,
                                       size_t offset);

/// Queries the inclusive [MIN(pk), MAX(pk)] bounds of @p table's @p pkColumn.
/// @param stmt The statement to execute the scalar queries on.
/// @param table The table to inspect.
/// @param pkColumn The single numeric primary-key column.
/// @return The bounds, or std::nullopt if the table is empty.
[[nodiscard]] LIGHTWEIGHT_API std::optional<std::pair<int64_t, int64_t>> QueryPkBounds(SqlStatement& stmt,
                                                                                       SqlSchema::Table const& table,
                                                                                       std::string const& pkColumn);

/// Deletes stale window sub-chunk entries [firstStale, end) of @p tableName's window
/// @p windowIndex from the worker's chunk archive (and their checksums). Used after a per-window
/// retry produced fewer sub-chunks than an earlier attempt (live data drift), so restore never
/// reads stale rows: names still in the worker's current archive are deleted outright, names
/// already sealed are tombstoned so the finalize merge skips them.
/// @param archive The worker's chunk archive the window's sub-chunks were written to.
/// @param ctx The backup context (checksums map).
/// @param tableName The (unsanitized) table name.
/// @param windowIndex The plan-time window index.
/// @param firstStale First stale sub-chunk id (== sub-chunk count of the final attempt).
/// @param end One past the last sub-chunk id any attempt flushed.
LIGHTWEIGHT_API void DeleteStaleSubChunks(WorkerChunkArchive& archive,
                                          BackupContext& ctx,
                                          std::string const& tableName,
                                          uint32_t windowIndex,
                                          size_t firstStale,
                                          size_t end);

/// Processes a single chunk (a bounded row-range of a table) into compressed msgpack chunk
/// entries in the worker's chunk archive.
/// @param ctx The backup context.
/// @param conn The database connection for this worker (borrowed from the pool).
/// @param chunk The chunk (table + row window) to process.
/// @param archive The worker's private chunk archive.
void ProcessChunkBackup(BackupContext& ctx, SqlConnection& conn, detail::Chunk const& chunk, WorkerChunkArchive& archive);

/// Backup worker: pops chunks from the queue and processes them on its borrowed connection,
/// writing compressed chunk entries into its private archive (sealed before returning).
/// @param chunkQueue The thread-safe queue of chunks to process.
/// @param ctx The backup context.
/// @param conn The database connection for this worker.
/// @param archive The worker's private chunk archive.
void ChunkWorker(ThreadSafeQueue<detail::Chunk>& chunkQueue,
                 BackupContext ctx,
                 SqlConnection& conn,
                 WorkerChunkArchive& archive);

} // namespace Lightweight::SqlBackup::detail
