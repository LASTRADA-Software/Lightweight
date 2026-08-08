// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../SqlConnection.hpp"
#include "../SqlError.hpp"
#include "SqlBackup.hpp"

#include <chrono>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

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
/// @param attempt The current retry attempt number (0-based).
/// @param settings The retry configuration.
/// @return The delay to wait before the next retry.
LIGHTWEIGHT_API std::chrono::milliseconds CalculateRetryDelay(unsigned attempt, RetrySettings const& settings) noexcept;

/// What a retry loop should do after an attempt failed.
///
/// @see ClassifyRetryOutcome
enum class RetryAction : uint8_t
{
    /// The error is transient and the retry budget is not exhausted — wait and try again.
    Retry,
    /// The error is not transient, or the retry budget is spent — surface the failure.
    GiveUp,
};

/// @brief Decides whether a failed attempt should be retried.
///
/// Extracted from the retry loops so the policy can be exercised without provoking a real
/// transient driver failure. This performs no I/O and touches no handle, so a test drives it by
/// constructing a @ref SqlErrorInfo — the same approach the `IsTransientError` cases above use.
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
bool ConnectWithRetry(SqlConnection& conn,
                      SqlConnectionString const& connectionString,
                      RetrySettings const& settings,
                      ProgressManager& progress,
                      std::string const& operation);

/// Retries a function on transient errors with exponential backoff.
///
/// @tparam Func The callable type.
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
            if (ClassifyRetryOutcome(e.info(), attempts, settings) == RetryAction::GiveUp)
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
