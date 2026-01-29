// SPDX-License-Identifier: Apache-2.0

#include "../SqlColumnTypeDefinitions.hpp"
#include "../SqlConnection.hpp"
#include "../SqlQuery.hpp"
#include "../SqlSchema.hpp"
#include "../SqlStatement.hpp"
#include "../ThreadSafeQueue.hpp"
#include "Backup.hpp"
#include "Common.hpp"
#include "Restore.hpp"
#include "SqlBackup.hpp"
#include "SqlBackupFormats.hpp"
#include "TableFilter.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <deque>
#include <format>
#include <mutex>
#include <ranges>
#include <set>
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

#if defined(__linux__)
    #include <sys/sysinfo.h>
#elif defined(_WIN32)
    #include <windows.h>
#elif defined(__APPLE__)
    #include <sys/sysctl.h>

    #include <mach/mach.h>
#endif

using namespace std::string_literals;

namespace Lightweight::SqlBackup
{

std::size_t GetAvailableSystemMemory() noexcept
{
#if defined(__linux__)
    struct sysinfo info {};
    if (sysinfo(&info) == 0)
        return static_cast<std::size_t>(info.freeram) * static_cast<std::size_t>(info.mem_unit);
#elif defined(_WIN32)
    MEMORYSTATUSEX status;
    status.dwLength = sizeof(status);
    if (GlobalMemoryStatusEx(&status))
        return static_cast<std::size_t>(status.ullAvailPhys);
#elif defined(__APPLE__)
    // On macOS, get total memory and estimate available as half
    int mib[2] = { CTL_HW, HW_MEMSIZE };
    uint64_t memsize = 0;
    size_t len = sizeof(memsize);
    if (sysctl(mib, 2, &memsize, &len, nullptr, 0) == 0)
        return static_cast<std::size_t>(memsize / 2); // Use half of total as "available"
#endif
    return std::size_t { 4 } * 1024 * 1024 * 1024; // Default: 4GB
}

RestoreSettings CalculateRestoreSettings(std::size_t availableMemory, unsigned concurrency)
{
    RestoreSettings settings;

    // Reserve memory for OS and other processes (use 75% of available)
    std::size_t const usableMemory = availableMemory * 3 / 4;
    std::size_t const memoryPerWorker = usableMemory / std::max(1U, concurrency);

    // Calculate batch size: assume ~1KB per row average
    // Target: each worker uses max 256MB for batch buffers
    std::size_t const maxBatchMemory = std::min(memoryPerWorker / 4, std::size_t { 256 } * 1024 * 1024);
    settings.batchSize = std::clamp(maxBatchMemory / 1024, std::size_t { 100 }, std::size_t { 4000 });

    // SQLite cache: 64MB per worker, max 256MB total
    settings.cacheSizeKB = std::min(std::size_t { 65536 }, memoryPerWorker / 1024 / 4);

    // Commit interval: more frequent commits if low memory
    settings.maxRowsPerCommit = (memoryPerWorker < 512 * 1024 * 1024) ? 5000 : 10000;

    settings.memoryLimitBytes = availableMemory;
    return settings;
}

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

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
std::string CreateMetadata(SqlConnectionString const& connectionString,
                           SqlSchema::TableList const& tables,
                           std::string const& schema)
{
    nlohmann::json metadata;
    metadata["format_version"] = "1.0";
    metadata["creation_time"] = detail::CurrentDateTime();
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
            t["rows"] = static_cast<size_t>(
                stmt.ExecuteDirectScalar<int64_t>(
                        std::format("SELECT COUNT(*) FROM {}", detail::FormatTableName(schema, table.name)))
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

    // Force single-threaded operation for MS SQL to avoid ODBC driver data races.
    // The MS SQL ODBC driver has shared internal buffers that race when multiple
    // connections execute queries concurrently.
    if (mainConn.ServerType() == SqlServerType::MICROSOFT_SQL)
        concurrency = 1U;

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

        detail::BackupContext ctx {
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
        // Collect tables for later processing (after schema scanning completes).
        SqlSchema::TableReadyCallback tableReadyCallback = [&](SqlSchema::Table&& table) {
            // Update max table name length for progress display
            size_t currentMax = maxTableNameLength.load();
            while (table.name.size() > currentMax
                   && !maxTableNameLength.compare_exchange_weak(currentMax, table.name.size()))
            {
            }

            // Store for metadata creation, counter thread, AND worker processing
            std::scoped_lock lock(completedTablesMutex);
            completedTables.push_back(std::move(table));
        };

        // Create table filter predicate to skip reading schema for non-matching tables
        SqlSchema::TableFilterPredicate tableFilterPredicate;
        if (!filter.MatchesAll())
        {
            tableFilterPredicate = [&filter, &schema](std::string_view /*schemaName*/, std::string_view tableName) {
                return filter.Matches(schema, tableName);
            };
        }

        // Run schema scanning FIRST - collect all tables before starting workers.
        // This avoids data races in the ODBC driver during concurrent query execution.
        // The MS SQL ODBC driver and OpenSSL have shared internal state that's not thread-safe
        // when multiple connections are executing queries concurrently with schema queries.
        auto stmt = SqlStatement { mainConn };
        SqlSchema::ReadAllTables(
            stmt, mainConn.DatabaseName(), schema, schemaCallback, tableReadyCallback, tableFilterPredicate);

        progress.Update({ .state = Progress::State::Finished,
                          .tableName = "Scanning schema",
                          .currentRows = completedTables.size(),
                          .totalRows = completedTables.size(),
                          .message = "" });

        progress.SetMaxTableNameLength(maxTableNameLength.load());

        // Back up data (unless schema-only mode)
        if (!backupSettings.schemaOnly)
        {
            // Now that schema scanning is complete, pre-create all worker connections.
            // All connections are established sequentially to avoid ODBC driver races.
            std::vector<std::unique_ptr<SqlConnection>> workerConnections;
            workerConnections.reserve(concurrency);
            for (auto const i: std::views::iota(0U, concurrency))
            {
                auto conn = std::make_unique<SqlConnection>(std::nullopt);
                if (!detail::ConnectWithRetry(
                        *conn, connectionString, retrySettings, progress, std::format("Worker {}", i + 1)))
                {
                    throw std::runtime_error(
                        std::format("Failed to create worker connection {}: {}", i + 1, conn->LastError().message));
                }
                workerConnections.push_back(std::move(conn));
            }

            // Push all collected tables to the queue for workers
            for (auto& table: completedTables)
                tableQueue.Push(SqlSchema::Table(table)); // Copy since completedTables is still needed

            tableQueue.MarkFinished();

            // Start data worker threads - schema scanning is already complete
            std::vector<std::thread> workers;
            workers.reserve(concurrency);
            for (auto const i: std::views::iota(0U, concurrency))
                workers.emplace_back(detail::BackupWorker, std::ref(tableQueue), ctx, std::ref(*workerConnections[i]));

            // Wait for all workers to complete
            for (auto& t: workers)
                t.join();
        }
        else
        {
            progress.Update({ .state = Progress::State::InProgress,
                              .tableName = "",
                              .currentRows = 0,
                              .totalRows = std::nullopt,
                              .message = "Schema-only backup: skipping data export" });
        }

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

        // Write checksums.json only if we have data
        if (!backupSettings.schemaOnly)
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
             RetrySettings const& retrySettings,
             RestoreSettings const& restoreSettings)
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

    auto const metadataStr = detail::ReadZipEntry<std::string>(zip, metadataIndex, metaStat.size);
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
            auto const checksumsStr = detail::ReadZipEntry<std::string>(zip, checksumIndex, checksumStat.size);
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
        detail::RecreateDatabaseSchema(connectionString, effectiveSchema, tableMap, progress);

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

    // Schema-only mode: create schema and apply constraints/indexes, then exit early
    if (restoreSettings.schemaOnly)
    {
        // Filter tableMap to only include created tables
        std::map<std::string, TableInfo> filteredTableMap;
        for (auto const& [name, info]: tableMap)
        {
            if (createdTables.contains(name))
                filteredTableMap[name] = info;
        }

        detail::ApplyDatabaseConstraints(connectionString, effectiveSchema, filteredTableMap, progress);
        detail::RestoreIndexes(connectionString, effectiveSchema, filteredTableMap, progress);

        zip_close(zip);
        progress.Update({ .state = Progress::State::Finished,
                          .tableName = "",
                          .currentRows = 0,
                          .totalRows = std::nullopt,
                          .message = "Schema-only restore complete" });
        progress.AllDone();
        return;
    }

    std::deque<detail::ZipEntryInfo> dataQueue;
    std::map<std::string, size_t> totalChunksPerTable; // Count chunks per table for completion detection
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

                // Count chunks per table for chunk-based completion detection
                totalChunksPerTable[tableName]++;

                // Only add entries that pass validation AND are counted
                dataQueue.push_back(detail::ZipEntryInfo {
                    .index = i,
                    .name = std::move(name),
                    .size = stat.size,
                    .valid = true,
                });
            }
        }
    }

    std::mutex queueMutex;
    std::mutex fileMutex;

    std::map<std::string, std::shared_ptr<std::atomic<size_t>>> tableProgress;
    std::map<std::string, std::shared_ptr<std::atomic<size_t>>> chunksProcessed;
    size_t totalRows = 0;
    for (auto const& [name, info]: tableMap)
    {
        // Only track progress for successfully created tables
        if (createdTables.contains(name))
        {
            tableProgress[name] = std::make_shared<std::atomic<size_t>>(0);
            chunksProcessed[name] = std::make_shared<std::atomic<size_t>>(0);
            totalRows += info.rowCount;
        }
    }

    // Set total items for ETA calculation. Even if some tables have unknown row counts (0),
    // the sum of known counts provides a reasonable approximation for progress display.
    if (totalRows > 0)
        progress.SetTotalItems(totalRows);

    // Filter tableMap to only include created tables for RestoreContext
    std::map<std::string, TableInfo> filteredTableMap;
    for (auto const& [name, info]: tableMap)
    {
        if (createdTables.contains(name))
            filteredTableMap[name] = info;
    }

    // Calculate restore settings based on available memory and concurrency
    RestoreSettings const effectiveSettings = restoreSettings.memoryLimitBytes > 0
                                                  ? restoreSettings
                                                  : CalculateRestoreSettings(GetAvailableSystemMemory(), concurrency);

    detail::RestoreContext ctx {
        .connectionString = connectionString,
        .schema = effectiveSchema,
        .tableMap = filteredTableMap,
        .dataQueue = dataQueue,
        .zip = zip,
        .queueMutex = queueMutex,
        .fileMutex = fileMutex,
        .progress = progress,
        .tableProgress = tableProgress,
        .chunksProcessed = chunksProcessed,
        .totalChunks = totalChunksPerTable,
        .checksums = checksums.empty() ? nullptr : &checksums,
        .retrySettings = retrySettings,
        .restoreSettings = effectiveSettings,
    };

    // Force single-threaded operation for MS SQL to avoid ODBC driver data races.
    // The MS SQL ODBC driver has shared internal buffers that race when multiple
    // connections execute queries concurrently.
    {
        SqlConnection checkConn;
        if (checkConn.Connect(connectionString) && checkConn.ServerType() == SqlServerType::MICROSOFT_SQL)
            concurrency = 1U;
    }

    // Pre-create worker connections to avoid data races in the ODBC driver
    // during concurrent connection establishment.
    std::vector<std::unique_ptr<SqlConnection>> workerConnections;
    workerConnections.reserve(concurrency);
    for (auto const i: std::views::iota(0U, concurrency))
    {
        auto conn = std::make_unique<SqlConnection>(std::nullopt);
        if (!detail::ConnectWithRetry(
                *conn, connectionString, retrySettings, progress, std::format("RestoreWorker {}", i + 1)))
        {
            zip_close(zip);
            throw std::runtime_error(
                std::format("Failed to create restore worker connection {}: {}", i + 1, conn->LastError().message));
        }
        workerConnections.push_back(std::move(conn));
    }

    std::vector<std::thread> threads;
    threads.reserve(concurrency);
    for (auto const i: std::views::iota(0U, concurrency))
        threads.emplace_back(detail::RestoreWorker, ctx, std::ref(*workerConnections[i]));

    for (auto& t: threads)
        t.join();

    detail::ApplyDatabaseConstraints(connectionString, effectiveSchema, filteredTableMap, progress);
    detail::RestoreIndexes(connectionString, effectiveSchema, filteredTableMap, progress);

    zip_close(zip);
    progress.Update({ .state = Progress::State::Finished,
                      .tableName = "",
                      .currentRows = 0,
                      .totalRows = std::nullopt,
                      .message = "Restore complete" });
    progress.AllDone();
}

void Restore(std::filesystem::path const& inputFile,
             SqlConnectionString const& connectionString,
             unsigned concurrency,
             ProgressManager& progress,
             std::string const& schema,
             std::string const& tableFilter,
             RetrySettings const& retrySettings)
{
    // Forward to the main implementation with default RestoreSettings
    Restore(inputFile, connectionString, concurrency, progress, schema, tableFilter, retrySettings, RestoreSettings {});
}

} // namespace Lightweight::SqlBackup
