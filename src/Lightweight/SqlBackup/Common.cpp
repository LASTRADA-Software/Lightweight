// SPDX-License-Identifier: Apache-2.0

#include "../SqlStatement.hpp"
#include "Common.hpp"

#include <chrono>
#include <format>
#include <thread>

#if defined(_WIN32)
    #include <Windows.h>
#endif

namespace Lightweight::SqlBackup::detail
{

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
        progress.Update(
            { .state = Progress::State::Warning,
              .tableName = operation,
              .currentRows = 0,
              .totalRows = std::nullopt,
              .message = std::format("Connection failed, retry {}/{}: {}", attempts, settings.maxRetries, error.message) });

        std::this_thread::sleep_for(CalculateRetryDelay(attempts - 1, settings));
    }
}

std::string CurrentDateTime()
{
#if defined(_WIN32)
    // Windows 10 (pre-1903) and older UCRT versions do not ship with the IANA timezone database,
    // causing std::chrono formatting with timezone specifiers to throw exceptions.
    // Use Win32 GetSystemTime() to retrieve UTC time directly, avoiding timezone database dependency.
    SYSTEMTIME st;
    GetSystemTime(&st);
    return std::format(
        "{:04}-{:02}-{:02}T{:02}:{:02}:{:02}Z", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
#else
    auto const now = std::chrono::system_clock::now();
    return std::format("{:%FT%TZ}", now);
#endif
}

std::string FormatTableName(std::string_view schema, std::string_view table)
{
    if (schema.empty())
        return std::format(R"("{}")", table);
    return std::format(R"("{}"."{}")", schema, table);
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
        progress.Update(
            { .state = Progress::State::Error,
              .tableName = tableName,
              .currentRows = 0,
              .totalRows = 0,
              .message = std::format("DropTable failed: {} SQL: DROP TABLE IF EXISTS {}", e.what(), formattedTableName) });
        return false;
    }
}

} // namespace Lightweight::SqlBackup::detail
