// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../SqlConnection.hpp"
#include "../SqlError.hpp"
#include "../SqlRetryPolicy.hpp"
#include "SqlBackup.hpp"

#include <chrono>
#include <cstdint>
#include <expected>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

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

/// Maximum declared buffer size for binary LOB columns during backup.
/// The actual data can grow beyond this via automatic ODBC buffer resizing.
/// Set to 16MB to handle typical BLOB/VARBINARY(MAX) columns.
constexpr size_t MaxBinaryLobBufferSize = 16 * 1024 * 1024;

/// Metadata for a ZIP entry used during restore operations.
struct ZipEntryInfo
{
    zip_int64_t index {};
    std::string name;
    zip_uint64_t size {};
    bool valid = false;
};

/// Determines if the given SQL error is a transient error that can be retried.
///
/// Thin adapter over @ref Lightweight::GenericRetryOps(), which is where the classification
/// actually lives — the backup engine no longer carries its own copy of the heuristics. The
/// dialect-agnostic classifier is used because this helper is reached from contexts that have an
/// error but not the connection it came from; a caller that does have a connection should build a
/// @ref Lightweight::SqlRetryPolicy with @c SqlRetryPolicy::For() and get the sharper per-DBMS
/// classification instead.
///
/// Transient errors include:
/// - Connection errors (ODBC class 08)
/// - Timeout errors (HYT00, HYT01)
/// - Transaction rollback due to deadlock/serialization (class 40)
/// - Database locked (common in SQLite)
///
/// @param error The SQL error information to check.
/// @return true if the error is transient and the operation can be retried.
LIGHTWEIGHT_API bool IsTransientError(SqlErrorInfo const& error);

/// Calculates the delay for the given retry attempt using exponential backoff.
///
/// Delegates to @ref Lightweight::SqlRetryPolicy::DelayFor().
///
/// @param attempt The current retry attempt number (0-based).
/// @param settings The retry configuration.
/// @return The delay to wait before the next retry.
LIGHTWEIGHT_API std::chrono::milliseconds CalculateRetryDelay(unsigned attempt, RetrySettings const& settings) noexcept;

/// What a retry loop should do after an attempt failed.
///
/// An alias for @ref Lightweight::SqlRetryAction; the backup engine shares the library-wide
/// vocabulary rather than defining its own.
///
/// @see ClassifyRetryOutcome
using RetryAction = SqlRetryAction;

/// @brief Decides whether a failed attempt should be retried.
///
/// Extracted from the retry loops so the policy can be exercised without provoking a real
/// transient driver failure. This performs no I/O and touches no handle, so a test drives it by
/// constructing a @ref SqlErrorInfo — the same approach the `IsTransientError` cases above use.
///
/// Delegates to @ref Lightweight::SqlRetryPolicy::Decide(), keeping the decision in one place.
///
/// @param error The error reported by the failed attempt.
/// @param attemptsSoFar How many retries have already been consumed.
/// @param settings The retry configuration supplying the budget.
/// @return @ref RetryAction::Retry when the caller should back off and try again.
[[nodiscard]] LIGHTWEIGHT_API RetryAction ClassifyRetryOutcome(SqlErrorInfo const& error,
                                                               unsigned attemptsSoFar,
                                                               RetrySettings const& settings);

/// Connects to the database with retry logic for transient errors.
///
/// @param conn The connection object to use.
/// @param connectionString The connection string.
/// @param settings The retry configuration.
/// @param progress Progress manager for reporting retry attempts.
/// @param operation Name of the operation for progress messages.
/// @return true if connection succeeded, false if failed after all retries.
LIGHTWEIGHT_API bool ConnectWithRetry(SqlConnection& conn,
                                      SqlConnectionString const& connectionString,
                                      RetrySettings const& settings,
                                      ProgressManager& progress,
                                      std::string const& operation);

/// Retries a function on transient errors with exponential backoff.
///
/// A thin wrapper over @ref Lightweight::SqlRetryPolicy::Execute() that routes each retry notice
/// into @p progress. @p func is taken by value because it is invoked repeatedly — forwarding an
/// rvalue more than once would use a moved-from callable.
///
/// @tparam Func The callable type.
/// @param func The function to execute.
/// @param settings Retry configuration.
/// @param progress Progress manager for reporting retry attempts.
/// @param operation Name of the operation for progress messages.
/// @return The result of the function.
/// @throws SqlException if max retries exceeded or non-transient error occurs.
template <typename Func>
auto RetryOnTransientError(Func func, RetrySettings const& settings, ProgressManager& progress, std::string const& operation)
    -> decltype(func())
{
    auto policy = SqlRetryPolicy { settings };

    policy.SetRetryObserver([&progress, &operation, &settings](SqlRetryAttempt const& attempt) {
        progress.Update({ .state = Progress::State::Warning,
                          .tableName = operation,
                          .currentRows = 0,
                          .totalRows = std::nullopt,
                          // Formatting the error info reproduces SqlException::what() exactly, so
                          // the wording of these progress lines is unchanged by the migration.
                          .message = std::format("Transient error, retry {}/{}: {}",
                                                 attempt.retryNumber,
                                                 settings.maxRetries,
                                                 std::format("{}", attempt.error)) });
    });

    return policy.Execute(std::move(func));
}

/// Returns the current date and time in ISO 8601 format.
///
/// @return ISO 8601 formatted timestamp string.
LIGHTWEIGHT_API std::string CurrentDateTime();

/// Reads a ZIP entry into a container.
///
/// @tparam Container The container type (e.g., std::string, std::vector<uint8_t>).
/// @param zip The ZIP archive handle.
/// @param index The index of the entry to read.
/// @param size The size of the entry in bytes.
/// @return The container with the entry contents, or empty on failure.
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

/// Formats a table name with optional schema prefix.
///
/// @param schema The schema name (may be empty).
/// @param table The table name.
/// @return Quoted table name, optionally prefixed with quoted schema.
LIGHTWEIGHT_API std::string FormatTableName(std::string_view schema, std::string_view table);

/// Returns the archive folder below `data/` that holds a table's chunks.
///
/// A single-schema archive uses `<table>`, a multi-schema archive `<schema>/<table>`. Dots in
/// either name become underscores, so the folder never adds a path segment of its own.
///
/// @param schema The table's schema in a multi-schema archive; empty in a single-schema archive.
/// @param table The table name.
/// @return The folder, relative to `data/`.
LIGHTWEIGHT_API std::string ChunkDirectory(std::string_view schema, std::string_view table);

/// Extracts the folder below `data/` from a chunk entry name `data/<folder>/<chunk>.msgpack`.
///
/// @param entryName The ZIP entry name.
/// @return The folder (see @ref ChunkDirectory), or `std::nullopt` if @p entryName is no chunk entry.
LIGHTWEIGHT_API std::optional<std::string_view> ChunkDirectoryOf(std::string_view entryName);

/// The archive folder that holds the caller-supplied attachments (see @ref BackupAttachment).
inline constexpr std::string_view AttachmentsPrefix = "attachments/";

/// Checks that attachment names can be extracted below `attachments/` (see @ref BackupAttachment::name).
///
/// Each name must be a relative, `/`-separated path of well-formed UTF-8 without control characters. Names
/// are compared ignoring ASCII letter case, since archives get extracted on case-insensitive file systems: no
/// two may be equal, and none may also be the folder of another.
///
/// @param names The attachment names, without the `attachments/` prefix.
/// @return Nothing when all names are valid, otherwise a message naming the first offending name.
[[nodiscard]] LIGHTWEIGHT_API std::expected<void, std::string> ValidateAttachmentNames(std::span<std::string const> names);

/// Joins @p names with ", ", for messages.
///
/// A plain loop: libc++ lacks views::join_with, and Clang cannot compile its `| ranges::to` pipe
/// against libstdc++ 14.
///
/// @param names The names to join.
/// @return The joined names.
LIGHTWEIGHT_API std::string JoinNames(std::vector<std::string> const& names);

/// Lower-cases ASCII letters, for comparing identifiers of a case-insensitive dialect.
///
/// @param text The identifier.
/// @return @p text with A-Z lower-cased.
LIGHTWEIGHT_API std::string ToLowerAscii(std::string_view text);

/// Drops duplicates from @p schemas, keeping the first spelling, and sorts them; an empty list
/// becomes the one unnamed schema.
///
/// @param schemas The schemas to back up.
/// @param caseInsensitive Whether names that differ only in case are one schema.
/// @return The schemas a backup covers.
LIGHTWEIGHT_API std::vector<std::string> NormalizeSchemaNames(std::vector<std::string> schemas, bool caseInsensitive);

/// Describes why @p tables cannot go into one archive, because two of them would share an archive
/// key or chunk folder.
///
/// Two causes exist: same-named tables of different schemas in an archive that records no schema
/// (the message points to a multi-schema archive), and names that differ only in a dot and an
/// underscore, which the chunk folder writes alike.
///
/// @param tables The tables to back up.
/// @param multiSchema Whether the archive records each table's schema.
/// @return The description, or an empty string if no two tables collide.
LIGHTWEIGHT_API std::string DescribeTableNameCollisions(SqlSchema::TableList const& tables, bool multiSchema);

/// Drops a table if it exists, handling FK constraints via cascade.
///
/// @param conn The database connection.
/// @param schema The schema name.
/// @param tableName The table name.
/// @param progress Progress manager for reporting errors.
/// @return true if table was dropped or didn't exist, false on error.
LIGHTWEIGHT_API bool DropTableIfExists(SqlConnection& conn,
                                       std::string const& schema,
                                       std::string const& tableName,
                                       ProgressManager& progress);

} // namespace Lightweight::SqlBackup::detail
