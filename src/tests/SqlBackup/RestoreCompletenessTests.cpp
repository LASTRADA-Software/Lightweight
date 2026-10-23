// SPDX-License-Identifier: Apache-2.0
//
// Regression tests for #673: a failed restore chunk must not abandon the chunks still
// queued for other tables, and a restore that did not deliver every table's data must not
// report success.

#include "../Utils.hpp"
#include "TestHelpers.hpp"

#include <Lightweight/SqlBackup.hpp>
#include <Lightweight/SqlBackup/Restore.hpp>
#include <Lightweight/SqlConnection.hpp>
#include <Lightweight/SqlStatement.hpp>
#include <Lightweight/Utils.hpp>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <format>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <source_location>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

using namespace Lightweight;
using namespace Lightweight::SqlBackup::Tests;

namespace
{

std::filesystem::path const BackupFile = "restore_completeness_test.zip";

std::vector<std::string> const TableNames = {
    "restore_incomplete_a",
    "restore_incomplete_b",
    "restore_incomplete_c",
};

/// The backup filter naming exactly the fixture tables.
[[nodiscard]] std::string TableFilter()
{
    std::string filter;
    for (auto const& name: TableNames)
    {
        if (!filter.empty())
            filter += ',';
        filter += name;
    }
    return filter;
}

/// Rows per table, and the chunk width that splits them into three chunks each.
size_t const RowsPerTable = 25;
size_t const RowsPerChunk = 10;

/// Fails the first @p failureCount `SqlStatement::ExecuteBatch()` calls with a non-transient
/// SQLSTATE. `RestoreChunkData()` therefore gives up immediately instead of retrying, which is
/// exactly the chunk failure the issue describes.
class ExecuteBatchFault final: public SqlFaultSource
{
  public:
    explicit ExecuteBatchFault(int failureCount):
        _remaining { failureCount }
    {
    }

    [[nodiscard]] std::optional<SqlErrorInfo> NextFailure(SQLHSTMT, std::source_location const& location) override
    {
        if (_remaining.load(std::memory_order_relaxed) <= 0)
            return std::nullopt;

        if (!std::string_view(location.function_name()).contains("ExecuteBatch"))
            return std::nullopt;

        if (_remaining.fetch_sub(1) <= 0)
            return std::nullopt;

        return SqlErrorInfo {
            .nativeErrorCode = 0,
            .sqlState = "HY000",
            .message = "injected restore chunk failure",
        };
    }

  private:
    std::atomic<int> _remaining;
};

/// Fails the first `ExecuteDirect` issued from `CreateTablesInOrder`, so one archived table cannot
/// be created and the restored schema is partial.
class SchemaCreationFault final: public SqlFaultSource
{
  public:
    SchemaCreationFault():
        _remaining { 1 }
    {
    }

    [[nodiscard]] std::optional<SqlErrorInfo> NextFailure(SQLHSTMT, std::source_location const& location) override
    {
        if (_remaining.load(std::memory_order_relaxed) <= 0)
            return std::nullopt;

        if (!std::string_view(location.function_name()).contains("CreateTablesInOrder"))
            return std::nullopt;

        if (_remaining.fetch_sub(1) <= 0)
            return std::nullopt;

        return SqlErrorInfo {
            .nativeErrorCode = 0,
            .sqlState = "HY000",
            .message = "injected schema creation failure",
        };
    }

  private:
    std::atomic<int> _remaining;
};

void CreateAndFillTables()
{
    SqlConnection conn;
    SqlStatement stmt { conn };

    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        for (auto const& name: TableNames)
        {
            migration.CreateTable(name)
                .PrimaryKey("id", SqlColumnTypeDefinitions::Integer {})
                .Column("content", SqlColumnTypeDefinitions::Varchar { 64 });
        }
    });

    for (auto const& name: TableNames)
    {
        stmt.Prepare(std::format("INSERT INTO {} (id, content) VALUES (?, ?)", name));
        for (auto const i: std::views::iota(size_t { 1 }, RowsPerTable + 1))
            (void) stmt.Execute(i, std::format("row{}", i));
    }
}

void DropTables()
{
    SqlConnection conn;
    SqlStatement stmt { conn };

    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        for (auto const& name: TableNames)
            migration.DropTable(name);
    });
}

/// Backs up the fixture tables, split into chunks of RowsPerChunk rows.
void BackupTables()
{
    SqlBackup::NullProgressManager pm;
    SqlBackup::BackupSettings settings;
    settings.rowsPerChunk = RowsPerChunk;

    SqlBackup::Backup(BackupFile, SqlConnection::DefaultConnectionString(), 1, pm, {}, TableFilter(), {}, settings);
}

[[nodiscard]] std::map<std::string, size_t> RowCounts()
{
    SqlConnection conn;
    SqlStatement stmt { conn };

    auto counts = std::map<std::string, size_t> {};
    for (auto const& name: TableNames)
    {
        auto const count = stmt.ExecuteDirectScalar<long long>(std::format("SELECT COUNT(*) FROM {}", name));
        counts[name] = static_cast<size_t>(count.value_or(0));
    }
    return counts;
}

/// Returns the tables that hold fewer rows than the fixture inserted.
[[nodiscard]] std::vector<std::string> ShortTables(std::map<std::string, size_t> const& counts)
{
    std::vector<std::string> shortTables;
    for (auto const& [tableName, count]: counts)
        if (count < RowsPerTable)
            shortTables.push_back(tableName);
    return shortTables;
}

/// Tests whether @p events holds a @p state event for @p tableName.
[[nodiscard]] bool HasEvent(std::vector<SqlBackup::Progress> const& events,
                            SqlBackup::Progress::State state,
                            std::string_view tableName)
{
    return std::ranges::any_of(events, [&](SqlBackup::Progress const& progress) {
        return progress.state == state && progress.tableName == tableName;
    });
}

/// Tests whether @p events holds an error whose message contains @p needle.
[[nodiscard]] bool HasErrorMessage(std::vector<SqlBackup::Progress> const& events, std::string_view needle)
{
    return std::ranges::any_of(events, [&](SqlBackup::Progress const& progress) {
        return progress.state == SqlBackup::Progress::State::Error && progress.message.contains(needle);
    });
}

/// Rewrites `checksums.json` so the first data chunk fails verification while the chunk itself
/// stays intact: the restore must report that table and keep restoring the others.
void CorruptFirstChunkChecksum(std::filesystem::path const& archive)
{
    std::string chunkEntry;
    for (auto const& entry: Zip::ZipArchive::Open(archive).value().GetAllEntries())
    {
        if (entry.name.starts_with("data/") && entry.name.ends_with(".msgpack"))
        {
            chunkEntry = entry.name;
            break;
        }
    }
    REQUIRE_FALSE(chunkEntry.empty());

    std::filesystem::path const rewritten = archive.string() + ".rewritten";
    ScopedFileRemoved const rewrittenCleaner { rewritten };
    RewriteArchive(archive,
                   rewritten,
                   [&](std::string const& name, std::string content) -> std::optional<std::pair<std::string, std::string>> {
                       if (name != "checksums.json")
                           return std::pair { name, std::move(content) };

                       auto checksums = nlohmann::json::parse(content);
                       checksums["files"][chunkEntry] = std::string(64, '0');
                       return std::pair { name, checksums.dump() };
                   });

    std::filesystem::remove(archive);
    std::filesystem::rename(rewritten, archive);
}

/// Runs Restore() with @p failures injected chunk failures and returns the progress events.
[[nodiscard]] std::vector<SqlBackup::Progress> RestoreWithFailures(unsigned concurrency, int failures)
{
    std::vector<SqlBackup::Progress> events;
    LambdaProgressManager pm { [&](SqlBackup::Progress const& progress) { events.push_back(progress); } };

    {
        ExecuteBatchFault fault { failures };
        SetFaultSource(&fault);
        auto const uninstall = detail::Finally([] { SetFaultSource(nullptr); });

        REQUIRE_NOTHROW(SqlBackup::Restore(BackupFile, SqlConnection::DefaultConnectionString(), concurrency, pm));
    }

    return events;
}

} // namespace

TEST_CASE_METHOD(SqlTestFixture,
                 "SqlBackup::Restore continues with other tables after a chunk failure",
                 "[SqlBackup][restore]")
{
    ScopedFileRemoved const backupFileCleaner { BackupFile };

    CreateAndFillTables();
    REQUIRE_NOTHROW(BackupTables());
    DropTables();

    auto const events = RestoreWithFailures(/*concurrency=*/1, /*failures=*/1);

    // The queue is shared by all workers: before the fix a single failed chunk made the only
    // worker return, leaving every other table's chunks untouched.
    auto const counts = RowCounts();
    auto const shortTables = ShortTables(counts);
    REQUIRE(shortTables.size() == 1);
    auto const& incompleteTable = shortTables.front();

    // The failed table is reported incomplete and never reported finished.
    CHECK(HasEvent(events, SqlBackup::Progress::State::Error, incompleteTable));
    CHECK_FALSE(HasEvent(events, SqlBackup::Progress::State::Finished, incompleteTable));

    // Every other table was restored completely and reported finished.
    for (auto const& [tableName, count]: counts)
    {
        if (tableName == incompleteTable)
            continue;
        INFO("table " << tableName << " has " << count << " rows");
        CHECK(count == RowsPerTable);
        CHECK(HasEvent(events, SqlBackup::Progress::State::Finished, tableName));
    }
}

TEST_CASE_METHOD(SqlTestFixture, "SqlBackup::Restore continues after a chunk fails verification", "[SqlBackup][restore]")
{
    ScopedFileRemoved const backupFileCleaner { BackupFile };

    CreateAndFillTables();
    REQUIRE_NOTHROW(BackupTables());
    DropTables();
    CorruptFirstChunkChecksum(BackupFile);

    auto const events = RestoreWithFailures(/*concurrency=*/1, /*failures=*/0);

    // The chunk is popped before its checksum is verified, so a fetch error used to abandon the
    // rest of the queue. Only the table owning the corrupt chunk may be short now.
    auto const counts = RowCounts();
    auto const shortTables = ShortTables(counts);
    REQUIRE(shortTables.size() == 1);
    auto const& incompleteTable = shortTables.front();

    CHECK(HasErrorMessage(events, "Checksum mismatch"));
    CHECK(HasEvent(events, SqlBackup::Progress::State::Error, incompleteTable));
    CHECK_FALSE(HasEvent(events, SqlBackup::Progress::State::Finished, incompleteTable));

    for (auto const& [tableName, count]: counts)
    {
        if (tableName == incompleteTable)
            continue;
        INFO("table " << tableName << " has " << count << " rows");
        CHECK(count == RowsPerTable);
        CHECK(HasEvent(events, SqlBackup::Progress::State::Finished, tableName));
    }
}

TEST_CASE_METHOD(SqlTestFixture,
                 "SqlBackup::Restore reports an incomplete run and keeps unrelated tables after failures",
                 "[SqlBackup][restore]")
{
    ScopedFileRemoved const backupFileCleaner { BackupFile };

    CreateAndFillTables();
    REQUIRE_NOTHROW(BackupTables());
    DropTables();

    auto const events = RestoreWithFailures(/*concurrency=*/4, /*failures=*/2);

    // Two chunks fail. Each failed chunk fails only its own table; the remaining tables must be
    // restored completely, whichever worker happens to process them.
    auto const counts = RowCounts();
    auto const shortTables = ShortTables(counts);
    REQUIRE_FALSE(shortTables.empty());
    for (auto const& [tableName, count]: counts)
    {
        INFO("table " << tableName << " has " << count << " rows");
        if (count == RowsPerTable)
        {
            CHECK(HasEvent(events, SqlBackup::Progress::State::Finished, tableName));
        }
        else
        {
            CHECK(HasEvent(events, SqlBackup::Progress::State::Error, tableName));
            CHECK_FALSE(HasEvent(events, SqlBackup::Progress::State::Finished, tableName));
        }
    }

    // The run must not claim success and must not constrain a database that is missing rows.
    CHECK_FALSE(HasEvent(events, SqlBackup::Progress::State::Finished, ""));
    CHECK(HasErrorMessage(events, "foreign key constraints and indexes were not applied"));
}

TEST_CASE_METHOD(SqlTestFixture,
                 "SqlBackup::Restore does not constrain a schema it could not create",
                 "[SqlBackup][restore]")
{
    ScopedFileRemoved const backupFileCleaner { BackupFile };

    CreateAndFillTables();
    REQUIRE_NOTHROW(BackupTables());
    DropTables();

    std::vector<SqlBackup::Progress> events;
    LambdaProgressManager pm { [&](SqlBackup::Progress const& progress) { events.push_back(progress); } };
    {
        SchemaCreationFault fault;
        SetFaultSource(&fault);
        auto const uninstall = detail::Finally([] { SetFaultSource(nullptr); });
        REQUIRE_NOTHROW(SqlBackup::Restore(BackupFile, SqlConnection::DefaultConnectionString(), 1, pm));
    }

    // One table could not be created, so the schema is partial: the run must report it and leave
    // the constraints and indexes alone instead of failing on a foreign key to the missing table.
    CHECK(HasErrorMessage(events, "did not restore completely"));
    CHECK(HasErrorMessage(events, "foreign key constraints and indexes were not applied"));
    CHECK_FALSE(HasEvent(events, SqlBackup::Progress::State::Finished, ""));
}

TEST_CASE("SqlBackup::detail::FindIncompleteTables reports failed and unattempted tables", "[SqlBackup][restore]")
{
    auto const tableMap = std::map<std::string, SqlBackup::TableInfo> {
        { "complete", {} },
        { "failed", {} },
        { "unattempted", {} },
    };
    auto const chunkTables = std::map<std::string, std::string> {};
    std::deque<SqlBackup::detail::ZipEntryInfo> queue;
    std::mutex queueMutex;
    std::mutex fileMutex;
    SqlBackup::NullProgressManager pm;
    SqlBackup::RestoreSettings restoreSettings;
    SqlBackup::RetrySettings retrySettings;
    SqlBackup::detail::FailedTables failedTables;
    failedTables.Add("failed");

    auto makeCounter = [](size_t value) {
        return std::make_shared<std::atomic<size_t>>(value);
    };

    SqlBackup::detail::RestoreContext const ctx {
        .connectionString = SqlConnectionString {},
        .schema = {},
        .tableMap = tableMap,
        .chunkTables = chunkTables,
        .dataQueue = queue,
        .zip = nullptr,
        .queueMutex = queueMutex,
        .fileMutex = fileMutex,
        .progress = pm,
        .tableProgress =
            {
                { "complete", makeCounter(0) },
                { "failed", makeCounter(0) },
                { "unattempted", makeCounter(0) },
            },
        .chunksProcessed =
            {
                { "complete", makeCounter(2) },
                { "failed", makeCounter(2) },
                { "unattempted", makeCounter(1) },
            },
        .totalChunks =
            {
                { "complete", 2 },
                { "failed", 2 },
                { "unattempted", 3 },
            },
        .checksums = nullptr,
        .retrySettings = retrySettings,
        .restoreSettings = restoreSettings,
        .failedTables = failedTables,
    };

    auto const incompleteTables = SqlBackup::detail::FindIncompleteTables(ctx);
    REQUIRE(incompleteTables.size() == 2);

    CHECK(incompleteTables[0].tableName == "failed");
    CHECK(incompleteTables[0].chunksProcessed == 2);
    CHECK(incompleteTables[0].totalChunks == 2);

    CHECK(incompleteTables[1].tableName == "unattempted");
    CHECK(incompleteTables[1].chunksProcessed == 1);
    CHECK(incompleteTables[1].totalChunks == 3);
}
