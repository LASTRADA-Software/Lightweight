// SPDX-License-Identifier: Apache-2.0

#include "../SqlQueryFormatter.hpp"
#include "../SqlStatement.hpp"
#include "../SqlTransaction.hpp"
#include "BatchManager.hpp"
#include "Common.hpp"
#include "MsgPackChunkFormats.hpp"
#include "Restore.hpp"
#include "Sha256.hpp"

#include <algorithm>
#include <format>
#include <ranges>
#include <thread>

using namespace std::string_literals;

namespace Lightweight::SqlBackup::detail
{

void IncrementChunkCounter(RestoreContext& ctx, std::string const& tableName, bool success)
{
    auto& chunkCounter = *ctx.chunksProcessed.at(tableName);
    size_t const processedChunks = chunkCounter.fetch_add(1) + 1;
    size_t const totalChunksForTable = ctx.totalChunks.at(tableName);

    if (processedChunks >= totalChunksForTable)
    {
        auto& rowCounter = *ctx.tableProgress.at(tableName);
        size_t const actualRowCount = rowCounter.load();
        ctx.progress.Update({ .state = success ? Progress::State::Finished : Progress::State::Error,
                              .tableName = tableName,
                              .currentRows = actualRowCount,
                              .totalRows = actualRowCount,
                              .message = success ? "Restore complete" : "Restore incomplete: errors occurred" });
    }
}

std::expected<RestoreChunkInfo, FetchChunkError> FetchNextRestoreChunk(RestoreContext& ctx)
{
    ZipEntryInfo entryInfo;
    {
        auto const lock = std::scoped_lock(ctx.queueMutex);
        if (ctx.dataQueue.empty())
            return RestoreChunkInfo { .tableName = {},
                                      .chunkPath = {},
                                      .content = {},
                                      .tableInfo = nullptr,
                                      .displayTotal = std::nullopt,
                                      .isEndOfStream = true }; // Signal worker to exit - queue empty
        entryInfo = ctx.dataQueue.front();
        ctx.dataQueue.pop_front();
    }

    std::vector<uint8_t> content;
    {
        auto const lock = std::scoped_lock(ctx.fileMutex);
        if (!entryInfo.valid)
            return std::unexpected(
                FetchChunkError { .tableName = "", .message = std::format("Invalid zip entry: {}", entryInfo.name) });
        content = ReadZipEntry<std::vector<uint8_t>>(ctx.zip, entryInfo.index, entryInfo.size);
    }

    // Parse path FIRST to get tableName for chunk-based completion tracking.
    // Path format: data/TABLE_NAME/chunk_ID.msgpack
    std::string const path = entryInfo.name;
    auto const firstSlash = path.find('/');
    if (firstSlash == std::string::npos)
        return std::unexpected(
            FetchChunkError { .tableName = "", .message = std::format("Malformed chunk path (no slash): {}", path) });

    auto const secondSlash = path.find('/', firstSlash + 1);
    if (secondSlash == std::string::npos)
        return std::unexpected(
            FetchChunkError { .tableName = "", .message = std::format("Malformed chunk path (no second slash): {}", path) });

    std::string const tableName = path.substr(firstSlash + 1, secondSlash - firstSlash - 1);
    if (!ctx.tableMap.contains(tableName))
        return std::unexpected(
            FetchChunkError { .tableName = tableName, .message = std::format("Unknown table in backup: {}", tableName) });

    // Verify checksum if available
    if (ctx.checksums)
    {
        auto const it = ctx.checksums->find(entryInfo.name);
        if (it != ctx.checksums->end())
        {
            std::string const actualHash = Sha256::Hash(content.data(), content.size());
            if (actualHash != it->second)
            {
                return std::unexpected(FetchChunkError {
                    .tableName = tableName,
                    .message = std::format(
                        "Checksum mismatch for {}: expected {}, got {}", entryInfo.name, it->second, actualHash) });
            }
        }
    }

    auto const& tableInfo = ctx.tableMap.at(tableName);
    std::optional<size_t> const displayTotal = tableInfo.rowCount > 0 ? std::optional { tableInfo.rowCount } : std::nullopt;

    return RestoreChunkInfo {
        .tableName = tableName,
        .chunkPath = path,
        .content = std::move(content),
        .tableInfo = &tableInfo,
        .displayTotal = displayTotal,
    };
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
bool RestoreChunkData(RestoreContext& ctx, SqlConnection& workerConn, RestoreChunkInfo const& chunk, size_t batchCapacity)
{
    auto const& tableInfo = *chunk.tableInfo;
    std::string const& tableName = chunk.tableName;
    std::string const& path = chunk.chunkPath;
    std::string const& fields = tableInfo.fields;

    size_t const currentTotal1 = ctx.tableProgress.at(tableName)->load();
    ctx.progress.Update({ .state = Progress::State::InProgress,
                          .tableName = tableName,
                          .currentRows = currentTotal1,
                          .totalRows = chunk.displayTotal,
                          .message = "Restoring chunk " + path });

    // Retry loop for chunk processing
    unsigned retryCount = 0;
    while (retryCount <= ctx.retrySettings.maxRetries)
    {
        try
        {
            // Use zero-copy reader directly from buffer (eliminates 2 memory copies)
            auto reader = CreateMsgPackChunkReaderFromBuffer(chunk.content);

            bool const isMsSql = workerConn.ServerType() == SqlServerType::MICROSOFT_SQL;
            bool const isSQLite = workerConn.ServerType() == SqlServerType::SQLITE;
            bool hasIdentity = false;
            std::string identityTable;

            if (isMsSql)
            {
                for (auto const& col: tableInfo.columns)
                {
                    if (col.primaryKey == SqlPrimaryKeyType::AUTO_INCREMENT)
                    {
                        hasIdentity = true;
                        break;
                    }
                }

                if (hasIdentity)
                {
                    identityTable = FormatTableName(ctx.schema, tableName);
                    SqlStatement { workerConn }.ExecuteDirect(std::format("SET IDENTITY_INSERT {} ON", identityTable));
                }
            }

            {
                auto const& formatter = workerConn.QueryFormatter();
                SqlStatement stmt { workerConn };
                auto const placeholders = std::ranges::fold_left(
                    std::views::iota(0UZ, tableInfo.columns.size()), std::string {}, [](std::string const& acc, size_t) {
                        return acc.empty() ? std::string("?") : acc + ", ?";
                    });
                stmt.Prepare(formatter.Insert(ctx.schema, tableName, fields, placeholders));

                ::Lightweight::detail::BatchManager batchManager(
                    [&](std::vector<SqlRawColumn> const& cols, size_t rows) { stmt.ExecuteBatch(cols, rows); },
                    tableInfo.columns,
                    batchCapacity,
                    workerConn.ServerType());

                // Use a transaction for the entire chunk to improve performance significantly.
                // We default to ROLLBACK on destruction to ensure atomic application of the chunk.
                SqlTransaction transaction(workerConn, SqlTransactionMode::ROLLBACK);

                // For SQLite: track rows since last commit for intermediate commits
                size_t rowsSinceCommit = 0;
                size_t const maxRowsPerCommit = ctx.restoreSettings.maxRowsPerCommit;

                ColumnBatch batch;
                while (reader->ReadBatch(batch))
                {
                    if (batch.rowCount == 0)
                        continue;

                    if (batch.columns.size() != tableInfo.columns.size())
                    {
                        throw std::runtime_error(
                            std::format("Column count mismatch in backup data: expected {} columns, got {}",
                                        tableInfo.columns.size(),
                                        batch.columns.size()));
                    }

                    batchManager.PushBatch(batch);
                    rowsSinceCommit += batch.rowCount;

                    // Intermediate commit for SQLite to reduce WAL memory accumulation
                    if (isSQLite && maxRowsPerCommit > 0 && rowsSinceCommit >= maxRowsPerCommit)
                    {
                        batchManager.Flush();
                        transaction.Commit();
                        transaction = SqlTransaction(workerConn, SqlTransactionMode::ROLLBACK);
                        rowsSinceCommit = 0;
                    }

                    auto& atomicCounter = *ctx.tableProgress.at(tableName);
                    size_t const previousTotal = atomicCounter.fetch_add(batch.rowCount);
                    size_t const currentTotal = previousTotal + batch.rowCount;

                    ctx.progress.Update({ .state = Progress::State::InProgress,
                                          .tableName = tableName,
                                          .currentRows = currentTotal,
                                          .totalRows = chunk.displayTotal,
                                          .message = "Restoring chunk " + path });

                    // Report items processed for ETA calculation
                    ctx.progress.OnItemsProcessed(batch.rowCount);
                }

                batchManager.Flush();
                transaction.Commit();
                stmt.CloseCursor();
            }

            if (hasIdentity)
            {
                try
                {
                    SqlStatement { workerConn }.ExecuteDirect(std::format("SET IDENTITY_INSERT {} OFF", identityTable));
                }
                catch (...) // NOLINT(bugprone-empty-catch)
                {
                    // Best-effort cleanup - failure doesn't affect restore correctness
                }
            }

            // Success - exit retry loop
            return true;
        }
        catch (SqlException const& e)
        {
            if (!IsTransientError(e.info()) || retryCount >= ctx.retrySettings.maxRetries)
            {
                ctx.progress.Update({ .state = Progress::State::Error,
                                      .tableName = tableName,
                                      .currentRows = 0,
                                      .totalRows = 0,
                                      .message = "Insert failed: "s + e.what() });
                return false; // Non-transient or max retries exceeded
            }

            ++retryCount;
            ctx.progress.Update(
                { .state = Progress::State::Warning,
                  .tableName = tableName,
                  .currentRows = 0,
                  .totalRows = tableInfo.rowCount,
                  .message =
                      std::format("Transient error, retry {}/{}: {}", retryCount, ctx.retrySettings.maxRetries, e.what()) });

            // Reconnect before retry
            workerConn.Close();
            std::this_thread::sleep_for(CalculateRetryDelay(retryCount - 1, ctx.retrySettings));

            if (!ConnectWithRetry(workerConn, ctx.connectionString, ctx.retrySettings, ctx.progress, tableName))
            {
                ctx.progress.Update({ .state = Progress::State::Error,
                                      .tableName = tableName,
                                      .currentRows = 0,
                                      .totalRows = 0,
                                      .message = "Failed to reconnect after transient error" });
                return false;
            }

            // Re-apply SQLite optimizations after reconnect
            if (workerConn.ServerType() == SqlServerType::SQLITE)
            {
                SqlStatement { workerConn }.ExecuteDirect("PRAGMA synchronous = OFF");
                SqlStatement { workerConn }.ExecuteDirect("PRAGMA journal_mode = WAL");
                SqlStatement { workerConn }.ExecuteDirect("PRAGMA foreign_keys = OFF");
                if (ctx.restoreSettings.cacheSizeKB > 0)
                {
                    SqlStatement { workerConn }.ExecuteDirect(
                        std::format("PRAGMA cache_size = -{}", ctx.restoreSettings.cacheSizeKB));
                }
            }

            // Loop will re-process the same chunk
        }
        catch (std::exception const& e)
        {
            ctx.progress.Update({ .state = Progress::State::Error,
                                  .tableName = tableName,
                                  .currentRows = 0,
                                  .totalRows = 0,
                                  .message = "Insert failed: "s + e.what() });
            return false;
        }
    }
    return false;
}

void RestoreWorker(RestoreContext ctx, SqlConnection& workerConn)
{
    size_t const batchCapacity = ctx.restoreSettings.batchSize > 0 ? ctx.restoreSettings.batchSize : 4000;

    try
    {
        // SQLite optimization: Turn off synchronization for faster restore
        if (workerConn.ServerType() == SqlServerType::SQLITE)
        {
            SqlStatement { workerConn }.ExecuteDirect("PRAGMA synchronous = OFF");
            SqlStatement { workerConn }.ExecuteDirect("PRAGMA journal_mode = WAL");
            SqlStatement { workerConn }.ExecuteDirect("PRAGMA foreign_keys = OFF");
            if (ctx.restoreSettings.cacheSizeKB > 0)
            {
                SqlStatement { workerConn }.ExecuteDirect(
                    std::format("PRAGMA cache_size = -{}", ctx.restoreSettings.cacheSizeKB));
            }
        }

        while (true)
        {
            auto const fetchResult = FetchNextRestoreChunk(ctx);

            // Handle fetch errors (invalid entry, malformed path, checksum mismatch, etc.)
            if (!fetchResult.has_value())
            {
                auto const& error = fetchResult.error();
                ctx.progress.Update({ .state = Progress::State::Error,
                                      .tableName = error.tableName,
                                      .currentRows = 0,
                                      .totalRows = std::nullopt,
                                      .message = error.message });
                if (!error.tableName.empty())
                    IncrementChunkCounter(ctx, error.tableName, false);
                return; // Abort worker on fetch error to ensure consistent restoration
            }

            auto const& chunk = *fetchResult;

            // Check if queue was empty (isEndOfStream signals end of data)
            if (chunk.isEndOfStream)
                return; // Queue empty, worker done

            bool const success = RestoreChunkData(ctx, workerConn, chunk, batchCapacity);
            IncrementChunkCounter(ctx, chunk.tableName, success);

            if (!success)
                return; // Abort worker on restore error to ensure consistent restoration
        }
    }
    catch (std::exception const& e)
    {
        ctx.progress.Update({ .state = Progress::State::Error,
                              .tableName = "",
                              .currentRows = 0,
                              .totalRows = std::nullopt,
                              .message = "Worker failed: "s + e.what() });
    }
    catch (...)
    {
        ctx.progress.Update({ .state = Progress::State::Error,
                              .tableName = "",
                              .currentRows = 0,
                              .totalRows = std::nullopt,
                              .message = "Worker failed: Unknown error" });
    }
}

void RestoreIndexes(SqlConnectionString const& connectionString,
                    std::string const& schema,
                    std::map<std::string, TableInfo> const& tableMap,
                    ProgressManager& progress)
{
    SqlConnection conn;
    if (!conn.Connect(connectionString))
    {
        progress.Update({ .state = Progress::State::Error,
                          .tableName = "",
                          .currentRows = 0,
                          .totalRows = std::nullopt,
                          .message = "Failed to connect for index restoration: " + conn.LastError().message });
        return;
    }

    // SQLite doesn't support schemas - skip schema prefix for SQLite
    bool const isSQLite = conn.ServerType() == SqlServerType::SQLITE;

    for (auto const& [tableName, info]: tableMap)
    {
        if (info.indexes.empty())
            continue;

        for (auto const& idx: info.indexes)
        {
            try
            {
                // Build column list for the CREATE INDEX statement
                std::string columnList;
                for (size_t i = 0; i < idx.columns.size(); ++i)
                {
                    if (i > 0)
                        columnList += ", ";
                    columnList += std::format(R"("{}")", idx.columns[i]);
                }

                // Build the CREATE INDEX statement
                std::string const uniqueKeyword = idx.isUnique ? "UNIQUE " : "";
                std::string const formattedTableName =
                    isSQLite ? std::format(R"("{}")", tableName) : FormatTableName(schema, tableName);
                std::string const sql = std::format(
                    R"(CREATE {}INDEX "{}" ON {} ({}))", uniqueKeyword, idx.name, formattedTableName, columnList);

                SqlStatement { conn }.ExecuteDirect(sql);

                progress.Update({ .state = Progress::State::InProgress,
                                  .tableName = tableName,
                                  .currentRows = 0,
                                  .totalRows = std::nullopt,
                                  .message = std::format("Created index {}", idx.name) });
            }
            catch (std::exception const& e)
            {
                // Index may already exist or there might be other issues
                // Log as warning but continue with other indexes
                progress.Update({ .state = Progress::State::Warning,
                                  .tableName = tableName,
                                  .currentRows = 0,
                                  .totalRows = std::nullopt,
                                  .message = std::format("Failed to create index {}: {}", idx.name, e.what()) });
            }
        }
    }
}

void ApplyDatabaseConstraints(SqlConnectionString const& connectionString,
                              std::string const& schema,
                              std::map<std::string, TableInfo> const& tableMap,
                              ProgressManager& progress)
{
    SqlConnection conn;
    if (!conn.Connect(connectionString))
    {
        progress.Update({ .state = Progress::State::Error,
                          .tableName = "",
                          .currentRows = 0,
                          .totalRows = std::nullopt,
                          .message = "Failed to connect for FK constraints: " + conn.LastError().message });
        return;
    }
    if (conn.ServerType() == SqlServerType::SQLITE)
        return;

    auto const& formatter = conn.QueryFormatter(); // NOLINT(misc-const-correctness)

    for (auto const& [tableName, info]: tableMap)
    {
        if (info.foreignKeys.empty())
            continue;

        std::vector<SqlAlterTableCommand> commands;
        commands.reserve(info.foreignKeys.size());
        for (auto const& fk: info.foreignKeys)
        {
            commands.emplace_back(
                SqlAlterTableCommands::AddCompositeForeignKey { .columns = fk.foreignKey.columns,
                                                                .referencedTableName = fk.primaryKey.table.table,
                                                                .referencedColumns = fk.primaryKey.columns });
        }

        try
        {
            auto sqls = formatter.AlterTable(schema, tableName, commands);
            for (auto const& sql: sqls)
                SqlStatement { conn }.ExecuteDirect(sql);
        }
        catch (std::exception const& e)
        {
            progress.Update({ .state = Progress::State::Error,
                              .tableName = tableName,
                              .currentRows = 0,
                              .totalRows = 0,
                              .message = std::string("AddForeignKey failed: ") + e.what() });
        }
    }
}

/// Computes table creation order using topological sort based on FK dependencies.
///
/// For SQLite, tables must be created in dependency order (referenced tables first)
/// because SQLite validates FK references on CREATE TABLE even with foreign_keys = OFF.
/// For other databases, returns tables in map iteration order since FKs are added later via ALTER TABLE.
///
/// @param tableMap Map of table names to their metadata.
/// @param isSQLite Whether the target database is SQLite.
/// @return Vector of table names in creation order.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
std::vector<std::string> ComputeTableCreationOrder(std::map<std::string, TableInfo> const& tableMap, bool isSQLite)
{
    std::vector<std::string> tableOrder;

    if (isSQLite)
    {
        // Build dependency graph and topologically sort
        std::set<std::string> remaining;
        std::set<std::string> created;
        for (auto const& [tableName, _]: tableMap)
            remaining.insert(tableName);

        while (!remaining.empty())
        {
            bool progressMade = false;
            for (auto it = remaining.begin(); it != remaining.end();)
            {
                auto const& tableName = *it;
                auto const& info = tableMap.at(tableName);

                // Check if all referenced tables are already created
                bool canCreate = true;
                for (auto const& fk: info.foreignKeys)
                {
                    std::string const& referencedTable = fk.primaryKey.table.table;
                    // Self-reference is OK, and tables not in our map are assumed external
                    if (referencedTable != tableName && remaining.contains(referencedTable))
                    {
                        canCreate = false;
                        break;
                    }
                }

                if (canCreate)
                {
                    tableOrder.push_back(tableName);
                    created.insert(tableName);
                    it = remaining.erase(it);
                    progressMade = true;
                }
                else
                {
                    ++it;
                }
            }

            if (!progressMade && !remaining.empty())
            {
                // Circular dependency - add remaining tables anyway and let the DB handle errors
                for (auto const& name: remaining)
                    tableOrder.push_back(name);
                break;
            }
        }
    }
    else
    {
        // For other databases, alphabetical order is fine (FKs added later via ALTER TABLE)
        for (auto const& [tableName, _]: tableMap)
            tableOrder.push_back(tableName);
    }

    return tableOrder;
}

/// Creates tables from the backup metadata in the specified order.
///
/// @param conn The database connection.
/// @param schema The schema name.
/// @param tableMap Map of table names to their metadata.
/// @param tableOrder Vector of table names in creation order.
/// @param isSQLite Whether the target database is SQLite.
/// @param progress Progress manager for reporting status.
/// @return Set of successfully created table names.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
std::set<std::string> CreateTablesInOrder(SqlConnection& conn,
                                          std::string const& schema,
                                          std::map<std::string, TableInfo> const& tableMap,
                                          std::vector<std::string> const& tableOrder,
                                          bool isSQLite,
                                          ProgressManager& progress)
{
    std::set<std::string> createdTables;
    auto const& formatter = conn.QueryFormatter();

    // Pass 0 (non-SQLite only): Drop all existing FK constraints that might prevent table drops.
    // This handles cases where tables from previous test runs have FKs pointing to tables we need to drop.
    if (!isSQLite)
    {
        for (auto const& [tableName, info]: tableMap)
        {
            for (auto const& fk: info.foreignKeys)
            {
                // Construct FK constraint name (matching the format used in AddForeignKey)
                std::string const fkName =
                    std::format("FK_{}_{}", tableName, fk.foreignKey.columns.empty() ? "" : fk.foreignKey.columns[0]);
                std::string const formattedTableName = FormatTableName(schema, tableName);
                try
                {
                    SqlStatement { conn }.ExecuteDirect(
                        std::format("ALTER TABLE {} DROP CONSTRAINT IF EXISTS \"{}\"", formattedTableName, fkName));
                }
                catch (...) // NOLINT(bugprone-empty-catch)
                {
                    // Best-effort cleanup - constraint might not exist
                }
            }
        }
    }

    // Pass 1: Create Tables (without FKs for non-SQLite)
    for (auto const& tableName: tableOrder)
    {
        auto const& info = tableMap.at(tableName);

        if (!DropTableIfExists(conn, schema, tableName, progress))
            continue;

        bool tableCreated = true;
        try
        {
            std::vector<SqlCompositeForeignKeyConstraint> foreignKeys;
            foreignKeys.reserve(info.foreignKeys.size());
            for (auto const& fk: info.foreignKeys)
            {
                foreignKeys.emplace_back(SqlCompositeForeignKeyConstraint {
                    .columns = fk.foreignKey.columns,
                    .referencedTableName = fk.primaryKey.table.table,
                    .referencedColumns = fk.primaryKey.columns,
                });
            }

            std::vector<SqlCompositeForeignKeyConstraint> const& fksToCreate =
                isSQLite ? foreignKeys : std::vector<SqlCompositeForeignKeyConstraint> {};

            auto const createSqls = formatter.CreateTable(schema, tableName, info.columns, fksToCreate);
            for (auto const& sql: createSqls)
            {
                try
                {
                    SqlStatement { conn }.ExecuteDirect(sql);
                }
                catch (std::exception const& e)
                {
                    tableCreated = false;
                    progress.Update({ .state = Progress::State::Error,
                                      .tableName = tableName,
                                      .currentRows = 0,
                                      .totalRows = 0,
                                      .message = std::format("CreateTable failed: {} SQL: {}", e.what(), sql) });
                }
            }
        }
        catch (std::exception const& e)
        {
            tableCreated = false;
            progress.Update({ .state = Progress::State::Error,
                              .tableName = tableName,
                              .currentRows = 0,
                              .totalRows = 0,
                              .message = std::string("CreateTable generation failed: ") + e.what() });
        }

        if (tableCreated)
            createdTables.insert(tableName);
    }

    return createdTables;
}

std::set<std::string> RecreateDatabaseSchema(SqlConnectionString const& connectionString,
                                             std::string const& schema,
                                             std::map<std::string, TableInfo> const& tableMap,
                                             ProgressManager& progress)
{
    SqlConnection conn;
    if (!conn.Connect(connectionString))
    {
        progress.Update({ .state = Progress::State::Error,
                          .tableName = "",
                          .currentRows = 0,
                          .totalRows = std::nullopt,
                          .message = "Failed to connect for schema recreation: " + conn.LastError().message });
        return {};
    }

    bool const isSQLite = conn.ServerType() == SqlServerType::SQLITE;

    // Speed up restore (SQLite only)
    if (isSQLite)
    {
        SqlStatement stmt { conn };
        stmt.ExecuteDirect("PRAGMA synchronous = OFF");
        stmt.ExecuteDirect("PRAGMA journal_mode = WAL");
        stmt.ExecuteDirect("PRAGMA foreign_keys = OFF"); // Disable FKs during restore
    }

    auto const tableOrder = ComputeTableCreationOrder(tableMap, isSQLite);
    return CreateTablesInOrder(conn, schema, tableMap, tableOrder, isSQLite, progress);
}

} // namespace Lightweight::SqlBackup::detail
