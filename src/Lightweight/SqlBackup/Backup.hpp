// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../SqlConnection.hpp"
#include "../SqlSchema.hpp"
#include "../ThreadSafeQueue.hpp"
#include "SqlBackup.hpp"

#include <map>
#include <mutex>
#include <string>

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

/// Processes a single table backup.
///
/// Reads all rows from the table and writes them to msgpack chunks in the ZIP archive.
/// Handles retry logic for transient errors with offset-based resumption.
///
/// @param ctx The backup context.
/// @param conn The database connection.
/// @param table The table schema information.
void ProcessTableBackup(BackupContext& ctx, SqlConnection& conn, SqlSchema::Table const& table);

/// Backup worker that processes tables from a thread-safe queue.
///
/// Workers block on the queue until a table is available or the queue is finished.
/// This allows workers to start immediately and begin processing tables as soon
/// as the schema producer pushes them.
///
/// Each worker receives a pre-created connection to avoid data races in the ODBC driver
/// during concurrent connection establishment.
///
/// @param tableQueue The thread-safe queue of tables to process.
/// @param ctx The backup context.
/// @param conn The database connection for this worker.
void BackupWorker(ThreadSafeQueue<SqlSchema::Table>& tableQueue, BackupContext ctx, SqlConnection& conn);

} // namespace Lightweight::SqlBackup::detail
