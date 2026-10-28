// SPDX-License-Identifier: Apache-2.0

#include "../SqlStatement.hpp"
#include "Common.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <expected>
#include <format>
#include <initializer_list>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <span>
#include <string>
#include <string_view>
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

namespace
{
    /// Whether @p byte is an ASCII control character (0x00-0x1F or 0x7F).
    constexpr bool IsControlByte(char byte) noexcept
    {
        auto const value = static_cast<unsigned char>(byte);
        return value < 0x20 || value == 0x7F;
    }

    /// The lead bytes of one kind of well-formed UTF-8 sequence, the range of its second byte, and its length.
    struct Utf8SequenceKind
    {
        unsigned char leadLow;
        unsigned char leadHigh;
        unsigned char secondLow;
        unsigned char secondHigh;
        std::size_t length;
    };

    /// The well-formed UTF-8 sequences of the Unicode standard (table 3-7), which leave out overlong forms,
    /// surrogates and code points above U+10FFFF; later bytes of a sequence lie in 0x80-0xBF.
    constexpr auto Utf8SequenceKinds = std::array {
        Utf8SequenceKind { .leadLow = 0x00, .leadHigh = 0x7F, .secondLow = 0x80, .secondHigh = 0xBF, .length = 1 },
        Utf8SequenceKind { .leadLow = 0xC2, .leadHigh = 0xDF, .secondLow = 0x80, .secondHigh = 0xBF, .length = 2 },
        Utf8SequenceKind { .leadLow = 0xE0, .leadHigh = 0xE0, .secondLow = 0xA0, .secondHigh = 0xBF, .length = 3 },
        Utf8SequenceKind { .leadLow = 0xE1, .leadHigh = 0xEC, .secondLow = 0x80, .secondHigh = 0xBF, .length = 3 },
        Utf8SequenceKind { .leadLow = 0xED, .leadHigh = 0xED, .secondLow = 0x80, .secondHigh = 0x9F, .length = 3 },
        Utf8SequenceKind { .leadLow = 0xEE, .leadHigh = 0xEF, .secondLow = 0x80, .secondHigh = 0xBF, .length = 3 },
        Utf8SequenceKind { .leadLow = 0xF0, .leadHigh = 0xF0, .secondLow = 0x90, .secondHigh = 0xBF, .length = 4 },
        Utf8SequenceKind { .leadLow = 0xF1, .leadHigh = 0xF3, .secondLow = 0x80, .secondHigh = 0xBF, .length = 4 },
        Utf8SequenceKind { .leadLow = 0xF4, .leadHigh = 0xF4, .secondLow = 0x80, .secondHigh = 0x8F, .length = 4 },
    };

    /// Whether @p byte lies in [@p low, @p high].
    constexpr bool IsByteInRange(char byte, unsigned char low, unsigned char high) noexcept
    {
        auto const value = static_cast<unsigned char>(byte);
        return value >= low && value <= high;
    }

    /// The kind of UTF-8 sequence that @p lead starts, or std::nullopt when no well-formed sequence starts with it.
    std::optional<Utf8SequenceKind> FindUtf8SequenceKind(char lead) noexcept
    {
        for (auto const& kind: Utf8SequenceKinds)
            if (IsByteInRange(lead, kind.leadLow, kind.leadHigh))
                return kind;
        return std::nullopt;
    }

    /// Whether @p text is well-formed UTF-8.
    bool IsWellFormedUtf8(std::string_view text) noexcept
    {
        while (!text.empty())
        {
            auto const kind = FindUtf8SequenceKind(text.front());
            if (!kind || text.size() < kind->length)
                return false;
            auto const followers = text.substr(1, kind->length - 1);
            if (!followers.empty() && !IsByteInRange(followers.front(), kind->secondLow, kind->secondHigh))
                return false;
            if (!std::ranges::all_of(followers | std::views::drop(1),
                                     [](char byte) { return IsByteInRange(byte, 0x80, 0xBF); }))
                return false;
            text.remove_prefix(kind->length);
        }
        return true;
    }

    /// @p name in double quotes for a message. Control bytes, and every byte above 0x7F of a name that is no
    /// well-formed UTF-8, are written as `\xHH`.
    std::string QuotedName(std::string_view name)
    {
        auto const escapeHighBytes = !IsWellFormedUtf8(name);
        auto quoted = std::string { '"' };
        for (char const byte: name)
        {
            auto const value = static_cast<unsigned char>(byte);
            if (IsControlByte(byte) || (escapeHighBytes && value > 0x7F))
                quoted += std::format("\\x{:02X}", value);
            else
                quoted += byte;
        }
        quoted += '"';
        return quoted;
    }

    /// Whether one of the `/`-separated segments of @p name is one of @p segments.
    bool HasPathSegment(std::string_view name, std::initializer_list<std::string_view> segments)
    {
        return std::ranges::any_of(std::views::split(name, '/'), [segments](auto const& part) {
            return std::ranges::contains(segments, std::string_view { part.begin(), part.end() });
        });
    }

    /// A rule every attachment name must keep, with the message part that describes a violation.
    struct AttachmentNameRule
    {
        bool (*isViolatedBy)(std::string_view name);
        std::string_view problem;
    };

    constexpr auto AttachmentNameRules = std::array {
        AttachmentNameRule { .isViolatedBy = [](std::string_view name) { return name.empty(); }, .problem = "is empty" },
        AttachmentNameRule { .isViolatedBy = [](std::string_view name) { return std::ranges::any_of(name, IsControlByte); },
                             .problem = "contains a control character" },
        AttachmentNameRule { .isViolatedBy = [](std::string_view name) { return !IsWellFormedUtf8(name); },
                             .problem = "is not well-formed UTF-8" },
        AttachmentNameRule { .isViolatedBy = [](std::string_view name) { return name.starts_with('/'); },
                             .problem = "starts with '/'" },
        AttachmentNameRule { .isViolatedBy = [](std::string_view name) { return name.contains('\\'); },
                             .problem = "contains '\\'" },
        AttachmentNameRule { .isViolatedBy = [](std::string_view name) { return name.contains(':'); },
                             .problem = "contains ':'" },
        AttachmentNameRule { .isViolatedBy = [](std::string_view name) { return name.ends_with('/'); },
                             .problem = "ends with '/'" },
        AttachmentNameRule { .isViolatedBy = [](std::string_view name) { return HasPathSegment(name, { "" }); },
                             .problem = "has an empty path segment" },
        AttachmentNameRule { .isViolatedBy = [](std::string_view name) { return HasPathSegment(name, { ".", ".." }); },
                             .problem = "has a '.' or '..' path segment" },
    };
} // namespace

std::expected<void, std::string> ValidateAttachmentNames(std::span<std::string const> names)
{
    auto namesByLowerCase = std::map<std::string, std::string_view> {};
    for (auto const& name: names)
    {
        for (auto const& rule: AttachmentNameRules)
            if (rule.isViolatedBy(name))
                return std::unexpected { std::format("attachment name {} {}", QuotedName(name), rule.problem) };

        auto const [existing, inserted] = namesByLowerCase.emplace(ToLowerAscii(name), name);
        if (!inserted)
            return std::unexpected { std::format(
                "attachment name {} duplicates {}, ignoring letter case", QuotedName(name), QuotedName(existing->second)) };
    }

    // A file cannot be extracted next to a folder of the same name.
    for (auto const& name: names)
    {
        auto const folder = ToLowerAscii(name) + '/';
        if (auto const inside = namesByLowerCase.lower_bound(folder);
            inside != namesByLowerCase.end() && inside->first.starts_with(folder))
            return std::unexpected { std::format(
                "attachment name {} is also the folder of {}", QuotedName(name), QuotedName(inside->second)) };
    }
    return {};
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
