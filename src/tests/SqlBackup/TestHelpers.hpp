#pragma once

#include <Lightweight/SqlBackup.hpp>
#include <Lightweight/SqlBackup/BatchManager.hpp>

#if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wnullability-extension"
#endif
#include <zip.h>
#if defined(__clang__)
    #pragma clang diagnostic pop
#endif

#include <algorithm>
#include <filesystem>
#include <format>
#include <functional>
#include <iostream>
#include <mutex>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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

template <typename Executor>
void RunBatchManagerTest(Executor&& executor,
                         std::vector<SqlColumnDeclaration> const& cols,
                         std::vector<std::vector<SqlBackup::BackupValue>> const& rows,
                         size_t capacity,
                         SqlServerType serverType = SqlServerType::UNKNOWN)
{
    Lightweight::detail::BatchManager bm(std::forward<Executor>(executor), cols, capacity, serverType);
    for (auto const& row: rows)
        bm.PushRow(row);
    bm.Flush();
}

template <typename Executor>
void RunBatchManagerBatchTest(Executor&& executor,
                              std::vector<SqlColumnDeclaration> const& cols,
                              std::vector<SqlBackup::ColumnBatch> const& batches,
                              size_t capacity,
                              SqlServerType serverType = SqlServerType::UNKNOWN)
{
    Lightweight::detail::BatchManager bm(std::forward<Executor>(executor), cols, capacity, serverType);
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
