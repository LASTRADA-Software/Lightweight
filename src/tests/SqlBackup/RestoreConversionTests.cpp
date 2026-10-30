// SPDX-License-Identifier: Apache-2.0

#include "../Utils.hpp"
#include "TestHelpers.hpp"

#include <Lightweight/DataBinder/SqlGuid.hpp>
#include <Lightweight/SqlBackup.hpp>
#include <Lightweight/SqlColumnTypeDefinitions.hpp>
#include <Lightweight/SqlConnection.hpp>
#include <Lightweight/SqlQueryFormatter.hpp>
#include <Lightweight/SqlStatement.hpp>
#include <Lightweight/Zip/ZipArchive.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <format>
#include <mutex>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

using namespace Lightweight;
using Lightweight::SqlBackup::Tests::LambdaProgressManager;
using Lightweight::SqlBackup::Tests::RewriteArchiveMetadata;
using Lightweight::SqlBackup::Tests::ScopedFileRemoved;

namespace
{

SqlConnectionString const& GetConnectionString()
{
    return SqlConnection::DefaultConnectionString();
}

/// Tests whether the test database stores GUIDs natively; if not, restore keeps GUID text unchanged.
bool TargetHasNativeGuidType()
{
    auto const connection = SqlConnection {};
    auto const* const formatter = SqlQueryFormatter::Get(connection.ServerType());
    return formatter != nullptr && formatter->HasNativeGuidType();
}

/// Returns @p guid as upper-case text, or `NULL`.
std::string GuidText(std::optional<SqlGuid> const& guid)
{
    return guid.transform([](SqlGuid const& value) { return to_string(value); }).value_or("NULL");
}

/// Reads `TOKEN` and `REF` of every row of @p tableName through the SqlGuid binder, as sorted
/// `"<TOKEN> <REF>"` texts.
std::vector<std::string> ReadGuidRows(std::string_view tableName)
{
    auto stmt = SqlStatement {};
    auto cursor = stmt.ExecuteDirect(std::format(R"(SELECT "TOKEN", "REF" FROM "{}")", tableName));
    std::vector<std::string> rows;
    while (cursor.FetchRow())
    {
        // Read in column order: the evaluation order of function arguments is unspecified.
        auto const key = GuidText(cursor.GetNullableColumn<SqlGuid>(1));
        auto const reference = GuidText(cursor.GetNullableColumn<SqlGuid>(2));
        rows.push_back(std::format("{} {}", key, reference));
    }
    std::ranges::sort(rows);
    return rows;
}

/// Returns the row count of @p tableName.
int64_t CountRows(std::string_view tableName)
{
    return SqlStatement {}.ExecuteDirectScalar<int64_t>(std::format(R"(SELECT COUNT(*) FROM "{}")", tableName)).value_or(-1);
}

/// Copies @p source to @p target, declaring column @p columnName of table @p tableName a GUID column
/// in the metadata, so that the column's archived text is restored into a GUID column.
void CopyArchiveDeclaringGuidColumn(std::filesystem::path const& source,
                                    std::filesystem::path const& target,
                                    std::string_view tableName,
                                    std::string_view columnName)
{
    bool declared = false;
    RewriteArchiveMetadata(source, target, [&](std::string const& text) {
        auto metadata = nlohmann::json::parse(text);
        for (auto& table: metadata["schema"])
        {
            if (table["name"].get<std::string>() != tableName)
                continue;
            for (auto& column: table["columns"])
            {
                if (column["name"].get<std::string>() != columnName)
                    continue;
                column["type"] = "guid";
                column.erase("size");
                declared = true;
            }
        }
        return metadata.dump();
    });
    REQUIRE(declared);
}

/// Counts the data chunks of @p tableName in the archive @p archive.
size_t CountChunks(std::filesystem::path const& archive, std::string_view tableName)
{
    auto const zip = Zip::ZipArchive::Open(archive);
    REQUIRE(zip.has_value());
    auto const prefix = std::format("data/{}/", tableName);
    return static_cast<size_t>(std::ranges::count_if(
        zip->GetAllEntries(), [&](Zip::EntryInfo const& entry) { return entry.name.starts_with(prefix); }));
}

/// Records the error updates of a restore, per table, and counts them as dbtool does for its exit code.
struct RecordingProgressManager: SqlBackup::ErrorTrackingProgressManager
{
    std::mutex mutex;
    std::vector<std::pair<std::string, std::string>> errors; // (table, message)

    void Update(SqlBackup::Progress const& progress) override
    {
        auto const lock = std::scoped_lock(mutex);
        ErrorTrackingProgressManager::Update(progress);
        if (progress.state == SqlBackup::Progress::State::Error)
            errors.emplace_back(progress.tableName, progress.message);
    }

    void AllDone() override {}

    /// Returns the messages of the errors reported for @p tableName.
    std::vector<std::string> ErrorsOf(std::string_view tableName)
    {
        auto const lock = std::scoped_lock(mutex);
        std::vector<std::string> messages;
        for (auto const& [table, message]: errors)
            if (table == tableName)
                messages.push_back(message);
        return messages;
    }
};

} // namespace

TEST_CASE_METHOD(SqlTestFixture,
                 "SqlBackup: restores GUIDs whose version nibble is outside 1..5 unchanged",
                 "[SqlBackup][SqlGuid]")
{
    using namespace SqlColumnTypeDefinitions;

    std::filesystem::path const backupFile = "guid_restore_backup.zip";
    auto const backupFileCleaner = ScopedFileRemoved { backupFile };

    // On MSSQL and PostgreSQL the archive declares TOKEN and REF as GUID columns, so the restore binds
    // them through GuidBatchColumn. The SQLite driver reports GUID columns as text: there the archive
    // declares them nvarchar, and this test checks only that the SQLite binder reads the values back.
    auto stmt = SqlStatement {};
    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.CreateTable("guid_restore").PrimaryKey("TOKEN", Guid {}).Column("REF", Guid {});
    });

    // Byte-swapped GUIDs as older Lightweight builds stored them (version nibble E, F, 0), next to
    // an RFC one. Inserted as SQL text so that the values do not depend on the parser under test.
    std::vector<std::pair<std::string_view, std::optional<std::string_view>>> const rows {
        { "00000000-0000-0000-0000-000000000001", "ABCDEF01-2345-0789-ABCD-EF0123456789" },
        { "11111111-2222-3333-8444-555555555555", std::nullopt },
        { "FCC64492-FF82-E040-9C3A-12AB34CD56EF", "6B29FC40-CA47-F067-B31D-00DD010662DA" },
    };
    std::vector<std::string> const expected {
        "00000000-0000-0000-0000-000000000001 ABCDEF01-2345-0789-ABCD-EF0123456789",
        "11111111-2222-3333-8444-555555555555 NULL",
        "FCC64492-FF82-E040-9C3A-12AB34CD56EF 6B29FC40-CA47-F067-B31D-00DD010662DA",
    };
    for (auto const& [key, reference]: rows)
        (void) stmt.ExecuteDirect(
            std::format(R"(INSERT INTO "guid_restore" ("TOKEN", "REF") VALUES ('{}', {}))",
                        key,
                        reference.transform([](auto const& text) { return std::format("'{}'", text); }).value_or("NULL")));
    REQUIRE(ReadGuidRows("guid_restore") == expected);

    LambdaProgressManager progress { [](SqlBackup::Progress const& update) {
        if (update.state == SqlBackup::Progress::State::Error)
            FAIL_CHECK("Backup/Restore error: " << update.message);
    } };
    REQUIRE_NOTHROW(SqlBackup::Backup(backupFile, GetConnectionString(), 1, progress));
    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) { migration.DropTable("guid_restore"); });

    REQUIRE_NOTHROW(SqlBackup::Restore(backupFile, GetConnectionString(), 1, progress));

    CHECK(ReadGuidRows("guid_restore") == expected);
}

TEST_CASE_METHOD(SqlTestFixture,
                 "SqlBackup: restore sets a GUID that does not parse to NULL in a nullable column",
                 "[SqlBackup][SqlGuid]")
{
    using namespace SqlColumnTypeDefinitions;

    std::filesystem::path const backupFile = "guid_restore_invalid_backup.zip";
    std::filesystem::path const guidBackupFile = "guid_restore_invalid_as_guid.zip";
    auto const backupFileCleaner = ScopedFileRemoved { backupFile };
    auto const guidBackupFileCleaner = ScopedFileRemoved { guidBackupFile };

    // Archive the text as a VARCHAR column, then declare that column a GUID column in the metadata.
    auto stmt = SqlStatement {};
    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.CreateTable("guid_restore_invalid").PrimaryKey("ID", Integer {}).Column("TOKEN", Varchar { 40 });
    });
    (void) stmt.ExecuteDirect(R"(INSERT INTO "guid_restore_invalid" ("ID", "TOKEN") VALUES (1, 'not-a-guid'))");

    SqlBackup::NullProgressManager backupProgress;
    REQUIRE_NOTHROW(SqlBackup::Backup(backupFile, GetConnectionString(), 1, backupProgress));
    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) { migration.DropTable("guid_restore_invalid"); });
    CopyArchiveDeclaringGuidColumn(backupFile, guidBackupFile, "guid_restore_invalid", "TOKEN");

    RecordingProgressManager restoreProgress;
    REQUIRE_NOTHROW(SqlBackup::Restore(guidBackupFile, GetConnectionString(), 1, restoreProgress));

    if (!TargetHasNativeGuidType())
    {
        // No native GUID type: the archived text passes through unchanged, so nothing fails.
        CHECK(restoreProgress.ErrorCount() == 0);
        CHECK(CountRows("guid_restore_invalid") == 1);
        CHECK(SqlStatement {}.ExecuteDirectScalar<std::string>(R"(SELECT "TOKEN" FROM "guid_restore_invalid")")
              == std::optional<std::string> { "not-a-guid" });
        return;
    }

    // The row is restored with the value that does not convert set to NULL.
    // TODO(Task 8): assert the failure report names the nulled cell.
    CHECK(CountRows("guid_restore_invalid") == 1);
    CHECK(SqlStatement {}.ExecuteDirectScalar<std::string>(R"(SELECT "TOKEN" FROM "guid_restore_invalid")") == std::nullopt);
}

TEST_CASE_METHOD(SqlTestFixture,
                 "SqlBackup: a value that does not convert becomes NULL; every table and chunk is restored",
                 "[SqlBackup][SqlGuid]")
{
    using namespace SqlColumnTypeDefinitions;

    std::filesystem::path const backupFile = "restore_one_bad_table_backup.zip";
    std::filesystem::path const guidBackupFile = "restore_one_bad_table_as_guid.zip";
    auto const backupFileCleaner = ScopedFileRemoved { backupFile };
    auto const guidBackupFileCleaner = ScopedFileRemoved { guidBackupFile };

    // "restore_bad_value" sorts before "restore_good_rows", and RewriteArchiveMetadata writes the
    // chunks in name order, so the one restore worker meets the bad value first.
    auto stmt = SqlStatement {};
    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.CreateTable("restore_bad_value").PrimaryKey("ID", Integer {}).Column("TOKEN", Varchar { 40 });
        migration.CreateTable("restore_good_rows").PrimaryKey("ID", Integer {}).Column("NAME", Varchar { 20 });
    });
    (void) stmt.ExecuteDirect(R"(INSERT INTO "restore_bad_value" ("ID", "TOKEN") VALUES (1, 'not-a-guid'))");
    for (auto const id: std::views::iota(2, 7))
        (void) stmt.ExecuteDirect(std::format(
            R"(INSERT INTO "restore_bad_value" ("ID", "TOKEN") VALUES ({0}, '0000000{0}-0000-0000-0000-000000000000'))",
            id));
    for (auto const id: std::views::iota(1, 4))
        (void) stmt.ExecuteDirect(
            std::format(R"(INSERT INTO "restore_good_rows" ("ID", "NAME") VALUES ({0}, 'row {0}'))", id));

    // Two rows per chunk, so the bad table has chunks after the one with the bad value.
    SqlBackup::NullProgressManager backupProgress;
    REQUIRE_NOTHROW(SqlBackup::Backup(
        backupFile, GetConnectionString(), 1, backupProgress, {}, "*", {}, SqlBackup::BackupSettings { .rowsPerChunk = 2 }));
    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.DropTable("restore_bad_value");
        migration.DropTable("restore_good_rows");
    });
    CopyArchiveDeclaringGuidColumn(backupFile, guidBackupFile, "restore_bad_value", "TOKEN");
    REQUIRE(CountChunks(guidBackupFile, "restore_bad_value") > 1);

    RecordingProgressManager restoreProgress;
    REQUIRE_NOTHROW(SqlBackup::Restore(guidBackupFile, GetConnectionString(), 1, restoreProgress));

    if (!TargetHasNativeGuidType())
    {
        // No native GUID type: the archived text passes through unchanged, so both tables are restored.
        CHECK(restoreProgress.ErrorCount() == 0);
        CHECK(CountRows("restore_good_rows") == 3);
        CHECK(CountRows("restore_bad_value") == 6);
        CHECK(SqlStatement {}.ExecuteDirectScalar<std::string>(R"(SELECT "TOKEN" FROM "restore_bad_value" WHERE "ID" = 1)")
              == std::optional<std::string> { "not-a-guid" });
        return;
    }

    // Both tables are restored in full, every chunk of the bad one included; the nullable TOKEN cell
    // that does not convert becomes NULL.
    // TODO(Task 8): assert the failure report names the nulled cell and dbtool's non-zero exit.
    CHECK(CountRows("restore_good_rows") == 3);
    CHECK(restoreProgress.ErrorsOf("restore_good_rows").empty());
    CHECK(CountRows("restore_bad_value") == 6);
    CHECK(SqlStatement {}.ExecuteDirectScalar<std::string>(R"(SELECT "TOKEN" FROM "restore_bad_value" WHERE "ID" = 1)")
          == std::nullopt);
    CHECK(std::ranges::count(restoreProgress.ErrorsOf("restore_bad_value"),
                             std::string { "Restore incomplete: errors occurred" })
          == 0);
}

TEST_CASE_METHOD(SqlTestFixture, "SqlBackup: restores the TINYINT values the target backend holds", "[SqlBackup]")
{
    using namespace SqlColumnTypeDefinitions;

    std::filesystem::path const backupFile = "restore_tinyint_backup.zip";
    auto const backupFileCleaner = ScopedFileRemoved { backupFile };

    // SQL Server's TINYINT is unsigned 0..255. SQLite's holds what an int8_t field wrote, too
    // (PostgreSQL creates Tinyint as SMALLINT, so there this only checks the round trip).
    auto stmt = SqlStatement {};
    bool const isMsSql = stmt.Connection().ServerType() == SqlServerType::MICROSOFT_SQL;
    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.CreateTable("restore_tinyint").PrimaryKey("ID", Integer {}).Column("T", Tinyint {});
    });
    std::vector<int64_t> values { 0, 127, 200, 255 };
    if (!isMsSql)
        values.insert(values.begin(), { -128, -5 });
    for (auto const index: std::views::iota(size_t { 0 }, values.size()))
        (void) stmt.ExecuteDirect(
            std::format(R"(INSERT INTO "restore_tinyint" ("ID", "T") VALUES ({}, {}))", index + 1, values[index]));

    LambdaProgressManager progress { [](SqlBackup::Progress const& update) {
        if (update.state == SqlBackup::Progress::State::Error)
            FAIL_CHECK("Backup/Restore error: " << update.message);
    } };
    REQUIRE_NOTHROW(SqlBackup::Backup(backupFile, GetConnectionString(), 1, progress));
    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) { migration.DropTable("restore_tinyint"); });
    REQUIRE_NOTHROW(SqlBackup::Restore(backupFile, GetConnectionString(), 1, progress));

    std::vector<int64_t> restored;
    auto cursor = stmt.ExecuteDirect(R"(SELECT "T" FROM "restore_tinyint" ORDER BY "ID")");
    while (cursor.FetchRow())
        restored.push_back(cursor.GetNullableColumn<int64_t>(1).value_or(-1));
    CHECK(restored == values);
}

TEST_CASE_METHOD(SqlTestFixture, "SqlBackup: a TINYINT value above 255 is restored as NULL on SQLite", "[SqlBackup]")
{
    using namespace SqlColumnTypeDefinitions;

    // Only SQLite stores 300 in a TINYINT column (SQL Server refuses it; PostgreSQL has no TINYINT).
    auto stmt = SqlStatement {};
    if (stmt.Connection().ServerType() != SqlServerType::SQLITE)
        return;

    std::filesystem::path const backupFile = "restore_tinyint_300_backup.zip";
    auto const backupFileCleaner = ScopedFileRemoved { backupFile };
    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.CreateTable("restore_tinyint_300").PrimaryKey("ID", Integer {}).Column("T", Tinyint {});
    });
    (void) stmt.ExecuteDirect(R"(INSERT INTO "restore_tinyint_300" ("ID", "T") VALUES (1, 300))");

    SqlBackup::NullProgressManager backupProgress;
    REQUIRE_NOTHROW(SqlBackup::Backup(backupFile, GetConnectionString(), 1, backupProgress));
    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) { migration.DropTable("restore_tinyint_300"); });

    RecordingProgressManager restoreProgress;
    REQUIRE_NOTHROW(SqlBackup::Restore(backupFile, GetConnectionString(), 1, restoreProgress));

    // The row is restored with the nullable T set to NULL.
    // TODO(Task 8): assert the failure report names the nulled cell.
    CHECK(CountRows("restore_tinyint_300") == 1);
    CHECK(SqlStatement {}.ExecuteDirectScalar<int64_t>(R"(SELECT "T" FROM "restore_tinyint_300")") == std::nullopt);
}
