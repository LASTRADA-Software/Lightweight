// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../SqlConnection.hpp"
#include "../SqlError.hpp"
#include "SqlBackup.hpp"

#include <chrono>
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
bool IsTransientError(SqlErrorInfo const& error);

/// Calculates the delay for the given retry attempt using exponential backoff.
///
/// @param attempt The current retry attempt number (0-based).
/// @param settings The retry configuration.
/// @return The delay to wait before the next retry.
std::chrono::milliseconds CalculateRetryDelay(unsigned attempt, RetrySettings const& settings) noexcept;

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

/// Returns the current date and time in ISO 8601 format.
///
/// @return ISO 8601 formatted timestamp string.
std::string CurrentDateTime();

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
std::string FormatTableName(std::string_view schema, std::string_view table);

/// Drops a table if it exists, handling FK constraints via cascade.
///
/// @param conn The database connection.
/// @param schema The schema name.
/// @param tableName The table name.
/// @param progress Progress manager for reporting errors.
/// @return true if table was dropped or didn't exist, false on error.
bool DropTableIfExists(SqlConnection& conn,
                       std::string const& schema,
                       std::string const& tableName,
                       ProgressManager& progress);

} // namespace Lightweight::SqlBackup::detail
