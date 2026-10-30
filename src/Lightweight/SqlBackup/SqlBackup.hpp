// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "../Api.hpp"
#include "../SqlConnectInfo.hpp"
#include "../SqlQuery/MigrationPlan.hpp"
#include "../SqlRetryPolicy.hpp"
#include "../SqlSchema.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

/// @defgroup Backup Backup and Restore
/// @brief Parallel chunked database dump and restore, with archive diffing.
///
/// Backups are taken online without a snapshot, so an archive carries no cross-table
/// consistency guarantee.

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

/// @ingroup Backup
/// A caller-supplied file that a backup stores in its archive next to the table data.
///
/// The archive holds it as `attachments/<name>` and lists it in `checksums.json`; restore ignores it.
struct BackupAttachment
{
    /// The relative path within `attachments/` as UTF-8, with `/` separating folders (e.g. `notes/readme.txt`).
    /// It must be well-formed UTF-8 without control characters, must not be empty, start or end with `/`,
    /// contain a backslash or a colon, or have an empty, `.` or `..` path segment. Ignoring ASCII letter case,
    /// it must differ from every other attachment's name and must not be the folder of another one.
    std::string name {};

    /// The bytes the archive stores for this attachment.
    std::vector<std::uint8_t> content {};
};

/// @ingroup Backup
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

    /// Target rows per chunk window. Tables with a single numeric primary key are split into
    /// windows of about this many keys (subject to a per-table window cap) that are backed up
    /// in parallel by multiple workers.
    std::size_t rowsPerChunk = 100'000;

    /// Uncompressed bytes each worker accumulates in its private temp archive before sealing it
    /// (the compression of that archive runs in the worker thread at that point, overlapped with
    /// the network-bound fetch). Bounds worker memory at about jobs x workerArchiveBytes and
    /// determines how many temp archives the finalize merge opens. Default: 256 MB.
    std::size_t workerArchiveBytes = 256ULL * 1024 * 1024;

    /// If true, only export schema metadata without backing up table data.
    bool schemaOnly = false;

    /// Deprecated no-op. Previously bypassed the MS SQL Server single-worker clamp; that clamp has
    /// been removed (all databases now back up multi-threaded), so this flag no longer has any
    /// effect. Retained temporarily to avoid an API/ABI break; pending removal.
    bool forceMssqlConcurrency = false;

    /// Files to store in the archive, compressed with `method` and `level`, also by a schema-only
    /// backup. An invalid or duplicate name fails the backup before it connects to the database.
    std::vector<BackupAttachment> attachments {};
};

/// @ingroup Backup
/// Configuration for restore operations including memory management.
struct RestoreSettings
{
    /// Batch size for insert operations (rows per batch).
    /// Default: 0 (auto-calculated based on available memory).
    std::size_t batchSize = 0;

    /// No longer used: every insert batch commits on its own, so `batchSize` bounds the rows per commit.
    /// Kept for source compatibility.
    std::size_t maxRowsPerCommit = 10000;

    /// Database page cache size in KB (used for SQLite PRAGMA cache_size,
    /// could be extended for other DBMS memory hints).
    /// Default: 65536 (64MB). Set to 0 to use database default.
    std::size_t cacheSizeKB = 65536;

    /// Memory limit in bytes (0 = auto-detect from system).
    std::size_t memoryLimitBytes = 0;

    /// If true, only recreate schema without importing data.
    bool schemaOnly = false;
};

/// Returns available system memory in bytes.
LIGHTWEIGHT_API std::size_t GetAvailableSystemMemory() noexcept;

/// Calculates optimal restore settings based on available memory.
///
/// @param availableMemory Available system memory in bytes.
/// @param concurrency Number of concurrent restore workers.
/// @return RestoreSettings optimized for the given memory constraints.
LIGHTWEIGHT_API RestoreSettings CalculateRestoreSettings(std::size_t availableMemory, unsigned concurrency);

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
///
/// An alias for the library-wide @ref SqlRetrySettings: the backup engine was where this policy
/// was first proven out, and it now shares one implementation with the rest of the library rather
/// than keeping a parallel copy. The field names, types and defaults are unchanged, so existing
/// designated-initializer call sites keep compiling verbatim.
///
/// @see SqlRetryPolicy
using RetrySettings = SqlRetrySettings;

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

    /// The table's name in the database.
    std::string name;

    /// The table's schema as a multi-schema archive records it; empty for a single-schema archive,
    /// whose tables all go into the schema the restore targets.
    std::string schema;
};

/// @ingroup Backup
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

/// @ingroup Backup
/// The interface for progress updates.
struct ProgressManager
{
    virtual ~ProgressManager() = default;
    /// Default constructor.
    ProgressManager() = default;
    /// Default copy constructor.
    ProgressManager(ProgressManager const&) = default;
    /// Default copy assignment operator.
    ProgressManager& operator=(ProgressManager const&) = default;
    /// Default move constructor.
    ProgressManager(ProgressManager&&) = default;
    /// Default move assignment operator.
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
    virtual void SetTotalItems(size_t totalItems)
    {
        (void) totalItems;
    }

    /// Adds to the total number of items for progressive ETA calculation.
    /// This is called as row counts become available during parallel counting.
    /// @param additionalItems Number of additional items (rows) to add to the total.
    virtual void AddTotalItems(size_t additionalItems)
    {
        (void) additionalItems;
    }

    /// Called when items are processed (for rate and ETA calculation).
    /// @param count Number of items (rows) just processed.
    virtual void OnItemsProcessed(size_t count)
    {
        (void) count;
    }

    /// Announces how many tables the operation will process, once, before any
    /// per-table `Update()` is emitted.
    ///
    /// Backup knows the complete table set after its schema scan and Restore
    /// after reading the archive manifest, i.e. both know the denominator before
    /// any data moves. Without this hook a consumer can only count the distinct
    /// table names it has seen so far, so a progress readout of the form
    /// "processed / total" has a total that climbs as tables are discovered —
    /// the GUI's per-table panel showed "12 / 12" then "12 / 47" then "12 / 700"
    /// on the same run, which reads as the job getting bigger rather than as
    /// progress being made.
    ///
    /// @param totalTables Number of tables that will be processed.
    virtual void SetTotalTables(size_t totalTables)
    {
        (void) totalTables;
    }
};

/// @ingroup Backup
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

/// Backs up the tables of several schemas to one file.
///
/// No schema or a single one writes exactly the archive @ref Backup writes for that schema. Two or
/// more write a multi-schema archive (format version 2.0), which records each table's schema and
/// each foreign key's referenced schema, so a restore recreates every table in its own schema.
///
/// @param outputFile the output file.
/// @param connectionString the connection string used to connect to the database.
/// @param concurrency the number of concurrent jobs.
/// @param progress the progress manager to use for progress updates.
/// @param schemas the database schemas to back up.
/// @param tableFilter comma-separated table filter patterns, as for @ref Backup.
/// @param retrySettings configuration for retry behavior on transient errors.
/// @param backupSettings configuration for compression method, level, and chunk size.
LIGHTWEIGHT_API void BackupSchemas(std::filesystem::path const& outputFile,
                                   SqlConnectionString const& connectionString,
                                   unsigned concurrency,
                                   ProgressManager& progress,
                                   std::vector<std::string> const& schemas,
                                   std::string const& tableFilter = "*",
                                   RetrySettings const& retrySettings = {},
                                   BackupSettings const& backupSettings = {});

/// Reads the schemas a backup archive records.
///
/// @param archiveFile the backup archive.
/// @return One name (possibly empty) for a single-schema archive, several for a multi-schema
///         archive, or an error message if the archive's metadata cannot be read.
[[nodiscard]] LIGHTWEIGHT_API std::expected<std::vector<std::string>, std::string> ReadArchiveSchemas(
    std::filesystem::path const& archiveFile);

/// What `VerifyArchive` found.
struct ArchiveVerification
{
    /// SHA-256 of the whole archive file, lowercase hex.
    std::string archiveSha256;
    /// Entries whose SHA-256 matched `checksums.json`.
    std::size_t verifiedEntries = 0;
    /// Whether the archive holds every table with its data: false for a filtered or schema-only backup
    /// and for one in which a table failed (`metadata.json`'s `complete`; archives without it count as complete).
    bool complete = true;
};

/// Checks that a backup archive is complete: it holds `metadata.json`, `checksums.json` lists every data
/// entry (`data/...`) and every attachment (`attachments/...`, folder entries aside), and every listed
/// entry's SHA-256 matches. An archive with neither data entries nor attachments (schema only) needs no
/// `checksums.json`. Also hashes the whole file.
///
/// @param archiveFile The archive.
/// @return The verification, or a message naming what is wrong.
[[nodiscard]] LIGHTWEIGHT_API std::expected<ArchiveVerification, std::string> VerifyArchive(
    std::filesystem::path const& archiveFile);

/// Lists the attachments a backup archive holds (see @ref BackupAttachment).
///
/// The names are checked against the rules of @ref BackupAttachment::name, the ones a backup enforces, so
/// a caller can use each name as a relative path, e.g. to extract the attachments below one folder.
///
/// @param archiveFile The archive.
/// @return The attachment names without the `attachments/` prefix, sorted, or a message saying why the
///         archive cannot be read or naming the first name that breaks those rules.
[[nodiscard]] LIGHTWEIGHT_API std::expected<std::vector<std::string>, std::string> ListArchiveAttachments(
    std::filesystem::path const& archiveFile);

/// Reads one attachment of a backup archive (see @ref BackupAttachment).
///
/// `checksums.json` must list the attachment, with the SHA-256 of the bytes the archive holds.
///
/// @param archiveFile The archive.
/// @param name The attachment's name, without the `attachments/` prefix.
/// @return The attachment's bytes, or a message naming the attachment when it is missing, unreadable, not
///         listed in `checksums.json` or does not match its checksum.
[[nodiscard]] LIGHTWEIGHT_API std::expected<std::vector<std::uint8_t>, std::string> ReadArchiveAttachment(
    std::filesystem::path const& archiveFile, std::string_view name);

/// Decides what a schema that the caller did not ask for, but took from a default (a profile), means
/// for restoring @p archiveFile.
///
/// A multi-schema archive refuses every target schema. A default that names one of its schemas is
/// therefore dropped, so the archive restores into its own schemas; any other default is kept, so
/// @ref Restore refuses it. Other archives keep the default.
///
/// @param archiveFile the backup archive.
/// @param defaultSchema the default schema.
/// @param caseInsensitive whether the target compares identifiers case-insensitively
///        (`SqlQueryFormatter::IdentifiersAreCaseInsensitive()`).
/// @return The schema to pass to @ref Restore.
[[nodiscard]] LIGHTWEIGHT_API std::string DefaultRestoreSchema(std::filesystem::path const& archiveFile,
                                                               std::string const& defaultSchema,
                                                               bool caseInsensitive);

/// Maximum number of failed-row ranges reported per table; further failing rows are only counted.
inline constexpr std::size_t MaxReportedRowRanges = 100;
/// Maximum number of distinct failure reasons reported per table; further occurrences are only counted.
inline constexpr std::size_t MaxReportedReasons = 20;
/// Maximum number of sample row keys kept per failure reason.
inline constexpr std::size_t MaxSampleKeysPerReason = 3;

/// A contiguous run of rows that failed to restore within one chunk.
struct FailedRowRange
{
    /// Archive path of the chunk the rows belong to.
    std::string chunkPath;
    /// First failed row (1-based, within the chunk).
    std::uint64_t firstRow {};
    /// Last failed row (1-based, within the chunk, inclusive).
    std::uint64_t lastRow {};
};

/// One distinct failure cause of a table, with its occurrence count.
struct FailureReason
{
    /// SQLSTATE reported by the driver (may be empty).
    std::string sqlState;
    /// Message of the first occurrence.
    std::string message;
    /// Number of failures sharing this reason.
    std::uint64_t occurrences {};
    /// A few example row keys (at most MaxSampleKeysPerReason).
    std::vector<std::string> sampleKeys;
};

/// Cells of one column that were restored as NULL because their value could not be converted.
struct ColumnValueFailures
{
    /// Column name.
    std::string column;
    /// Number of cells set to NULL.
    std::uint64_t cellsSetToNull {};
    /// Reason of the first occurrence.
    std::string sampleReason;
};

/// Bounded failure report for one table.
struct TableRestoreReport
{
    /// Name of the table.
    std::string tableName;
    /// Total number of rows that failed to restore.
    std::uint64_t failedRowCount {};
    /// Ranges of failed rows, capped at MaxReportedRowRanges.
    std::vector<FailedRowRange> failedRowRanges;
    /// Failed rows not represented in failedRowRanges because the cap was reached.
    std::uint64_t rowsBeyondRangeCap {};
    /// Distinct failure reasons, capped at MaxReportedReasons.
    std::vector<FailureReason> reasons;
    /// Failures not represented in reasons because the cap was reached.
    std::uint64_t occurrencesBeyondReasonCap {};
    /// Per-column value conversion failures.
    std::vector<ColumnValueFailures> valueFailures;
    /// Set if the whole table failed; holds the first reason.
    std::optional<std::string> tableFailure;
    /// Number of chunks of this table that could not be read.
    std::uint64_t unreadableChunks {};

    /// @return true if anything failed for this table.
    [[nodiscard]] bool HasFailures() const noexcept
    {
        return failedRowCount != 0 || !failedRowRanges.empty() || !valueFailures.empty() || tableFailure.has_value()
               || unreadableChunks != 0;
    }
};

/// Bounded failure report of a whole restore.
struct RestoreReport
{
    /// Only tables with failures, sorted by tableName.
    std::vector<TableRestoreReport> tables;
    /// Sum of failedRowCount over all tables.
    std::uint64_t totalFailedRows {};
    /// True if a worker stopped early.
    bool workerStopped {};
    /// Chunks left unprocessed by a stopped worker.
    std::vector<std::string> incompleteTables;
    /// Unreadable archive entries no table could be named for.
    std::uint64_t unattributedChunkErrors {};

    /// @return true if anything failed during the restore.
    [[nodiscard]] bool HasFailures() const noexcept
    {
        return !tables.empty() || workerStopped || !incompleteTables.empty() || unattributedChunkErrors != 0;
    }
};

/// Restores the database from a file.
///
/// A multi-schema archive restores every table into the schema it records, creating missing
/// schemas; it refuses a target @p schema, and on a database without schemas (SQLite) it is
/// refused unless every table name is unique across the schemas.
///
/// A value or row that fails to restore does not abort the restore: a value that does not convert
/// to its nullable column becomes NULL, a row the database rejects (or whose NOT NULL value does not
/// convert) is left out, and every other row is restored. A table fails as a whole only on an error
/// that affects every row (e.g. the driver cannot bind a column type); its remaining chunks are
/// skipped. A chunk that cannot be read is skipped. Only a lost connection that does not come back
/// within @p retrySettings stops a worker. Errors are also reported through @p progress, at most one
/// terminal update per table.
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
/// @return The bounded report of what failed to restore; empty when everything was restored, and
///         when the restore failed before restoring data (which @p progress reports).
LIGHTWEIGHT_API RestoreReport Restore(std::filesystem::path const& inputFile,
                                      SqlConnectionString const& connectionString,
                                      unsigned concurrency,
                                      ProgressManager& progress,
                                      std::string const& schema = {},
                                      std::string const& tableFilter = "*",
                                      RetrySettings const& retrySettings = {});

/// Restores the database from a file with explicit memory management settings.
///
/// @param inputFile the input file.
/// @param connectionString the connection string used to connect to the database.
/// @param concurrency the number of concurrent jobs.
/// @param progress the progress manager to use for progress updates.
/// @param schema the database schema to restore into (optional, overrides backup metadata).
/// @param tableFilter comma-separated table filter patterns (default: "*" for all tables).
/// @param retrySettings configuration for retry behavior on transient errors.
/// @param restoreSettings configuration for memory management during restore.
/// @return The bounded report of what failed to restore, as by the overload without @p restoreSettings.
LIGHTWEIGHT_API RestoreReport Restore(std::filesystem::path const& inputFile,
                                      SqlConnectionString const& connectionString,
                                      unsigned concurrency,
                                      ProgressManager& progress,
                                      std::string const& schema,
                                      std::string const& tableFilter,
                                      RetrySettings const& retrySettings,
                                      RestoreSettings const& restoreSettings);

/// Returns a copy of `connectionString` with the values of `PWD=` and
/// `Password=` attributes replaced by `***`.
///
/// Same as @ref SqlConnectionString::SanitizePwd, which defines the quoting
/// and key-matching rules and is also what dbtool's `list-profiles` applies.
/// Applied by CreateMetadata() so a secretRef-resolved plaintext password
/// never lands in an archive's metadata.json.
///
/// @param connectionString Raw ODBC connection string.
/// @return The connection string with password values masked.
[[nodiscard]] LIGHTWEIGHT_API std::string RedactConnectionStringSecrets(std::string_view connectionString);

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
/// A single-schema archive keys each table by its name, a multi-schema archive by `schema.name`.
///
/// @param metadataJson content of the metadata.json file.
LIGHTWEIGHT_API std::map<std::string, TableInfo> ParseSchema(std::string_view metadataJson,
                                                             ProgressManager* progress = nullptr);

} // namespace Lightweight::SqlBackup
