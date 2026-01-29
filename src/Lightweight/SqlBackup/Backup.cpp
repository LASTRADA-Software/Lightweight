// SPDX-License-Identifier: Apache-2.0

#include "../DataBinder/SqlDate.hpp"
#include "../DataBinder/SqlDateTime.hpp"
#include "../DataBinder/SqlGuid.hpp"
#include "../DataBinder/SqlRawColumn.hpp"
#include "../DataBinder/SqlTime.hpp"
#include "../SqlColumnTypeDefinitions.hpp"
#include "../SqlQuery.hpp"
#include "../SqlQueryFormatter.hpp"
#include "../SqlStatement.hpp"
#include "Backup.hpp"
#include "Common.hpp"
#include "MsgPackChunkFormats.hpp"
#include "Sha256.hpp"
#include "SqlBackupFormats.hpp"

#include <algorithm>
#include <cstring>
#include <format>
#include <limits>
#include <print>
#include <ranges>
#include <variant>
#include <vector>

// #define DEBUG_BACKUP_WORKER

#ifdef DEBUG_BACKUP_WORKER
    #include <iostream>
#endif

/// Compile-time constant for debug output. Enable by defining DEBUG_BACKUP_WORKER.
constexpr bool DebugBackupWorker =
#ifdef DEBUG_BACKUP_WORKER
    true;
#else
    false;
#endif

using namespace std::string_literals;

namespace Lightweight::SqlBackup::detail
{

/// Builds a SELECT query for MSSQL with CONVERT for DECIMAL columns.
///
/// MSSQL ODBC driver loses precision when reading DECIMAL as string directly.
/// Using CONVERT(VARCHAR, ...) forces SQL Server to convert with full precision.
///
/// @param schema The schema name (may be empty).
/// @param tableName The table name.
/// @param columns The column definitions.
/// @param primaryKeys The primary key column names for ORDER BY.
/// @param offset The row offset for pagination.
/// @return The SQL query string.
std::string BuildMssqlDecimalSelectQuery(std::string_view schema,
                                         std::string const& tableName,
                                         std::vector<SqlSchema::Column> const& columns,
                                         std::vector<std::string> const& primaryKeys,
                                         size_t offset)
{
    using namespace SqlColumnTypeDefinitions;

    std::string sql;
    sql.reserve(256);
    std::format_to(std::back_inserter(sql), "SELECT ");

    for (size_t i = 0; i < columns.size(); ++i)
    {
        if (i > 0)
            std::format_to(std::back_inserter(sql), ", ");

        auto const& col = columns[i];
        if (std::holds_alternative<Decimal>(col.type))
        {
            // MSSQL ODBC driver loses precision when reading DECIMAL as string directly.
            // Using CONVERT(VARCHAR, ...) forces SQL Server to convert with full precision.
            auto const& dec = std::get<Decimal>(col.type);
            // VARCHAR size should accommodate precision + scale + decimal point + sign
            auto const varcharSize = dec.precision + 3;
            std::format_to(std::back_inserter(sql), "CONVERT(VARCHAR({}), [{}])", varcharSize, col.name);
        }
        else
        {
            std::format_to(std::back_inserter(sql), "[{}]", col.name);
        }
    }

    if (schema.empty())
        std::format_to(std::back_inserter(sql), " FROM [{}]", tableName);
    else
        std::format_to(std::back_inserter(sql), " FROM [{}].[{}]", schema, tableName);

    // ORDER BY is required for OFFSET
    std::format_to(std::back_inserter(sql), " ORDER BY ");
    if (!primaryKeys.empty())
    {
        for (size_t i = 0; i < primaryKeys.size(); ++i)
        {
            if (i > 0)
                std::format_to(std::back_inserter(sql), ", ");
            std::format_to(std::back_inserter(sql), "[{}] ASC", primaryKeys[i]);
        }
    }
    else
    {
        std::format_to(std::back_inserter(sql), "[{}] ASC", columns[0].name);
    }

    if (offset > 0)
        std::format_to(std::back_inserter(sql), " OFFSET {} ROWS", offset);

    return sql;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
std::string BuildSelectQueryWithOffset(SqlQueryFormatter const& formatter,
                                       SqlServerType serverType,
                                       std::string_view schema,
                                       std::string const& tableName,
                                       std::vector<SqlSchema::Column> const& columns,
                                       std::vector<std::string> const& primaryKeys,
                                       size_t offset)
{
    using namespace SqlColumnTypeDefinitions;

    // Check if we need special handling for MSSQL DECIMAL columns
    bool const needsMssqlDecimalConvert =
        serverType == SqlServerType::MICROSOFT_SQL
        && std::ranges::any_of(columns, [](auto const& c) { return std::holds_alternative<Decimal>(c.type); });

    if (needsMssqlDecimalConvert)
        return BuildMssqlDecimalSelectQuery(schema, tableName, columns, primaryKeys, offset);

    // Use Query Builder with schema support
    auto query = schema.empty() ? SqlQueryBuilder(formatter).FromTable(tableName).Select()
                                : SqlQueryBuilder(formatter).FromSchemaTable(schema, tableName).Select();

    for (auto const& col: columns)
        query.Field(col.name);

    // ORDER BY is required for:
    // 1. MS SQL Server when using OFFSET
    // 2. Deterministic results for resumption
    if (!primaryKeys.empty())
    {
        for (auto const& pk: primaryKeys)
            query.OrderBy(pk, SqlResultOrdering::ASCENDING);
    }
    else
    {
        // Fallback: order by first column (less reliable but better than no order)
        query.OrderBy(columns[0].name, SqlResultOrdering::ASCENDING);
    }

    if (offset > 0)
        return query.Range(offset, (std::numeric_limits<size_t>::max)()).ToSql();
    else
        return query.All().ToSql();
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void ProcessTableBackup(BackupContext& ctx, SqlConnection& conn, SqlSchema::Table const& table)
{
    std::string const& tableName = table.name;

    if (table.columns.empty())
    {
        ctx.progress.Update({ .state = Progress::State::Error,
                              .tableName = tableName,
                              .currentRows = 0,
                              .totalRows = 0,
                              .message = "No columns found for table "s + tableName });
        return;
    }

    SqlStatement stmt { conn };
    auto const& formatter = conn.QueryFormatter();

    auto const formattedTableName = FormatTableName(table.schema, tableName);
    auto const totalRows = static_cast<size_t>(
        stmt.ExecuteDirectScalar<int64_t>(std::format("SELECT COUNT(*) FROM {}", formattedTableName)).value_or(0));

    // Report row count for ETA calculation (replaces RowCounterThread)
    ctx.progress.AddTotalItems(totalRows);

    ctx.progress.Update({ .state = Progress::State::Started,
                          .tableName = tableName,
                          .currentRows = 0,
                          .totalRows = totalRows,
                          .message = "Started backup"s });

    if constexpr (DebugBackupWorker)
    {
        std::println(stderr, "DEBUG: ProcessTableBackup: Starting backup for {} ({} rows)", tableName, totalRows);
        std::println(stderr, "DEBUG: Creating writer...");
    }
    auto writer = CreateMsgPackChunkWriter(ctx.backupSettings.chunkSizeBytes);
    if constexpr (DebugBackupWorker)
        std::println(stderr, "DEBUG: Writer created.");

    size_t chunkId = 0;
    size_t processedRows = 0;
    unsigned retryCount = 0;

    auto flushChunk = [&]() {
        std::string data = writer->Flush();
        if (data.empty())
            return;

        std::string const extension = ".msgpack";
        std::string sName = table.name;
        std::ranges::replace(sName, '.', '_');
        std::string const entryName = std::format("data/{}/{:04}{}", sName, chunkId + 1, extension);

        // Compute SHA-256 checksum before acquiring the lock
        std::string const checksum = Sha256::Hash(data);

        auto const lock = std::scoped_lock(ctx.zipMutex);

        // Store checksum
        if (ctx.checksums && ctx.checksumMutex)
        {
            auto const checksumLock = std::scoped_lock(*ctx.checksumMutex);
            (*ctx.checksums)[entryName] = checksum;
        }

        // libzip defers writing until close(), so we must ensure the buffer survives.
        // We give ownership to libzip by allocating with malloc and passing freeData=true.
        // zip_source_buffer takes ownership when freeData=1, and zip_file_add takes ownership of source on success.
        // NOLINTNEXTLINE(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory,clang-analyzer-unix.Malloc)
        void* persistentData = std::malloc(data.size());
        if (!persistentData)
            throw std::bad_alloc();

        // NOLINTNEXTLINE(bugprone-not-null-terminated-result)
        std::memcpy(persistentData, data.data(), data.size());

        zip_source_t* source = zip_source_buffer(ctx.zip, persistentData, data.size(), 1);
        if (!source)
        {
            std::free(persistentData); // NOLINT(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory)
            throw std::bad_alloc();
        }

        // libzip takes ownership via zip_source_buffer(freeData=1), so no leak
        zip_int64_t const entryIndex = zip_file_add( // NOLINT(clang-analyzer-unix.Malloc)
            ctx.zip,
            entryName.c_str(),
            source,
            ZIP_FL_OVERWRITE | ZIP_FL_ENC_UTF_8);
        if (entryIndex < 0)
        {
            zip_source_free(source);
            throw std::runtime_error("Failed to add file to zip: " + entryName);
        }

        // Apply compression settings to the newly added entry
        zip_set_file_compression(ctx.zip,
                                 static_cast<zip_uint64_t>(entryIndex),
                                 static_cast<zip_int32_t>(ctx.backupSettings.method),
                                 ctx.backupSettings.level);

        writer->Clear();
        chunkId++;
    };

    if constexpr (DebugBackupWorker)
        std::println(stderr, "DEBUG: Reserving row...");
    std::vector<BackupValue> row;
    row.reserve(table.columns.size());
    if constexpr (DebugBackupWorker)
        std::println(stderr, "DEBUG: Row reserved. Entering FetchRow loop...");

    auto const columnCount = table.columns.size();

    // Retry loop with offset-based resumption
    while (retryCount <= ctx.retrySettings.maxRetries)
    {
        try
        {
            // Build query with current offset for resumption
            auto const selectQuery = BuildSelectQueryWithOffset(
                formatter, conn.ServerType(), table.schema, tableName, table.columns, table.primaryKeys, processedRows);
            stmt.ExecuteDirect(selectQuery);

            while (stmt.FetchRow())
            {
                if constexpr (DebugBackupWorker)
                    std::println(stderr, "DEBUG: FetchRow returned true for {}", tableName);
                row.clear();
                auto const cols = static_cast<SQLUSMALLINT>(columnCount);
                for (SQLUSMALLINT i = 1; i <= cols; ++i)
                {
                    auto const& colDef = table.columns[i - 1];
                    using namespace SqlColumnTypeDefinitions;

                    try
                    {
                        if constexpr (DebugBackupWorker)
                            std::println(
                                stderr, "DEBUG: Processing col {} ({}) type index: {}", i, colDef.name, colDef.type.index());
                        if (std::holds_alternative<Binary>(colDef.type) || std::holds_alternative<VarBinary>(colDef.type))
                        {
                            try
                            {
                                auto valOpt = stmt.GetNullableColumn<SqlDynamicBinary<MaxBinaryLobBufferSize>>(i);
                                if (valOpt)
                                {
                                    row.emplace_back(std::vector<uint8_t>(valOpt->data(), valOpt->data() + valOpt->size()));
                                }
                                else
                                {
                                    row.emplace_back(std::monostate {});
                                }
                            }
                            catch (std::exception const& e)
                            {
                                if constexpr (DebugBackupWorker)
                                    std::println(
                                        stderr, "DEBUG: Exception in Binary col {} ({}): {}", i, colDef.name, e.what());
                                throw;
                            }
                        }
                        else if (std::holds_alternative<Bool>(colDef.type))
                        {
                            auto valOpt = stmt.GetNullableColumn<bool>(i);
                            if (valOpt)
                                row.emplace_back(*valOpt);
                            else
                                row.emplace_back(std::monostate {});
                        }
                        else if (std::holds_alternative<Integer>(colDef.type) || std::holds_alternative<Bigint>(colDef.type)
                                 || std::holds_alternative<Smallint>(colDef.type)
                                 || std::holds_alternative<Tinyint>(colDef.type))
                        {
                            auto valOpt = stmt.GetNullableColumn<int64_t>(i);
                            if (valOpt)
                                row.emplace_back(*valOpt);
                            else
                                row.emplace_back(std::monostate {});
                        }
                        else if (std::holds_alternative<Real>(colDef.type))
                        {
                            auto valOpt = stmt.GetNullableColumn<double>(i);
                            if (valOpt)
                                row.emplace_back(*valOpt);
                            else
                                row.emplace_back(std::monostate {});
                        }
                        else if (std::holds_alternative<NVarchar>(colDef.type) || std::holds_alternative<NChar>(colDef.type))
                        {
                            auto valOpt = stmt.GetNullableColumn<std::u16string>(i);
                            if (valOpt)
                            {
                                auto u8 = ToUtf8(*valOpt);
                                row.emplace_back(std::string(reinterpret_cast<char const*>(u8.data()), u8.size()));
                            }
                            else
                                row.emplace_back(std::monostate {});
                        }
                        else if (std::holds_alternative<Guid>(colDef.type))
                        {
                            // Read GUID columns using SqlGuid for proper ODBC binding, then convert to string
                            auto valOpt = stmt.GetNullableColumn<SqlGuid>(i);
                            if (valOpt)
                                row.emplace_back(to_string(*valOpt));
                            else
                                row.emplace_back(std::monostate {});
                        }
                        else if (std::holds_alternative<Decimal>(colDef.type))
                        {
                            // Read Decimal as string directly to preserve full precision.
                            // Using double would lose precision for values exceeding ~15-17 significant digits,
                            // which is problematic for DECIMAL(38,10) and similar high-precision types.
                            auto valOpt = stmt.GetNullableColumn<std::string>(i);
                            if (valOpt)
                                row.emplace_back(*valOpt);
                            else
                                row.emplace_back(std::monostate {});
                        }
                        else if (std::holds_alternative<Date>(colDef.type))
                        {
                            // Read Date using native type to avoid ODBC driver conversion issues.
                            auto valOpt = stmt.GetNullableColumn<SqlDate>(i);
                            if (valOpt)
                                row.emplace_back(std::format("{}", *valOpt));
                            else
                                row.emplace_back(std::monostate {});
                        }
                        else if (std::holds_alternative<Time>(colDef.type))
                        {
                            // PostgreSQL and MS SQL Server: Read TIME as string to preserve fractional seconds.
                            // Standard ODBC SQL_TIME_STRUCT (used by SqlTime) doesn't support fractional seconds.
                            // MS SQL Server's TIME type supports up to 100ns precision, but SQL_C_TYPE_TIME loses it.
                            // PostgreSQL's TIME supports microsecond precision.
                            if (conn.ServerType() == SqlServerType::POSTGRESQL
                                || conn.ServerType() == SqlServerType::MICROSOFT_SQL)
                            {
                                auto valOpt = stmt.GetNullableColumn<std::string>(i);
                                if (valOpt)
                                    row.emplace_back(*valOpt);
                                else
                                    row.emplace_back(std::monostate {});
                            }
                            else
                            {
                                // Read Time using native type for other databases (e.g., SQLite).
                                auto valOpt = stmt.GetNullableColumn<SqlTime>(i);
                                if (valOpt)
                                    row.emplace_back(std::format("{}", *valOpt));
                                else
                                    row.emplace_back(std::monostate {});
                            }
                        }
                        else if (std::holds_alternative<DateTime>(colDef.type)
                                 || std::holds_alternative<Timestamp>(colDef.type))
                        {
                            // Read DateTime/Timestamp using native type to avoid MS SQL Server ODBC driver
                            // issues with SQL_TYPE_TIMESTAMP to SQL_C_CHAR conversion (error 22003).
                            auto valOpt = stmt.GetNullableColumn<SqlDateTime>(i);
                            if (valOpt)
                                row.emplace_back(std::format("{}", *valOpt));
                            else
                                row.emplace_back(std::monostate {});
                        }
                        else if (std::holds_alternative<Varchar>(colDef.type) || std::holds_alternative<Char>(colDef.type)
                                 || std::holds_alternative<Text>(colDef.type))
                        {
                            // Read text/string types as strings directly.
                            // Note: We must not use SqlDynamicBinary for text types on PostgreSQL as its ODBC driver
                            // does not support reading TEXT as SQL_C_BINARY.
                            auto valOpt = stmt.GetNullableColumn<std::string>(i);
                            if (valOpt)
                                row.emplace_back(*valOpt);
                            else
                                row.emplace_back(std::monostate {});
                        }
                        else
                        {
                            // Fallback for any unhandled types - try reading as string
                            auto valOpt = stmt.GetNullableColumn<std::string>(i);
                            if (valOpt)
                                row.emplace_back(*valOpt);
                            else
                                row.emplace_back(std::monostate {});
                        }
                    }
                    catch (std::exception const& e)
                    {
                        if constexpr (DebugBackupWorker)
                            std::println(stderr,
                                         "DEBUG: Exception processing row for table {} col {} ({}): {}",
                                         tableName,
                                         i,
                                         colDef.name,
                                         e.what());
                        throw;
                    }
                } // End for

                try
                {
                    writer->WriteRow(row);
                }
                catch (std::exception const& e)
                {
                    if constexpr (DebugBackupWorker)
                        std::println(stderr, "DEBUG: Exception in WriteRow for table {}: {}", tableName, e.what());
                    throw;
                }
                processedRows++;

                // Report items processed for rate calculation
                ctx.progress.OnItemsProcessed(1);

                if (writer->IsChunkFull())
                {
                    flushChunk();
                    ctx.progress.Update({ .state = Progress::State::InProgress,
                                          .tableName = tableName,
                                          .currentRows = processedRows,
                                          .totalRows = totalRows,
                                          .message = "Writing chunk..."s });
                }
            }

            // Successfully completed - exit retry loop
            break;
        }
        catch (SqlException const& e)
        {
            if (!IsTransientError(e.info()) || retryCount >= ctx.retrySettings.maxRetries)
            {
                if constexpr (DebugBackupWorker)
                    std::println(stderr, "DEBUG: Exception in FetchRow loop for table {}: {}", tableName, e.what());
                throw;
            }

            ++retryCount;
            flushChunk(); // Save completed rows before retry

            ctx.progress.Update({ .state = Progress::State::Warning,
                                  .tableName = tableName,
                                  .currentRows = processedRows,
                                  .totalRows = totalRows,
                                  .message = std::format("Transient error, retry {}/{}, resuming from row {}: {}",
                                                         retryCount,
                                                         ctx.retrySettings.maxRetries,
                                                         processedRows,
                                                         e.what()) });

            // Reconnect and resume from last successful row
            conn.Close();
            std::this_thread::sleep_for(CalculateRetryDelay(retryCount - 1, ctx.retrySettings));

            if (!ConnectWithRetry(conn, ctx.connectionString, ctx.retrySettings, ctx.progress, tableName))
            {
                ctx.progress.Update({ .state = Progress::State::Error,
                                      .tableName = tableName,
                                      .currentRows = processedRows,
                                      .totalRows = totalRows,
                                      .message = "Failed to reconnect after transient error" });
                return;
            }
            stmt = SqlStatement { conn };
            // Loop will rebuild query with new offset
        }
        catch (std::exception const& e)
        {
            if constexpr (DebugBackupWorker)
                std::println(stderr, "DEBUG: Exception in FetchRow loop for table {}: {}", tableName, e.what());
            throw;
        }
        catch (...)
        {
            if constexpr (DebugBackupWorker)
                std::println(stderr, "DEBUG: Unknown exception in FetchRow loop for table {}", tableName);
            throw;
        }
    }

    try
    {
        flushChunk(); // Final flush
        ctx.progress.Update({ .state = Progress::State::Finished,
                              .tableName = tableName,
                              .currentRows = processedRows,
                              .totalRows = totalRows,
                              .message = "Finished table backup"s });
    }
    catch (std::exception const& e)
    {
        if constexpr (DebugBackupWorker)
            std::println(stderr, "DEBUG: Exception in Final FlushChunk for table {}: {}", tableName, e.what());
        throw;
    }
}

void BackupWorker(ThreadSafeQueue<SqlSchema::Table>& tableQueue, BackupContext ctx, SqlConnection& conn)
{
    try
    {
        SqlSchema::Table table;
        while (tableQueue.WaitAndPop(table))
        {
            try
            {
                // ProcessTableBackup takes ctx by reference, so we can pass our local copy.
                ProcessTableBackup(ctx, conn, table);
            }
            catch (std::exception const& e)
            {
                ctx.progress.Update({ .state = Progress::State::Error,
                                      .tableName = table.name,
                                      .currentRows = 0,
                                      .totalRows = 0,
                                      .message = std::string("Backup failed: ") + e.what() });
            }
        }
    }
    catch (std::exception const& e)
    {
        ctx.progress.Update({ .state = Progress::State::Error,
                              .tableName = "Unknown",
                              .currentRows = 0,
                              .totalRows = 0,
                              .message = "Worker Fatal: "s + e.what() });
    }
    catch (...)
    {
        ctx.progress.Update({ .state = Progress::State::Error,
                              .tableName = "Unknown",
                              .currentRows = 0,
                              .totalRows = 0,
                              .message = "Worker Fatal: Unknown exception" });
    }
}

} // namespace Lightweight::SqlBackup::detail
