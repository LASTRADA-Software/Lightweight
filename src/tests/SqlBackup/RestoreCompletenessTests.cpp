// SPDX-License-Identifier: Apache-2.0
//
// Regression tests for #673: a failed restore chunk must not abandon the chunks still
// queued for other tables, and a restore that did not deliver every table's data must not
// report success.

#include "../Utils.hpp"
#include "TestHelpers.hpp"

#include <Lightweight/SqlBackup.hpp>
#include <Lightweight/SqlConnection.hpp>
#include <Lightweight/SqlStatement.hpp>
#include <Lightweight/Utils.hpp>

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wnullability-extension"
#endif
#include <zip.h>
#if defined(__clang__)
    #pragma clang diagnostic pop
#endif

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <format>
#include <map>
#include <optional>
#include <ranges>
#include <source_location>
#include <string>
#include <string_view>
#include <vector>

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
    int err = 0;
    zip_t* zip = zip_open(archive.string().c_str(), ZIP_CREATE, &err);
    REQUIRE(zip != nullptr);

    std::string checksumsJson;
    zip_int64_t const checksumsIndex = zip_name_locate(zip, "checksums.json", 0);
    REQUIRE(checksumsIndex >= 0);
    {
        zip_stat_t stat;
        REQUIRE(zip_stat_index(zip, static_cast<zip_uint64_t>(checksumsIndex), 0, &stat) == 0);
        checksumsJson.resize(stat.size);
        zip_file_t* file = zip_fopen_index(zip, static_cast<zip_uint64_t>(checksumsIndex), 0);
        REQUIRE(file != nullptr);
        REQUIRE(zip_fread(file, checksumsJson.data(), checksumsJson.size())
                == static_cast<zip_int64_t>(checksumsJson.size()));
        zip_fclose(file);
    }

    auto checksums = nlohmann::json::parse(checksumsJson);
    std::string chunkEntry;
    for (zip_int64_t i = 0, n = zip_get_num_entries(zip, 0); i < n; ++i)
    {
        std::string const name = zip_get_name(zip, static_cast<zip_uint64_t>(i), 0);
        if (name.starts_with("data/") && name.ends_with(".msgpack"))
        {
            chunkEntry = name;
            break;
        }
    }
    REQUIRE_FALSE(chunkEntry.empty());
    checksums["files"][chunkEntry] = std::string(64, '0');

    std::string const modified = checksums.dump();
    zip_source_t* source = zip_source_buffer(zip, modified.data(), modified.size(), 0);
    REQUIRE(source != nullptr);
    REQUIRE(zip_file_replace(zip, static_cast<zip_uint64_t>(checksumsIndex), source, 0) == 0);
    REQUIRE(zip_close(zip) == 0);
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
                 "SqlBackup::Restore keeps concurrent workers on unrelated tables after failures",
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
}
