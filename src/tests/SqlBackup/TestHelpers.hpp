#pragma once

#include <Lightweight/SqlBackup.hpp>
#include <Lightweight/SqlBackup/BatchManager.hpp>
#include <Lightweight/SqlBackup/MsgPackChunkFormats.hpp>
#include <Lightweight/SqlBackup/Sha256.hpp>
#include <Lightweight/SqlStatement.hpp>
#include <Lightweight/Zip/ZipArchive.hpp>

#if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wnullability-extension"
#endif
#include <zip.h>
#if defined(__clang__)
    #pragma clang diagnostic pop
#endif

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <functional>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace Lightweight::SqlBackup::Tests
{

struct LambdaProgressManager: SqlBackup::ProgressManager
{
    mutable std::mutex mutex;
    std::function<void(SqlBackup::Progress const&)> callback;

    explicit LambdaProgressManager(std::function<void(SqlBackup::Progress const&)> cb):
        callback(std::move(cb))
    {
    }

    void Update(SqlBackup::Progress const& p) override
    {
        auto const lock = std::scoped_lock(mutex);
        if (callback)
            callback(p);
    }

    void AllDone() override
    {
        // do nothing
    }
};

/// Records every progress update of a backup or restore, and counts the errors as dbtool does
/// for its exit code. Safe to update from several restore workers at once.
struct RecordingProgressManager: SqlBackup::ErrorTrackingProgressManager
{
    mutable std::mutex mutex;
    /// The message of each update, in order.
    std::vector<std::string> messages;
    /// The state of each update, parallel to `messages`.
    std::vector<SqlBackup::Progress::State> states;
    /// The table of each update, parallel to `messages`.
    std::vector<std::string> tables;

    void Update(SqlBackup::Progress const& progress) override
    {
        auto const lock = std::scoped_lock(mutex);
        ErrorTrackingProgressManager::Update(progress);
        messages.emplace_back(progress.message);
        states.emplace_back(progress.state);
        tables.emplace_back(progress.tableName);
    }

    void AllDone() override {}

    /// Tests whether an error update's message contains @p needle.
    /// @param needle The text to look for.
    /// @return True if some error update's message contains @p needle.
    [[nodiscard]] bool HasErrorContaining(std::string_view needle) const
    {
        auto const lock = std::scoped_lock(mutex);
        return std::ranges::any_of(std::views::iota(size_t { 0 }, messages.size()), [&](size_t index) {
            return states[index] == SqlBackup::Progress::State::Error && messages[index].contains(needle);
        });
    }

    /// Returns the messages of the errors reported for @p tableName.
    /// @param tableName The table.
    /// @return The messages, in order.
    [[nodiscard]] std::vector<std::string> ErrorsOf(std::string_view tableName) const
    {
        auto const lock = std::scoped_lock(mutex);
        std::vector<std::string> result;
        for (auto const index: std::views::iota(size_t { 0 }, messages.size()))
            if (states[index] == SqlBackup::Progress::State::Error && tables[index] == tableName)
                result.push_back(messages[index]);
        return result;
    }
};

/// Returns the failure report of @p tableName in @p report.
/// @param report The report a restore returned.
/// @param tableName The table.
/// @return The table's report, or an empty one if the table has no failures.
inline TableRestoreReport TableReportOf(RestoreReport const& report, std::string_view tableName)
{
    auto const table = std::ranges::find(report.tables, tableName, &TableRestoreReport::tableName);
    if (table != report.tables.end())
        return *table;
    auto empty = TableRestoreReport {};
    empty.tableName = tableName;
    return empty;
}

/// What `RewriteArchive` makes of one entry: its new name and content, or std::nullopt to drop it.
using ArchiveEntryRewrite =
    std::function<std::optional<std::pair<std::string, std::string>>(std::string const& name, std::string content)>;

/// Writes @p target with the entries of @p source, each passed through @p rewrite.
inline void RewriteArchive(std::filesystem::path const& source,
                           std::filesystem::path const& target,
                           ArchiveEntryRewrite const& rewrite)
{
    auto input = Zip::ZipArchive::Open(source);
    if (!input)
        throw std::runtime_error("cannot open " + input.error().message);
    auto output = Zip::ZipArchive::Create(target);
    if (!output)
        throw std::runtime_error("cannot create " + output.error().message);
    for (auto const& entry: input->GetAllEntries())
    {
        auto content = input->ReadEntryAsString(entry.index);
        if (!content)
            throw std::runtime_error("cannot read " + entry.name);
        if (auto const rewritten = rewrite(entry.name, std::move(*content)))
            if (!output->AddString(rewritten->first, rewritten->second))
                throw std::runtime_error("cannot write " + rewritten->first);
    }
    if (!output->Close())
        throw std::runtime_error("cannot close the rewritten archive");
}

/// Returns the row count of @p tableName, or -1 if it cannot be read.
inline int64_t CountRows(std::string_view tableName)
{
    return SqlStatement {}.ExecuteDirectScalar<int64_t>(std::format(R"(SELECT COUNT(*) FROM "{}")", tableName)).value_or(-1);
}

/// Returns the sorted `ID`s of @p tableName.
inline std::vector<int64_t> ReadIds(std::string_view tableName)
{
    auto stmt = SqlStatement {};
    auto cursor = stmt.ExecuteDirect(std::format(R"(SELECT "ID" FROM "{}" ORDER BY "ID")", tableName));
    std::vector<int64_t> ids;
    while (cursor.FetchRow())
        ids.push_back(cursor.GetColumn<int64_t>(1));
    return ids;
}

/// Drops @p tableNames.
inline void DropTables(std::vector<std::string> const& tableNames)
{
    SqlStatement {}.MigrateDirect([&](SqlMigrationQueryBuilder& migration) {
        for (auto const& name: tableNames)
            migration.DropTable(name);
    });
}

/// Encodes @p rows as one MsgPack data chunk.
inline std::string MakeChunk(std::vector<std::vector<BackupValue>> const& rows)
{
    auto writer = CreateMsgPackChunkWriter();
    for (auto const& row: rows)
        writer->WriteRow(row);
    return writer->Flush();
}

/// Encodes @p batches as one MsgPack data chunk holding one batch per element, in order.
inline std::string MakeMultiBatchChunk(std::vector<std::vector<std::vector<BackupValue>>> const& batches)
{
    auto chunk = std::string {};
    for (auto const& rows: batches)
        chunk += MakeChunk(rows);
    return chunk;
}

/// Returns the sorted names of the data chunks of @p tableName in @p archive.
/// @throws std::runtime_error if @p archive cannot be opened.
inline std::vector<std::string> ChunkNames(std::filesystem::path const& archive, std::string_view tableName)
{
    auto const zip = Zip::ZipArchive::Open(archive);
    if (!zip)
        throw std::runtime_error("cannot open " + zip.error().message);
    auto const prefix = std::format("data/{}/", tableName);
    std::vector<std::string> names;
    for (auto const& entry: zip->GetAllEntries())
        if (entry.name.starts_with(prefix))
            names.push_back(entry.name);
    std::ranges::sort(names);
    return names;
}

/// Whether RewriteChunks() updates `checksums.json` for the chunks it replaces.
enum class ChecksumUpdate : std::uint8_t
{
    /// The replaced chunks keep their old checksum, so the restore detects them as corrupt.
    Keep,
    /// The replaced chunks get a valid checksum, so the restore reads their content.
    Recompute,
};

/// Copies @p source to @p target, replacing the content of the entries named in @p replacements.
inline void RewriteChunks(std::filesystem::path const& source,
                          std::filesystem::path const& target,
                          std::map<std::string, std::string> const& replacements,
                          ChecksumUpdate checksums = ChecksumUpdate::Recompute)
{
    RewriteArchive(source,
                   target,
                   [&](std::string const& name, std::string content) -> std::optional<std::pair<std::string, std::string>> {
                       if (auto const replacement = replacements.find(name); replacement != replacements.end())
                           return std::pair { name, replacement->second };
                       if (name == "checksums.json" && checksums == ChecksumUpdate::Recompute)
                       {
                           auto json = nlohmann::json::parse(content);
                           for (auto const& [entry, replacementContent]: replacements)
                               json["files"][entry] = Sha256::Hash(replacementContent);
                           return std::pair { name, json.dump() };
                       }
                       return std::pair { name, std::move(content) };
                   });
}

struct ScopedFileRemoved
{
    std::filesystem::path path;

    void RemoveIfExists() const
    {
        std::error_code ec;
        if (std::filesystem::exists(path, ec))
            std::filesystem::remove(path, ec);
    }

    explicit ScopedFileRemoved(std::filesystem::path path):
        path(std::move(path))
    {
        RemoveIfExists();
    }

    ~ScopedFileRemoved()
    {
        RemoveIfExists();
    }

    ScopedFileRemoved(ScopedFileRemoved const&) = delete;
    ScopedFileRemoved& operator=(ScopedFileRemoved const&) = delete;
    ScopedFileRemoved(ScopedFileRemoved&&) = delete;
    ScopedFileRemoved& operator=(ScopedFileRemoved&&) = delete;
};

/// Receives the rows a BatchManager inserts: the raw columns of a range and its row count.
using BatchExecutor = std::function<void(std::vector<SqlRawColumn> const& rawColumns, std::size_t rowCount)>;

/// Hooks that insert every range by passing it to @p executor, whose exceptions propagate from
/// BatchManager::Flush().
/// @param executor Receives each inserted range.
/// @return The hooks.
inline Lightweight::detail::BatchManagerHooks MakeHooks(BatchExecutor executor)
{
    return Lightweight::detail::BatchManagerHooks {
        .tryInsert = [executor = std::move(executor)](std::span<SqlRawColumn const> slice, std::size_t rowCount)
            -> std::expected<void, Lightweight::detail::BatchInsertError> {
            executor(std::vector<SqlRawColumn>(slice.begin(), slice.end()), rowCount);
            return {};
        },
        .classify = {},
        .onRowFailed = {},
        .onCellNulled = {},
        .onSettled = {},
    };
}

inline void RunBatchManagerTest(BatchExecutor executor,
                                std::vector<SqlColumnDeclaration> const& cols,
                                std::vector<std::vector<SqlBackup::BackupValue>> const& rows,
                                size_t capacity,
                                SqlServerType serverType = SqlServerType::UNKNOWN,
                                SqlServerType sourceServerType = SqlServerType::UNKNOWN)
{
    Lightweight::detail::BatchManager bm(MakeHooks(std::move(executor)), cols, capacity, serverType, sourceServerType);
    for (auto const& row: rows)
        bm.PushRow(row);
    bm.Flush();
}

inline void RunBatchManagerBatchTest(BatchExecutor executor,
                                     std::vector<SqlColumnDeclaration> const& cols,
                                     std::vector<SqlBackup::ColumnBatch> const& batches,
                                     size_t capacity,
                                     SqlServerType serverType = SqlServerType::UNKNOWN,
                                     SqlServerType sourceServerType = SqlServerType::UNKNOWN)
{
    Lightweight::detail::BatchManager bm(MakeHooks(std::move(executor)), cols, capacity, serverType, sourceServerType);
    for (auto const& batch: batches)
        bm.PushBatch(batch);
    bm.Flush();
}

/// Copies the backup archive @p source to @p target, replacing its `metadata.json` with what
/// @p rewriteMetadata returns for the original. Entries are written in name order, so a test
/// controls the order in which a restore takes the tables' chunks (`data/<table>/...`).
/// @throws std::runtime_error if an archive cannot be read or written.
inline void RewriteArchiveMetadata(std::filesystem::path const& source,
                                   std::filesystem::path const& target,
                                   std::function<std::string(std::string const&)> const& rewriteMetadata)
{
    // NOLINTBEGIN(clang-analyzer-nullability.*)
    auto const check = [](bool ok, std::string_view what) {
        if (!ok)
            throw std::runtime_error(std::format("RewriteArchiveMetadata: {} failed", what));
    };

    int error = 0;
    zip_t* sourceZip = zip_open(source.string().c_str(), ZIP_RDONLY, &error);
    check(sourceZip != nullptr, "opening the source archive");

    std::vector<std::pair<std::string, zip_uint64_t>> entries;
    auto const entryCount = static_cast<zip_uint64_t>(zip_get_num_entries(sourceZip, 0));
    for (auto const index: std::views::iota(zip_uint64_t { 0 }, entryCount))
        if (char const* name = zip_get_name(sourceZip, index, 0); name != nullptr)
            entries.emplace_back(name, index);
    std::ranges::sort(entries);

    zip_stat_t metadataStat {};
    check(zip_stat(sourceZip, "metadata.json", 0, &metadataStat) == 0, "finding metadata.json");
    std::string metadata(static_cast<size_t>(metadataStat.size), '\0');
    zip_file_t* metadataFile = zip_fopen(sourceZip, "metadata.json", 0);
    check(metadataFile != nullptr, "opening metadata.json");
    auto const bytesRead = zip_fread(metadataFile, metadata.data(), metadataStat.size);
    zip_fclose(metadataFile);
    check(std::cmp_equal(bytesRead, metadataStat.size), "reading metadata.json");
    std::string const rewritten = rewriteMetadata(metadata); // must outlive zip_close

    zip_t* targetZip = zip_open(target.string().c_str(), ZIP_CREATE | ZIP_TRUNCATE, &error);
    check(targetZip != nullptr, "creating the target archive");
    for (auto const& [name, index]: entries)
    {
        zip_source_t* entrySource = nullptr;
        if (name == "metadata.json")
            entrySource = zip_source_buffer(targetZip, rewritten.data(), rewritten.size(), 0);
        else
#if LIBZIP_VERSION_MAJOR > 1 || (LIBZIP_VERSION_MAJOR == 1 && LIBZIP_VERSION_MINOR >= 8)
            entrySource = zip_source_zip_file(targetZip, sourceZip, index, 0, 0, -1, nullptr);
#else
            // NOLINTNEXTLINE(clang-diagnostic-deprecated-declarations)
            entrySource = zip_source_zip(targetZip, sourceZip, index, 0, 0, -1);
#endif
        check(zip_file_add(targetZip, name.c_str(), entrySource, ZIP_FL_OVERWRITE) >= 0, "adding an entry");
    }
    check(zip_close(targetZip) == 0, "writing the target archive");
    zip_close(sourceZip);
    // NOLINTEND(clang-analyzer-nullability.*)
}

} // namespace Lightweight::SqlBackup::Tests
