// SPDX-License-Identifier: Apache-2.0

#include "../tools/shared/BackupRecording.hpp"
#include "SqlBackup/TestHelpers.hpp"
#include "UniqueTempPath.hpp"
#include "Utils.hpp"

#include <Lightweight/SqlBackup.hpp>
#include <Lightweight/SqlColumnTypeDefinitions.hpp>
#include <Lightweight/SqlConnection.hpp>
#include <Lightweight/SqlMigration.hpp>
#include <Lightweight/SqlScopedLock.hpp>
#include <Lightweight/SqlStatement.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

using namespace Lightweight;
using namespace SqlMigration;
using Lightweight::SqlBackup::Tests::LambdaProgressManager;
using Lightweight::SqlBackup::Tests::ScopedFileRemoved;

namespace
{

/// A clean migration manager on the test database; the records go through a mapper of their own.
class BackupRecordingFixture: public SqlTestFixture
{
  public:
    BackupRecordingFixture()
    {
        MigrationManager::GetInstance().RemoveAllMigrations();
        MigrationManager::GetInstance().RemoveAllReleases();
    }
    BackupRecordingFixture(BackupRecordingFixture const&) = delete;
    BackupRecordingFixture(BackupRecordingFixture&&) = delete;
    BackupRecordingFixture& operator=(BackupRecordingFixture const&) = delete;
    BackupRecordingFixture& operator=(BackupRecordingFixture&&) = delete;
    ~BackupRecordingFixture() override
    {
        MigrationManager::GetInstance().RemoveAllMigrations();
        MigrationManager::GetInstance().RemoveAllReleases();
        MigrationManager::GetInstance().CloseDataMapper();
    }
};

/// A directory removed with everything in it when the scope ends.
class ScopedDirectory
{
  public:
    explicit ScopedDirectory(std::filesystem::path path):
        _path { std::move(path) }
    {
        std::filesystem::create_directories(_path);
    }
    ScopedDirectory(ScopedDirectory const&) = delete;
    ScopedDirectory(ScopedDirectory&&) = delete;
    ScopedDirectory& operator=(ScopedDirectory const&) = delete;
    ScopedDirectory& operator=(ScopedDirectory&&) = delete;
    ~ScopedDirectory()
    {
        auto error = std::error_code {};
        std::filesystem::remove_all(_path, error);
    }

    /// @return The directory.
    [[nodiscard]] std::filesystem::path const& Path() const noexcept
    {
        return _path;
    }

  private:
    std::filesystem::path _path;
};

void NoChange(SqlMigrationQueryBuilder& /*plan*/) {}

/// Creates the table `rec_subject` with two rows.
void CreateSubjectTable()
{
    using namespace SqlColumnTypeDefinitions;
    auto stmt = SqlStatement {};
    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.DropTableIfExists("rec_subject");
        migration.CreateTable("rec_subject").PrimaryKey("id", Integer {}).Column("v", Varchar { 32 });
    });
    stmt.Prepare(R"(INSERT INTO "rec_subject" ("id", "v") VALUES (?, ?))");
    (void) stmt.Execute(1, std::string { "alpha" });
    (void) stmt.Execute(2, std::string { "beta" });
}

/// A progress manager that fails the test on any error.
LambdaProgressManager FailOnError()
{
    return LambdaProgressManager { [](SqlBackup::Progress const& p) {
        if (p.state == SqlBackup::Progress::State::Error)
            FAIL_CHECK("Backup or restore error: " << p.message);
    } };
}

/// Backs up the tables @p tableFilter matches into @p file; the default backs up the whole database.
void BackUpDatabase(std::filesystem::path const& file, std::string const& tableFilter = "*")
{
    auto progress = FailOnError();
    SqlBackup::Backup(file, SqlConnection::DefaultConnectionString(), 1, progress, /*schema=*/ {}, tableFilter);
    REQUIRE(std::filesystem::exists(file));
}

/// Restores every table of @p file.
void RestoreDatabase(std::filesystem::path const& file)
{
    auto progress = FailOnError();
    SqlBackup::Restore(file, SqlConnection::DefaultConnectionString(), 1, progress, /*schema=*/ {}, "*");
}

/// The number of `schema_backups` rows.
std::int64_t RecordCount(DataMapper& dataMapper)
{
    auto stmt = SqlStatement { dataMapper.Connection() };
    return stmt.ExecuteDirectScalar<std::int64_t>(R"(SELECT COUNT(*) FROM "schema_backups")").value_or(-1);
}

/// The `kind` column of every `schema_backups` row, joined by spaces in id order.
std::string RecordedKinds(DataMapper& dataMapper)
{
    auto stmt = SqlStatement { dataMapper.Connection() };
    auto cursor = stmt.ExecuteDirect(R"(SELECT "kind" FROM "schema_backups" ORDER BY "id")");
    auto kinds = std::string {};
    while (cursor.FetchRow())
    {
        if (!kinds.empty())
            kinds += ' ';
        kinds += cursor.GetColumn<std::string>(1);
    }
    return kinds;
}

} // namespace

TEST_CASE_METHOD(BackupRecordingFixture,
                 "RecordBackup verifies the archive and records the state captured at its start",
                 "[BackupRecording]")
{
    CreateSubjectTable();
    auto const directory = ScopedDirectory { UniqueTempPath("backup-recording") };
    auto const file = directory.Path() / std::filesystem::path { u8"Sicherung-Müller.zip" };

    auto mapper = Tools::ConnectRecordDataMapper(SqlConnection::DefaultConnectionString(), {});
    auto const start = Tools::CaptureBackupStart(mapper);
    CHECK_FALSE(start.observedState.lastAppliedMigration.has_value());
    CHECK(start.observedState.digest == EmptyMigrationHistoryDigest);

    BackUpDatabase(file);
    auto const recorded = Tools::RecordBackup(mapper, file, start, "dbtool 0.0.0");

    if (!recorded)
    {
        FAIL("recorded has no value");
        return;
    }
    CHECK(recorded->kind == BackupRecordKind::Backup);
    CHECK(recorded->archiveFileName == "Sicherung-M\xC3\xBCller.zip");
    CHECK(recorded->archiveSha256 == SqlBackup::VerifyArchive(file)->archiveSha256);
    CHECK(recorded->createdAt == start.startedAt);
    CHECK(recorded->observedState.digest == start.observedState.digest);
    CHECK(recorded->toolVersion == "dbtool 0.0.0");
    auto const latest = ReadLatestBackupRecord(mapper);
    if (!latest)
    {
        FAIL("latest has no value");
        return;
    }
    CHECK((*latest).id == recorded->id);
    CHECK((*latest).archiveFileName == "Sicherung-M\xC3\xBCller.zip");
}

TEST_CASE_METHOD(BackupRecordingFixture,
                 "RecordBackup keeps the state from the start, so a migration applied meanwhile makes it stale",
                 "[BackupRecording]")
{
    CreateSubjectTable();
    auto const file = UniqueTempPath("backup-recording-stale", ".zip");
    auto const cleanup = ScopedFileRemoved { file };
    auto& manager = MigrationManager::GetInstance();
    auto const migration = Migration<20990301000001> { "applied while backing up", NoChange };
    manager.CreateMigrationHistory();

    auto mapper = Tools::ConnectRecordDataMapper(SqlConnection::DefaultConnectionString(), {});
    auto const start = Tools::CaptureBackupStart(mapper);
    BackUpDatabase(file);
    REQUIRE(manager.ApplyPendingMigrations() == 1);
    auto const recorded = Tools::RecordBackup(mapper, file, start, "dbtool 0.0.0");

    if (!recorded)
    {
        FAIL("recorded has no value");
        return;
    }
    CHECK_FALSE(recorded->observedState.lastAppliedMigration.has_value());
    CHECK_FALSE(BackupRecordCovers(*recorded, ReadMigrationHistoryState(mapper)));
}

TEST_CASE_METHOD(BackupRecordingFixture,
                 "RecordBackup writes nothing for an archive that fails verification",
                 "[BackupRecording]")
{
    auto const file = UniqueTempPath("backup-recording-garbage", ".zip");
    auto const cleanup = ScopedFileRemoved { file };
    {
        auto stream = std::ofstream { file, std::ios::binary };
        stream << "not an archive";
    }

    auto mapper = Tools::ConnectRecordDataMapper(SqlConnection::DefaultConnectionString(), {});
    auto const recorded = Tools::RecordBackup(mapper, file, Tools::CaptureBackupStart(mapper), "dbtool 0.0.0");

    REQUIRE_FALSE(recorded.has_value());
    CHECK(recorded.error().kind == Tools::BackupRecordError::Kind::VerificationFailed);
    CHECK(Tools::DescribeUnrecordedBackup(recorded.error()).starts_with("The archive failed verification: "));
    CHECK(Tools::DescribeUnrecordedBackup(recorded.error())
              .ends_with("; the backup is not recorded and cannot satisfy a migration backup requirement."));
    CHECK_FALSE(ReadLatestBackupRecord(mapper).has_value());
}

TEST_CASE_METHOD(BackupRecordingFixture, "RecordRestore records the state the restore left", "[BackupRecording]")
{
    CreateSubjectTable();
    auto const file = UniqueTempPath("backup-recording-restore", ".zip");
    auto const cleanup = ScopedFileRemoved { file };
    BackUpDatabase(file);
    auto& manager = MigrationManager::GetInstance();
    auto const migration = Migration<20990301000001> { "applied before the record", NoChange };
    manager.CreateMigrationHistory();
    REQUIRE(manager.ApplyPendingMigrations() == 1);

    auto mapper = Tools::ConnectRecordDataMapper(SqlConnection::DefaultConnectionString(), {});
    auto const before = SqlDateTime { std::chrono::system_clock::now() };
    auto const recorded = Tools::RecordRestore(mapper, file, "dbtool 0.0.0");

    if (!recorded)
    {
        FAIL("recorded has no value");
        return;
    }
    CHECK(recorded->kind == BackupRecordKind::Restore);
    CHECK(recorded->createdAt.value() >= before.value());
    CHECK(recorded->observedState.lastAppliedMigration.has_value());
    CHECK(recorded->observedState.lastAppliedMigration.value_or(SqlMigration::MigrationTimestamp {}).value
          == 20990301000001);
    CHECK(BackupRecordCovers(*recorded, ReadMigrationHistoryState(mapper)));
}

TEST_CASE_METHOD(BackupRecordingFixture, "RecordWaiver writes a waived row that satisfies nothing", "[BackupRecording]")
{
    auto mapper = Tools::ConnectRecordDataMapper(SqlConnection::DefaultConnectionString(), {});
    auto const waiver = Tools::RecordWaiver(mapper, "dbtool 0.0.0");

    if (!waiver)
    {
        FAIL("waiver has no value");
        return;
    }
    CHECK(waiver->kind == BackupRecordKind::Waived);
    CHECK(waiver->archiveFileName.empty());
    CHECK(waiver->archiveSha256.empty());
    CHECK_FALSE(BackupRecordCovers(*waiver, ReadMigrationHistoryState(mapper)));
    CHECK_FALSE(ReadLatestBackupRecord(mapper).has_value());
    CHECK(RecordedKinds(mapper) == "waived");
}

TEST_CASE_METHOD(BackupRecordingFixture,
                 "ConnectRecordDataMapper refuses a schema name it cannot quote safely",
                 "[BackupRecording]")
{
    CHECK_THROWS_AS(Tools::ConnectRecordDataMapper(SqlConnection::DefaultConnectionString(), "dbo; DROP TABLE x"),
                    std::invalid_argument);
}

TEST_CASE_METHOD(BackupRecordingFixture,
                 "A backup taken under the migration lock restores without the lock",
                 "[BackupRecording]")
{
    using namespace std::chrono_literals;
    CreateSubjectTable();
    auto const file = UniqueTempPath("backup-recording-locked", ".zip");
    auto const cleanup = ScopedFileRemoved { file };
    auto lockConnection = SqlConnection {};
    auto restoreProgress = LambdaProgressManager { [](SqlBackup::Progress const& p) {
        if (p.state == SqlBackup::Progress::State::Error)
            FAIL_CHECK("Restore error: " << p.message);
    } };
    {
        auto const lock = SqlScopedLock { lockConnection, "backup_recording_lock" };
        REQUIRE(lock.IsLocked());
        auto progress = LambdaProgressManager { [](SqlBackup::Progress const& p) {
            if (p.state == SqlBackup::Progress::State::Error)
                FAIL_CHECK("Backup error: " << p.message);
        } };
        SqlBackup::Backup(file, SqlConnection::DefaultConnectionString(), 1, progress, /*schema=*/ {}, "*");
    }

    SqlBackup::Restore(file, SqlConnection::DefaultConnectionString(), 1, restoreProgress, /*schema=*/ {}, "*");

    auto const relock = SqlScopedLock { lockConnection, "backup_recording_lock", 1s };
    CHECK(relock.IsLocked());
}

TEST_CASE_METHOD(BackupRecordingFixture,
                 "A restore keeps the live records and appends its own with a fresh id",
                 "[BackupRecording]")
{
    CreateSubjectTable();
    auto const file = UniqueTempPath("backup-recording-roundtrip", ".zip");
    auto const cleanup = ScopedFileRemoved { file };
    auto mapper = Tools::ConnectRecordDataMapper(SqlConnection::DefaultConnectionString(), {});
    auto const start = Tools::CaptureBackupStart(mapper);
    BackUpDatabase(file);
    auto const first = Tools::RecordBackup(mapper, file, start, "dbtool 0.0.0");
    if (!first)
    {
        FAIL("first has no value");
        return;
    }
    REQUIRE(Tools::RecordWaiver(mapper, "dbtool 0.0.0").has_value());
    // Taken while schema_backups holds rows; the archive leaves the table out.
    auto const second = UniqueTempPath("backup-recording-roundtrip-2", ".zip");
    auto const cleanupSecond = ScopedFileRemoved { second };
    BackUpDatabase(second);
    auto const archive = Zip::ZipArchive::Open(second);
    if (!archive)
    {
        FAIL("archive has no value");
        return;
    }
    CHECK(std::ranges::none_of(archive->GetAllEntries(),
                               [](Zip::EntryInfo const& entry) { return entry.name.contains("schema_backups"); }));

    RestoreDatabase(second);
    auto const restored = Tools::RecordRestore(mapper, second, "dbtool 0.0.0");

    if (!restored)
    {
        FAIL("restored has no value");
        return;
    }
    CHECK(restored->id > first->id);
    CHECK(RecordedKinds(mapper) == "backup waived restore");
    auto const latest = ReadLatestBackupRecord(mapper);
    if (!latest)
    {
        FAIL("latest has no value");
        return;
    }
    CHECK((*latest).id == restored->id);
}

TEST_CASE_METHOD(BackupRecordingFixture, "A filtered backup, restored, does not satisfy a requirement", "[BackupRecording]")
{
    CreateSubjectTable();
    auto& manager = MigrationManager::GetInstance();
    auto const migration = Migration<20990301000001> { "needs a backup", NoChange };
    manager.RegisterRelease("3.0.0", MigrationTimestamp { 20990301000001 }, { .requiresBackup = true });
    auto const file = UniqueTempPath("backup-recording-filtered", ".zip");
    auto const cleanup = ScopedFileRemoved { file };
    auto mapper = Tools::ConnectRecordDataMapper(SqlConnection::DefaultConnectionString(), {});

    BackUpDatabase(file, "rec_subject");
    REQUIRE(SqlBackup::VerifyArchive(file).has_value());
    CHECK_FALSE(SqlBackup::VerifyArchive(file)->complete);
    auto const backedUp = Tools::RecordBackup(mapper, file, Tools::CaptureBackupStart(mapper), "dbtool 0.0.0");
    REQUIRE_FALSE(backedUp.has_value());
    CHECK(backedUp.error().kind == Tools::BackupRecordError::Kind::PartialArchive);

    RestoreDatabase(file);
    auto const restored = Tools::RecordRestore(mapper, file, "dbtool 0.0.0");

    REQUIRE_FALSE(restored.has_value());
    CHECK(restored.error().kind == Tools::BackupRecordError::Kind::PartialArchive);
    CHECK(Tools::DescribeUnrecordedRestore(restored.error())
              .ends_with("the restore cannot satisfy a migration backup requirement."));
    CHECK_FALSE(ReadLatestBackupRecord(mapper).has_value());
    auto const pending = std::vector { MigrationTimestamp { 20990301000001 } };
    auto const requirement = manager.BackupRequirementFor(pending);
    CHECK(requirement.required);
    CHECK_FALSE(requirement.satisfied);
}

TEST_CASE_METHOD(BackupRecordingFixture,
                 "An archive whose metadata lacks the completeness flag still records",
                 "[BackupRecording]")
{
    CreateSubjectTable();
    auto const current = UniqueTempPath("backup-recording-current", ".zip");
    auto const older = UniqueTempPath("backup-recording-older", ".zip");
    auto const cleanupCurrent = ScopedFileRemoved { current };
    auto const cleanupOlder = ScopedFileRemoved { older };
    BackUpDatabase(current);
    SqlBackup::Tests::RewriteArchive(current, older, [](std::string const& name, std::string content) {
        if (name == "metadata.json")
        {
            auto metadata = nlohmann::json::parse(content);
            metadata.erase("complete");
            content = metadata.dump();
        }
        return std::optional { std::pair { name, std::move(content) } };
    });
    auto mapper = Tools::ConnectRecordDataMapper(SqlConnection::DefaultConnectionString(), {});

    auto const recorded = Tools::RecordRestore(mapper, older, "dbtool 0.0.0");

    if (!recorded)
    {
        FAIL("recorded has no value");
        return;
    }
    CHECK(recorded->kind == BackupRecordKind::Restore);
    CHECK(RecordCount(mapper) == 1);
}

TEST_CASE("DescribeUnrecordedBackup says a backup it could not record cannot satisfy a requirement", "[BackupRecording]")
{
    CHECK(
        Tools::DescribeUnrecordedBackup({ .kind = Tools::BackupRecordError::Kind::WriteFailed, .message = "denied" })
        == "The backup could not be recorded in schema_backups (denied); it cannot satisfy a migration backup requirement.");
    CHECK(Tools::DescribeUnrecordedBackup(
              { .kind = Tools::BackupRecordError::Kind::VerificationFailed, .message = "checksums.json is missing" })
          == "The archive failed verification: checksums.json is missing; the backup is not recorded and cannot satisfy a "
             "migration backup requirement.");
    CHECK(Tools::DescribeUnrecordedRestore({ .kind = Tools::BackupRecordError::Kind::WriteFailed, .message = "denied" })
          == "The restore could not be recorded in schema_backups (denied); it cannot satisfy a migration backup "
             "requirement.");
}

TEST_CASE("ToolVersion names the tool and this build's version", "[BackupRecording]")
{
    CHECK(Tools::ToolVersion("dbtool").starts_with("dbtool "));
    CHECK(Tools::ToolVersion("dbtool").size() > std::string_view { "dbtool " }.size());
}
