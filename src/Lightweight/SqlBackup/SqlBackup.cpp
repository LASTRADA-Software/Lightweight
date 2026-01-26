// SPDX-License-Identifier: Apache-2.0

#include "../DataBinder/SqlDate.hpp"
#include "../DataBinder/SqlDateTime.hpp"
#include "../DataBinder/SqlGuid.hpp"
#include "../DataBinder/SqlRawColumn.hpp"
#include "../DataBinder/SqlTime.hpp"
#include "../SqlColumnTypeDefinitions.hpp"
#include "../SqlConnection.hpp"
#include "../SqlError.hpp"
#include "../SqlQuery.hpp"
#include "../SqlQueryFormatter.hpp"
#include "../SqlSchema.hpp"
#include "../SqlStatement.hpp"
#include "../SqlTransaction.hpp"
#include "../ThreadSafeQueue.hpp"
#include "BatchManager.hpp"
#include "MsgPackChunkFormats.hpp"
#include "Sha256.hpp"
#include "SqlBackup.hpp"
#include "SqlBackupFormats.hpp"
#include "TableFilter.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <deque>
#include <format>
#include <limits>
// #define DEBUG_BACKUP_WORKER
#ifdef DEBUG_BACKUP_WORKER
    #include <iostream>
#endif
#include <mutex>
#include <set>
#include <sstream>
#include <thread>
#include <variant>
#include <vector>

#if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wnullability-extension"
#endif
#include <zip.h>
#if defined(__clang__)
    #pragma clang diagnostic pop
#endif

#include <nlohmann/json.hpp>

using namespace std::string_literals;

namespace Lightweight::SqlBackup
{

bool IsCompressionMethodSupported(CompressionMethod method) noexcept
{
    // Check both compression (1) and decompression (0) support
    return zip_compression_method_supported(static_cast<zip_int32_t>(method), 1) != 0
           && zip_compression_method_supported(static_cast<zip_int32_t>(method), 0) != 0;
}

std::vector<CompressionMethod> GetSupportedCompressionMethods() noexcept
{
    std::vector<CompressionMethod> methods;
    constexpr std::array allMethods = {
        CompressionMethod::Store, CompressionMethod::Deflate, CompressionMethod::Bzip2,
        CompressionMethod::Lzma,  CompressionMethod::Zstd,    CompressionMethod::Xz,
    };

    for (auto method: allMethods)
    {
        if (IsCompressionMethodSupported(method))
            methods.push_back(method);
    }
    return methods;
}

std::string_view CompressionMethodName(CompressionMethod method) noexcept
{
    switch (method)
    {
        case CompressionMethod::Store:
            return "store";
        case CompressionMethod::Deflate:
            return "deflate";
        case CompressionMethod::Bzip2:
            return "bzip2";
        case CompressionMethod::Lzma:
            return "lzma";
        case CompressionMethod::Zstd:
            return "zstd";
        case CompressionMethod::Xz:
            return "xz";
    }
    return "unknown"; // LCOV_EXCL_LINE - unreachable default case
}

namespace
{

    /// Maximum declared buffer size for binary LOB columns during backup.
    /// The actual data can grow beyond this via automatic ODBC buffer resizing.
    /// Set to 16MB to handle typical BLOB/VARBINARY(MAX) columns.
    constexpr size_t MaxBinaryLobBufferSize = 16 * 1024 * 1024;

    /// Determines if the given SQL error is a transient error that can be retried.
    ///
    /// Transient errors include:
    /// - Connection errors (ODBC class 08)
    /// - Timeout errors (HYT00, HYT01)
    /// - Transaction rollback due to deadlock/serialization (class 40)
    /// - Database locked (common in SQLite)
    bool IsTransientError(SqlErrorInfo const& error)
    {
        std::string_view state = error.sqlState;

        // Connection errors (Class 08)
        if (state.starts_with("08"))
            return true; // 08001, 08S01, 08006, etc.

        // Timeout errors
        if (state == "HYT00" || state == "HYT01")
            return true;

        // Transaction rollback due to deadlock/serialization (Class 40)
        if (state.starts_with("40"))
            return true;

        // Database locked (common in SQLite)
        if (error.message.find("database is locked") != std::string::npos)
            return true;
        if (error.message.find("SQLITE_BUSY") != std::string::npos)
            return true;

        return false;
    }

    /// Calculates the delay for the given retry attempt using exponential backoff.
    std::chrono::milliseconds CalculateRetryDelay(unsigned attempt, RetrySettings const& settings) noexcept
    {
        auto delay = settings.initialDelay;
        for (unsigned i = 0; i < attempt; ++i)
        {
            delay = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::duration<double, std::milli>(static_cast<double>(delay.count()) * settings.backoffMultiplier));
        }
        return std::min(delay, settings.maxDelay);
    }

    /// Retries a function on transient errors with exponential backoff.
    ///
    /// @param func The function to execute.
    /// @param settings Retry configuration.
    /// @param progress Progress manager for reporting retry attempts.
    /// @param operation Name of the operation for progress messages.
    /// @return The result of the function.
    /// @throws SqlException if max retries exceeded or non-transient error occurs.
    template <typename Func>
    auto RetryOnTransientError(Func&& func, // NOLINT(cppcoreguidelines-missing-std-forward)
                               RetrySettings const& settings,
                               ProgressManager& progress,
                               std::string const& operation) -> decltype(func())
    {
        unsigned attempts = 0;

        while (true)
        {
            try
            {
                return func();
            }
            catch (SqlException const& e)
            {
                if (!IsTransientError(e.info()) || attempts >= settings.maxRetries)
                    throw;

                ++attempts;
                progress.Update(
                    { .state = Progress::State::Warning,
                      .tableName = operation,
                      .currentRows = 0,
                      .totalRows = std::nullopt,
                      .message = std::format("Transient error, retry {}/{}: {}", attempts, settings.maxRetries, e.what()) });

                std::this_thread::sleep_for(CalculateRetryDelay(attempts - 1, settings));
            }
        }
    }

    /// Connects to the database with retry logic for transient errors.
    bool ConnectWithRetry(SqlConnection& conn,
                          SqlConnectionString const& connectionString,
                          RetrySettings const& settings,
                          ProgressManager& progress,
                          std::string const& operation)
    {
        unsigned attempts = 0;

        while (true)
        {
            if (conn.Connect(connectionString))
                return true;

            auto const& error = conn.LastError();
            if (!IsTransientError(error) || attempts >= settings.maxRetries)
                return false;

            ++attempts;
            progress.Update({ .state = Progress::State::Warning,
                              .tableName = operation,
                              .currentRows = 0,
                              .totalRows = std::nullopt,
                              .message = std::format(
                                  "Connection failed, retry {}/{}: {}", attempts, settings.maxRetries, error.message) });

            std::this_thread::sleep_for(CalculateRetryDelay(attempts - 1, settings));
        }
    }

    std::string CurrentDateTime()
    {
        auto const now = std::chrono::system_clock::now();
        return std::format("{:%FT%TZ}", now);
    }

    struct ZipEntryInfo
    {
        zip_int64_t index {};
        std::string name;
        zip_uint64_t size {};
        bool valid = false;
    };

    template <typename Container>
    Container ReadZipEntry(zip_t* zip, zip_int64_t index, zip_uint64_t size)
    {
        zip_file_t* file = zip_fopen_index(zip, static_cast<zip_uint64_t>(index), 0);
        if (!file)
            return {}; // LCOV_EXCL_LINE - zip file open failure

        Container data;
        data.resize(size);

        zip_int64_t bytesRead = zip_fread(file, data.data(), size);
        zip_fclose(file);

        if (bytesRead < 0 || std::cmp_not_equal(bytesRead, size))
            return {}; // LCOV_EXCL_LINE - zip file read failure

        return data;
    }

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

    std::string FormatTableName(std::string_view schema, std::string_view table)
    {
        if (schema.empty())
            return std::format(R"("{}")", table);
        return std::format(R"("{}"."{}")", schema, table);
    }

    /// Builds a SELECT query with ORDER BY for deterministic results.
    ///
    /// This is critical for:
    /// 1. MS SQL Server which requires ORDER BY when using OFFSET
    /// 2. Deterministic results for resumption after transient errors
    ///
    /// For MS SQL Server, DECIMAL columns are wrapped in CONVERT(VARCHAR, ...) to preserve
    /// full precision, as the ODBC driver loses precision when reading DECIMAL as SQL_C_CHAR.
    // NOLINTNEXTLINE(readability-function-cognitive-complexity)
    std::string BuildSelectQueryWithOffset(SqlQueryFormatter const& formatter,
                                           SqlServerType serverType,
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
        {
            // Build raw SQL query for MSSQL with CONVERT for DECIMAL columns
            std::stringstream sql;
            sql << "SELECT ";

            for (size_t i = 0; i < columns.size(); ++i)
            {
                if (i > 0)
                    sql << ", ";

                auto const& col = columns[i];
                if (std::holds_alternative<Decimal>(col.type))
                {
                    // MSSQL ODBC driver loses precision when reading DECIMAL as string directly.
                    // Using CONVERT(VARCHAR, ...) forces SQL Server to convert with full precision.
                    auto const& dec = std::get<Decimal>(col.type);
                    // VARCHAR size should accommodate precision + scale + decimal point + sign
                    auto const varcharSize = dec.precision + 3;
                    sql << "CONVERT(VARCHAR(" << varcharSize << "), [" << col.name << "])";
                }
                else
                {
                    sql << "[" << col.name << "]";
                }
            }

            sql << " FROM [" << tableName << "]";

            // ORDER BY is required for OFFSET
            sql << " ORDER BY ";
            if (!primaryKeys.empty())
            {
                for (size_t i = 0; i < primaryKeys.size(); ++i)
                {
                    if (i > 0)
                        sql << ", ";
                    sql << "[" << primaryKeys[i] << "] ASC";
                }
            }
            else
            {
                sql << "[" << columns[0].name << "] ASC";
            }

            if (offset > 0)
            {
                sql << " OFFSET " << offset << " ROWS";
            }

            return sql.str();
        }

        // Use Query Builder for non-MSSQL or when no DECIMAL columns
        auto query = SqlQueryBuilder(formatter).FromTable(tableName).Select();

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
    void ProcessTableBackup(BackupContext& ctx, SqlSchema::Table const& table)
    {
        std::string const& tableName = table.name;
        SqlConnection conn;
        if (!ConnectWithRetry(conn, ctx.connectionString, ctx.retrySettings, ctx.progress, tableName))
        {
            ctx.progress.Update({ .state = Progress::State::Error,
                                  .tableName = tableName,
                                  .currentRows = 0,
                                  .totalRows = 0,
                                  .message = "Failed to connect to database for backup: " + conn.LastError().message });
            return;
        }

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

        auto const formattedTableName = FormatTableName(ctx.schema, tableName);
        auto const totalRows = static_cast<size_t>(
            stmt.ExecuteDirectScalar<int64_t>(std::format("SELECT COUNT(*) FROM {}", formattedTableName)).value_or(0));

        ctx.progress.Update({ .state = Progress::State::Started,
                              .tableName = tableName,
                              .currentRows = 0,
                              .totalRows = totalRows,
                              .message = "Started backup"s });

#ifdef DEBUG_BACKUP_WORKER
        std::cerr << "DEBUG: ProcessTableBackup: Starting backup for " << tableName << " (" << totalRows << " rows)\n";
        std::cerr << "DEBUG: Creating writer...\n";
#endif
        auto writer = CreateMsgPackChunkWriter(ctx.backupSettings.chunkSizeBytes);
#ifdef DEBUG_BACKUP_WORKER
        std::cerr << "DEBUG: Writer created.\n";
#endif

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

            auto lock = std::scoped_lock(ctx.zipMutex);

            // Store checksum
            if (ctx.checksums && ctx.checksumMutex)
            {
                auto checksumLock = std::scoped_lock(*ctx.checksumMutex);
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

#ifdef DEBUG_BACKUP_WORKER
        std::cerr << "DEBUG: Reserving row...\n";
#endif
        std::vector<BackupValue> row;
        row.reserve(table.columns.size());
#ifdef DEBUG_BACKUP_WORKER
        std::cerr << "DEBUG: Row reserved. Entering FetchRow loop...\n";
#endif

        auto const columnCount = table.columns.size();

        // Retry loop with offset-based resumption
        while (retryCount <= ctx.retrySettings.maxRetries)
        {
            try
            {
                // Build query with current offset for resumption
                auto const selectQuery = BuildSelectQueryWithOffset(
                    formatter, conn.ServerType(), tableName, table.columns, table.primaryKeys, processedRows);
                stmt.ExecuteDirect(selectQuery);

                while (stmt.FetchRow())
                {
#ifdef DEBUG_BACKUP_WORKER
                    std::cerr << "DEBUG: FetchRow returned true for " << tableName << "\n";
#endif
                    row.clear();
                    auto const cols = static_cast<SQLUSMALLINT>(columnCount);
                    for (SQLUSMALLINT i = 1; i <= cols; ++i)
                    {
                        auto const& colDef = table.columns[i - 1];
                        using namespace SqlColumnTypeDefinitions;

                        try
                        {
#ifdef DEBUG_BACKUP_WORKER
                            std::cerr << "DEBUG: Processing col " << i << " (" << colDef.name
                                      << ") type index: " << colDef.type.index() << "\n";
#endif
                            if (std::holds_alternative<Binary>(colDef.type)
                                || std::holds_alternative<VarBinary>(colDef.type))
                            {
                                try
                                {
                                    auto valOpt = stmt.GetNullableColumn<SqlDynamicBinary<MaxBinaryLobBufferSize>>(i);
                                    if (valOpt)
                                    {
                                        row.emplace_back(
                                            std::vector<uint8_t>(valOpt->data(), valOpt->data() + valOpt->size()));
                                    }
                                    else
                                    {
                                        row.emplace_back(std::monostate {});
                                    }
                                }
                                catch ([[maybe_unused]] std::exception const& e)
                                {
#ifdef DEBUG_BACKUP_WORKER
                                    std::cerr << "DEBUG: Exception in Binary col " << i << " (" << colDef.name
                                              << "): " << e.what() << "\n";
#endif
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
                            else if (std::holds_alternative<Integer>(colDef.type)
                                     || std::holds_alternative<Bigint>(colDef.type)
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
                            else if (std::holds_alternative<NVarchar>(colDef.type)
                                     || std::holds_alternative<NChar>(colDef.type))
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
                            else if (std::holds_alternative<Varchar>(colDef.type)
                                     || std::holds_alternative<Char>(colDef.type)
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
                        catch ([[maybe_unused]] std::exception const& e)
                        {
#ifdef DEBUG_BACKUP_WORKER
                            std::cerr << "DEBUG: Exception processing row for table " << tableName << " col " << i << " ("
                                      << colDef.name << "): " << e.what() << "\n";
#endif
                            throw;
                        }
                    } // End for

                    try
                    {
                        writer->WriteRow(row);
                    }
                    catch ([[maybe_unused]] std::exception const& e)
                    {
#ifdef DEBUG_BACKUP_WORKER
                        std::cerr << "DEBUG: Exception in WriteRow for table " << tableName << ": " << e.what() << "\n";
#endif
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
#ifdef DEBUG_BACKUP_WORKER
                    std::cerr << "DEBUG: Exception in FetchRow loop for table " << tableName << ": " << e.what() << "\n";
#endif
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
            catch ([[maybe_unused]] std::exception const& e)
            {
#ifdef DEBUG_BACKUP_WORKER
                std::cerr << "DEBUG: Exception in FetchRow loop for table " << tableName << ": " << e.what() << "\n";
#endif
                throw;
            }
            catch (...)
            {
#ifdef DEBUG_BACKUP_WORKER
                std::cerr << "DEBUG: Unknown exception in FetchRow loop for table " << tableName << "\n";
#endif
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
        catch ([[maybe_unused]] std::exception const& e)
        {
#ifdef DEBUG_BACKUP_WORKER
            std::cerr << "DEBUG: Exception in Final FlushChunk for table " << tableName << ": " << e.what() << "\n";
#endif
            throw;
        }
    }

    bool DropTableIfExists(SqlConnection& conn,
                           std::string const& schema,
                           std::string const& tableName,
                           ProgressManager& progress)
    {
        std::string const formattedTableName = FormatTableName(schema, tableName);
        try
        {
            auto const& formatter = conn.QueryFormatter();
            // Use cascade=true to drop FK constraints referencing this table first.
            // This handles MS SQL Server which requires explicit FK constraint drops,
            // PostgreSQL which uses CASCADE syntax, and SQLite which ignores cascade.
            auto dropSqls = formatter.DropTable(schema, tableName, /*ifExists=*/true, /*cascade=*/true);
            for (auto const& sql: dropSqls)
                SqlStatement { conn }.ExecuteDirect(sql);
            return true;
        }
        catch (std::exception const& e)
        {
            progress.Update({ .state = Progress::State::Error,
                              .tableName = tableName,
                              .currentRows = 0,
                              .totalRows = 0,
                              .message = std::format(
                                  "DropTable failed: {} SQL: DROP TABLE IF EXISTS {}", e.what(), formattedTableName) });
            return false;
        }
    }

    /// Backup worker that processes tables from a thread-safe queue.
    ///
    /// Workers block on the queue until a table is available or the queue is finished.
    /// This allows workers to start immediately and begin processing tables as soon
    /// as the schema producer pushes them.
    void BackupWorker(ThreadSafeQueue<SqlSchema::Table>& tableQueue, BackupContext ctx)
    {
        try
        {
            SqlSchema::Table table;
            while (tableQueue.WaitAndPop(table))
            {
                try
                {
                    // ProcessTableBackup takes ctx by reference, so we can pass our local copy.
                    ProcessTableBackup(ctx, table);
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

    /// Context for the row counter thread that runs in parallel with backup workers.
    struct RowCounterContext
    {
        SqlConnectionString const& connectionString;
        std::string const& schema;
        std::vector<SqlSchema::Table>& completedTables;
        std::mutex& completedTablesMutex;
        std::atomic<bool>& schemaScanningComplete;
        std::mutex& schemaScanningDone_mutex;
        std::condition_variable& schemaScanningDone_cv;
        ProgressManager& progress;
    };

    /// Row counter thread function that counts rows for ETA calculation in parallel with backup.
    ///
    /// This thread processes tables as they become available in completedTables,
    /// running COUNT(*) queries and reporting totals to the progress manager.
    /// Runs in parallel with backup workers, but waits for schema scanning to complete first
    /// to avoid database contention during schema metadata queries.
    void RowCounterThread(RowCounterContext ctx)
    {
        try
        {
            // Wait until schema scanning is complete before starting to count.
            // This avoids database contention during schema metadata queries.
            {
                std::unique_lock lock(ctx.schemaScanningDone_mutex);
                ctx.schemaScanningDone_cv.wait(lock, [&ctx] { return ctx.schemaScanningComplete.load(); });
            }

            auto counterConn = SqlConnection(ctx.connectionString);
            auto counterStmt = SqlStatement { counterConn };

            // Process tables as they become available
            size_t processedCount = 0;
            while (true)
            {
                SqlSchema::Table tableToCount;
                bool hasTable = false;

                {
                    std::scoped_lock lock(ctx.completedTablesMutex);
                    if (processedCount < ctx.completedTables.size())
                    {
                        tableToCount = ctx.completedTables[processedCount];
                        hasTable = true;
                        processedCount++;
                    }
                }

                if (hasTable)
                {
                    auto const formattedTableName = FormatTableName(ctx.schema, tableToCount.name);
                    auto const rowCount = static_cast<size_t>(
                        counterStmt.ExecuteDirectScalar<int64_t>(std::format("SELECT COUNT(*) FROM {}", formattedTableName))
                            .value_or(0));
                    ctx.progress.AddTotalItems(rowCount);
                }
                else
                {
                    // Check if schema scanning is done
                    std::scoped_lock lock(ctx.completedTablesMutex);
                    if (ctx.schemaScanningComplete.load() && processedCount >= ctx.completedTables.size())
                        break;
                }

                // Brief sleep to avoid busy-waiting when waiting for new tables
                if (!hasTable)
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
        catch (std::exception const& e)
        {
            // Log error but don't fail backup - ETA is non-critical
            ctx.progress.Update({ .state = Progress::State::Warning,
                                  .tableName = "Counter",
                                  .currentRows = 0,
                                  .totalRows = std::nullopt,
                                  .message = std::format("Row counting failed: {}", e.what()) });
        }
    }

    // BatchManager and BatchColumn moved to BatchManager.hpp/cpp

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
        std::map<std::string, std::string> const* checksums; // entryName -> expected SHA-256 hash (optional)
        RetrySettings const& retrySettings;
    };

    // NOLINTNEXTLINE(readability-function-cognitive-complexity)
    void RestoreWorker(RestoreContext ctx)
    {
        constexpr size_t batchCapacity = 4000;

        try
        {
            SqlConnection workerConn;
            if (!ConnectWithRetry(workerConn, ctx.connectionString, ctx.retrySettings, ctx.progress, "RestoreWorker"))
            {
                ctx.progress.Update({ .state = Progress::State::Error,
                                      .tableName = "",
                                      .currentRows = 0,
                                      .totalRows = std::nullopt,
                                      .message = "Restore worker failed to connect: " + workerConn.LastError().message });
                return;
            }

            // SQLite optimization: Turn off synchronization for faster restore
            if (workerConn.ServerType() == SqlServerType::SQLITE)
            {
                SqlStatement { workerConn }.ExecuteDirect("PRAGMA synchronous = OFF");
                SqlStatement { workerConn }.ExecuteDirect("PRAGMA journal_mode = WAL");
                SqlStatement { workerConn }.ExecuteDirect("PRAGMA foreign_keys = OFF");
            }

            while (true)
            {
                ZipEntryInfo entryInfo;
                {
                    auto lock = std::scoped_lock(ctx.queueMutex);
                    if (ctx.dataQueue.empty())
                        return;
                    entryInfo = ctx.dataQueue.front();
                    ctx.dataQueue.pop_front();
                }

                std::vector<uint8_t> content;

                {
                    auto lock = std::scoped_lock(ctx.fileMutex);
                    if (!entryInfo.valid)
                        continue;
                    content = ReadZipEntry<std::vector<uint8_t>>(ctx.zip, entryInfo.index, entryInfo.size);
                }

                // Verify checksum if available
                if (ctx.checksums)
                {
                    auto it = ctx.checksums->find(entryInfo.name);
                    if (it != ctx.checksums->end())
                    {
                        std::string const actualHash = Sha256::Hash(content.data(), content.size());
                        if (actualHash != it->second)
                        {
                            ctx.progress.Update({ .state = Progress::State::Error,
                                                  .tableName = entryInfo.name,
                                                  .currentRows = 0,
                                                  .totalRows = std::nullopt,
                                                  .message = std::format("Checksum mismatch for {}: expected {}, got {}",
                                                                         entryInfo.name,
                                                                         it->second,
                                                                         actualHash) });
                            continue;
                        }
                    }
                }

                // Parse standard chunk filenames: table_name.chunk_index.msgpack
                // Backup writes: data/table_name/chunk_ID.extension
                std::string const path = entryInfo.name;
                auto const firstSlash = path.find('/');
                if (firstSlash == std::string::npos)
                    continue;

                auto const secondSlash = path.find('/', firstSlash + 1);
                if (secondSlash == std::string::npos)
                    continue;
                std::string const tableName = path.substr(firstSlash + 1, secondSlash - firstSlash - 1);
                if (!ctx.tableMap.contains(tableName))
                    continue;
                auto const& tableInfo = ctx.tableMap.at(tableName);
                std::string const& fields = tableInfo.fields;

                size_t const currentTotal1 = ctx.tableProgress.at(tableName)->load();
                ctx.progress.Update({ .state = Progress::State::InProgress,
                                      .tableName = tableName,
                                      .currentRows = currentTotal1,
                                      .totalRows = tableInfo.rowCount,
                                      .message = "Restoring chunk " + path });

                // Retry loop for chunk processing
                unsigned retryCount = 0;
                while (retryCount <= ctx.retrySettings.maxRetries)
                {
                    try
                    {
                        std::stringstream ss(std::string(content.begin(), content.end()));

                        bool const isMsSql = workerConn.ServerType() == SqlServerType::MICROSOFT_SQL;
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
                                SqlStatement { workerConn }.ExecuteDirect(
                                    std::format("SET IDENTITY_INSERT {} ON", identityTable));
                            }
                        }

                        // Detect format
                        auto reader = CreateMsgPackChunkReader(ss);

                        {
                            auto const& formatter = workerConn.QueryFormatter();
                            SqlStatement stmt { workerConn };
                            std::string placeholders;
                            for (size_t i = 0; i < tableInfo.columns.size(); ++i)
                            {
                                if (i > 0)
                                    placeholders += ", ";

                                placeholders += "?";
                            }
                            stmt.Prepare(formatter.Insert(ctx.schema, tableName, fields, placeholders));

                            detail::BatchManager batchManager(
                                [&](std::vector<SqlRawColumn> const& cols, size_t rows) { stmt.ExecuteBatch(cols, rows); },
                                tableInfo.columns,
                                batchCapacity,
                                workerConn.ServerType());

                            // Use a transaction for the entire chunk to improve performance significantly.
                            // We default to ROLLBACK on destruction to ensure atomic application of the chunk.
                            SqlTransaction transaction(workerConn, SqlTransactionMode::ROLLBACK);

                            ColumnBatch batch;
                            while (reader->ReadBatch(batch))
                            {
                                if (batch.rowCount == 0)
                                    continue;

                                if (batch.columns.size() != tableInfo.columns.size())
                                {
                                    // TODO: error out or skip this batch?
                                    continue;
                                }

                                batchManager.PushBatch(batch);

                                auto& atomicCounter = *ctx.tableProgress.at(tableName);
                                size_t const previousTotal = atomicCounter.fetch_add(batch.rowCount);
                                size_t const currentTotal = previousTotal + batch.rowCount;

                                ctx.progress.Update({ .state = Progress::State::InProgress,
                                                      .tableName = tableName,
                                                      .currentRows = currentTotal,
                                                      .totalRows = tableInfo.rowCount,
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
                                SqlStatement { workerConn }.ExecuteDirect(
                                    std::format("SET IDENTITY_INSERT {} OFF", identityTable));
                            }
                            catch (...) // NOLINT(bugprone-empty-catch)
                            {
                                // Best-effort cleanup - failure doesn't affect restore correctness
                            }
                        }

                        // Success - exit retry loop
                        break;
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
                            break; // Non-transient or max retries exceeded
                        }

                        ++retryCount;
                        ctx.progress.Update(
                            { .state = Progress::State::Warning,
                              .tableName = tableName,
                              .currentRows = 0,
                              .totalRows = tableInfo.rowCount,
                              .message = std::format(
                                  "Transient error, retry {}/{}: {}", retryCount, ctx.retrySettings.maxRetries, e.what()) });

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
                            break;
                        }

                        // Re-apply SQLite optimizations after reconnect
                        if (workerConn.ServerType() == SqlServerType::SQLITE)
                        {
                            SqlStatement { workerConn }.ExecuteDirect("PRAGMA synchronous = OFF");
                            SqlStatement { workerConn }.ExecuteDirect("PRAGMA journal_mode = WAL");
                            SqlStatement { workerConn }.ExecuteDirect("PRAGMA foreign_keys = OFF");
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
                        break;
                    }
                }

                auto& atomicCounter = *ctx.tableProgress.at(tableName);
                size_t currentTotal = atomicCounter.load();
                if (currentTotal >= tableInfo.rowCount)
                {
                    ctx.progress.Update({ .state = Progress::State::Finished,
                                          .tableName = tableName,
                                          .currentRows = currentTotal,
                                          .totalRows = tableInfo.rowCount,
                                          .message = "Restore complete" });
                }
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
                    std::string const formattedTableName = FormatTableName(schema, tableName);
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
    // NOLINTNEXTLINE(readability-function-cognitive-complexity)
    std::set<std::string> RecreateDatabaseSchema(SqlConnectionString const& connectionString,
                                                 std::string const& schema,
                                                 std::map<std::string, TableInfo> const& tableMap,
                                                 ProgressManager& progress)
    {
        std::set<std::string> createdTables;

        SqlConnection conn;
        if (!conn.Connect(connectionString))
        {
            progress.Update({ .state = Progress::State::Error,
                              .tableName = "",
                              .currentRows = 0,
                              .totalRows = std::nullopt,
                              .message = "Failed to connect for schema recreation: " + conn.LastError().message });
            return createdTables;
        }
        SqlStatement stmt { conn };
        bool const isSQLite = conn.ServerType() == SqlServerType::SQLITE;

        // Speed up restore (SQLite only)
        if (isSQLite)
        {
            stmt.ExecuteDirect("PRAGMA synchronous = OFF");
            stmt.ExecuteDirect("PRAGMA journal_mode = WAL");
            stmt.ExecuteDirect("PRAGMA foreign_keys = OFF"); // Disable FKs during restore
        }

        auto const& formatter = conn.QueryFormatter();

        // For SQLite, we need to create tables in FK dependency order (referenced tables first)
        // since SQLite validates FK references on CREATE TABLE even with foreign_keys = OFF.
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
                bool progress_made = false;
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
                        progress_made = true;
                    }
                    else
                    {
                        ++it;
                    }
                }

                if (!progress_made && !remaining.empty())
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

                auto createSqls = formatter.CreateTable(schema, tableName, info.columns, fksToCreate);
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
} // namespace

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
std::string CreateMetadata(SqlConnectionString const& connectionString,
                           SqlSchema::TableList const& tables,
                           std::string const& schema)
{
    nlohmann::json metadata;
    metadata["format_version"] = "1.0";
    metadata["creation_time"] = CurrentDateTime();
    metadata["original_connection_string"] = connectionString.value;
    metadata["schema_name"] = schema;
    metadata["schema"] = nlohmann::json::array();

    SqlConnection conn;
    if (!conn.Connect(connectionString))
        throw std::runtime_error("Failed to connect for metadata creation: " + conn.LastError().message);
    SqlStatement stmt { conn };

    // Add server identification information
    nlohmann::json serverInfo;
    serverInfo["name"] = conn.ServerName();
    serverInfo["version"] = conn.ServerVersion();
    serverInfo["driver"] = conn.DriverName();

    // Query full version string using dialect-specific query
    try
    {
        auto const versionQuery = conn.QueryFormatter().QueryServerVersion();
        auto const fullVersion = stmt.ExecuteDirectScalar<std::string>(versionQuery);
        if (fullVersion.has_value())
            serverInfo["full_version"] = fullVersion.value();
    }
    catch (...) // NOLINT(bugprone-empty-catch)
    {
        // Best-effort - if the query fails, we still have the basic server info
    }

    metadata["server"] = serverInfo;

    for (auto const& table: tables)
    {
        nlohmann::json t;
        t["name"] = table.name;

        try
        {
            t["rows"] =
                static_cast<size_t>(stmt.ExecuteDirectScalar<int64_t>(
                                            std::format("SELECT COUNT(*) FROM {}", FormatTableName(schema, table.name)))
                                        .value_or(0));
        }
        catch (...)
        {
            t["rows"] = 0;
        }

        t["columns"] = nlohmann::json::array();
        for (auto const& col: table.columns)
        {
            nlohmann::json c;
            c["name"] = col.name;
            c["is_primary_key"] = col.isPrimaryKey;
            c["is_auto_increment"] = col.isAutoIncrement;
            c["is_nullable"] = col.isNullable;
            c["is_unique"] = col.isUnique;
            if (!col.defaultValue.empty())
                c["default_value"] = col.defaultValue;
            auto const& type = col.type;
            if (std::holds_alternative<SqlColumnTypeDefinitions::Integer>(type))
                c["type"] = "integer";
            else if (std::holds_alternative<SqlColumnTypeDefinitions::Bigint>(type))
                c["type"] = "bigint";
            else if (std::holds_alternative<SqlColumnTypeDefinitions::Smallint>(type))
                c["type"] = "smallint";
            else if (std::holds_alternative<SqlColumnTypeDefinitions::Tinyint>(type))
                c["type"] = "tinyint";
            else if (std::holds_alternative<SqlColumnTypeDefinitions::Real>(type))
            {
                c["type"] = "real";
                c["precision"] = std::get<SqlColumnTypeDefinitions::Real>(type).precision;
            }
            else if (std::holds_alternative<SqlColumnTypeDefinitions::Text>(type))
            {
                c["type"] = "text";
                c["size"] = std::get<SqlColumnTypeDefinitions::Text>(type).size;
            }
            else if (std::holds_alternative<SqlColumnTypeDefinitions::Varchar>(type))
            {
                c["type"] = "varchar";
                c["size"] = std::get<SqlColumnTypeDefinitions::Varchar>(type).size;
            }
            else if (std::holds_alternative<SqlColumnTypeDefinitions::NVarchar>(type))
            {
                c["type"] = "nvarchar";
                c["size"] = std::get<SqlColumnTypeDefinitions::NVarchar>(type).size;
            }
            else if (std::holds_alternative<SqlColumnTypeDefinitions::Char>(type))
            {
                c["type"] = "char";
                c["size"] = std::get<SqlColumnTypeDefinitions::Char>(type).size;
            }
            else if (std::holds_alternative<SqlColumnTypeDefinitions::NChar>(type))
            {
                c["type"] = "nchar";
                c["size"] = std::get<SqlColumnTypeDefinitions::NChar>(type).size;
            }
            else if (std::holds_alternative<SqlColumnTypeDefinitions::Bool>(type))
                c["type"] = "bool";
            else if (std::holds_alternative<SqlColumnTypeDefinitions::Binary>(type))
            {
                c["type"] = "binary";
                c["size"] = std::get<SqlColumnTypeDefinitions::Binary>(type).size;
            }
            else if (std::holds_alternative<SqlColumnTypeDefinitions::VarBinary>(type))
            {
                c["type"] = "varbinary";
                c["size"] = std::get<SqlColumnTypeDefinitions::VarBinary>(type).size;
            }
            else if (std::holds_alternative<SqlColumnTypeDefinitions::Date>(type))
                c["type"] = "date";
            else if (std::holds_alternative<SqlColumnTypeDefinitions::DateTime>(type))
                c["type"] = "datetime";
            else if (std::holds_alternative<SqlColumnTypeDefinitions::Time>(type))
                c["type"] = "time";
            else if (std::holds_alternative<SqlColumnTypeDefinitions::Timestamp>(type))
                c["type"] = "timestamp";
            else if (std::holds_alternative<SqlColumnTypeDefinitions::Guid>(type))
                c["type"] = "guid";
            else if (std::holds_alternative<SqlColumnTypeDefinitions::Decimal>(type))
            {
                c["type"] = "decimal";
                c["precision"] = std::get<SqlColumnTypeDefinitions::Decimal>(type).precision;
                c["scale"] = std::get<SqlColumnTypeDefinitions::Decimal>(type).scale;
            }
            else
                throw std::runtime_error(
                    std::format("Unsupported column type for column '{}' in table '{}'", col.name, table.name));

            t["columns"].push_back(c);
        }

        t["foreign_keys"] = nlohmann::json::array();
        for (auto const& fk: table.foreignKeys)
        {
            nlohmann::json f;
            f["name"] = ""; // FK name not always available/relevant? SqlSchema doesn't seem to have name.
            f["columns"] = fk.foreignKey.columns;
            f["referenced_table"] = fk.primaryKey.table.table;
            f["referenced_columns"] = fk.primaryKey.columns;
            t["foreign_keys"].push_back(f);
        }

        t["indexes"] = nlohmann::json::array();
        for (auto const& idx: table.indexes)
        {
            nlohmann::json indexJson;
            indexJson["name"] = idx.name;
            indexJson["columns"] = idx.columns;
            indexJson["is_unique"] = idx.isUnique;
            t["indexes"].push_back(indexJson);
        }

        t["primary_keys"] = table.primaryKeys;

        metadata["schema"].push_back(t);
    }

    return metadata.dump();
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void Backup(std::filesystem::path const& outputFile,
            SqlConnectionString const& connectionString,
            unsigned concurrency,
            ProgressManager& progress,
            std::string const& schema,
            std::string const& tableFilter,
            RetrySettings const& retrySettings,
            BackupSettings const& backupSettings)
{
    concurrency = std::max(1U, concurrency);

    SqlConnection mainConn { std::nullopt };
    if (!mainConn.Connect(connectionString))
    {
        auto const error = mainConn.LastError();
        throw std::runtime_error(std::format("Failed to connect to database: {}", error.message));
    }

    // Create ZIP archive early so workers can write to it
    int err = 0;
    zip_t* zip = zip_open(outputFile.string().c_str(), ZIP_CREATE | ZIP_TRUNCATE, &err);
    if (!zip)
    {
        zip_error_t zerr;
        zip_error_init_with_code(&zerr, err);
        std::string errMsg = zip_error_strerror(&zerr);
        zip_error_fini(&zerr);

        progress.Update({ .state = Progress::State::Error,
                          .tableName = "Unknown",
                          .currentRows = 0,
                          .totalRows = 0,
                          .message = "Failed to create ZIP archive: " + errMsg });
        return;
    }

    try
    {
        std::mutex zipMutex;
        std::map<std::string, std::string> checksums;
        std::mutex checksumMutex;

        // Thread-safe queue for streaming tables from schema reader to workers
        ThreadSafeQueue<SqlSchema::Table> tableQueue;

        // Storage for completed tables (for metadata creation after workers finish)
        std::vector<SqlSchema::Table> completedTables;
        std::mutex completedTablesMutex;

        // Parse the table filter
        auto const filter = TableFilter::Parse(tableFilter);

        // Track max table name length for progress display
        std::atomic<size_t> maxTableNameLength { 0 };

        BackupContext ctx {
            .zip = zip,
            .zipMutex = zipMutex,
            .progress = progress,
            .connectionString = connectionString,
            .schema = schema,
            .checksums = &checksums,
            .checksumMutex = &checksumMutex,
            .retrySettings = retrySettings,
            .backupSettings = backupSettings,
        };

        // Start data worker threads FIRST - they will wait on the queue
        std::vector<std::thread> workers;
        workers.reserve(concurrency);
        std::generate_n(
            std::back_inserter(workers), concurrency, [&] { return std::thread(BackupWorker, std::ref(tableQueue), ctx); });

        // Flag to signal when schema scanning is complete (for counter thread)
        std::atomic<bool> schemaScanningComplete { false };
        std::mutex schemaScanningDone_mutex;
        std::condition_variable schemaScanningDone_cv;

        // Counter thread: counts rows for ETA calculation in parallel with backup.
        // Waits for schema scanning to complete before starting COUNT(*) queries
        // to avoid database contention during schema metadata queries.
        RowCounterContext counterCtx {
            .connectionString = connectionString,
            .schema = schema,
            .completedTables = completedTables,
            .completedTablesMutex = completedTablesMutex,
            .schemaScanningComplete = schemaScanningComplete,
            .schemaScanningDone_mutex = schemaScanningDone_mutex,
            .schemaScanningDone_cv = schemaScanningDone_cv,
            .progress = progress,
        };
        std::thread counterThread(RowCounterThread, counterCtx);

        // Schema progress callback
        SqlSchema::ReadAllTablesCallback schemaCallback =
            [&progress](std::string_view tableName, size_t const current, size_t const total) {
                progress.Update({ .state = Progress::State::InProgress,
                                  .tableName = "Scanning schema",
                                  .currentRows = current,
                                  .totalRows = total,
                                  .message = std::format("Scanning table {}", tableName) });
            };

        // Table-ready callback: called when each table's schema is complete.
        // Push tables to workers immediately to enable pipelining.
        SqlSchema::TableReadyCallback tableReadyCallback = [&](SqlSchema::Table&& table) {
            // Update max table name length for progress display
            size_t currentMax = maxTableNameLength.load();
            while (table.name.size() > currentMax
                   && !maxTableNameLength.compare_exchange_weak(currentMax, table.name.size()))
            {
            }

            // Store for metadata creation AND for counter thread
            {
                std::scoped_lock lock(completedTablesMutex);
                completedTables.push_back(table); // Copy for metadata & counting
            }

            // Push to worker queue immediately - enables pipelining
            tableQueue.Push(std::move(table));
        };

        // Create table filter predicate to skip reading schema for non-matching tables
        SqlSchema::TableFilterPredicate tableFilterPredicate;
        if (!filter.MatchesAll())
        {
            tableFilterPredicate = [&filter, &schema](std::string_view /*schemaName*/, std::string_view tableName) {
                return filter.Matches(schema, tableName);
            };
        }

        // Run schema scanning - tables are pushed to workers immediately via callback
        // The filter predicate ensures only matching tables have their full schema read
        auto stmt = SqlStatement { mainConn };
        SqlSchema::ReadAllTables(
            stmt, mainConn.DatabaseName(), schema, schemaCallback, tableReadyCallback, tableFilterPredicate);

        progress.Update({ .state = Progress::State::Finished,
                          .tableName = "Scanning schema",
                          .currentRows = completedTables.size(),
                          .totalRows = completedTables.size(),
                          .message = "" });

        progress.SetMaxTableNameLength(maxTableNameLength.load());

        // Signal schema scanning is complete - tables already pushed by callback
        schemaScanningComplete = true;
        {
            std::scoped_lock lock(schemaScanningDone_mutex);
        }
        schemaScanningDone_cv.notify_one();
        tableQueue.MarkFinished();

        // Wait for all workers to complete
        for (auto& t: workers)
            t.join();

        // Wait for counter thread to finish
        counterThread.join();

        // Create metadata.json from completed tables (moved to end since we need full list)
        auto const metadataJson = CreateMetadata(connectionString, completedTables, schema);

        // Add metadata.json to ZIP
        // Use malloc for metadata to ensure consistent memory management with chunks.
        // We transfer ownership to libzip (freep=1).
        // zip_source_buffer takes ownership when freeData=1, and zip_file_add takes ownership of source on success.
        // NOLINTNEXTLINE(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory,clang-analyzer-unix.Malloc)
        void* persistentMeta = std::malloc(metadataJson.size());
        if (!persistentMeta)
            throw std::bad_alloc();
        // NOLINTNEXTLINE(bugprone-not-null-terminated-result)
        std::memcpy(persistentMeta, metadataJson.data(), metadataJson.size());

        zip_source_t* metaSource = zip_source_buffer(zip, persistentMeta, metadataJson.size(), 1);
        if (!metaSource)
        {
            std::free(persistentMeta); // NOLINT(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory)
            throw std::bad_alloc();
        }

        // NOLINTNEXTLINE(clang-analyzer-unix.Malloc) - libzip takes ownership via zip_source_buffer(freeData=1)
        zip_int64_t const metaIndex = zip_file_add(zip, "metadata.json", metaSource, ZIP_FL_OVERWRITE | ZIP_FL_ENC_UTF_8);
        if (metaIndex < 0)
        {
            zip_source_free(metaSource);
            throw std::runtime_error("Failed to add metadata.json to zip");
        }

        // Apply compression to metadata.json
        zip_set_file_compression(zip,
                                 static_cast<zip_uint64_t>(metaIndex),
                                 static_cast<zip_int32_t>(backupSettings.method),
                                 backupSettings.level);

        // Write checksums.json
        {
            nlohmann::json checksumsJson;
            checksumsJson["algorithm"] = "sha256";
            checksumsJson["files"] = nlohmann::json::object();
            for (auto const& [entryName, hash]: checksums)
                checksumsJson["files"][entryName] = hash;

            std::string const checksumsStr = checksumsJson.dump();

            // zip_source_buffer takes ownership when freeData=1, and zip_file_add takes ownership of source on success.
            // NOLINTNEXTLINE(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory,clang-analyzer-unix.Malloc)
            void* persistentChecksums = std::malloc(checksumsStr.size());
            if (!persistentChecksums)
                throw std::bad_alloc();
            // NOLINTNEXTLINE(bugprone-not-null-terminated-result)
            std::memcpy(persistentChecksums, checksumsStr.data(), checksumsStr.size());

            zip_source_t* checksumSource = zip_source_buffer(zip, persistentChecksums, checksumsStr.size(), 1);
            if (!checksumSource)
            {
                std::free(persistentChecksums); // NOLINT(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory)
                throw std::bad_alloc();
            }

            // libzip takes ownership via zip_source_buffer(freeData=1), so no leak
            zip_int64_t const checksumIndex = zip_file_add( // NOLINT(clang-analyzer-unix.Malloc)
                zip,
                "checksums.json",
                checksumSource,
                ZIP_FL_OVERWRITE | ZIP_FL_ENC_UTF_8);
            if (checksumIndex < 0)
            {
                zip_source_free(checksumSource);
                throw std::runtime_error("Failed to add checksums.json to zip");
            }

            // Apply compression to checksums.json
            zip_set_file_compression(zip,
                                     static_cast<zip_uint64_t>(checksumIndex),
                                     static_cast<zip_int32_t>(backupSettings.method),
                                     backupSettings.level);
        }

        if (zip_close(zip) < 0)
        {
            zip_error_t* zerr = zip_get_error(zip);
            throw std::runtime_error(std::format("Failed to close zip: {}", zip_error_strerror(zerr)));
        }
        progress.AllDone();
    }
    // LCOV_EXCL_START - Exception handlers for backup failures
    catch (std::exception const& e)
    {
        progress.Update({ .state = Progress::State::Error,
                          .tableName = "Unknown",
                          .currentRows = 0,
                          .totalRows = 0,
                          .message = "Backup failed: "s + e.what() });
    }
    catch (...)
    {
        progress.Update({ .state = Progress::State::Error,
                          .tableName = "Unknown",
                          .currentRows = 0,
                          .totalRows = 0,
                          .message = "Backup failed: Unknown error" });
    }
    // LCOV_EXCL_STOP
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
std::map<std::string, TableInfo> ParseSchema(std::string_view metadataJson, ProgressManager* progress)
{
    nlohmann::json metadata = nlohmann::json::parse(metadataJson);
    auto schema = metadata["schema"];

    std::map<std::string, TableInfo> tableMap;

    for (auto const& tableJson: schema)
    {
        std::string tableName = tableJson["name"];
        TableInfo info;
        info.rowCount = tableJson.value<size_t>("rows", 0);
        bool first = true;

        for (auto const& colJson: tableJson["columns"])
        {
            SqlColumnDeclaration col;
            col.name = colJson["name"];
            bool isPk = colJson["is_primary_key"];
            bool isAuto = colJson["is_auto_increment"];

            if (isPk && isAuto)
                col.primaryKey = SqlPrimaryKeyType::AUTO_INCREMENT;
            else if (isPk)
                col.primaryKey = SqlPrimaryKeyType::MANUAL;
            else
                col.primaryKey = SqlPrimaryKeyType::NONE;

            col.required = !colJson.value("is_nullable", true);
            col.unique = colJson.value("is_unique", false);
            col.defaultValue = colJson.value("default_value", "");

            std::string typeStr = colJson["type"];

            if (typeStr == "integer")
                col.type = SqlColumnTypeDefinitions::Integer {};
            else if (typeStr == "bigint")
                col.type = SqlColumnTypeDefinitions::Bigint {};
            else if (typeStr == "smallint")
                col.type = SqlColumnTypeDefinitions::Smallint {};
            else if (typeStr == "tinyint")
                col.type = SqlColumnTypeDefinitions::Tinyint {};
            else if (typeStr == "real")
                col.type = SqlColumnTypeDefinitions::Real { .precision = colJson.value<size_t>("precision", 53) };
            else if (typeStr == "text")
                col.type = SqlColumnTypeDefinitions::Text {};
            else if (typeStr == "varchar")
                col.type = SqlColumnTypeDefinitions::Varchar { .size = colJson.value<size_t>("size", 8192) };
            else if (typeStr == "nvarchar")
                col.type = SqlColumnTypeDefinitions::NVarchar { .size = colJson.value<size_t>("size", 8192) };
            else if (typeStr == "char")
                col.type = SqlColumnTypeDefinitions::Char { .size = colJson.value<size_t>("size", 255) };
            else if (typeStr == "nchar")
                col.type = SqlColumnTypeDefinitions::NChar { .size = colJson.value<size_t>("size", 255) };
            else if (typeStr == "bool")
                col.type = SqlColumnTypeDefinitions::Bool {};
            else if (typeStr == "binary")
            {
                auto s = colJson.value<size_t>("size", 8192);
                col.type = SqlColumnTypeDefinitions::Binary { .size = s ? s : 65535 };
            }
            else if (typeStr == "varbinary")
            {
                auto s = colJson.value<size_t>("size", 8192);
                col.type = SqlColumnTypeDefinitions::VarBinary { .size = s ? s : 65535 };
            }
            else if (typeStr == "date")
                col.type = SqlColumnTypeDefinitions::Date {};
            else if (typeStr == "datetime")
                col.type = SqlColumnTypeDefinitions::DateTime {};
            else if (typeStr == "time")
                col.type = SqlColumnTypeDefinitions::Time {};
            else if (typeStr == "timestamp")
                col.type = SqlColumnTypeDefinitions::Timestamp {};
            else if (typeStr == "decimal")
            {
                col.type = SqlColumnTypeDefinitions::Decimal { .precision = colJson.value<size_t>("precision", 18),
                                                               .scale = colJson.value<size_t>("scale", 2) };
            }
            else if (typeStr == "guid")
                col.type = SqlColumnTypeDefinitions::Guid {};
            else
            {
                if (progress)
                {
                    progress->Update({ .state = Progress::State::Warning,
                                       .tableName = tableName,
                                       .currentRows = 0,
                                       .totalRows = std::nullopt,
                                       .message = std::format("Column '{}' has unknown type '{}'", col.name, typeStr) });
                }
                // Fallback to TEXT
                col.type = SqlColumnTypeDefinitions::Text {};
            }

            info.columns.push_back(col);

            if (!first)
                info.fields += ",";
            info.fields += '"' + col.name + '"';
            first = false;

            bool const isBinary = typeStr == "binary" || typeStr == "varbinary";
            info.isBinaryColumn.push_back(isBinary);
        }

        if (tableJson.contains("foreign_keys"))
        {
            for (auto const& fkJson: tableJson["foreign_keys"])
            {
                SqlSchema::ForeignKeyConstraint fk;
                // We don't have full Qualified names in JSON, assuming same catalog/schema or simple table names
                fk.foreignKey.table.table = tableName;
                fk.foreignKey.columns = fkJson["columns"].get<std::vector<std::string>>();

                fk.primaryKey.table.table = fkJson["referenced_table"];
                fk.primaryKey.columns = fkJson["referenced_columns"].get<std::vector<std::string>>();

                info.foreignKeys.push_back(fk);
            }
        }

        // Parse indexes (backward compatible - older backups may not have this field)
        if (tableJson.contains("indexes"))
        {
            for (auto const& indexJson: tableJson["indexes"])
            {
                SqlSchema::IndexDefinition idx;
                idx.name = indexJson["name"];
                idx.columns = indexJson["columns"].get<std::vector<std::string>>();
                idx.isUnique = indexJson.value("is_unique", false);
                info.indexes.push_back(idx);
            }
        }

        if (tableJson.contains("primary_keys"))
        {
            auto const pks = tableJson["primary_keys"].get<std::vector<std::string>>();
            for (size_t i = 0; i < pks.size(); ++i)
            {
                auto const& pkName = pks[i];
                auto it =
                    std::ranges::find_if(info.columns, [&](SqlColumnDeclaration const& d) { return d.name == pkName; });
                if (it != info.columns.end())
                {
                    it->primaryKeyIndex = static_cast<uint16_t>(i + 1);
                    // Ensure primaryKey type is set to MANUAL if it was NONE, but don't overwrite AUTO_INCREMENT
                    if (it->primaryKey == SqlPrimaryKeyType::NONE)
                        it->primaryKey = SqlPrimaryKeyType::MANUAL;
                }
            }
        }

        tableMap[tableName] = std::move(info);
    }
    return tableMap;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void Restore(std::filesystem::path const& inputFile,
             SqlConnectionString const& connectionString,
             unsigned concurrency,
             ProgressManager& progress,
             std::string const& schema,
             std::string const& tableFilter,
             RetrySettings const& retrySettings)
{
    concurrency = std::max(1U, concurrency);

    if (!std::filesystem::exists(inputFile))
    {
        progress.Update({ .state = Progress::State::Error,
                          .tableName = "",
                          .currentRows = 0,
                          .totalRows = 0,
                          .message = "Input file does not exist" });
        return;
    }

    int err = 0;
    zip_t* zip = zip_open(inputFile.string().c_str(), ZIP_RDONLY, &err);
    // LCOV_EXCL_START - Error handling for zip file operations
    if (!zip)
    {
        zip_error_t zerr;
        zip_error_init_with_code(&zerr, err);
        std::string errMsg = zip_error_strerror(&zerr);
        zip_error_fini(&zerr);
        progress.Update({ .state = Progress::State::Error,
                          .tableName = "",
                          .currentRows = 0,
                          .totalRows = 0,
                          .message = "Failed to open ZIP archive: " + errMsg });
        return;
    }
    // LCOV_EXCL_STOP

    zip_int64_t metadataIndex = zip_name_locate(zip, "metadata.json", 0);
    // LCOV_EXCL_START - Error handling for missing metadata
    if (metadataIndex < 0)
    {
        progress.Update({ .state = Progress::State::Error,
                          .tableName = "",
                          .currentRows = 0,
                          .totalRows = 0,
                          .message = "metadata.json not found in archive" });
        zip_close(zip);
        return;
    }
    // LCOV_EXCL_STOP

    zip_stat_t metaStat;
    // LCOV_EXCL_START - Error handling for metadata stat failure
    if (zip_stat_index(zip, static_cast<zip_uint64_t>(metadataIndex), 0, &metaStat) < 0)
    {
        progress.Update({ .state = Progress::State::Error,
                          .tableName = "",
                          .currentRows = 0,
                          .totalRows = 0,
                          .message = "Failed to stat metadata.json" });
        zip_close(zip);
        return;
    }
    // LCOV_EXCL_STOP

    auto const metadataStr = ReadZipEntry<std::string>(zip, metadataIndex, metaStat.size);
    nlohmann::json const metadata = nlohmann::json::parse(metadataStr);

    // Validate format version
    std::string const formatVersion = metadata.value("format_version", "");
    if (!formatVersion.empty() && formatVersion != "1.0")
    {
        progress.Update({ .state = Progress::State::Error,
                          .tableName = "",
                          .currentRows = 0,
                          .totalRows = std::nullopt,
                          .message = std::format("Unsupported backup format version: {}. Expected: 1.0", formatVersion) });
        zip_close(zip);
        return;
    }

    std::string const effectiveSchema = !schema.empty() ? schema : metadata.value("schema_name", "");

    // Load checksums if available
    std::map<std::string, std::string> checksums;
    zip_int64_t const checksumIndex = zip_name_locate(zip, "checksums.json", 0);
    if (checksumIndex >= 0)
    {
        zip_stat_t checksumStat;
        if (zip_stat_index(zip, static_cast<zip_uint64_t>(checksumIndex), 0, &checksumStat) >= 0)
        {
            auto const checksumsStr = ReadZipEntry<std::string>(zip, checksumIndex, checksumStat.size);
            try
            {
                nlohmann::json const checksumsJson = nlohmann::json::parse(checksumsStr);
                if (checksumsJson.contains("files") && checksumsJson["files"].is_object())
                {
                    for (auto const& [name, hash]: checksumsJson["files"].items())
                        checksums[name] = hash.get<std::string>();
                }
            }
            // LCOV_EXCL_START - Error handling for checksums parsing failure
            catch (...)
            {
                progress.Update({ .state = Progress::State::Warning,
                                  .tableName = "",
                                  .currentRows = 0,
                                  .totalRows = std::nullopt,
                                  .message = "Failed to parse checksums.json, skipping verification" });
            }
            // LCOV_EXCL_STOP
        }
    }

    std::map<std::string, TableInfo> tableMap = ParseSchema(metadataStr, &progress);

    // Apply table filter
    auto const filter = TableFilter::Parse(tableFilter);
    if (!filter.MatchesAll())
    {
        std::erase_if(tableMap, [&](auto const& pair) { return !filter.Matches(effectiveSchema, pair.first); });
    }

    std::set<std::string> const createdTables =
        RecreateDatabaseSchema(connectionString, effectiveSchema, tableMap, progress);

    // Report summary of schema creation
    size_t const failedTables = tableMap.size() - createdTables.size();
    if (failedTables > 0)
    {
        progress.Update({ .state = Progress::State::Warning,
                          .tableName = "",
                          .currentRows = 0,
                          .totalRows = std::nullopt,
                          .message = std::format("Schema creation: {} of {} tables created, {} failed",
                                                 createdTables.size(),
                                                 tableMap.size(),
                                                 failedTables) });
    }

    // Early exit if all tables failed
    if (createdTables.empty() && !tableMap.empty())
    {
        progress.Update({ .state = Progress::State::Error,
                          .tableName = "",
                          .currentRows = 0,
                          .totalRows = std::nullopt,
                          .message = "Restore aborted: No tables could be created" });
        zip_close(zip);
        progress.AllDone();
        return;
    }

    std::deque<ZipEntryInfo> dataQueue;
    zip_int64_t numEntries = zip_get_num_entries(zip, 0);
    for (zip_int64_t i = 0; i < numEntries; ++i)
    {
        zip_stat_t stat;
        if (zip_stat_index(zip, static_cast<zip_uint64_t>(i), 0, &stat) < 0)
            continue;

        std::string name = stat.name;
        if (name.starts_with("data/") && name.ends_with(".msgpack"))
        {
            // Extract table name from path: data/TABLE_NAME/chunk.msgpack
            auto const firstSlash = name.find('/');
            auto const secondSlash = name.find('/', firstSlash + 1);
            if (firstSlash != std::string::npos && secondSlash != std::string::npos)
            {
                std::string const tableName = name.substr(firstSlash + 1, secondSlash - firstSlash - 1);
                // Skip chunks for tables that failed to create
                if (!createdTables.contains(tableName))
                    continue;
            }

            dataQueue.push_back(ZipEntryInfo {
                .index = i,
                .name = std::move(name),
                .size = stat.size,
                .valid = true,
            });
        }
    }

    std::mutex queueMutex;
    std::mutex fileMutex;

    std::map<std::string, std::shared_ptr<std::atomic<size_t>>> tableProgress;
    size_t totalRows = 0;
    for (auto const& [name, info]: tableMap)
    {
        // Only track progress for successfully created tables
        if (createdTables.contains(name))
        {
            tableProgress[name] = std::make_shared<std::atomic<size_t>>(0);
            totalRows += info.rowCount;
        }
    }

    // Set total items for ETA calculation
    progress.SetTotalItems(totalRows);

    // Filter tableMap to only include created tables for RestoreContext
    std::map<std::string, TableInfo> filteredTableMap;
    for (auto const& [name, info]: tableMap)
    {
        if (createdTables.contains(name))
            filteredTableMap[name] = info;
    }

    RestoreContext ctx {
        .connectionString = connectionString,
        .schema = effectiveSchema,
        .tableMap = filteredTableMap,
        .dataQueue = dataQueue,
        .zip = zip,
        .queueMutex = queueMutex,
        .fileMutex = fileMutex,
        .progress = progress,
        .tableProgress = tableProgress,
        .checksums = checksums.empty() ? nullptr : &checksums,
        .retrySettings = retrySettings,
    };

    std::vector<std::thread> threads;
    std::generate_n(std::back_inserter(threads), concurrency, [&] { return std::thread(RestoreWorker, ctx); });

    for (auto& t: threads)
        t.join();

    ApplyDatabaseConstraints(connectionString, effectiveSchema, filteredTableMap, progress);
    RestoreIndexes(connectionString, effectiveSchema, filteredTableMap, progress);

    zip_close(zip);
    progress.Update({ .state = Progress::State::Finished,
                      .tableName = "",
                      .currentRows = 0,
                      .totalRows = std::nullopt,
                      .message = "Restore complete" });
    progress.AllDone();
}

} // namespace Lightweight::SqlBackup
