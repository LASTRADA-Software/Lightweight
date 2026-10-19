// SPDX-License-Identifier: Apache-2.0

#include "../SqlStatement.hpp"
#include "Common.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <format>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <Windows.h>
#endif

namespace Lightweight::SqlBackup::detail
{

bool IsTransientError(SqlErrorInfo const& error)
{
    return GenericRetryOps().IsTransient(error);
}

std::chrono::milliseconds CalculateRetryDelay(unsigned attempt, RetrySettings const& settings) noexcept
{
    return SqlRetryPolicy { settings }.DelayFor(attempt);
}

RetryAction ClassifyRetryOutcome(SqlErrorInfo const& error, unsigned attemptsSoFar, RetrySettings const& settings)
{
    return SqlRetryPolicy { settings }.Decide(error, SqlRetryState { .retriesSoFar = attemptsSoFar }).action;
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
        if (ClassifyRetryOutcome(error, attempts, settings) == RetryAction::GiveUp)
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
    auto const quoted = [](std::string_view name) {
        auto result = std::string { '"' };
        for (char const character: name)
        {
            result += character;
            if (character == '"')
                result += '"';
        }
        result += '"';
        return result;
    };
    if (schema.empty())
        return quoted(table);
    return std::format("{}.{}", quoted(schema), quoted(table));
}

std::string ChunkDirectory(std::string_view schema, std::string_view table)
{
    auto const sanitize = [](std::string_view name) {
        auto sanitized = std::string { name };
        std::ranges::replace(sanitized, '.', '_');
        return sanitized;
    };
    if (schema.empty())
        return sanitize(table);
    return std::format("{}/{}", sanitize(schema), sanitize(table));
}

std::optional<std::string_view> ChunkDirectoryOf(std::string_view entryName)
{
    constexpr auto DataPrefix = std::string_view { "data/" };
    if (!entryName.starts_with(DataPrefix) || !entryName.ends_with(".msgpack"))
        return std::nullopt;
    auto const lastSlash = entryName.rfind('/');
    if (lastSlash <= DataPrefix.size())
        return std::nullopt;
    return entryName.substr(DataPrefix.size(), lastSlash - DataPrefix.size());
}

std::string JoinNames(std::vector<std::string> const& names)
{
    std::string joined;
    for (auto const& name: names)
    {
        if (!joined.empty())
            joined += ", ";
        joined += name;
    }
    return joined;
}

std::string ToLowerAscii(std::string_view text)
{
    auto lowered = std::string { text };
    std::ranges::transform(lowered, lowered.begin(), [](char character) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    });
    return lowered;
}

std::vector<std::string> NormalizeSchemaNames(std::vector<std::string> schemas, bool caseInsensitive)
{
    if (schemas.empty())
        schemas.emplace_back();
    std::set<std::string> seen;
    std::erase_if(schemas,
                  [&](std::string const& name) { return !seen.insert(caseInsensitive ? ToLowerAscii(name) : name).second; });
    std::ranges::sort(schemas);
    return schemas;
}

std::string DescribeTableNameCollisions(SqlSchema::TableList const& tables, bool multiSchema)
{
    auto const displayName = [](SqlSchema::Table const& table) {
        return table.schema.empty() ? table.name : std::format("{}.{}", table.schema, table.name);
    };

    std::map<std::string, SqlSchema::Table const*> tablesByKey;
    std::map<std::string, SqlSchema::Table const*> tablesByFolder;
    std::set<std::string> colliding;
    bool acrossSchemas = false;
    for (auto const& table: tables)
    {
        auto const& archiveSchema = multiSchema ? table.schema : std::string {};
        auto const key = archiveSchema.empty() ? table.name : std::format("{}.{}", archiveSchema, table.name);
        auto const [keyEntry, newKey] = tablesByKey.emplace(key, &table);
        auto const [folderEntry, newFolder] = tablesByFolder.emplace(ChunkDirectory(archiveSchema, table.name), &table);
        if (newKey && newFolder)
            continue;
        auto const& other = newKey ? *folderEntry->second : *keyEntry->second;
        acrossSchemas = acrossSchemas || (!multiSchema && other.schema != table.schema);
        colliding.insert(displayName(other));
        colliding.insert(displayName(table));
    }
    if (colliding.empty())
        return {};

    auto const names = JoinNames({ colliding.begin(), colliding.end() });
    if (acrossSchemas)
        return std::format("tables of the same name exist in several schemas ({}); back these schemas up as a "
                           "multi-schema archive, which keeps each table's schema (dbtool: --all-schemas or "
                           "--schema a,b)",
                           names);
    return std::format("these tables would share an archive folder, because a dot in a name is written as an "
                       "underscore: {}",
                       names);
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
            SqlStatement { conn }.ExecuteDirect(sql).ConsumePendingResults();
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
