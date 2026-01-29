#pragma once

#include <Lightweight/SqlBackup.hpp>
#include <Lightweight/SqlBackup/BatchManager.hpp>

#include <filesystem>
#include <functional>
#include <iostream>
#include <mutex>
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

} // namespace Lightweight::SqlBackup::Tests
