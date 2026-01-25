// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "../Api.hpp"
#include "../SqlConnectInfo.hpp"
#include "../SqlQuery/MigrationPlan.hpp"
#include "../SqlSchema.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace Lightweight::SqlBackup
{

/// Compression methods supported for ZIP entries.
///
/// The values correspond to the ZIP compression method IDs used by libzip.
/// Not all methods may be available at runtime depending on how libzip was compiled.
/// Use IsCompressionMethodSupported() to check availability.
// NOLINTNEXTLINE(performance-enum-size) - Values must match libzip ZIP_CM_* constants
enum class CompressionMethod : std::int32_t
{
    Store = 0,   ///< No compression (ZIP_CM_STORE)
    Deflate = 8, ///< Deflate compression (ZIP_CM_DEFLATE) - most compatible
    Bzip2 = 12,  ///< Bzip2 compression (ZIP_CM_BZIP2)
    Lzma = 14,   ///< LZMA compression (ZIP_CM_LZMA)
    Zstd = 93,   ///< Zstandard compression (ZIP_CM_ZSTD)
    Xz = 95,     ///< XZ compression (ZIP_CM_XZ)
};

/// Configuration for backup operations including compression and chunking.
struct BackupSettings
{
    /// The compression method to use.
    CompressionMethod method = CompressionMethod::Deflate;

    /// The compression level (0-9).
    /// - For Deflate: 1 = fastest, 9 = best compression, 6 = default
    /// - For Bzip2: 1-9 (block size in 100k units)
    /// - For Zstd: maps to zstd levels
    /// - For Store: ignored
    std::uint32_t level = 6;

    /// The target size in bytes for each chunk before flushing.
    /// Chunks are flushed when the buffer exceeds this size.
    /// Default: 10 MB.
    std::size_t chunkSizeBytes = 10 * 1024 * 1024;
};

/// Checks if a compression method is supported by the current libzip installation.
///
/// @param method The compression method to check.
/// @return true if the method is available for both compression and decompression.
LIGHTWEIGHT_API bool IsCompressionMethodSupported(CompressionMethod method) noexcept;

/// Returns a list of all compression methods that are supported by the current libzip installation.
LIGHTWEIGHT_API std::vector<CompressionMethod> GetSupportedCompressionMethods() noexcept;

/// Returns the human-readable name of a compression method.
LIGHTWEIGHT_API std::string_view CompressionMethodName(CompressionMethod method) noexcept;

/// Configuration for retry behavior on transient errors during backup/restore operations.
struct RetrySettings
{
    /// Maximum number of retry attempts for transient errors.
    unsigned maxRetries = 3;

    /// Initial delay between retry attempts.
    std::chrono::milliseconds initialDelay { 500 };

    /// Multiplier applied to delay after each failed attempt (exponential backoff).
    double backoffMultiplier = 2.0;

    /// Maximum delay between retry attempts.
    std::chrono::milliseconds maxDelay { 30000 };
};

/// Information about a table being backed up.
struct TableInfo
{
    /// The list of columns in the table in SQL format.
    std::string fields;

    /// The list of columns in the table.
    std::vector<bool> isBinaryColumn;

    /// The list of columns in the table.
    std::vector<SqlColumnDeclaration> columns;

    /// The list of foreign key constraints in the table.
    std::vector<SqlSchema::ForeignKeyConstraint> foreignKeys;

    /// The indexes on the table (excluding primary key index).
    std::vector<SqlSchema::IndexDefinition> indexes;

    /// The number of rows in the table.
    size_t rowCount = 0;
};

/// Progress information for backup/restore operations status updates.
struct Progress
{
    /// The state of an individual backup/restore operation.
    enum class State : std::uint8_t
    {
        Started,
        InProgress,
        Finished,
        Error,
        Warning
    };

    /// The state of an individual backup/restore operation.
    State state {};

    /// The name of the table being backed up / restored.
    std::string tableName;

    /// The current number of rows processed.
    size_t currentRows {};

    /// The total number of rows to be processed, if known.
    std::optional<size_t> totalRows;

    /// A message associated with the progress update.
    std::string message;
};

/// The interface for progress updates.
struct ProgressManager
{
    virtual ~ProgressManager() = default;
    ProgressManager() = default;
    ProgressManager(ProgressManager const&) = default;
    ProgressManager& operator=(ProgressManager const&) = default;
    ProgressManager(ProgressManager&&) = default;
    ProgressManager& operator=(ProgressManager&&) = default;

    /// Gets called when the progress of an individual backup/restore operation changes.
    virtual void Update(Progress const& p) = 0;

    /// Gets called when all backup/restore operations are finished.
    virtual void AllDone() = 0;

    /// Sets the maximum length of a table name.
    /// This is used to align the output of the progress manager.
    virtual void SetMaxTableNameLength(size_t /*len*/) {}

    /// Returns the number of errors encountered during the operation.
    [[nodiscard]] virtual size_t ErrorCount() const noexcept
    {
        return 0;
    }

    /// Sets the total number of items to be processed (for ETA calculation).
    /// @param totalItems Total number of items (rows) to process across all tables.
    virtual void SetTotalItems(size_t /*totalItems*/) {}

    /// Called when items are processed (for rate and ETA calculation).
    /// @param count Number of items (rows) just processed.
    virtual void OnItemsProcessed(size_t /*count*/) {}
};

/// Base class for progress managers that tracks errors automatically.
class ErrorTrackingProgressManager: public ProgressManager
{
  public:
    void Update(Progress const& progress) override
    {
        if (progress.state == Progress::State::Error)
            ++_errorCount;
    }

    [[nodiscard]] size_t ErrorCount() const noexcept override
    {
        return _errorCount;
    }

  private:
    size_t _errorCount = 0;
};

struct NullProgressManager: ErrorTrackingProgressManager
{
    void Update(Progress const& progress) override
    {
        ErrorTrackingProgressManager::Update(progress);
    }
    void AllDone() override {}
};

/// Backs up the database to a file.
///
/// @param outputFile the output file.
/// @param connectionString the connection string used to connect to the database.
/// @param concurrency the number of concurrent jobs.
/// @param progress the progress manager to use for progress updates.
/// @param schema the database schema to backup (optional).
/// @param tableFilter comma-separated table filter patterns (default: "*" for all tables).
///        Supports glob wildcards (* and ?) and schema.table notation.
///        Examples: "Users,Products", "*_log", "dbo.Users", "sales.*"
/// @param retrySettings configuration for retry behavior on transient errors.
/// @param backupSettings configuration for compression method, level, and chunk size.
LIGHTWEIGHT_API void Backup(std::filesystem::path const& outputFile,
                            SqlConnectionString const& connectionString,
                            unsigned concurrency,
                            ProgressManager& progress,
                            std::string const& schema = {},
                            std::string const& tableFilter = "*",
                            RetrySettings const& retrySettings = {},
                            BackupSettings const& backupSettings = {});

/// Restores the database from a file.
///
/// @param inputFile the input file.
/// @param connectionString the connection string used to connect to the database.
/// @param concurrency the number of concurrent jobs.
/// @param progress the progress manager to use for progress updates.
/// @param schema the database schema to restore into (optional, overrides backup metadata).
/// @param tableFilter comma-separated table filter patterns (default: "*" for all tables).
///        Supports glob wildcards (* and ?) and schema.table notation.
///        Examples: "Users,Products", "*_log", "dbo.Users", "sales.*"
/// @param retrySettings configuration for retry behavior on transient errors.
LIGHTWEIGHT_API void Restore(std::filesystem::path const& inputFile,
                             SqlConnectionString const& connectionString,
                             unsigned concurrency,
                             ProgressManager& progress,
                             std::string const& schema = {},
                             std::string const& tableFilter = "*",
                             RetrySettings const& retrySettings = {});

/// Creates the metadata JSON content.
///
/// @param connectionString the connection string used to connect to the database.
/// @param tables the list of tables to backup.
/// @param schema the database schema used for these tables (optional).
LIGHTWEIGHT_API std::string CreateMetadata(SqlConnectionString const& connectionString,
                                           SqlSchema::TableList const& tables,
                                           std::string const& schema = {});

/// Parses the metadata JSON content and returns a map of table info.
///
/// @param metadataJson content of the metadata.json file.
LIGHTWEIGHT_API std::map<std::string, TableInfo> ParseSchema(std::string_view metadataJson,
                                                             ProgressManager* progress = nullptr);

} // namespace Lightweight::SqlBackup
