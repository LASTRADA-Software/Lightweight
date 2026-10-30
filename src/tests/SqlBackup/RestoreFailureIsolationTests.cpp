// SPDX-License-Identifier: Apache-2.0
//
// A value or row that fails to restore never aborts the restore: the row is isolated and reported,
// the rest of its chunk, its table and the other tables are restored, and Restore() returns a
// bounded failure report. Only a lost connection stops a worker.

#include "../ScriptedFaultSource.hpp"
#include "../Utils.hpp"
#include "TestHelpers.hpp"

#include <Lightweight/SqlBackup.hpp>
#include <Lightweight/SqlBackup/Common.hpp>
#include <Lightweight/SqlBackup/Restore.hpp>
#include <Lightweight/SqlColumnTypeDefinitions.hpp>
#include <Lightweight/SqlConnection.hpp>
#include <Lightweight/SqlErrorDetection.hpp>
#include <Lightweight/SqlQueryFormatter.hpp>
#include <Lightweight/SqlRetryPolicy.hpp>
#include <Lightweight/SqlStatement.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <map>
#include <mutex>
#include <optional>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

using namespace Lightweight;
using Lightweight::SqlBackup::Tests::ChecksumUpdate;
using Lightweight::SqlBackup::Tests::ChunkNames;
using Lightweight::SqlBackup::Tests::CountRows;
using Lightweight::SqlBackup::Tests::DropTables;
using Lightweight::SqlBackup::Tests::MakeChunk;
using Lightweight::SqlBackup::Tests::MakeMultiBatchChunk;
using Lightweight::SqlBackup::Tests::ReadIds;
using Lightweight::SqlBackup::Tests::RecordingProgressManager;
using Lightweight::SqlBackup::Tests::RewriteChunks;
using Lightweight::SqlBackup::Tests::ScopedFileRemoved;
using Lightweight::SqlBackup::Tests::TableReportOf;
using Lightweight::Tests::ScopedFaultSource;
using Lightweight::Tests::ScriptedFaultSource;

namespace
{

using Row = std::vector<SqlBackup::BackupValue>;

SqlConnectionString const& GetConnectionString()
{
    return SqlConnection::DefaultConnectionString();
}

/// Creates `<tableName>(ID INTEGER PRIMARY KEY, V VARCHAR(20))` holding rows 1..@p rowCount.
void CreateTableWithRows(std::string const& tableName, int rowCount)
{
    using namespace SqlColumnTypeDefinitions;
    auto stmt = SqlStatement {};
    stmt.MigrateDirect([&](SqlMigrationQueryBuilder& migration) {
        migration.CreateTable(tableName).PrimaryKey("ID", Integer {}).Column("V", Varchar { 20 });
    });
    for (auto const id: std::views::iota(1, rowCount + 1))
        (void) stmt.ExecuteDirect(std::format(R"(INSERT INTO "{0}" ("ID", "V") VALUES ({1}, 'row {1}'))", tableName, id));
}

/// The `(ID, V)` rows with the given IDs, as a backup of CreateTableWithRows() holds them.
std::vector<Row> IdValueRows(std::vector<int64_t> const& ids)
{
    std::vector<Row> rows;
    rows.reserve(ids.size());
    for (auto const id: ids)
        rows.push_back(Row { id, std::format("row {}", id) });
    return rows;
}

/// Remembers the row count each table finished with.
struct FinalRowsProgressManager: RecordingProgressManager
{
    /// The `currentRows` of the last finishing update, per table.
    std::map<std::string, std::size_t> finalRows;

    void Update(SqlBackup::Progress const& progress) override
    {
        RecordingProgressManager::Update(progress);
        if (progress.state == SqlBackup::Progress::State::Finished && !progress.tableName.empty())
        {
            auto const lock = std::scoped_lock(mutex);
            finalRows[progress.tableName] = progress.currentRows;
        }
    }

    /// The sum of the counts reported via OnItemsProcessed().
    std::atomic<std::size_t> itemsProcessed = 0;

    void OnItemsProcessed(std::size_t count) override
    {
        itemsProcessed += count;
    }
};

/// The number of checks one `ExecuteBatch` of a clean restore of @p restore makes, found by
/// counting them during a restore of three batches.
/// @param restore Restores the archive into an empty table.
/// @return The number of checks per batch.
template <typename RestoreFn>
int CountChecksPerBatch(RestoreFn const& restore)
{
    auto counter = ScriptedFaultSource { SqlErrorInfo {}, 0, "ExecuteBatch" };
    {
        ScopedFaultSource const installed { &counter };
        SqlBackup::NullProgressManager progress;
        CHECK_FALSE(restore(progress).HasFailures());
    }
    REQUIRE(counter.ConsultCount() % 3 == 0);
    REQUIRE(counter.ConsultCount() > 0);
    return counter.ConsultCount() / 3;
}

/// Retry settings that retry quickly.
SqlBackup::RetrySettings FastRetry()
{
    return SqlBackup::RetrySettings {
        .maxRetries = 3,
        .initialDelay = std::chrono::milliseconds { 1 },
        .backoffMultiplier = 1.0,
        .maxDelay = std::chrono::milliseconds { 1 },
    };
}

/// Backs up the database with @p rowsPerChunk rows per chunk.
void BackupTo(std::filesystem::path const& archive, std::size_t rowsPerChunk = 0)
{
    SqlBackup::NullProgressManager progress;
    auto settings = SqlBackup::BackupSettings {};
    if (rowsPerChunk > 0)
        settings.rowsPerChunk = rowsPerChunk;
    REQUIRE_NOTHROW(SqlBackup::Backup(archive, GetConnectionString(), 1, progress, {}, "*", {}, settings));
}

} // namespace

TEST_CASE_METHOD(SqlTestFixture, "duplicate primary keys fail only the duplicate rows", "[SqlBackup][RestoreFailures]")
{
    std::filesystem::path const backupFile = "rfi_duplicates_backup.zip";
    std::filesystem::path const brokenFile = "rfi_duplicates_broken.zip";
    auto const backupFileCleaner = ScopedFileRemoved { backupFile };
    auto const brokenFileCleaner = ScopedFileRemoved { brokenFile };

    CreateTableWithRows("rfi_dup_a", 10);
    CreateTableWithRows("rfi_dup_b", 5);
    BackupTo(backupFile);
    DropTables({ "rfi_dup_a", "rfi_dup_b" });

    auto const chunks = ChunkNames(backupFile, "rfi_dup_a");
    REQUIRE(chunks.size() == 1);
    auto const& chunk = chunks.front();
    // Rows 4 and 9 duplicate row 1's ID.
    RewriteChunks(backupFile, brokenFile, { { chunk, MakeChunk(IdValueRows({ 1, 2, 3, 1, 5, 6, 7, 8, 1, 10 })) } });

    RecordingProgressManager progress;
    auto const report = SqlBackup::Restore(brokenFile, GetConnectionString(), 1, progress);

    // Rows after the failures in the same chunk, on the same worker connection, are present: the
    // failed batches were rolled back cleanly (on PostgreSQL that is the 25P02 guard).
    CHECK(ReadIds("rfi_dup_a") == std::vector<int64_t> { 1, 2, 3, 5, 6, 7, 8, 10 });
    CHECK(CountRows("rfi_dup_b") == 5);

    CHECK(report.HasFailures());
    CHECK(report.TotalFailedRows() == 2);
    CHECK_FALSE(report.WorkerStopped());
    CHECK(report.incompleteTables.empty());
    auto const tableReport = TableReportOf(report, "rfi_dup_a");
    CHECK(tableReport.failedRowCount == 2);
    REQUIRE(tableReport.failedRowRanges.size() == 2);
    CHECK(tableReport.failedRowRanges[0].chunkPath == chunk);
    CHECK(tableReport.failedRowRanges[0].firstRow == 4);
    CHECK(tableReport.failedRowRanges[0].lastRow == 4);
    CHECK(tableReport.failedRowRanges[1].chunkPath == chunk);
    CHECK(tableReport.failedRowRanges[1].firstRow == 9);
    CHECK(tableReport.failedRowRanges[1].lastRow == 9);
    REQUIRE(tableReport.reasons.size() == 1);
    CHECK(tableReport.reasons[0].occurrences == 2);
    CHECK(std::ranges::contains(tableReport.reasons[0].sampleKeys, std::string { "1" }));
    CHECK_FALSE(TableReportOf(report, "rfi_dup_b").HasFailures());

    // dbtool exits non-zero; the table's single terminal update names the failures.
    CHECK(progress.ErrorCount() > 0);
    auto const errors = progress.ErrorsOf("rfi_dup_a");
    CHECK(std::ranges::count(errors, std::string { "2 rows failed, 0 cells set to NULL" }) == 1);
    CHECK(progress.ErrorsOf("rfi_dup_b").empty());
}

TEST_CASE_METHOD(SqlTestFixture, "nullable unconvertible value is restored as NULL", "[SqlBackup][RestoreFailures]")
{
    using namespace SqlColumnTypeDefinitions;

    // Only SQLite stores 300 in a TINYINT column (SQL Server refuses it; PostgreSQL has no TINYINT).
    auto stmt = SqlStatement {};
    if (stmt.Connection().ServerType() != SqlServerType::SQLITE)
        return;

    std::filesystem::path const backupFile = "rfi_nullable_backup.zip";
    auto const backupFileCleaner = ScopedFileRemoved { backupFile };
    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.CreateTable("rfi_nullable").PrimaryKey("ID", Integer {}).Column("T", Tinyint {});
    });
    (void) stmt.ExecuteDirect(R"(INSERT INTO "rfi_nullable" ("ID", "T") VALUES (1, 300))");
    BackupTo(backupFile);
    DropTables({ "rfi_nullable" });

    RecordingProgressManager progress;
    auto const report = SqlBackup::Restore(backupFile, GetConnectionString(), 1, progress);

    CHECK(CountRows("rfi_nullable") == 1);
    CHECK(SqlStatement {}.ExecuteDirectScalar<int64_t>(R"(SELECT "T" FROM "rfi_nullable")") == std::nullopt);
    auto const tableReport = TableReportOf(report, "rfi_nullable");
    CHECK(tableReport.failedRowCount == 0);
    REQUIRE(tableReport.valueFailures.size() == 1);
    CHECK(tableReport.valueFailures[0].column == "T");
    CHECK(tableReport.valueFailures[0].cellsSetToNull == 1);
    CHECK(progress.ErrorsOf("rfi_nullable") == std::vector<std::string> { "0 rows failed, 1 cells set to NULL" });
}

TEST_CASE_METHOD(SqlTestFixture, "NOT NULL unconvertible value fails only its row", "[SqlBackup][RestoreFailures]")
{
    using namespace SqlColumnTypeDefinitions;

    auto stmt = SqlStatement {};
    if (stmt.Connection().ServerType() != SqlServerType::SQLITE)
        return;

    std::filesystem::path const backupFile = "rfi_required_backup.zip";
    auto const backupFileCleaner = ScopedFileRemoved { backupFile };
    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.CreateTable("rfi_required").PrimaryKey("ID", Integer {}).RequiredColumn("T", Tinyint {});
    });
    (void) stmt.ExecuteDirect(R"(INSERT INTO "rfi_required" ("ID", "T") VALUES (1, 5), (2, 300), (3, 7))");
    BackupTo(backupFile);
    DropTables({ "rfi_required" });
    auto const chunks = ChunkNames(backupFile, "rfi_required");
    REQUIRE(chunks.size() == 1);

    RecordingProgressManager progress;
    auto const report = SqlBackup::Restore(backupFile, GetConnectionString(), 1, progress);

    CHECK(ReadIds("rfi_required") == std::vector<int64_t> { 1, 3 });
    auto const tableReport = TableReportOf(report, "rfi_required");
    CHECK(tableReport.failedRowCount == 1);
    REQUIRE(tableReport.failedRowRanges.size() == 1);
    CHECK(tableReport.failedRowRanges[0].chunkPath == chunks.front());
    CHECK(tableReport.failedRowRanges[0].firstRow == 2);
    CHECK(tableReport.failedRowRanges[0].lastRow == 2);
    CHECK(tableReport.valueFailures.empty());
}

TEST_CASE_METHOD(SqlTestFixture,
                 "bind-class error fails the table and the restore continues",
                 "[SqlBackup][RestoreFailures]")
{
    std::filesystem::path const backupFile = "rfi_bind_backup.zip";
    auto const backupFileCleaner = ScopedFileRemoved { backupFile };

    CreateTableWithRows("rfi_bind_a", 3);
    CreateTableWithRows("rfi_bind_b", 4);
    BackupTo(backupFile);
    DropTables({ "rfi_bind_a", "rfi_bind_b" });

    auto source = ScriptedFaultSource {
        SqlErrorInfo { .nativeErrorCode = 0, .sqlState = "HYC00", .message = "Driver does not support this parameter" },
        1,
        "ExecuteBatch"
    };
    auto report = SqlBackup::RestoreReport {};
    RecordingProgressManager progress;
    {
        ScopedFaultSource const installed { &source };
        report = SqlBackup::Restore(backupFile, GetConnectionString(), 1, progress);
    }

    // One injection, and no retry of it: a retry would have succeeded and inserted rows.
    CHECK(source.RemainingFailures() == 0);
    auto const reportA = TableReportOf(report, "rfi_bind_a");
    auto const reportB = TableReportOf(report, "rfi_bind_b");
    auto const aFailed = reportA.tableFailure.has_value();
    auto const bFailed = reportB.tableFailure.has_value();
    REQUIRE(aFailed != bFailed);
    auto const& failed = aFailed ? reportA : reportB;
    auto const restoredName = std::string { aFailed ? "rfi_bind_b" : "rfi_bind_a" };
    CHECK(failed.tableFailure.value_or("").starts_with("HYC00"));
    CHECK(CountRows(failed.tableName) == 0);
    CHECK(CountRows(restoredName) == (aFailed ? 4 : 3));
    CHECK_FALSE(TableReportOf(report, restoredName).HasFailures());
    CHECK_FALSE(report.WorkerStopped());
}

TEST_CASE_METHOD(SqlTestFixture,
                 "transient error in the chunk transaction does not duplicate rows",
                 "[SqlBackup][RestoreFailures]")
{
    std::filesystem::path const backupFile = "rfi_transient_backup.zip";
    auto const backupFileCleaner = ScopedFileRemoved { backupFile };

    CreateTableWithRows("rfi_transient", 6);
    BackupTo(backupFile);
    DropTables({ "rfi_transient" });

    // memoryLimitBytes > 0, or Restore() replaces these settings and batches ~4000 rows.
    auto const settings = SqlBackup::RestoreSettings { .batchSize = 2, .memoryLimitBytes = std::size_t { 1 } << 30U };
    auto const restore = [&](SqlBackup::ProgressManager& progress) {
        return SqlBackup::Restore(backupFile, GetConnectionString(), 1, progress, {}, "*", FastRetry(), settings);
    };

    auto const checksPerBatch = CountChecksPerBatch(restore);
    REQUIRE(CountRows("rfi_transient") == 6);

    // The second batch hits a lost connection after the first one went into the chunk transaction.
    auto source = ScriptedFaultSource {
        SqlErrorInfo { .nativeErrorCode = 0, .sqlState = "08S01", .message = "Communication link failure" },
        1,
        "ExecuteBatch",
        checksPerBatch,
    };
    auto report = SqlBackup::RestoreReport {};
    {
        ScopedFaultSource const installed { &source };
        RecordingProgressManager progress;
        report = restore(progress);
    }

    CHECK(source.RemainingFailures() == 0);
    CHECK(ReadIds("rfi_transient") == std::vector<int64_t> { 1, 2, 3, 4, 5, 6 });
    CHECK_FALSE(report.HasFailures());
}

TEST_CASE_METHOD(SqlTestFixture,
                 "a row error in the chunk transaction rolls the chunk back and replays it exactly once",
                 "[SqlBackup][RestoreFailures]")
{
    std::filesystem::path const backupFile = "rfi_replay_backup.zip";
    auto const backupFileCleaner = ScopedFileRemoved { backupFile };

    CreateTableWithRows("rfi_replay", 6);
    BackupTo(backupFile);
    DropTables({ "rfi_replay" });

    // memoryLimitBytes > 0, or Restore() replaces these settings and batches ~4000 rows.
    auto const settings = SqlBackup::RestoreSettings { .batchSize = 2, .memoryLimitBytes = std::size_t { 1 } << 30U };
    auto const restore = [&](SqlBackup::ProgressManager& progress) {
        return SqlBackup::Restore(backupFile, GetConnectionString(), 1, progress, {}, "*", FastRetry(), settings);
    };
    auto const checksPerBatch = CountChecksPerBatch(restore);
    REQUIRE(CountRows("rfi_replay") == 6);

    // The second batch of the chunk transaction fails with a row-level error, after the first one
    // went in. Unless the chunk transaction is rolled back, the replay finds those rows duplicated.
    auto source = ScriptedFaultSource {
        std::map<int, SqlErrorInfo> {
            { checksPerBatch, SqlErrorInfo { .nativeErrorCode = 0, .sqlState = "23000", .message = "injected row error" } },
        },
        "ExecuteBatch"
    };
    auto report = SqlBackup::RestoreReport {};
    FinalRowsProgressManager progress;
    {
        ScopedFaultSource const installed { &source };
        report = restore(progress);
    }

    CHECK(source.RemainingFailures() == 0);
    CHECK(ReadIds("rfi_replay") == std::vector<int64_t> { 1, 2, 3, 4, 5, 6 });
    CHECK_FALSE(report.HasFailures());
    // The rows of the rolled-back attempt are not counted twice.
    CHECK(progress.finalRows["rfi_replay"] == 6);
    CHECK(progress.itemsProcessed == 6);
}

TEST_CASE_METHOD(SqlTestFixture,
                 "a row error after an intermediate commit replays only the rows since that commit",
                 "[SqlBackup][RestoreFailures]")
{
    std::filesystem::path const backupFile = "rfi_bounded_backup.zip";
    std::filesystem::path const brokenFile = "rfi_bounded_broken.zip";
    auto const backupFileCleaner = ScopedFileRemoved { backupFile };
    auto const brokenFileCleaner = ScopedFileRemoved { brokenFile };

    CreateTableWithRows("rfi_bounded", 1);
    BackupTo(backupFile);
    DropTables({ "rfi_bounded" });
    auto const chunks = ChunkNames(backupFile, "rfi_bounded");
    REQUIRE(chunks.size() == 1);
    // Row 7 duplicates row 1, which an intermediate commit after row 4 has committed. Were the rows
    // up to that commit replayed, they would fail as duplicates too.
    RewriteChunks(backupFile, brokenFile, { { chunks.front(), MakeChunk(IdValueRows({ 1, 2, 3, 4, 5, 6, 1, 8 })) } });

    struct Outcome
    {
        SqlBackup::RestoreReport report;
        int checks = 0;
    };
    // memoryLimitBytes > 0, or Restore() replaces these settings and batches ~4000 rows.
    auto const restore = [&](std::size_t maxRowsPerCommit) {
        auto const settings = SqlBackup::RestoreSettings { .batchSize = 2,
                                                           .maxRowsPerCommit = maxRowsPerCommit,
                                                           .memoryLimitBytes = std::size_t { 1 } << 30U };
        auto counter = ScriptedFaultSource { SqlErrorInfo {}, 0, "ExecuteBatch" };
        SqlBackup::NullProgressManager progress;
        auto outcome = Outcome {};
        {
            ScopedFaultSource const installed { &counter };
            outcome.report =
                SqlBackup::Restore(brokenFile, GetConnectionString(), 1, progress, {}, "*", FastRetry(), settings);
        }
        outcome.checks = counter.ConsultCount();
        return outcome;
    };

    auto const singleCommit = restore(0);
    auto const bounded = restore(4);

    CHECK(ReadIds("rfi_bounded") == std::vector<int64_t> { 1, 2, 3, 4, 5, 6, 8 });
    CHECK(TableReportOf(bounded.report, "rfi_bounded").failedRowCount == 1);
    CHECK(TableReportOf(singleCommit.report, "rfi_bounded").failedRowCount == 1);
    // Where the dialect commits every maxRowsPerCommit rows, the replay starts after row 4.
    if (SqlStatement {}.Connection().QueryFormatter().BoundsBulkLoadTransactionSize())
        CHECK(bounded.checks < singleCommit.checks);
    else
        CHECK(bounded.checks == singleCommit.checks);
}

TEST_CASE_METHOD(SqlTestFixture,
                 "transient error while replaying a chunk resumes after its committed batches",
                 "[SqlBackup][RestoreFailures]")
{
    std::filesystem::path const backupFile = "rfi_replay_transient_backup.zip";
    auto const backupFileCleaner = ScopedFileRemoved { backupFile };

    CreateTableWithRows("rfi_replay_transient", 6);
    BackupTo(backupFile);
    DropTables({ "rfi_replay_transient" });

    // memoryLimitBytes > 0, or Restore() replaces these settings and batches ~4000 rows.
    auto const settings = SqlBackup::RestoreSettings { .batchSize = 2, .memoryLimitBytes = std::size_t { 1 } << 30U };
    auto const restore = [&](SqlBackup::ProgressManager& progress) {
        return SqlBackup::Restore(backupFile, GetConnectionString(), 1, progress, {}, "*", FastRetry(), settings);
    };
    auto const checksPerBatch = CountChecksPerBatch(restore);
    REQUIRE(CountRows("rfi_replay_transient") == 6);

    // A row-level error in the chunk transaction's second batch hands the chunk over to the
    // batch-by-batch replay, whose second batch then loses the connection after its first committed.
    // The retry must resume after that committed batch: from the chunk start, it would find its rows
    // duplicated and report them failed.
    auto source = ScriptedFaultSource {
        std::map<int, SqlErrorInfo> {
            { checksPerBatch, SqlErrorInfo { .nativeErrorCode = 0, .sqlState = "23000", .message = "injected row error" } },
            { 3 * checksPerBatch,
              SqlErrorInfo { .nativeErrorCode = 0, .sqlState = "08S01", .message = "Communication link failure" } },
        },
        "ExecuteBatch"
    };
    auto report = SqlBackup::RestoreReport {};
    FinalRowsProgressManager progress;
    {
        ScopedFaultSource const installed { &source };
        report = restore(progress);
    }

    CHECK(source.RemainingFailures() == 0);
    CHECK(ReadIds("rfi_replay_transient") == std::vector<int64_t> { 1, 2, 3, 4, 5, 6 });
    CHECK_FALSE(report.HasFailures());
    CHECK(progress.finalRows["rfi_replay_transient"] == 6);
    CHECK(progress.itemsProcessed == 6);
}

TEST_CASE_METHOD(SqlTestFixture,
                 "transient error after an intermediate commit resumes after it across the chunk's batches",
                 "[SqlBackup][RestoreFailures]")
{
    std::filesystem::path const backupFile = "rfi_resume_batches_backup.zip";
    std::filesystem::path const splitFile = "rfi_resume_batches_split.zip";
    auto const backupFileCleaner = ScopedFileRemoved { backupFile };
    auto const splitFileCleaner = ScopedFileRemoved { splitFile };

    CreateTableWithRows("rfi_resume_batches", 6);
    BackupTo(backupFile);
    DropTables({ "rfi_resume_batches" });
    auto const chunks = ChunkNames(backupFile, "rfi_resume_batches");
    REQUIRE(chunks.size() == 1);
    // The chunk's first MsgPack batch holds fewer rows than an intermediate commit settles, so the
    // retry's skip of the committed rows spans that whole batch and ends inside the next one.
    RewriteChunks(backupFile,
                  splitFile,
                  { { chunks.front(), MakeMultiBatchChunk({ IdValueRows({ 1 }), IdValueRows({ 2, 3, 4, 5, 6 }) }) } });

    // memoryLimitBytes > 0, or Restore() replaces these settings and batches ~4000 rows.
    auto const settings =
        SqlBackup::RestoreSettings { .batchSize = 2, .maxRowsPerCommit = 2, .memoryLimitBytes = std::size_t { 1 } << 30U };
    auto const restore = [&](SqlBackup::ProgressManager& progress) {
        return SqlBackup::Restore(splitFile, GetConnectionString(), 1, progress, {}, "*", FastRetry(), settings);
    };
    auto const checksPerBatch = CountChecksPerBatch(restore);
    REQUIRE(CountRows("rfi_resume_batches") == 6);

    // The connection is lost in the second batch, after the first one (rows 1 and 2) was committed.
    // Were the watermark moved back to the end of the first MsgPack batch, row 2 would be inserted
    // again and fail as a duplicate.
    auto source = ScriptedFaultSource {
        SqlErrorInfo { .nativeErrorCode = 0, .sqlState = "08S01", .message = "Communication link failure" },
        1,
        "ExecuteBatch",
        checksPerBatch,
    };
    auto report = SqlBackup::RestoreReport {};
    FinalRowsProgressManager progress;
    {
        ScopedFaultSource const installed { &source };
        report = restore(progress);
    }

    CHECK(source.RemainingFailures() == 0);
    CHECK(ReadIds("rfi_resume_batches") == std::vector<int64_t> { 1, 2, 3, 4, 5, 6 });
    CHECK_FALSE(report.HasFailures());
    CHECK(progress.finalRows["rfi_resume_batches"] == 6);
    CHECK(progress.itemsProcessed == 6);
}

TEST_CASE_METHOD(SqlTestFixture,
                 "transient error late in a replayed chunk neither repeats nor loses its failure reports",
                 "[SqlBackup][RestoreFailures]")
{
    std::filesystem::path const backupFile = "rfi_many_backup.zip";
    std::filesystem::path const brokenFile = "rfi_many_broken.zip";
    auto const backupFileCleaner = ScopedFileRemoved { backupFile };
    auto const brokenFileCleaner = ScopedFileRemoved { brokenFile };

    CreateTableWithRows("rfi_many", 1);
    BackupTo(backupFile);
    DropTables({ "rfi_many" });

    // IDs 1..10 each twice in a row, then 11..15: 10 failing rows, reported before the chunk ends.
    auto ids = std::vector<int64_t> {};
    for (auto const id: std::views::iota(int64_t { 1 }, int64_t { 11 }))
        ids.insert(ids.end(), { id, id });
    for (auto const id: std::views::iota(int64_t { 11 }, int64_t { 16 }))
        ids.push_back(id);
    auto const chunks = ChunkNames(backupFile, "rfi_many");
    REQUIRE(chunks.size() == 1);
    RewriteChunks(backupFile, brokenFile, { { chunks.front(), MakeChunk(IdValueRows(ids)) } });

    auto const settings = SqlBackup::RestoreSettings { .batchSize = 2, .memoryLimitBytes = std::size_t { 1 } << 30U };
    auto const restore = [&](SqlBackup::ProgressManager& progress) {
        return SqlBackup::Restore(brokenFile, GetConnectionString(), 1, progress, {}, "*", FastRetry(), settings);
    };
    auto expectedIds = std::vector<int64_t> {};
    for (auto const id: std::views::iota(int64_t { 1 }, int64_t { 16 }))
        expectedIds.push_back(id);

    // Count the checks of the whole restore.
    auto counter = ScriptedFaultSource { SqlErrorInfo {}, 0, "ExecuteBatch" };
    {
        ScopedFaultSource const installed { &counter };
        SqlBackup::NullProgressManager progress;
        CHECK(TableReportOf(restore(progress), "rfi_many").failedRowCount == 10);
    }
    REQUIRE(ReadIds("rfi_many") == expectedIds);
    REQUIRE(counter.ConsultCount() > 0);

    // The last check, in the last batch, loses the connection: the retry must resume after what
    // the replay committed, so that no failure is reported twice and no row is lost.
    auto source = ScriptedFaultSource {
        std::map<int, SqlErrorInfo> {
            { counter.ConsultCount() - 1,
              SqlErrorInfo { .nativeErrorCode = 0, .sqlState = "08S01", .message = "Communication link failure" } },
        },
        "ExecuteBatch"
    };
    auto report = SqlBackup::RestoreReport {};
    {
        ScopedFaultSource const installed { &source };
        SqlBackup::NullProgressManager progress;
        report = restore(progress);
    }

    CHECK(source.RemainingFailures() == 0);
    CHECK(ReadIds("rfi_many") == expectedIds);
    CHECK(TableReportOf(report, "rfi_many").failedRowCount == 10);
    CHECK(report.TotalFailedRows() == 10);
}

TEST_CASE_METHOD(SqlTestFixture, "concurrent workers aggregate failures exactly", "[SqlBackup][RestoreFailures]")
{
    std::filesystem::path const backupFile = "rfi_concurrent_backup.zip";
    std::filesystem::path const brokenFile = "rfi_concurrent_broken.zip";
    auto const backupFileCleaner = ScopedFileRemoved { backupFile };
    auto const brokenFileCleaner = ScopedFileRemoved { brokenFile };

    CreateTableWithRows("rfi_concurrent", 24);
    BackupTo(backupFile, 3);
    DropTables({ "rfi_concurrent" });

    // In every chunk, the third row duplicates the first one's ID.
    auto const chunks = ChunkNames(backupFile, "rfi_concurrent");
    REQUIRE(chunks.size() == 8);
    auto replacements = std::map<std::string, std::string> {};
    for (auto const index: std::views::iota(size_t { 0 }, chunks.size()))
    {
        auto const& chunk = chunks[index];
        auto const first = static_cast<int64_t>((index * 3) + 1);
        replacements.emplace(chunk, MakeChunk(IdValueRows({ first, first + 1, first })));
    }
    RewriteChunks(backupFile, brokenFile, replacements);

    RecordingProgressManager progress;
    auto const report = SqlBackup::Restore(brokenFile, GetConnectionString(), 4, progress);

    CHECK(CountRows("rfi_concurrent") == 16);
    auto const tableReport = TableReportOf(report, "rfi_concurrent");
    CHECK(tableReport.failedRowCount == chunks.size());
    CHECK(report.TotalFailedRows() == chunks.size());
    CHECK(tableReport.failedRowRanges.size() == chunks.size());
    CHECK(std::ranges::all_of(tableReport.failedRowRanges,
                              [](auto const& range) { return range.firstRow == 3 && range.lastRow == 3; }));
    CHECK(std::ranges::count(progress.ErrorsOf("rfi_concurrent"), std::string { "8 rows failed, 0 cells set to NULL" })
          == 1);
}

TEST_CASE_METHOD(SqlTestFixture, "corrupt chunk is skipped and the restore continues", "[SqlBackup][RestoreFailures]")
{
    std::filesystem::path const backupFile = "rfi_corrupt_backup.zip";
    std::filesystem::path const brokenFile = "rfi_corrupt_broken.zip";
    auto const backupFileCleaner = ScopedFileRemoved { backupFile };
    auto const brokenFileCleaner = ScopedFileRemoved { brokenFile };

    CreateTableWithRows("rfi_corrupt_a", 4);
    CreateTableWithRows("rfi_corrupt_b", 3);
    BackupTo(backupFile, 2);
    DropTables({ "rfi_corrupt_a", "rfi_corrupt_b" });
    auto const chunks = ChunkNames(backupFile, "rfi_corrupt_a");
    REQUIRE(chunks.size() == 2);

    auto checksums = ChecksumUpdate::Keep;
    SECTION("its checksum does not match")
    {
        checksums = ChecksumUpdate::Keep;
    }
    SECTION("its checksum matches, its content is not MsgPack")
    {
        checksums = ChecksumUpdate::Recompute;
    }
    RewriteChunks(backupFile, brokenFile, { { chunks.front(), "this is not a data chunk" } }, checksums);

    RecordingProgressManager progress;
    auto const report = SqlBackup::Restore(brokenFile, GetConnectionString(), 1, progress);

    CHECK(ReadIds("rfi_corrupt_a") == std::vector<int64_t> { 3, 4 });
    CHECK(CountRows("rfi_corrupt_b") == 3);
    CHECK(TableReportOf(report, "rfi_corrupt_a").unreadableChunks == 1);
    CHECK_FALSE(TableReportOf(report, "rfi_corrupt_b").HasFailures());
    CHECK_FALSE(report.WorkerStopped());
    CHECK(report.incompleteTables.empty());
    CHECK(progress.ErrorCount() > 0);
}

TEST_CASE_METHOD(SqlTestFixture, "undecodable chunk content is reported, not a worker stop", "[SqlBackup][RestoreFailures]")
{
    std::filesystem::path const backupFile = "rfi_undecodable_backup.zip";
    std::filesystem::path const brokenFile = "rfi_undecodable_broken.zip";
    auto const backupFileCleaner = ScopedFileRemoved { backupFile };
    auto const brokenFileCleaner = ScopedFileRemoved { brokenFile };

    CreateTableWithRows("rfi_undecodable_a", 3);
    CreateTableWithRows("rfi_undecodable_b", 3);
    BackupTo(backupFile);
    DropTables({ "rfi_undecodable_a", "rfi_undecodable_b" });
    auto const chunks = ChunkNames(backupFile, "rfi_undecodable_a");
    REQUIRE(chunks.size() == 1);

    // A validly checksummed chunk whose rows lack the V column.
    RewriteChunks(
        backupFile, brokenFile, { { chunks.front(), MakeChunk({ Row { int64_t { 1 } }, Row { int64_t { 2 } } }) } });

    RecordingProgressManager progress;
    auto const report = SqlBackup::Restore(brokenFile, GetConnectionString(), 1, progress);

    CHECK(CountRows("rfi_undecodable_a") == 0);
    CHECK(CountRows("rfi_undecodable_b") == 3);
    CHECK(TableReportOf(report, "rfi_undecodable_a").unreadableChunks == 1);
    CHECK_FALSE(TableReportOf(report, "rfi_undecodable_b").HasFailures());
    CHECK_FALSE(report.WorkerStopped());
    CHECK(report.incompleteTables.empty());
}

TEST_CASE("bind-class errors are exactly the four SQLSTATEs that fail every row", "[SqlBackup][RestoreFailures]")
{
    auto const isBindClass = [](std::string sqlState) {
        return IsBindClassError(SqlErrorInfo { .nativeErrorCode = 0, .sqlState = std::move(sqlState), .message = {} });
    };
    for (auto const* const sqlState: { "HYC00", "HY003", "HY004", "07006" })
    {
        CAPTURE(sqlState);
        CHECK(isBindClass(sqlState));
    }
    for (auto const* const sqlState: { "23000", "23505", "22003", "HY000", "08S01", "" })
    {
        CAPTURE(sqlState);
        CHECK_FALSE(isBindClass(sqlState));
    }
}

TEST_CASE("only SQL Server's parameter status array names the failing rows", "[SqlBackup][RestoreFailures]")
{
    CHECK(SqlQueryFormatter::Get(SqlServerType::MICROSOFT_SQL)->ParameterStatusIdentifiesFailedRows());
    CHECK_FALSE(SqlQueryFormatter::Get(SqlServerType::POSTGRESQL)->ParameterStatusIdentifiesFailedRows());
    CHECK_FALSE(SqlQueryFormatter::Get(SqlServerType::SQLITE)->ParameterStatusIdentifiesFailedRows());
}

TEST_CASE_METHOD(SqlTestFixture,
                 "ClassifyRetryDecision tells a permanent error from a spent retry budget",
                 "[SqlBackup][RestoreFailures]")
{
    auto const connection = SqlConnection {};
    auto const settings = FastRetry();
    auto const error = [](std::string sqlState) {
        return SqlErrorInfo { .nativeErrorCode = 0, .sqlState = std::move(sqlState), .message = {} };
    };
    CHECK(SqlBackup::detail::ClassifyRetryDecision(connection, error("23000"), 0, settings).reason
          == SqlRetryGiveUpReason::NotTransient);
    CHECK(SqlBackup::detail::ClassifyRetryDecision(connection, error("08S01"), 0, settings).action == SqlRetryAction::Retry);
    CHECK(SqlBackup::detail::ClassifyRetryDecision(connection, error("08S01"), settings.maxRetries, settings).reason
          == SqlRetryGiveUpReason::RetriesExhausted);

    CHECK(SqlBackup::detail::IsPermanentError(connection, error("23000"), 0, settings));
    CHECK_FALSE(SqlBackup::detail::IsPermanentError(connection, error("08S01"), 0, settings));
    CHECK_FALSE(SqlBackup::detail::IsPermanentError(connection, error("08S01"), settings.maxRetries, settings));
}

TEST_CASE_METHOD(SqlTestFixture, "a table failed by several workers counts its rows once", "[SqlBackup][RestoreFailures]")
{
    std::filesystem::path const backupFile = "rfi_bind_concurrent_backup.zip";
    auto const backupFileCleaner = ScopedFileRemoved { backupFile };

    CreateTableWithRows("rfi_bind_concurrent", 16);
    BackupTo(backupFile, 4);
    DropTables({ "rfi_bind_concurrent" });
    REQUIRE(ChunkNames(backupFile, "rfi_bind_concurrent").size() == 4);

    // Every batch fails with a bind-class error, so each worker in a chunk of the table fails it.
    auto source = ScriptedFaultSource {
        SqlErrorInfo { .nativeErrorCode = 0, .sqlState = "HYC00", .message = "Driver does not support this parameter" },
        1000,
        "ExecuteBatch"
    };
    auto report = SqlBackup::RestoreReport {};
    RecordingProgressManager progress;
    {
        ScopedFaultSource const installed { &source };
        report = SqlBackup::Restore(backupFile, GetConnectionString(), 4, progress);
    }

    CHECK(CountRows("rfi_bind_concurrent") == 0);
    auto const tableReport = TableReportOf(report, "rfi_bind_concurrent");
    REQUIRE(tableReport.tableFailure.has_value());
    CHECK(tableReport.tableFailure.value_or("").starts_with("HYC00"));
    CHECK(tableReport.failedRowCount == 16);
    CHECK(report.TotalFailedRows() == 16);
    CHECK(std::ranges::count_if(progress.ErrorsOf("rfi_bind_concurrent"),
                                [](std::string const& message) { return message.starts_with("Table failed:"); })
          == 1);
}

TEST_CASE_METHOD(SqlTestFixture,
                 "a table failing after a committed chunk counts only its missing rows",
                 "[SqlBackup][RestoreFailures]")
{
    std::filesystem::path const backupFile = "rfi_late_failure_backup.zip";
    auto const backupFileCleaner = ScopedFileRemoved { backupFile };

    CreateTableWithRows("rfi_late_failure", 9);
    BackupTo(backupFile, 3);
    DropTables({ "rfi_late_failure" });
    REQUIRE(ChunkNames(backupFile, "rfi_late_failure").size() == 3);

    // Measure the checked calls of one clean chunk (one batch), then fail the second chunk's batch.
    auto counter = ScriptedFaultSource { SqlErrorInfo {}, 0, "ExecuteBatch" };
    {
        ScopedFaultSource const installed { &counter };
        SqlBackup::NullProgressManager progress;
        CHECK_FALSE(SqlBackup::Restore(backupFile, GetConnectionString(), 1, progress).HasFailures());
    }
    REQUIRE(CountRows("rfi_late_failure") == 9);
    REQUIRE(counter.ConsultCount() % 3 == 0);
    auto const checksPerChunk = counter.ConsultCount() / 3;

    auto source = ScriptedFaultSource {
        SqlErrorInfo { .nativeErrorCode = 0, .sqlState = "HYC00", .message = "Driver does not support this parameter" },
        1,
        "ExecuteBatch",
        checksPerChunk,
    };
    auto report = SqlBackup::RestoreReport {};
    {
        ScopedFaultSource const installed { &source };
        RecordingProgressManager progress;
        report = SqlBackup::Restore(backupFile, GetConnectionString(), 1, progress);
    }

    CHECK(source.RemainingFailures() == 0);
    auto const inTarget = CountRows("rfi_late_failure");
    CHECK(inTarget == 3);
    auto const tableReport = TableReportOf(report, "rfi_late_failure");
    REQUIRE(tableReport.tableFailure.has_value());
    auto const missingRows = static_cast<std::uint64_t>(9 - inTarget);
    CHECK(tableReport.failedRowCount == missingRows);
    CHECK(report.TotalFailedRows() == missingRows);
}
