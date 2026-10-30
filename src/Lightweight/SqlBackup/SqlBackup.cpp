// SPDX-License-Identifier: Apache-2.0

#include "../SqlAdvisoryLock.hpp"
#include "../SqlColumnTypeDefinitions.hpp"
#include "../SqlConnection.hpp"
#include "../SqlMigration.hpp"
#include "../SqlQuery.hpp"
#include "../SqlSchema.hpp"
#include "../SqlStatement.hpp"
#include "../ThreadSafeQueue.hpp"
#include "../TracyProfiler.hpp"
#include "../Zip/ZipArchive.hpp"
#include "../Zip/ZipPath.hpp"
#include "Backup.hpp"
#include "ChunkPlanner.hpp"
#include "Common.hpp"
#include "ConnectionPool.hpp"
#include "Restore.hpp"
#include "RestoreFailureLog.hpp"
#include "Sha256.hpp"
#include "SqlBackup.hpp"
#include "SqlBackupFormats.hpp"
#include "TableFilter.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <deque>
#include <expected>
#include <format>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <ranges>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
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
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
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

std::string RedactConnectionStringSecrets(std::string_view connectionString)
{
    return SqlConnectionString::SanitizePwd(connectionString);
}

namespace
{
    /// The format of an archive that holds one schema (or the tables of all schemas, unqualified).
    constexpr auto SingleSchemaFormatVersion = std::string_view { "1.0" };

    /// The format of an archive that records each table's schema.
    constexpr auto MultiSchemaFormatVersion = std::string_view { "2.0" };

    /// Re-keys the tables of a multi-schema archive by their bare names and drops their schemas, for
    /// a target without schemas.
    /// @param tableMap The tables of the archive.
    /// @param caseInsensitive Whether the target treats names that differ only in case as one.
    /// @return The flattened tables, or the names that occur in more than one schema.
    std::expected<std::map<std::string, TableInfo>, std::vector<std::string>> FlattenSchemas(
        std::map<std::string, TableInfo> const& tableMap, bool caseInsensitive)
    {
        std::map<std::string, TableInfo> flattened;
        std::map<std::string, std::string> seenNames; // compared spelling -> first name
        std::set<std::string> duplicates;
        for (auto const& [key, info]: tableMap)
        {
            auto table = info;
            table.schema.clear();
            for (auto& foreignKey: table.foreignKeys)
            {
                foreignKey.foreignKey.table.schema.clear();
                foreignKey.primaryKey.table.schema.clear();
            }
            auto const [seen, inserted] =
                seenNames.emplace(caseInsensitive ? detail::ToLowerAscii(info.name) : info.name, info.name);
            if (!inserted)
            {
                duplicates.insert(seen->second);
                duplicates.insert(info.name);
                continue;
            }
            flattened.emplace(info.name, std::move(table));
        }
        if (!duplicates.empty())
            return std::unexpected { std::vector<std::string>(duplicates.begin(), duplicates.end()) };
        return flattened;
    }

    /// Whether an archive holds every table with its data; stored as `metadata.json`'s `complete`.
    enum class ArchiveCompleteness : std::uint8_t
    {
        Complete, ///< Every table, with its data.
        Partial,  ///< Filtered, schema-only, or a table failed.
    };

    /// Tables a backup leaves out and a restore skips: the backup records and the bookkeeping of every
    /// advisory-lock handler (a held migration lock on SQLite). They describe the live database, not its data.
    [[nodiscard]] bool IsBookkeepingTable(std::string_view tableName)
    {
        if (tableName == SqlMigration::BackupRecordTableName)
            return true;
        auto const handlers =
            std::array { &SqliteAdvisoryLockOps(), &SqlServerAdvisoryLockOps(), &PostgreSqlAdvisoryLockOps() };
        return std::ranges::any_of(handlers, [tableName](SqlAdvisoryLockHandler const* handler) {
            return std::ranges::contains(handler->BookkeepingTableNames(), tableName);
        });
    }

    /// Forwards to another progress manager and counts the errors reported through it.
    class ErrorCountingProgress final: public ProgressManager
    {
      public:
        explicit ErrorCountingProgress(ProgressManager& inner) noexcept:
            _inner { &inner }
        {
        }

        void Update(Progress const& p) override
        {
            if (p.state == Progress::State::Error)
                ++_errors;
            _inner->Update(p);
        }
        void AllDone() override
        {
            _inner->AllDone();
        }
        void SetMaxTableNameLength(size_t len) override
        {
            _inner->SetMaxTableNameLength(len);
        }
        [[nodiscard]] size_t ErrorCount() const noexcept override
        {
            return _inner->ErrorCount();
        }
        void SetTotalItems(size_t totalItems) override
        {
            _inner->SetTotalItems(totalItems);
        }
        void AddTotalItems(size_t additionalItems) override
        {
            _inner->AddTotalItems(additionalItems);
        }
        void OnItemsProcessed(size_t count) override
        {
            _inner->OnItemsProcessed(count);
        }
        void SetTotalTables(size_t totalTables) override
        {
            _inner->SetTotalTables(totalTables);
        }

        /// @return The number of errors reported through this manager.
        [[nodiscard]] size_t ReportedErrors() const noexcept
        {
            return _errors.load();
        }

      private:
        ProgressManager* _inner;
        std::atomic<size_t> _errors { 0 };
    };

    /// Adds @p content to @p zip as the entry @p entryName, compressed as @p backupSettings asks.
    /// libzip reads the bytes only during zip_close, so @p content must stay alive until then.
    void AddBufferEntry(zip_t* zip,
                        std::string const& entryName,
                        std::span<std::byte const> content,
                        BackupSettings const& backupSettings)
    {
        zip_source_t* source = zip_source_buffer(zip, content.data(), content.size(), 0);
        if (!source)
            throw std::runtime_error(std::format("Failed to create the zip source of {}: {}", entryName, zip_strerror(zip)));
        zip_int64_t const index = zip_file_add(zip, entryName.c_str(), source, ZIP_FL_OVERWRITE | ZIP_FL_ENC_UTF_8);
        if (index < 0)
        {
            zip_source_free(source);
            throw std::runtime_error(std::format("Failed to add {} to zip: {}", entryName, zip_strerror(zip)));
        }
        zip_set_file_compression(
            zip, static_cast<zip_uint64_t>(index), static_cast<zip_int32_t>(backupSettings.method), backupSettings.level);
    }

    /// Builds checksums.json from the SHA-256 (lowercase hex) of each listed entry.
    std::string CreateChecksumsJson(std::map<std::string, std::string> const& checksums)
    {
        auto checksumsJson = nlohmann::json { { "algorithm", "sha256" }, { "files", nlohmann::json::object() } };
        for (auto const& [entryName, hash]: checksums)
            checksumsJson["files"][entryName] = hash;
        return checksumsJson.dump();
    }
} // namespace

/// Builds metadata.json; @p schemaNames with two or more entries makes it a multi-schema archive.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static std::string CreateArchiveMetadata(SqlConnectionString const& connectionString,
                                         SqlSchema::TableList const& tables,
                                         std::string const& schema,
                                         std::vector<std::string> const& schemaNames,
                                         ArchiveCompleteness completeness)
{
    bool const multiSchema = schemaNames.size() > 1;
    nlohmann::json metadata;
    metadata["format_version"] = multiSchema ? MultiSchemaFormatVersion : SingleSchemaFormatVersion;
    metadata["creation_time"] = detail::CurrentDateTime();
    metadata["complete"] = completeness == ArchiveCompleteness::Complete;
    // Redact PWD/Password so a secretRef-resolved plaintext credential is never
    // persisted into the archive's metadata. Nothing reads this field back at
    // restore time; it is diagnostic only.
    metadata["original_connection_string"] = RedactConnectionStringSecrets(connectionString.value);
    metadata["schema_name"] = schema;
    if (multiSchema)
        metadata["schema_names"] = schemaNames;
    metadata["schema"] = nlohmann::json::array();

    SqlConnection conn { std::nullopt };
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
        if (multiSchema)
            t["schema"] = table.schema;

        try
        {
            t["rows"] = static_cast<size_t>(
                stmt
                    .ExecuteDirectScalar<int64_t>(std::format(
                        "SELECT COUNT(*) FROM {}", detail::FormatTableName(multiSchema ? table.schema : schema, table.name)))
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
            if (multiSchema)
                f["referenced_schema"] = fk.primaryKey.table.schema.empty() ? table.schema : fk.primaryKey.table.schema;
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

std::string CreateMetadata(SqlConnectionString const& connectionString,
                           SqlSchema::TableList const& tables,
                           std::string const& schema)
{
    return CreateArchiveMetadata(connectionString, tables, schema, {}, ArchiveCompleteness::Complete);
}

void Backup(std::filesystem::path const& outputFile,
            SqlConnectionString const& connectionString,
            unsigned concurrency,
            ProgressManager& progress,
            std::string const& schema,
            std::string const& tableFilter,
            RetrySettings const& retrySettings,
            BackupSettings const& backupSettings)
{
    BackupSchemas(
        outputFile, connectionString, concurrency, progress, { schema }, tableFilter, retrySettings, backupSettings);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void BackupSchemas(std::filesystem::path const& outputFile,
                   SqlConnectionString const& connectionString,
                   unsigned concurrency,
                   ProgressManager& outerProgress,
                   std::vector<std::string> const& schemas,
                   std::string const& tableFilter,
                   RetrySettings const& retrySettings,
                   BackupSettings const& backupSettings)
{
    ZoneScopedN("SqlBackup::Backup");
    // Counts the table errors, which make the archive partial.
    auto progress = ErrorCountingProgress { outerProgress };
    auto const attachmentNameView = backupSettings.attachments | std::views::transform(&BackupAttachment::name);
    auto const attachmentNames = std::vector<std::string>(attachmentNameView.begin(), attachmentNameView.end());
    if (auto const valid = detail::ValidateAttachmentNames(attachmentNames); !valid)
    {
        progress.Update({ .state = Progress::State::Error,
                          .tableName = "",
                          .currentRows = 0,
                          .totalRows = 0,
                          .message = "Backup failed: " + valid.error() });
        return;
    }
    {
        auto const sanitized = SqlConnectionString::SanitizePwd(connectionString.value);
        ZoneTextObject(sanitized);
    }
    ZoneValue(concurrency);

    SqlConnection mainConn { std::nullopt };
    if (!mainConn.Connect(connectionString))
    {
        auto const error = mainConn.LastError();
        throw std::runtime_error(std::format("Failed to connect to database: {}", error.message));
    }

    auto const schemaNames =
        detail::NormalizeSchemaNames(schemas, mainConn.QueryFormatter().IdentifiersAreCaseInsensitive());
    bool const multiSchema = schemaNames.size() > 1;
    if (multiSchema && std::ranges::contains(schemaNames, std::string {}))
    {
        progress.Update({ .state = Progress::State::Error,
                          .tableName = "",
                          .currentRows = 0,
                          .totalRows = 0,
                          .message = "Backup failed: an empty schema name cannot be combined with other schemas" });
        return;
    }
    // The schema of a single-schema archive; a multi-schema archive records one per table instead.
    std::string const schema = multiSchema ? std::string {} : schemaNames.front();

    // All databases run multi-threaded. (Historically MS SQL Server was clamped to a single worker
    // over a suspected ODBC driver data race. Each worker uses its own independent SqlConnection,
    // and an initial multi-threaded backup compared byte-identical to a single-threaded baseline via
    // backup-diff; broader stress validation is ongoing. Re-introduce a clamp if a real race surfaces.)
    concurrency = std::max(1U, concurrency);

    // Create ZIP archive early so workers can write to it
    int err = 0;
    zip_t* zip = zip_open(Zip::ZipPathUtf8(outputFile).c_str(), ZIP_CREATE | ZIP_TRUNCATE, &err);
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

    // Discards the archive handle on every error path (the merge phase alone has several throw
    // sites) so a failed backup does not leak the libzip handle and its pending entry state when
    // Backup() is used as a long-lived library API. Disarmed after the successful zip_close.
    struct ZipDiscardGuard
    {
        zip_t* zip;
        explicit ZipDiscardGuard(zip_t* z) noexcept:
            zip { z }
        {
        }
        ZipDiscardGuard(ZipDiscardGuard const&) = delete;
        ZipDiscardGuard& operator=(ZipDiscardGuard const&) = delete;
        ZipDiscardGuard(ZipDiscardGuard&&) = delete;
        ZipDiscardGuard& operator=(ZipDiscardGuard&&) = delete;
        ~ZipDiscardGuard()
        {
            if (zip)
                zip_discard(zip);
        }
    };
    auto zipGuard = ZipDiscardGuard { zip };

    try
    {
        // Temp directory for flushed chunk payloads (same volume as the archive). libzip defers
        // reading sources until zip_close, so buffering chunks in memory would hold the entire
        // uncompressed backup in RAM; file-backed sources keep RSS bounded for huge databases.
        // The guard removes the directory after zip_close (and on any exception, where the
        // abandoned archive's sources are never read).
        struct TempDirGuard
        {
            std::filesystem::path dir;
            explicit TempDirGuard(std::filesystem::path d) noexcept:
                dir { std::move(d) }
            {
            }
            TempDirGuard(TempDirGuard const&) = delete;
            TempDirGuard& operator=(TempDirGuard const&) = delete;
            TempDirGuard(TempDirGuard&&) = delete;
            TempDirGuard& operator=(TempDirGuard&&) = delete;
            ~TempDirGuard()
            {
                std::error_code ec;
                std::filesystem::remove_all(dir, ec);
            }
        };
        auto tempDirectory = outputFile;
        tempDirectory += ".tmp";
        auto const tempDirGuard = TempDirGuard { std::move(tempDirectory) };
        std::filesystem::create_directories(tempDirGuard.dir);

        // The merge keeps the workers' sealed archives open as raw-copy sources until the final
        // zip_close has streamed their bytes; this guard closes them afterwards (and on error
        // paths). Declared after tempDirGuard so the handles are closed before the files vanish.
        struct MergeSourcesGuard
        {
            std::vector<zip_t*> sources;
            MergeSourcesGuard() = default;
            MergeSourcesGuard(MergeSourcesGuard const&) = delete;
            MergeSourcesGuard& operator=(MergeSourcesGuard const&) = delete;
            MergeSourcesGuard(MergeSourcesGuard&&) = delete;
            MergeSourcesGuard& operator=(MergeSourcesGuard&&) = delete;
            ~MergeSourcesGuard()
            {
                for (auto* source: sources)
                    zip_close(source);
            }
        };
        auto mergeSources = MergeSourcesGuard {};

        std::mutex zipMutex;
        std::map<std::string, std::string> checksums;
        std::mutex checksumMutex;

        // Thread-safe queue for streaming chunks of work to the backup workers.
        ThreadSafeQueue<detail::Chunk> chunkQueue;

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
            .multiSchema = multiSchema,
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

        // Run schema scanning FIRST - collect all tables before starting workers.
        // This avoids data races in the ODBC driver during concurrent query execution.
        // The MS SQL ODBC driver and OpenSSL have shared internal state that's not thread-safe
        // when multiple connections are executing queries concurrently with schema queries.
        auto stmt = SqlStatement { mainConn };
        for (auto const& scannedSchema: schemaNames)
        {
            ZoneScopedN("Backup::Phase::SchemaScan");
            // Skips reading the schema of tables the filter rejects, and of the bookkeeping tables.
            SqlSchema::TableFilterPredicate const tableFilterPredicate =
                [&filter, &scannedSchema](std::string_view /*schemaName*/, std::string_view tableName) {
                    return !IsBookkeepingTable(tableName) && filter.Matches(scannedSchema, tableName);
                };
            SqlSchema::ReadAllTables(
                stmt, mainConn.DatabaseName(), scannedSchema, schemaCallback, tableReadyCallback, tableFilterPredicate);
        }

        progress.Update({ .state = Progress::State::Finished,
                          .tableName = "Scanning schema",
                          .currentRows = completedTables.size(),
                          .totalRows = completedTables.size(),
                          .message = "" });

        if (auto const collisions = detail::DescribeTableNameCollisions(completedTables, multiSchema); !collisions.empty())
        {
            progress.Update({ .state = Progress::State::Error,
                              .tableName = "",
                              .currentRows = 0,
                              .totalRows = 0,
                              .message = "Backup failed: " + collisions });
            return;
        }

        progress.SetMaxTableNameLength(maxTableNameLength.load());

        // The schema scan above enumerated every table that will be backed up,
        // so the denominator for a "n / total tables" readout is known here —
        // before any data export emits its first per-table Update(). Publishing
        // it now is what stops a consumer having to infer the total from the
        // table names it has happened to see, which made the total climb during
        // the run instead of staying put.
        progress.SetTotalTables(completedTables.size());

        // Back up data (unless schema-only mode)
        if (!backupSettings.schemaOnly)
        {
            ZoneScopedN("Backup::Phase::DataExport");
            // Now that schema scanning is complete, pre-create the worker connection pool.
            // All connections are established sequentially to avoid ODBC driver races.
            detail::ConnectionPool pool { connectionString, concurrency, retrySettings, progress };

            // Plan the chunk work-list: every PK-range window is its own queue entry so multiple
            // workers can process one table concurrently. Window bounds (MIN/MAX per PK table) are
            // queried on the main connection here, before the workers start. The plan owns the
            // per-table states the chunks point into; it outlives the workers (joined below).
            auto planStmt = SqlStatement { mainConn };
            auto const plan = detail::PlanChunks(
                completedTables,
                backupSettings.rowsPerChunk,
                [&planStmt](SqlSchema::Table const& table, std::string const& pkColumn) {
                    return detail::QueryPkBounds(planStmt, table, pkColumn);
                },
                mainConn.ServerType());

            // Plan-time progress: PK-range totals are known now (estimate = key span; their state
            // carries it). OFFSET tables have totalRows == 0 here and report their exact total
            // from the worker after COUNT(*), as before. Empty PK tables have no chunks and are
            // reported done immediately.
            for (auto const& tableState: plan.tableStates)
                progress.AddTotalItems(tableState.totalRows.load());
            for (auto const* emptyTable: plan.emptyTables)
            {
                progress.AddTotalItems(0);
                progress.Update({ .state = Progress::State::Finished,
                                  .tableName = emptyTable->name,
                                  .currentRows = 0,
                                  .totalRows = size_t { 0 },
                                  .message = "Finished table backup" });
            }

            for (auto const& chunk: plan.chunks)
                chunkQueue.Push(chunk);
            chunkQueue.MarkFinished();

            // One private compressed chunk archive per worker (deque: stable addresses for the
            // thread lambdas). Chunk compression happens inside the workers as rotations fill,
            // overlapped with the network-bound fetch.
            std::deque<detail::WorkerChunkArchive> workerArchives;
            for (auto const workerId: std::views::iota(0U, concurrency))
                workerArchives.emplace_back(tempDirGuard.dir,
                                            workerId,
                                            backupSettings.workerArchiveBytes,
                                            backupSettings.method,
                                            backupSettings.level);

            // Start data worker threads - schema scanning is already complete.
            // Each worker borrows a connection lease from the pool for its lifetime.
            std::vector<std::thread> workers;
            workers.reserve(concurrency);
            for (auto const workerId: std::views::iota(0U, concurrency))
            {
                auto& archive = workerArchives[workerId];
                workers.emplace_back([&chunkQueue, ctx, &pool, &archive] {
                    auto lease = pool.Acquire();
                    detail::ChunkWorker(chunkQueue, ctx, lease.Get(), archive);
                });
            }

            // Wait for all workers to complete
            for (auto& t: workers)
                t.join();

            // Raw-merge the workers' sealed archives into the final zip: every entry is copied
            // COMPRESSED (zipmerge-style, ZIP_FL_COMPRESSED carries the source's compression
            // method), so the final zip_close below only streams already-compressed bytes instead
            // of deflating gigabytes single-threadedly. Tombstoned names (stale sub-chunks of
            // retried windows; unique per worker by plan-time naming) are skipped; rotation order
            // plus ZIP_FL_OVERWRITE makes a retried window's latest attempt win.
            {
                ZoneScopedN("Backup::Phase::Merge");
                auto tombstones = std::set<std::string> {};
                for (auto const& archive: workerArchives)
                    tombstones.insert(archive.Tombstones().begin(), archive.Tombstones().end());

                for (auto const& archive: workerArchives)
                    for (auto const& archivePath: archive.SealedArchives())
                    {
                        int mergeErr = 0;
                        zip_t* src = zip_open(Zip::ZipPathUtf8(archivePath).c_str(), ZIP_RDONLY, &mergeErr);
                        if (!src)
                            throw std::runtime_error("Failed to open worker chunk archive for merge: "
                                                     + Zip::ZipPathUtf8(archivePath));
                        mergeSources.sources.push_back(src);

                        auto const numEntries = zip_get_num_entries(src, 0);
                        for (zip_int64_t entry = 0; entry < numEntries; ++entry)
                        {
                            char const* name = zip_get_name(src, static_cast<zip_uint64_t>(entry), 0);
                            if (!name || tombstones.contains(name))
                                continue;
                        // zip_source_zip_file (and its trailing password argument) was added in
                        // libzip 1.8; older libzip (e.g. Ubuntu 24.04's apt libzip 1.7) only has
                        // the deprecated zip_source_zip with no password parameter. Same guard as
                        // the round-trip merge in the SqlBackup tests.
#if LIBZIP_VERSION_MAJOR > 1 || (LIBZIP_VERSION_MAJOR == 1 && LIBZIP_VERSION_MINOR >= 8)
                            zip_source_t* source = zip_source_zip_file(
                                zip, src, static_cast<zip_uint64_t>(entry), ZIP_FL_COMPRESSED, 0, -1, nullptr);
#else
                            // NOLINTNEXTLINE(clang-diagnostic-deprecated-declarations)
                            zip_source_t* source =
                                zip_source_zip(zip, src, static_cast<zip_uint64_t>(entry), ZIP_FL_COMPRESSED, 0, -1);
#endif
                            if (!source)
                                throw std::runtime_error(std::format("Failed to create merge source for {}", name));
                            if (zip_file_add(zip, name, source, ZIP_FL_OVERWRITE | ZIP_FL_ENC_UTF_8) < 0)
                            {
                                zip_source_free(source);
                                throw std::runtime_error(std::format("Failed to merge {} into the backup archive", name));
                            }
                        }
                    }
            }
        }
        else
        {
            progress.Update({ .state = Progress::State::InProgress,
                              .tableName = "",
                              .currentRows = 0,
                              .totalRows = std::nullopt,
                              .message = "Schema-only backup: skipping data export" });
        }

        // Finalize: write metadata + checksums and close the archive. Data entries were merged in
        // precompressed (raw copy), so Backup::ZipClose below only streams bytes — the heavy
        // deflate work already happened inside the workers.
        ZoneScopedN("Backup::Phase::Finalize");

        // Create metadata.json from completed tables (moved to end since we need full list)
        auto const completeness = filter.MatchesAll() && !backupSettings.schemaOnly && progress.ReportedErrors() == 0
                                      ? ArchiveCompleteness::Complete
                                      : ArchiveCompleteness::Partial;
        // The buffers added below (metadata, checksums, attachments) stay alive until zip_close reads them.
        auto const metadataJson =
            CreateArchiveMetadata(connectionString, completedTables, schema, schemaNames, completeness);
        AddBufferEntry(zip, "metadata.json", std::as_bytes(std::span { metadataJson }), backupSettings);

        for (auto const& attachment: backupSettings.attachments)
        {
            auto entryName = std::format("{}{}", detail::AttachmentsPrefix, attachment.name);
            AddBufferEntry(zip, entryName, std::as_bytes(std::span { attachment.content }), backupSettings);
            checksums.insert_or_assign(std::move(entryName),
                                       Sha256::Hash(attachment.content.data(), attachment.content.size()));
        }

        // A schema-only backup without attachments has nothing to checksum.
        auto const checksumsJson = CreateChecksumsJson(checksums);
        if (!backupSettings.schemaOnly || !backupSettings.attachments.empty())
            AddBufferEntry(zip, "checksums.json", std::as_bytes(std::span { checksumsJson }), backupSettings);

        int zipCloseResult = 0;
        {
            ZoneScopedN("Backup::ZipClose");
            zipCloseResult = zip_close(zip);
        }
        if (zipCloseResult < 0)
        {
            zip_error_t* zerr = zip_get_error(zip);
            throw std::runtime_error(std::format("Failed to close zip: {}", zip_error_strerror(zerr)));
        }
        zipGuard.zip = nullptr; // zip_close succeeded and consumed the handle
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

namespace
{
    /// Reads the DBMS an archive was taken from.
    /// @param metadataJson The archive's metadata.json text.
    /// @return The type named by `server.name`, or UNKNOWN if absent or unrecognized.
    SqlServerType ParseSourceServerType(std::string_view metadataJson)
    {
        auto const metadata = nlohmann::json::parse(metadataJson, nullptr, false);
        if (!metadata.is_object() || !metadata.contains("server") || !metadata["server"].is_object())
            return SqlServerType::UNKNOWN;
        auto const& name = metadata["server"].value("name", nlohmann::json {});
        return name.is_string() ? ServerTypeFromServerName(name.get<std::string>()) : SqlServerType::UNKNOWN;
    }
} // namespace

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
        info.name = tableName;
        info.schema = tableJson.value("schema", "");
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
                // Only a multi-schema archive records schemas; otherwise both tables share the restore's schema.
                fk.foreignKey.table.schema = info.schema;
                fk.foreignKey.table.table = tableName;
                fk.foreignKey.columns = fkJson["columns"].get<std::vector<std::string>>();

                fk.primaryKey.table.schema = fkJson.value("referenced_schema", "");
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

        auto key = info.schema.empty() ? tableName : std::format("{}.{}", info.schema, tableName);
        tableMap[std::move(key)] = std::move(info);
    }
    return tableMap;
}

std::expected<std::vector<std::string>, std::string> ReadArchiveSchemas(std::filesystem::path const& archiveFile)
{
    int error = 0;
    auto const zip =
        std::unique_ptr<zip_t, decltype(&zip_close)> { zip_open(Zip::ZipPathUtf8(archiveFile).c_str(), ZIP_RDONLY, &error),
                                                       &zip_close };
    if (!zip)
        return std::unexpected { std::format("Failed to open backup archive {}", Zip::ZipPathUtf8(archiveFile)) };

    zip_stat_t metadataStat;
    zip_int64_t const metadataIndex = zip_name_locate(zip.get(), "metadata.json", 0);
    if (metadataIndex < 0 || zip_stat_index(zip.get(), static_cast<zip_uint64_t>(metadataIndex), 0, &metadataStat) < 0)
        return std::unexpected { "metadata.json not found in archive" };
    auto const metadataText = detail::ReadZipEntry<std::string>(zip.get(), metadataIndex, metadataStat.size);

    try
    {
        auto const metadata = nlohmann::json::parse(metadataText);
        if (metadata.value("format_version", "") == MultiSchemaFormatVersion)
            return metadata.value("schema_names", std::vector<std::string> {});
        return std::vector<std::string> { metadata.value("schema_name", "") };
    }
    catch (nlohmann::json::exception const& exception)
    {
        return std::unexpected { std::format("Malformed metadata.json: {}", exception.what()) };
    }
}

namespace
{
    using ChecksumList = std::map<std::string, std::string>;

    /// Opens a backup archive for reading; @return the archive, or a message naming the file.
    std::expected<Zip::ZipArchive, std::string> OpenBackupArchive(std::filesystem::path const& archiveFile)
    {
        return Zip::ZipArchive::Open(archiveFile).transform_error([&archiveFile](Zip::ZipError const& error) {
            return std::format("Failed to open backup archive {}: {}", Zip::ZipPathUtf8(archiveFile), error.message);
        });
    }

    /// The entries `checksums.json` lists with their hashes; empty when the archive has none.
    /// `metadata.json`'s `complete`; true when the key is missing (archives of older versions).
    std::expected<bool, std::string> ReadArchiveCompleteness(Zip::ZipArchive const& archive)
    {
        auto const index = archive.LocateEntry("metadata.json");
        if (!index)
            return std::unexpected { "metadata.json not found in archive" };
        auto const text = archive.ReadEntryAsString(*index);
        if (!text)
            return std::unexpected { std::format("Failed to read metadata.json: {}", text.error().message) };
        try
        {
            return nlohmann::json::parse(*text).value("complete", true);
        }
        catch (nlohmann::json::exception const& exception)
        {
            return std::unexpected { std::format("Malformed metadata.json: {}", exception.what()) };
        }
    }

    std::expected<ChecksumList, std::string> ReadListedChecksums(Zip::ZipArchive const& archive)
    {
        auto const index = archive.LocateEntry("checksums.json");
        if (!index)
            return ChecksumList {};
        auto const text = archive.ReadEntryAsString(*index);
        if (!text)
            return std::unexpected { std::format("Failed to read checksums.json: {}", text.error().message) };
        try
        {
            auto listed = ChecksumList {};
            // Named: a range-for does not keep the parsed document alive before C++23's lifetime extension.
            auto const checksums = nlohmann::json::parse(*text);
            for (auto const& [name, hash]: checksums.at("files").items())
                listed.emplace(name, hash.get<std::string>());
            return listed;
        }
        catch (nlohmann::json::exception const& exception)
        {
            return std::unexpected { std::format("Malformed checksums.json: {}", exception.what()) };
        }
    }

    /// Hashes each listed entry; @return how many matched, or a message naming the first that does not.
    std::expected<std::size_t, std::string> VerifyListedEntries(Zip::ZipArchive const& archive, ChecksumList const& listed)
    {
        auto verified = std::size_t { 0 };
        for (auto const& [name, expectedHash]: listed)
        {
            auto const index = archive.LocateEntry(name);
            if (!index)
                return std::unexpected { std::format("{} is listed in checksums.json but missing from the archive", name) };
            auto const content = archive.ReadEntry(*index);
            if (!content)
                return std::unexpected { std::format("Failed to read {}: {}", name, content.error().message) };
            if (Sha256::Hash(content->data(), content->size()) != expectedHash)
                return std::unexpected { std::format("checksum mismatch in {}", name) };
            ++verified;
        }
        return verified;
    }

    /// SHA-256 of the whole file in 1 MiB chunks, lowercase hex.
    std::expected<std::string, std::string> HashFile(std::filesystem::path const& path)
    {
        auto file = std::ifstream { path, std::ios::binary };
        if (!file)
            return std::unexpected { std::format("Failed to open backup archive {}", Zip::ZipPathUtf8(path)) };
        constexpr std::size_t chunkSize = 1024 * 1024;
        auto buffer = std::vector<char>(chunkSize);
        auto hasher = Sha256 {};
        while (file.read(buffer.data(), static_cast<std::streamsize>(buffer.size())) || file.gcount() > 0)
            hasher.Update(buffer.data(), static_cast<std::size_t>(file.gcount()));
        if (!file.eof())
            return std::unexpected { std::format("Failed to read backup archive {}", Zip::ZipPathUtf8(path)) };
        return Sha256::ToHex(hasher.Finalize());
    }
} // namespace

namespace
{
    /// Whether @p entryName is an attachment: an entry below `attachments/` that is no folder entry.
    bool IsAttachmentEntry(std::string_view entryName)
    {
        return entryName.starts_with(detail::AttachmentsPrefix) && !entryName.ends_with('/');
    }

    /// Runs @p read and turns an exception it throws (e.g. allocating a corrupt entry size) into a message.
    template <typename Read>
    auto Guarded(std::filesystem::path const& archiveFile, std::string_view action, Read const& read) -> decltype(read())
    {
        try
        {
            return read();
        }
        catch (std::exception const& exception)
        {
            return std::unexpected { std::format(
                "Failed to {} backup archive {}: {}", action, Zip::ZipPathUtf8(archiveFile), exception.what()) };
        }
    }

    std::expected<ArchiveVerification, std::string> VerifyArchiveUnguarded(std::filesystem::path const& archiveFile)
    {
        auto const archive = OpenBackupArchive(archiveFile);
        if (!archive)
            return std::unexpected { archive.error() };

        if (!archive->LocateEntry("metadata.json"))
            return std::unexpected { "metadata.json not found in archive" };

        auto checksummedEntries = std::vector<std::string> {};
        for (auto const& entry: archive->GetAllEntries())
            if (entry.name.starts_with("data/") || IsAttachmentEntry(entry.name))
                checksummedEntries.push_back(entry.name);

        auto const listed = ReadListedChecksums(*archive);
        if (!listed)
            return std::unexpected { listed.error() };
        if (listed->empty() && !checksummedEntries.empty() && !archive->LocateEntry("checksums.json"))
            return std::unexpected { "checksums.json not found in archive" };
        for (auto const& entryName: checksummedEntries)
            if (!listed->contains(entryName))
                return std::unexpected { std::format("{} is not listed in checksums.json", entryName) };

        auto const verified = VerifyListedEntries(*archive, *listed);
        if (!verified)
            return std::unexpected { verified.error() };

        auto const complete = ReadArchiveCompleteness(*archive);
        if (!complete)
            return std::unexpected { complete.error() };

        auto const archiveSha256 = HashFile(archiveFile);
        if (!archiveSha256)
            return std::unexpected { archiveSha256.error() };
        return ArchiveVerification { .archiveSha256 = *archiveSha256, .verifiedEntries = *verified, .complete = *complete };
    }

    std::expected<std::vector<std::string>, std::string> ListArchiveAttachmentsUnguarded(
        std::filesystem::path const& archiveFile)
    {
        auto const archive = OpenBackupArchive(archiveFile);
        if (!archive)
            return std::unexpected { archive.error() };

        auto names = std::vector<std::string> {};
        for (auto const& entry: archive->GetAllEntries())
            if (IsAttachmentEntry(entry.name))
                names.push_back(entry.name.substr(detail::AttachmentsPrefix.size()));
        std::ranges::sort(names);
        if (auto const valid = detail::ValidateAttachmentNames(names); !valid)
            return std::unexpected { std::format(
                "Backup archive {} holds an invalid attachment: {}", Zip::ZipPathUtf8(archiveFile), valid.error()) };
        return names;
    }

    std::expected<std::vector<std::uint8_t>, std::string> ReadArchiveAttachmentUnguarded(
        std::filesystem::path const& archiveFile, std::string_view name)
    {
        auto const archive = OpenBackupArchive(archiveFile);
        if (!archive)
            return std::unexpected { archive.error() };

        auto const entryName = std::format("{}{}", detail::AttachmentsPrefix, name);
        auto const index = archive->LocateEntry(entryName);
        if (!index)
            return std::unexpected { std::format(
                "Attachment {} not found in backup archive {}", name, Zip::ZipPathUtf8(archiveFile)) };

        auto const listed = ReadListedChecksums(*archive);
        if (!listed)
            return std::unexpected { listed.error() };
        auto const expectedHash = listed->find(entryName);
        if (expectedHash == listed->end())
            return std::unexpected { std::format("{} is not listed in checksums.json", entryName) };

        auto content = archive->ReadEntry(*index);
        if (!content)
            return std::unexpected { std::format("Failed to read attachment {}: {}", name, content.error().message) };
        if (Sha256::Hash(content->data(), content->size()) != expectedHash->second)
            return std::unexpected { std::format("checksum mismatch in {}", entryName) };
        return std::move(*content);
    }
} // namespace

std::expected<ArchiveVerification, std::string> VerifyArchive(std::filesystem::path const& archiveFile)
{
    return Guarded(archiveFile, "verify", [&archiveFile] { return VerifyArchiveUnguarded(archiveFile); });
}

std::expected<std::vector<std::string>, std::string> ListArchiveAttachments(std::filesystem::path const& archiveFile)
{
    return Guarded(archiveFile, "read", [&archiveFile] { return ListArchiveAttachmentsUnguarded(archiveFile); });
}

std::expected<std::vector<std::uint8_t>, std::string> ReadArchiveAttachment(std::filesystem::path const& archiveFile,
                                                                            std::string_view name)
{
    return Guarded(archiveFile, "read", [&archiveFile, name] { return ReadArchiveAttachmentUnguarded(archiveFile, name); });
}

std::string DefaultRestoreSchema(std::filesystem::path const& archiveFile,
                                 std::string const& defaultSchema,
                                 bool caseInsensitive)
{
    auto const archiveSchemas = ReadArchiveSchemas(archiveFile);
    if (defaultSchema.empty() || !archiveSchemas || archiveSchemas->size() <= 1)
        return defaultSchema;
    auto const comparable = [caseInsensitive](std::string const& name) {
        return caseInsensitive ? detail::ToLowerAscii(name) : name;
    };
    auto const wanted = comparable(defaultSchema);
    bool const recorded = std::ranges::any_of(
        *archiveSchemas, [&](std::string const& archiveSchema) { return comparable(archiveSchema) == wanted; });
    return recorded ? std::string {} : defaultSchema;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
RestoreReport Restore(std::filesystem::path const& inputFile,
                      SqlConnectionString const& connectionString,
                      unsigned concurrency,
                      ProgressManager& progress,
                      std::string const& schema,
                      std::string const& tableFilter,
                      RetrySettings const& retrySettings,
                      RestoreSettings const& restoreSettings)
{
    ZoneScopedN("SqlBackup::Restore");
    {
        auto const inputFileStr = Zip::ZipPathUtf8(inputFile);
        ZoneTextObject(inputFileStr);
    }
    ZoneValue(concurrency);

    concurrency = std::max(1U, concurrency);

    if (!std::filesystem::exists(inputFile))
    {
        progress.Update({ .state = Progress::State::Error,
                          .tableName = "",
                          .currentRows = 0,
                          .totalRows = 0,
                          .message = "Input file does not exist" });
        return {};
    }

    int err = 0;
    zip_t* zip = zip_open(Zip::ZipPathUtf8(inputFile).c_str(), ZIP_RDONLY, &err);
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
        return {};
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
        return {};
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
        return {};
    }
    // LCOV_EXCL_STOP

    auto const metadataStr = detail::ReadZipEntry<std::string>(zip, metadataIndex, metaStat.size);
    nlohmann::json const metadata = nlohmann::json::parse(metadataStr);

    // Validate format version
    std::string const formatVersion = metadata.value("format_version", "");
    if (!formatVersion.empty() && formatVersion != SingleSchemaFormatVersion && formatVersion != MultiSchemaFormatVersion)
    {
        progress.Update({ .state = Progress::State::Error,
                          .tableName = "",
                          .currentRows = 0,
                          .totalRows = std::nullopt,
                          .message = std::format("Unsupported backup format version: {}. Expected: {} or {}",
                                                 formatVersion,
                                                 SingleSchemaFormatVersion,
                                                 MultiSchemaFormatVersion) });
        zip_close(zip);
        return {};
    }

    bool const multiSchema = formatVersion == MultiSchemaFormatVersion;
    if (multiSchema && !schema.empty())
    {
        progress.Update(
            { .state = Progress::State::Error,
              .tableName = "",
              .currentRows = 0,
              .totalRows = std::nullopt,
              .message = std::format("Restore refused: the archive holds several schemas ({}), which are restored into "
                                     "the schemas they come from; a target schema ('{}') cannot be applied to it",
                                     detail::JoinNames(metadata.value("schema_names", std::vector<std::string> {})),
                                     schema) });
        zip_close(zip);
        return {};
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
    auto const sourceServerType = ParseSourceServerType(metadataStr);

    // Older archives may hold bookkeeping tables; the live ones stay as they are.
    std::erase_if(tableMap, [](auto const& pair) {
        auto const& [key, info] = pair;
        return IsBookkeepingTable(info.name.empty() ? key : info.name);
    });

    // Apply table filter
    auto const filter = TableFilter::Parse(tableFilter);
    if (!filter.MatchesAll())
    {
        std::erase_if(tableMap, [&](auto const& pair) {
            auto const& [key, info] = pair;
            return !filter.Matches(info.schema.empty() ? effectiveSchema : info.schema, info.name.empty() ? key : info.name);
        });
    }

    // The archive folder of each table's chunks -> its key in tableMap.
    std::map<std::string, std::string> chunkTables;
    for (auto const& [key, info]: tableMap)
        chunkTables.emplace(detail::ChunkDirectory(info.schema, info.name.empty() ? key : info.name), key);

    if (multiSchema)
    {
        SqlConnection probe { std::nullopt };
        if (!probe.Connect(connectionString))
        {
            progress.Update({ .state = Progress::State::Error,
                              .tableName = "",
                              .currentRows = 0,
                              .totalRows = std::nullopt,
                              .message = "Failed to connect to the target database: " + probe.LastError().message });
            zip_close(zip);
            return {};
        }
        if (!probe.QueryFormatter().SupportsSchemas())
        {
            auto flattened = FlattenSchemas(tableMap, probe.QueryFormatter().IdentifiersAreCaseInsensitive());
            if (!flattened)
            {
                progress.Update({ .state = Progress::State::Error,
                                  .tableName = "",
                                  .currentRows = 0,
                                  .totalRows = std::nullopt,
                                  .message = std::format("Restore refused: the target database has no schemas, and "
                                                         "these tables exist in more than one schema of the archive: {}",
                                                         detail::JoinNames(flattened.error())) });
                zip_close(zip);
                return {};
            }
            for (auto& [directory, key]: chunkTables)
                key = tableMap.at(key).name;
            tableMap = std::move(*flattened);
        }
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
        return {};
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
        return {};
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
        auto const directory = detail::ChunkDirectoryOf(name);
        if (!directory)
            continue;
        auto const table = chunkTables.find(std::string { *directory });
        // Skip chunks of tables that were filtered out or failed to create
        if (table == chunkTables.end() || !createdTables.contains(table->second))
            continue;

        // Count chunks per table for chunk-based completion detection
        totalChunksPerTable[table->second]++;

        // Only add entries that pass validation AND are counted
        dataQueue.push_back(detail::ZipEntryInfo {
            .index = i,
            .name = std::move(name),
            .size = stat.size,
            .valid = true,
        });
    }

    std::mutex queueMutex;
    std::mutex fileMutex;

    std::map<std::string, std::shared_ptr<std::atomic<size_t>>> tableProgress;
    std::map<std::string, std::shared_ptr<std::atomic<size_t>>> chunksProcessed;
    std::map<std::string, std::shared_ptr<std::atomic<std::uint64_t>>> committedRows;
    size_t totalRows = 0;
    for (auto const& [name, info]: tableMap)
    {
        // Only track progress for successfully created tables
        if (createdTables.contains(name))
        {
            tableProgress[name] = std::make_shared<std::atomic<size_t>>(0);
            chunksProcessed[name] = std::make_shared<std::atomic<size_t>>(0);
            committedRows[name] = std::make_shared<std::atomic<std::uint64_t>>(0);
            totalRows += info.rowCount;
        }
    }

    // Set total items for ETA calculation. Even if some tables have unknown row counts (0),
    // the sum of known counts provides a reasonable approximation for progress display.
    if (totalRows > 0)
        progress.SetTotalItems(totalRows);

    // `tableProgress` was just populated with exactly the tables that will be
    // restored, so — as on the backup path — the "n / total tables" denominator
    // is known before the first per-table Update() and is published once here
    // rather than being inferred from the table names a consumer has seen.
    progress.SetTotalTables(tableProgress.size());

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

    detail::FailedTables failedDataTables;
    detail::RestoreFailureLog failureLog;
    detail::RestoreContext ctx {
        .connectionString = connectionString,
        .schema = effectiveSchema,
        .tableMap = filteredTableMap,
        .chunkTables = chunkTables,
        .dataQueue = dataQueue,
        .zip = zip,
        .queueMutex = queueMutex,
        .fileMutex = fileMutex,
        .progress = progress,
        .tableProgress = tableProgress,
        .chunksProcessed = chunksProcessed,
        .totalChunks = totalChunksPerTable,
        .committedRows = committedRows,
        .checksums = checksums.empty() ? nullptr : &checksums,
        .retrySettings = retrySettings,
        .restoreSettings = effectiveSettings,
        .failedTables = failedDataTables,
        .failureLog = failureLog,
        .sourceServerType = sourceServerType,
    };

    // All databases restore multi-threaded. (Historically MS SQL Server was clamped to a single
    // worker over a suspected ODBC driver data race — the same clamp the backup side dropped.
    // Each worker uses its own independent SqlConnection and works on its own chunk; restored
    // data is verified via backup-diff round trips. Re-introduce a clamp if a real race surfaces.)

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

    // A failed table whose chunks a stopped worker left unprocessed is settled here.
    for (auto const& tableName: chunksProcessed | std::views::keys)
        detail::SettleFailedTableRows(ctx, tableName);
    auto report = failureLog.Snapshot();

    // A worker stops on a lost connection; once all have stopped, chunks may be left unrestored.
    for (auto const& [tableName, processed]: chunksProcessed)
    {
        if (processed->load() >= totalChunksPerTable[tableName])
            continue;
        report.incompleteTables.push_back(tableName);
        progress.Update({ .state = Progress::State::Error,
                          .tableName = tableName,
                          .currentRows = tableProgress.at(tableName)->load(),
                          .totalRows = std::nullopt,
                          .message = "Not fully restored: the restore stopped after an earlier error" });
    }

    detail::ApplyDatabaseConstraints(connectionString, effectiveSchema, filteredTableMap, progress);
    detail::RestoreIndexes(connectionString, effectiveSchema, filteredTableMap, progress);

    zip_close(zip);
    progress.Update({ .state = Progress::State::Finished,
                      .tableName = "",
                      .currentRows = 0,
                      .totalRows = std::nullopt,
                      .message = "Restore complete" });
    progress.AllDone();
    return report;
}

RestoreReport Restore(std::filesystem::path const& inputFile,
                      SqlConnectionString const& connectionString,
                      unsigned concurrency,
                      ProgressManager& progress,
                      std::string const& schema,
                      std::string const& tableFilter,
                      RetrySettings const& retrySettings)
{
    // Forward to the main implementation with default RestoreSettings
    return Restore(
        inputFile, connectionString, concurrency, progress, schema, tableFilter, retrySettings, RestoreSettings {});
}

} // namespace Lightweight::SqlBackup
