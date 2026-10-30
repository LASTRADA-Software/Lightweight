// SPDX-License-Identifier: Apache-2.0

#include "../SqlQueryFormatter.hpp"
#include "../SqlStatement.hpp"
#include "../SqlTransaction.hpp"
#include "../TracyProfiler.hpp"
#include "BatchManager.hpp"
#include "Common.hpp"
#include "MsgPackChunkFormats.hpp"
#include "Restore.hpp"
#include "Sha256.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <expected>
#include <format>
#include <optional>
#include <ranges>
#include <set>
#include <span>
#include <string_view>
#include <thread>
#include <vector>

using namespace std::string_literals;

namespace Lightweight::SqlBackup::detail
{

namespace
{
    /// Switches SQL Server's IDENTITY_INSERT on for a table, and off again when it goes out of scope.
    class IdentityInsertScope
    {
      public:
        /// Switches IDENTITY_INSERT on for @p formattedTableName.
        /// @param connection The connection the inserts run on.
        /// @param formattedTableName The quoted, schema-qualified table name.
        IdentityInsertScope(SqlConnection& connection, std::string formattedTableName):
            _connection { connection },
            _table { std::move(formattedTableName) }
        {
            (void) SqlStatement { _connection }.ExecuteDirect(std::format("SET IDENTITY_INSERT {} ON", _table));
        }

        IdentityInsertScope(IdentityInsertScope const&) = delete;
        IdentityInsertScope(IdentityInsertScope&&) = delete;
        IdentityInsertScope& operator=(IdentityInsertScope const&) = delete;
        IdentityInsertScope& operator=(IdentityInsertScope&&) = delete;

        ~IdentityInsertScope()
        {
            try
            {
                (void) SqlStatement { _connection }.ExecuteDirect(std::format("SET IDENTITY_INSERT {} OFF", _table));
            }
            catch (...) // NOLINT(bugprone-empty-catch): a destructor must not throw
            {
                // A lingering IDENTITY_INSERT fails the next table's ON, which reports it.
            }
        }

      private:
        SqlConnection& _connection;
        std::string _table;
    };

    /// Applies the SQLite settings that speed up a restore to a (re)connected worker connection.
    /// @param connection The worker connection.
    /// @param settings The restore settings, for the page cache size.
    void ApplySqliteRestorePragmas(SqlConnection& connection, RestoreSettings const& settings)
    {
        if (connection.ServerType() != SqlServerType::SQLITE)
            return;
        (void) SqlStatement { connection }.ExecuteDirect("PRAGMA synchronous = OFF");
        (void) SqlStatement { connection }.ExecuteDirect("PRAGMA journal_mode = WAL");
        (void) SqlStatement { connection }.ExecuteDirect("PRAGMA foreign_keys = OFF");
        if (settings.cacheSizeKB > 0)
            (void) SqlStatement { connection }.ExecuteDirect(std::format("PRAGMA cache_size = -{}", settings.cacheSizeKB));
    }

    /// Reads the next batch of @p reader into @p batch.
    /// @param reader The chunk reader.
    /// @param batch Receives the batch.
    /// @param columnCount The number of columns of the table.
    /// @return Whether a batch was read, or why the chunk's content cannot be read (it does not decode,
    ///         or its column count does not match the table's).
    std::expected<bool, std::string> ReadNextBatch(ChunkReader& reader, ColumnBatch& batch, std::size_t columnCount)
    {
        ZoneScopedN("Restore::ReadBatch");
        try
        {
            if (!reader.ReadBatch(batch))
                return false;
        }
        catch (std::exception const& e)
        {
            return std::unexpected(std::string { e.what() });
        }
        if (batch.rowCount > 0 && batch.columns.size() != columnCount)
            return std::unexpected(std::format(
                "Column count mismatch in backup data: expected {} columns, got {}", columnCount, batch.columns.size()));
        return true;
    }

    /// Returns the rows of @p status that the driver reported as `SQL_PARAM_ERROR` — and only those.
    /// @param status A parameter status array.
    /// @return The zero-based indices of the failed rows.
    std::vector<std::size_t> RowsWithParamError(std::span<SQLUSMALLINT const> status)
    {
        return std::views::iota(std::size_t { 0 }, status.size())
               | std::views::filter([&](std::size_t row) { return status[row] == SQL_PARAM_ERROR; })
               | std::ranges::to<std::vector>();
    }

    /// Renders @p error as `"<sqlState> <message>"`, the form a table failure is reported in.
    /// @param error The error.
    /// @return The text.
    std::string DescribeError(SqlErrorInfo const& error)
    {
        return std::format("{} {}", error.sqlState, error.message);
    }

    /// Renders the terminal progress message of a table with failures.
    /// @param report The table's failures.
    /// @return `"<n> rows failed, <m> cells set to NULL"`, followed by the table failure and the
    ///         number of unreadable chunks, if any.
    std::string DescribeTableFailures(TableRestoreReport const& report)
    {
        auto const nulledCells = std::ranges::fold_left(
            report.valueFailures, std::uint64_t { 0 }, [](std::uint64_t sum, ColumnValueFailures const& column) {
                return sum + column.cellsSetToNull;
            });
        auto message = std::format("{} rows failed, {} cells set to NULL", report.failedRowCount, nulledCells);
        if (report.tableFailure)
            message += std::format(", table failed: {}", *report.tableFailure);
        if (report.unreadableChunks > 0)
            message += std::format(", {} chunks unreadable", report.unreadableChunks);
        return message;
    }

    /// What one chunk's restore attempts share across retries.
    struct ChunkRestoreState
    {
        /// The leading rows of the chunk that are inserted or reported; a retry skips them.
        std::size_t settledRows = 0;
        /// Whether a row of the chunk failed or a value of it was set to NULL.
        bool hadFailures = false;
    };

    /// Inserts the rows of @p chunk that @p state does not mark settled, one committed batch at a time.
    ///
    /// @param ctx The restore context.
    /// @param workerConn The worker connection.
    /// @param chunk The chunk.
    /// @param batchCapacity The batch size.
    /// @param retryCount The retries consumed so far, for classifying insert errors.
    /// @param state Updated as rows settle and fail.
    /// @return Why the chunk's content (from some row on) cannot be read; std::nullopt if it was read in full.
    /// @throws SqlException for an insert error that is not about the data (e.g. a lost connection).
    /// @throws TableRestoreFailure for an insert error that fails the table.
    std::optional<std::string> InsertChunkRows(RestoreContext& ctx,
                                               SqlConnection& workerConn,
                                               RestoreChunkInfo const& chunk,
                                               std::size_t batchCapacity,
                                               unsigned retryCount,
                                               ChunkRestoreState& state)
    {
        auto const& tableInfo = *chunk.tableInfo;
        std::string const& tableName = chunk.tableName;
        std::string const targetSchema = TargetSchema(tableInfo, ctx.schema);
        std::string const targetTableName = TargetTableName(tableName, tableInfo);

        std::unique_ptr<ChunkReader> reader;
        try
        {
            // Zero-copy reader directly over the buffer.
            reader = CreateMsgPackChunkReaderFromBuffer(chunk.content);
        }
        catch (std::exception const& e)
        {
            return std::string { e.what() };
        }

        // Switched off again on every exit, so that a worker going on after a failed table can
        // switch it on for the next one (SQL Server allows it for one table at a time).
        std::optional<IdentityInsertScope> identityInsert;
        if (workerConn.ServerType() == SqlServerType::MICROSOFT_SQL
            && std::ranges::any_of(tableInfo.columns,
                                   [](auto const& col) { return col.primaryKey == SqlPrimaryKeyType::AUTO_INCREMENT; }))
            identityInsert.emplace(workerConn, FormatTableName(targetSchema, targetTableName));

        SqlStatement stmt { workerConn };
        auto const placeholders = std::ranges::fold_left(
            std::views::iota(0UZ, tableInfo.columns.size()), std::string {}, [](std::string const& acc, size_t) {
                return acc.empty() ? std::string("?") : acc + ", ?";
            });
        {
            ZoneScopedN("Restore::Prepare");
            stmt.Prepare(workerConn.QueryFormatter().Insert(targetSchema, targetTableName, tableInfo.fields, placeholders));
        }

        std::vector<SQLUSMALLINT> paramStatus;
        auto& committedRows = *ctx.committedRows.at(tableName);
        // Without a trustworthy status array the failing rows of a batch are found by bisection.
        auto const trustsParamStatus = workerConn.QueryFormatter().ParameterStatusIdentifiesFailedRows();
        auto hooks = ::Lightweight::detail::BatchManagerHooks {
            // Each batch is its own transaction: committed when every row went in, rolled back otherwise,
            // which leaves the connection ready for the next attempt (on PostgreSQL, out of 25P02).
            .tryInsert = [&](std::span<SqlRawColumn const> slice,
                             std::size_t rowCount) -> std::expected<void, ::Lightweight::detail::BatchInsertError> {
                ZoneScopedN("Restore::ExecuteBatch");
                ZoneValue(rowCount);
                paramStatus.assign(rowCount, SQL_PARAM_UNUSED);
                auto processed = SQLULEN { 0 };
                try
                {
                    auto transaction = SqlTransaction { workerConn, SqlTransactionMode::ROLLBACK };
                    (void) stmt.ExecuteBatch(slice, rowCount, paramStatus, processed);
                    if (!transaction.TryCommit())
                        return std::unexpected(::Lightweight::detail::BatchInsertError {
                            .error = SqlErrorInfo::FromConnectionHandle(workerConn.NativeHandle()),
                            .reportedFailedRows = {},
                        });
                    committedRows.fetch_add(rowCount);
                    return {};
                }
                catch (SqlException const& e)
                {
                    return std::unexpected(::Lightweight::detail::BatchInsertError {
                        .error = e.info(),
                        .reportedFailedRows =
                            trustsParamStatus ? RowsWithParamError(paramStatus) : std::vector<std::size_t> {},
                    });
                }
            },
            .classify =
                [&](SqlErrorInfo const& error) {
                    using ::Lightweight::detail::FailureDisposition;
                    if (IsBindClassError(error))
                        return FailureDisposition::FailTable;
                    if (ClassifyRetryDecision(workerConn, error, retryCount, ctx.retrySettings).reason
                        == SqlRetryGiveUpReason::NotTransient)
                        return FailureDisposition::IsolateRows;
                    return FailureDisposition::Propagate;
                },
            .onRowFailed =
                [&](std::size_t rowInChunk, std::string_view sqlState, std::string_view message, std::string sampleKey) {
                    state.hadFailures = true;
                    ctx.failureLog.RecordRowFailure(
                        tableName, chunk.chunkPath, rowInChunk, sqlState, message, std::move(sampleKey));
                },
            .onCellNulled =
                [&](std::string_view column, std::string_view reason) {
                    state.hadFailures = true;
                    ctx.failureLog.RecordNulledCell(tableName, column, reason);
                },
            .onSettled = [&](std::size_t settledRows) { state.settledRows = std::max(state.settledRows, settledRows); },
        };
        ::Lightweight::detail::BatchManager batchManager(
            std::move(hooks), tableInfo.columns, batchCapacity, workerConn.ServerType(), ctx.sourceServerType);

        std::optional<std::string> unreadable;
        std::size_t rowsReadBefore = 0;
        ColumnBatch batch;
        while (true)
        {
            auto const hasBatch = ReadNextBatch(*reader, batch, tableInfo.columns.size());
            if (!hasBatch)
            {
                unreadable = hasBatch.error();
                break;
            }
            if (!*hasBatch)
                break;
            if (batch.rowCount == 0)
                continue;

            // Rows an earlier attempt settled are not inserted again.
            auto const skipRows =
                state.settledRows > rowsReadBefore ? std::min(state.settledRows - rowsReadBefore, batch.rowCount) : 0;
            rowsReadBefore += batch.rowCount;
            {
                ZoneScopedN("Restore::PushBatch");
                batchManager.PushBatch(batch, skipRows);
            }

            auto const newRows = batch.rowCount - skipRows;
            size_t const currentTotal = ctx.tableProgress.at(tableName)->fetch_add(newRows) + newRows;
            ctx.progress.Update({ .state = Progress::State::InProgress,
                                  .tableName = tableName,
                                  .currentRows = currentTotal,
                                  .totalRows = chunk.displayTotal,
                                  .message = "Restoring chunk " + chunk.chunkPath });

            // Report items processed for ETA calculation
            ctx.progress.OnItemsProcessed(newRows);
        }

        {
            ZoneScopedN("Restore::Flush");
            batchManager.Flush();
        }
        return unreadable;
    }

    /// Records that @p chunk's table fails as a whole, for @p reason, unless it already failed.
    /// @param ctx The restore context.
    /// @param chunk The chunk the table failed on.
    /// @param reason Why it failed.
    /// @return ChunkRestoreOutcome::TableFailed.
    ChunkRestoreOutcome FailTable(RestoreContext& ctx, RestoreChunkInfo const& chunk, std::string const& reason)
    {
        // Workers in other chunks of the table may fail it at the same time: only the first records it.
        // Its failed row count is settled once its chunks are processed (SettleFailedTableRows()).
        if (!ctx.failedTables.Add(chunk.tableName))
            return ChunkRestoreOutcome::TableFailed;
        ctx.failureLog.RecordTableFailure(chunk.tableName, reason, 0);
        ctx.progress.Update({ .state = Progress::State::Error,
                              .tableName = chunk.tableName,
                              .currentRows = 0,
                              .totalRows = 0,
                              .message = std::format("Table failed: {} (chunk {})", reason, chunk.chunkPath) });
        return ChunkRestoreOutcome::TableFailed;
    }
} // namespace

bool IsBindClassError(SqlErrorInfo const& error) noexcept
{
    static constexpr auto BindClassStates = std::array<std::string_view, 4> { "HYC00", "HY003", "HY004", "07006" };
    return std::ranges::contains(BindClassStates, std::string_view { error.sqlState });
}

void SettleFailedTableRows(RestoreContext& ctx, std::string const& tableName)
{
    if (!ctx.failedTables.Contains(tableName))
        return;
    auto const archived = std::uint64_t { ctx.tableMap.at(tableName).rowCount };
    auto const committed = ctx.committedRows.at(tableName)->load();
    ctx.failureLog.SetFailedTableRowCount(tableName, archived > committed ? archived - committed : 0);
}

void IncrementChunkCounter(RestoreContext& ctx, std::string const& tableName)
{
    auto& chunkCounter = *ctx.chunksProcessed.at(tableName);
    size_t const processedChunks = chunkCounter.fetch_add(1) + 1;
    if (processedChunks < ctx.totalChunks.at(tableName))
        return;

    SettleFailedTableRows(ctx, tableName);
    size_t const actualRowCount = ctx.tableProgress.at(tableName)->load();
    auto const failures = ctx.failureLog.TableSnapshot(tableName);
    auto const failed = failures.HasFailures();
    ctx.progress.Update({ .state = failed ? Progress::State::Error : Progress::State::Finished,
                          .tableName = tableName,
                          .currentRows = actualRowCount,
                          .totalRows = actualRowCount,
                          .message = failed ? DescribeTableFailures(failures) : "Restore complete" });
}

std::expected<RestoreChunkInfo, FetchChunkError> FetchNextRestoreChunk(RestoreContext& ctx)
{
    ZoneScopedN("Restore::FetchChunk");
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
        ZoneScopedN("Restore::UnzipChunk");
        auto const lock = std::scoped_lock(ctx.fileMutex);
        if (!entryInfo.valid)
            return std::unexpected(
                FetchChunkError { .tableName = "", .message = std::format("Invalid zip entry: {}", entryInfo.name) });
        content = ReadZipEntry<std::vector<uint8_t>>(ctx.zip, entryInfo.index, entryInfo.size);
    }

    // Parse path FIRST to get tableName for chunk-based completion tracking.
    // Path format: data/<folder>/chunk_ID.msgpack, where <folder> is the table's ChunkDirectory.
    std::string const path = entryInfo.name;
    auto const directory = ChunkDirectoryOf(path);
    if (!directory)
        return std::unexpected(
            FetchChunkError { .tableName = "", .message = std::format("Malformed chunk path: {}", path) });

    auto const table = ctx.chunkTables.find(std::string { *directory });
    if (table == ctx.chunkTables.end() || !ctx.tableMap.contains(table->second))
        return std::unexpected(
            FetchChunkError { .tableName = "", .message = std::format("Unknown table in backup: {}", *directory) });
    std::string const& tableName = table->second;

    // Verify checksum if available
    if (ctx.checksums)
    {
        auto const it = ctx.checksums->find(entryInfo.name);
        if (it != ctx.checksums->end())
        {
            ZoneScopedN("Restore::Sha256Verify");
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

ChunkRestoreOutcome RestoreChunkData(RestoreContext& ctx,
                                     SqlConnection& workerConn,
                                     RestoreChunkInfo const& chunk,
                                     size_t batchCapacity)
{
    ZoneScopedN("SqlBackup::RestoreChunkData");
    ZoneTextObject(chunk.tableName);
    ZoneValue(batchCapacity);
    std::string const& tableName = chunk.tableName;

    ctx.progress.Update({ .state = Progress::State::InProgress,
                          .tableName = tableName,
                          .currentRows = ctx.tableProgress.at(tableName)->load(),
                          .totalRows = chunk.displayTotal,
                          .message = "Restoring chunk " + chunk.chunkPath });

    // Outlives each attempt (and its BatchManager), so that a retry resumes after the settled rows.
    auto state = ChunkRestoreState {};
    unsigned retryCount = 0;
    while (true)
    {
        try
        {
            if (auto const unreadable = InsertChunkRows(ctx, workerConn, chunk, batchCapacity, retryCount, state))
            {
                // Not a lost connection: the rows read before stay committed, the rest is skipped.
                ctx.failureLog.RecordUnreadableChunk(tableName);
                ctx.progress.Update({ .state = Progress::State::Error,
                                      .tableName = tableName,
                                      .currentRows = 0,
                                      .totalRows = 0,
                                      .message = std::format("Unreadable chunk {}: {}", chunk.chunkPath, *unreadable) });
                return ChunkRestoreOutcome::RestoredWithFailures;
            }
            return state.hadFailures ? ChunkRestoreOutcome::RestoredWithFailures : ChunkRestoreOutcome::Restored;
        }
        catch (::Lightweight::detail::TableRestoreFailure const& e)
        {
            return FailTable(ctx, chunk, DescribeError(e.error));
        }
        catch (SqlException const& e)
        {
            auto const decision = ClassifyRetryDecision(workerConn, e.info(), retryCount, ctx.retrySettings);
            // Not about the connection, and not raised by an insert (e.g. the table cannot be prepared).
            if (decision.reason == SqlRetryGiveUpReason::NotTransient)
                return FailTable(ctx, chunk, DescribeError(e.info()));

            if (decision.action == SqlRetryAction::GiveUp)
            {
                ctx.progress.Update({ .state = Progress::State::Error,
                                      .tableName = tableName,
                                      .currentRows = 0,
                                      .totalRows = 0,
                                      .message = "Insert failed: "s + e.what() });
                return ChunkRestoreOutcome::WorkerFailed;
            }

            ++retryCount;
            ctx.progress.Update(
                { .state = Progress::State::Warning,
                  .tableName = tableName,
                  .currentRows = 0,
                  .totalRows = chunk.tableInfo->rowCount,
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
                return ChunkRestoreOutcome::WorkerFailed;
            }
            ApplySqliteRestorePragmas(workerConn, ctx.restoreSettings);

            // Loop will re-process the same chunk, after its settled rows.
        }
        catch (std::exception const& e)
        {
            return FailTable(ctx, chunk, e.what());
        }
    }
}

void RestoreWorker(RestoreContext ctx, SqlConnection& workerConn)
{
    static std::atomic<int> workerCounter { 0 };
    auto const workerName = std::format("RestoreWorker-{}", workerCounter.fetch_add(1));
    TracySetThreadName(workerName.c_str());

    size_t const batchCapacity = ctx.restoreSettings.batchSize > 0 ? ctx.restoreSettings.batchSize : 4000;

    try
    {
        // SQLite optimization: Turn off synchronization for faster restore
        ApplySqliteRestorePragmas(workerConn, ctx.restoreSettings);

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
                // The chunk is skipped; an unknown table counts as unattributed.
                ctx.failureLog.RecordUnreadableChunk(error.tableName);
                if (!error.tableName.empty())
                    IncrementChunkCounter(ctx, error.tableName);
                continue;
            }

            auto const& chunk = *fetchResult;

            // Check if queue was empty (isEndOfStream signals end of data)
            if (chunk.isEndOfStream)
                return; // Queue empty, worker done

            if (ctx.failedTables.Contains(chunk.tableName))
            {
                IncrementChunkCounter(ctx, chunk.tableName); // the table already failed
                continue;
            }

            auto const outcome = RestoreChunkData(ctx, workerConn, chunk, batchCapacity);
            // The connection is gone: stop, leaving the chunk uncounted so that its table shows as incomplete.
            if (outcome == ChunkRestoreOutcome::WorkerFailed)
                return;
            IncrementChunkCounter(ctx, chunk.tableName);
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
    SqlConnection conn { std::nullopt };
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
                auto const targetTableName = TargetTableName(tableName, info);
                std::string const formattedTableName = isSQLite
                                                           ? std::format(R"("{}")", targetTableName)
                                                           : FormatTableName(TargetSchema(info, schema), targetTableName);
                std::string const sql = std::format(
                    R"(CREATE {}INDEX "{}" ON {} ({}))", uniqueKeyword, idx.name, formattedTableName, columnList);

                SqlStatement { conn }.ExecuteDirect(sql).ConsumePendingResults();

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
    SqlConnection conn { std::nullopt };
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

        // Source databases can contain literally duplicated FK constraints (the same column list,
        // added repeatedly under different auto-generated names). The recreated constraint name is
        // derived from table + columns (BuildForeignKeyConstraintName), so recreating each
        // duplicate would collide (error 2714). Key the dedup on exactly the name-determining
        // parts: only the first FK per column list is recreated.
        std::set<std::string> seenColumnLists;
        std::vector<SqlAlterTableCommand> commands;
        commands.reserve(info.foreignKeys.size());
        for (auto const& fk: info.foreignKeys)
        {
            std::string key;
            for (auto const& column: fk.foreignKey.columns)
                key += column + "|";
            if (!seenColumnLists.insert(std::move(key)).second)
                continue;

            commands.emplace_back(
                SqlAlterTableCommands::AddCompositeForeignKey { .columns = fk.foreignKey.columns,
                                                                .referencedTableName = fk.primaryKey.table.table,
                                                                .referencedColumns = fk.primaryKey.columns,
                                                                .referencedSchemaName = fk.primaryKey.table.schema });
        }
        if (commands.empty())
            continue;

        try
        {
            auto sqls = formatter.AlterTable(TargetSchema(info, schema), TargetTableName(tableName, info), commands);
            for (auto const& sql: sqls)
                SqlStatement { conn }.ExecuteDirect(sql).ConsumePendingResults();
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
                auto const targetTableName = TargetTableName(tableName, info);
                std::string const fkName =
                    std::format("FK_{}_{}", targetTableName, fk.foreignKey.columns.empty() ? "" : fk.foreignKey.columns[0]);
                std::string const formattedTableName = FormatTableName(TargetSchema(info, schema), targetTableName);
                try
                {
                    (void) SqlStatement { conn }.ExecuteDirect(
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
        auto const targetSchema = TargetSchema(info, schema);
        auto const targetTableName = TargetTableName(tableName, info);

        if (!DropTableIfExists(conn, targetSchema, targetTableName, progress))
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

            auto const createSqls = formatter.CreateTable(targetSchema, targetTableName, info.columns, fksToCreate);
            for (auto const& sql: createSqls)
            {
                try
                {
                    SqlStatement { conn }.ExecuteDirect(sql).ConsumePendingResults();
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

/// Creates each schema that a multi-schema archive records for @p tableMap, unless it exists.
///
/// @param conn The database connection.
/// @param tableMap Map of table names to their metadata.
/// @param progress Progress manager for reporting errors.
void CreateRecordedSchemas(SqlConnection& conn, std::map<std::string, TableInfo> const& tableMap, ProgressManager& progress)
{
    std::set<std::string> schemas;
    for (auto const& [tableName, info]: tableMap)
        if (!info.schema.empty())
            schemas.insert(info.schema);

    for (auto const& schemaName: schemas)
    {
        auto const sql = conn.QueryFormatter().CreateSchemaIfNotExists(schemaName);
        if (sql.empty())
            continue;
        try
        {
            SqlStatement { conn }.ExecuteDirect(sql).ConsumePendingResults();
        }
        catch (std::exception const& e)
        {
            progress.Update({ .state = Progress::State::Error,
                              .tableName = "",
                              .currentRows = 0,
                              .totalRows = 0,
                              .message = std::format("CreateSchema {} failed: {} SQL: {}", schemaName, e.what(), sql) });
        }
    }
}

std::set<std::string> RecreateDatabaseSchema(SqlConnectionString const& connectionString,
                                             std::string const& schema,
                                             std::map<std::string, TableInfo> const& tableMap,
                                             ProgressManager& progress)
{
    SqlConnection conn { std::nullopt };
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
        (void) stmt.ExecuteDirect("PRAGMA synchronous = OFF");
        (void) stmt.ExecuteDirect("PRAGMA journal_mode = WAL");
        (void) stmt.ExecuteDirect("PRAGMA foreign_keys = OFF"); // Disable FKs during restore
    }

    CreateRecordedSchemas(conn, tableMap, progress);

    auto const tableOrder = ComputeTableCreationOrder(tableMap, isSQLite);
    return CreateTablesInOrder(conn, schema, tableMap, tableOrder, isSQLite, progress);
}

} // namespace Lightweight::SqlBackup::detail
