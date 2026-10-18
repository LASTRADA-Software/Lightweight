// SPDX-License-Identifier: Apache-2.0

#include "Utils.hpp"

#include <Lightweight/SqlMigration.hpp>
#include <Lightweight/SqlSchema.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <chrono>
#include <format>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

using namespace Lightweight;
using namespace std::chrono_literals;
using namespace SqlMigration;

namespace
{

class BackupRequirementFixture: public SqlTestFixture
{
  public:
    BackupRequirementFixture()
    {
        auto& manager = MigrationManager::GetInstance();
        manager.RemoveAllMigrations();
        manager.RemoveAllReleases();
        manager.RemoveAllPreMigrateSteps();
    }
    BackupRequirementFixture(BackupRequirementFixture const&) = delete;
    BackupRequirementFixture(BackupRequirementFixture&&) = delete;
    BackupRequirementFixture& operator=(BackupRequirementFixture const&) = delete;
    BackupRequirementFixture& operator=(BackupRequirementFixture&&) = delete;
    ~BackupRequirementFixture() override
    {
        MigrationManager::GetInstance().RemoveAllReleases();
        MigrationManager::GetInstance().RemoveAllPreMigrateSteps();
        MigrationManager::GetInstance().CloseDataMapper();
    }
};

/// A migration with an empty plan.
template <uint64_t Timestamp>
using EmptyMigration = Migration<Timestamp>;

void NoChange(SqlMigrationQueryBuilder& /*plan*/) {}

/// A record that saw @p state; the hash is a placeholder of the right length.
BackupRecord MakeRecord(MigrationHistoryState state,
                        BackupRecordKind kind = BackupRecordKind::Backup,
                        SqlDateTime createdAt = SqlDateTime::Now())
{
    return BackupRecord { .id = 0,
                          .createdAt = createdAt,
                          .archiveFileName = "backup.zip",
                          .archiveSha256 = std::string(64, 'a'),
                          .observedState = std::move(state),
                          .toolVersion = "test 1.0",
                          .kind = kind };
}

/// A history state that differs from every other by its digest alone.
MigrationHistoryState StateWithDigest(char fill, std::optional<MigrationTimestamp> lastApplied = std::nullopt)
{
    return MigrationHistoryState { .lastAppliedMigration = lastApplied,
                                   .lastAppliedAt = std::nullopt,
                                   .digest = std::string(64, fill) };
}

/// The state the database is in now.
MigrationHistoryState CurrentState()
{
    return ReadMigrationHistoryState(MigrationManager::GetInstance().GetDataMapper());
}

/// Backdates or restamps the applied time of one recorded migration.
void SetAppliedAt(std::uint64_t version, SqlDateTime appliedAt)
{
    auto stmt = SqlStatement { MigrationManager::GetInstance().GetDataMapper().Connection() };
    stmt.Prepare(R"(UPDATE "schema_migrations" SET "applied_at" = ? WHERE "version" = ?)");
    (void) stmt.Execute(appliedAt, version);
}

/// The applied time of one recorded migration.
SqlDateTime AppliedAtOf(std::uint64_t version)
{
    auto stmt = SqlStatement { MigrationManager::GetInstance().GetDataMapper().Connection() };
    auto const appliedAt = stmt.ExecuteDirectScalar<SqlDateTime>(
        std::format(R"(SELECT "applied_at" FROM "schema_migrations" WHERE "version" = {})", version));
    if (!appliedAt)
    {
        FAIL("migration " << version << " has no applied time");
        return {};
    }
    return *appliedAt;
}

/// A step whose predicate demands no backup.
PreMigrateStepOptions DemandsNoBackup()
{
    return { .requiresBackup = [](SqlConnection&, MigrationManager&) { return false; } };
}

void NoOpStep(SqlConnection& /*connection*/, MigrationManager& /*manager*/, ProgressTask& /*task*/, ExecutionMode /*mode*/)
{
}

bool TableExists(std::string_view name)
{
    auto stmt = SqlStatement {};
    auto const tables = SqlSchema::ReadAllTables(stmt, std::string {}, std::string {});
    return std::ranges::any_of(tables, [name](SqlSchema::Table const& table) { return table.name == name; });
}

/// The timestamps of the pending migrations, as `migrate` runs them.
std::vector<MigrationTimestamp> PendingTimestamps()
{
    auto timestamps = std::vector<MigrationTimestamp> {};
    for (auto const* migration: MigrationManager::GetInstance().GetPending())
        timestamps.push_back(migration->GetTimestamp());
    return timestamps;
}

/// Applies the one registered migration, then writes a record covering the resulting state.
void ApplyAndBackUp(BackupRecordKind kind = BackupRecordKind::Backup)
{
    auto& manager = MigrationManager::GetInstance();
    manager.CreateMigrationHistory();
    REQUIRE(manager.ApplyPendingMigrations() == 1);
    auto const state = ReadMigrationHistoryState(manager.GetDataMapper());
    if (!state.lastAppliedMigration)
    {
        FAIL("state.lastAppliedMigration has no value");
        return;
    }
    REQUIRE(WriteBackupRecord(manager.GetDataMapper(), MakeRecord(state, kind)).has_value());
}

} // namespace

// --- The freshness rule ---------------------------------------------------------------------

TEST_CASE("BackupRecordCovers needs a backup or restore record that saw the current digest",
          "[SqlMigration][BackupRequirement]")
{
    auto const state = StateWithDigest('a', MigrationTimestamp { 2 });
    CHECK(BackupRecordCovers(MakeRecord(state), state));
    CHECK(BackupRecordCovers(MakeRecord(state, BackupRecordKind::Restore), state));
    CHECK_FALSE(BackupRecordCovers(MakeRecord(StateWithDigest('b', MigrationTimestamp { 2 })), state));
    CHECK_FALSE(BackupRecordCovers(MakeRecord(state, BackupRecordKind::Waived), state));
    CHECK_FALSE(BackupRecordCovers(MakeRecord(state, static_cast<BackupRecordKind>(0x7F)), state));
    CHECK(BackupRecordCovers(MakeRecord(MigrationHistoryState {}), MigrationHistoryState {}));
    CHECK_FALSE(BackupRecordCovers(MakeRecord(StateWithDigest('a')), MigrationHistoryState {}));
}

TEST_CASE("BackupRecordCovers ignores created_at, so clock skew cannot flip the result", "[SqlMigration][BackupRequirement]")
{
    auto const now = std::chrono::system_clock::now();
    auto const state = StateWithDigest('a', MigrationTimestamp { 2 });
    for (auto const createdAt: { SqlDateTime { now + 24h * 365 }, SqlDateTime { now - 24h * 365 } })
    {
        CHECK(BackupRecordCovers(MakeRecord(state, BackupRecordKind::Backup, createdAt), state));
        CHECK_FALSE(BackupRecordCovers(
            MakeRecord(StateWithDigest('b', MigrationTimestamp { 2 }), BackupRecordKind::Backup, createdAt), state));
    }
}

TEST_CASE_METHOD(BackupRequirementFixture,
                 "The history digest follows the physical rows",
                 "[SqlMigration][BackupRequirement]")
{
    auto& manager = MigrationManager::GetInstance();
    CHECK(CurrentState().digest == EmptyMigrationHistoryDigest);
    auto const first = EmptyMigration<20990201000001> { "first", NoChange };
    ApplyAndBackUp();
    auto const applied = CurrentState();
    CHECK(applied.digest.size() == 64);
    CHECK(applied.digest != EmptyMigrationHistoryDigest);
    CHECK(CurrentState().digest == applied.digest);
    SetAppliedAt(20990201000001, SqlDateTime { AppliedAtOf(20990201000001).value() + 1s });
    CHECK(CurrentState().digest != applied.digest);
    std::ignore = manager;
}

// --- Scope: only the work the command runs counts ---------------------------------------------

TEST_CASE_METHOD(BackupRequirementFixture,
                 "Nothing demands a backup: none is required and nothing is read or created",
                 "[SqlMigration][BackupRequirement]")
{
    auto& manager = MigrationManager::GetInstance();
    auto const plain = EmptyMigration<20990201000001> { "plain", NoChange };
    manager.RegisterRelease("1.0.0", MigrationTimestamp { 20990201000001 });
    manager.AddPreMigrateStep("Quiet step", DemandsNoBackup(), NoOpStep);
    manager.AddPreMigrateStep("Step without predicate", {}, NoOpStep);
    auto const requirement = manager.BackupRequirementFor(PendingTimestamps());
    CHECK_FALSE(requirement.required);
    CHECK(requirement.satisfied);
    CHECK(requirement.reasons.empty());
    CHECK_FALSE(requirement.lastBackup.has_value());
    CHECK_FALSE(TableExists(BackupRecordTableName));
}

TEST_CASE_METHOD(BackupRequirementFixture,
                 "migrate: a pending migration of a release that requires a backup demands one",
                 "[SqlMigration][BackupRequirement]")
{
    auto& manager = MigrationManager::GetInstance();
    auto const pending = EmptyMigration<20990201000001> { "needs backup", NoChange };
    manager.RegisterRelease("3.0.0", MigrationTimestamp { 20990201000001 }, { .requiresBackup = true });
    REQUIRE(manager.GetAllReleases().front().requiresBackup);
    auto const requirement = manager.BackupRequirementFor(PendingTimestamps());
    CHECK(requirement.required);
    CHECK_FALSE(requirement.satisfied);
    CHECK(requirement.reasons == std::vector<std::string> { "release 3.0.0" });
    CHECK_FALSE(requirement.lastBackup.has_value());
}

TEST_CASE_METHOD(BackupRequirementFixture,
                 "migrate-to-release: only the pending migrations up to that release count",
                 "[SqlMigration][BackupRequirement]")
{
    auto& manager = MigrationManager::GetInstance();
    auto const first = EmptyMigration<20990201000001> { "old release", NoChange };
    auto const second = EmptyMigration<20990201000002> { "new release", NoChange };
    manager.RegisterRelease("2.0.0", MigrationTimestamp { 20990201000001 });
    manager.RegisterRelease("3.0.0", MigrationTimestamp { 20990201000002 }, { .requiresBackup = true });
    auto const upTo2 = std::vector { MigrationTimestamp { 20990201000001 } };
    CHECK_FALSE(manager.BackupRequirementFor(upTo2).required);
    CHECK(manager.BackupRequirementFor(PendingTimestamps()).reasons == std::vector<std::string> { "release 3.0.0" });
}

TEST_CASE_METHOD(BackupRequirementFixture, "apply: only the named migration counts", "[SqlMigration][BackupRequirement]")
{
    auto& manager = MigrationManager::GetInstance();
    auto const demanding = Migration<20990201000001> { "demanding", MigrationMetadata { .requiresBackup = true }, NoChange };
    auto const plain = EmptyMigration<20990201000002> { "plain", NoChange };
    CHECK(demanding.RequiresBackup());
    CHECK_FALSE(plain.RequiresBackup());
    CHECK_FALSE(manager.BackupRequirementFor(std::vector { MigrationTimestamp { 20990201000002 } }).required);
    CHECK(manager.BackupRequirementFor(std::vector { MigrationTimestamp { 20990201000001 } }).reasons
          == std::vector<std::string> { "migration 20990201000001 \"demanding\"" });
}

TEST_CASE_METHOD(BackupRequirementFixture,
                 "A migration's own flag is not named again when its release already is",
                 "[SqlMigration][BackupRequirement]")
{
    auto& manager = MigrationManager::GetInstance();
    auto const demanding = Migration<20990201000001> { "demanding", MigrationMetadata { .requiresBackup = true }, NoChange };
    auto const other = Migration<20990201000002> { "other", MigrationMetadata { .requiresBackup = true }, NoChange };
    manager.RegisterRelease("3.0.0", MigrationTimestamp { 20990201000001 }, { .requiresBackup = true });
    CHECK(manager.BackupRequirementFor(PendingTimestamps()).reasons
          == std::vector<std::string> { "release 3.0.0", "migration 20990201000002 \"other\"" });
}

TEST_CASE_METHOD(BackupRequirementFixture,
                 "A step counts only when its predicate says so",
                 "[SqlMigration][BackupRequirement]")
{
    auto& manager = MigrationManager::GetInstance();
    auto asked = 0;
    manager.AddPreMigrateStep("Widen text columns",
                              { .requiresBackup =
                                    [&asked](SqlConnection& connection, MigrationManager& stepManager) {
                                        ++asked;
                                        CHECK(&connection == &stepManager.GetDataMapper().Connection());
                                        return true;
                                    } },
                              NoOpStep);
    manager.AddPreMigrateStep("Quiet step", DemandsNoBackup(), NoOpStep);
    auto const requirement = manager.BackupRequirementFor({});
    CHECK(asked == 1);
    CHECK(requirement.required);
    CHECK(requirement.reasons == std::vector<std::string> { "step \"Widen text columns\"" });
}

TEST_CASE_METHOD(BackupRequirementFixture,
                 "A predicate that throws makes the check throw",
                 "[SqlMigration][BackupRequirement]")
{
    auto& manager = MigrationManager::GetInstance();
    manager.AddPreMigrateStep(
        "Broken",
        { .requiresBackup = [](SqlConnection&, MigrationManager&) -> bool { throw std::runtime_error("no plan"); } },
        NoOpStep);
    CHECK_THROWS_WITH(manager.BackupRequirementFor({}), Catch::Matchers::ContainsSubstring("no plan"));
}

// --- Satisfied or not ------------------------------------------------------------------------

TEST_CASE_METHOD(BackupRequirementFixture,
                 "A backup taken after the last migration satisfies the requirement",
                 "[SqlMigration][BackupRequirement]")
{
    auto& manager = MigrationManager::GetInstance();
    auto const applied = EmptyMigration<20990201000001> { "applied", NoChange };
    ApplyAndBackUp();
    auto const pending = EmptyMigration<20990201000002> { "needs backup", NoChange };
    manager.RegisterRelease("3.0.0", MigrationTimestamp { 20990201000002 }, { .requiresBackup = true });
    auto const requirement = manager.BackupRequirementFor(PendingTimestamps());
    CHECK(requirement.required);
    CHECK(requirement.satisfied);
    if (!requirement.lastBackup)
    {
        FAIL("requirement.lastBackup has no value");
        return;
    }
    CHECK((*requirement.lastBackup).archiveFileName == "backup.zip");
}

TEST_CASE_METHOD(BackupRequirementFixture,
                 "A restore record satisfies the requirement too",
                 "[SqlMigration][BackupRequirement]")
{
    auto& manager = MigrationManager::GetInstance();
    auto const applied = EmptyMigration<20990201000001> { "applied", NoChange };
    ApplyAndBackUp(BackupRecordKind::Restore);
    auto const pending = EmptyMigration<20990201000002> { "needs backup", NoChange };
    manager.RegisterRelease("3.0.0", MigrationTimestamp { 20990201000002 }, { .requiresBackup = true });
    auto const requirement = manager.BackupRequirementFor(PendingTimestamps());
    CHECK(requirement.satisfied);
    if (!requirement.lastBackup)
    {
        FAIL("requirement.lastBackup has no value");
        return;
    }
    CHECK((*requirement.lastBackup).kind == BackupRecordKind::Restore);
}

TEST_CASE_METHOD(BackupRequirementFixture,
                 "A backup that predates the last migration does not count",
                 "[SqlMigration][BackupRequirement]")
{
    auto& manager = MigrationManager::GetInstance();
    auto const applied = EmptyMigration<20990201000001> { "applied", NoChange };
    manager.CreateMigrationHistory();
    REQUIRE(manager.ApplyPendingMigrations() == 1);
    auto const pending = EmptyMigration<20990201000002> { "needs backup", NoChange };
    manager.RegisterRelease("3.0.0", MigrationTimestamp { 20990201000002 }, { .requiresBackup = true });

    // Names the right migration, but saw another history.
    REQUIRE(
        WriteBackupRecord(manager.GetDataMapper(), MakeRecord(StateWithDigest('a', MigrationTimestamp { 20990201000001 })))
            .has_value());
    CHECK_FALSE(manager.BackupRequirementFor(PendingTimestamps()).satisfied);

    // A newer record of an older (empty) history does not count either; the newest record is compared.
    REQUIRE(WriteBackupRecord(manager.GetDataMapper(), MakeRecord(MigrationHistoryState {})).has_value());
    auto const requirement = manager.BackupRequirementFor(PendingTimestamps());
    CHECK_FALSE(requirement.satisfied);
    if (!requirement.lastBackup)
    {
        FAIL("requirement.lastBackup has no value");
        return;
    }
    CHECK_FALSE((*requirement.lastBackup).observedState.lastAppliedMigration.has_value());
}

TEST_CASE_METHOD(BackupRequirementFixture,
                 "A waived row never satisfies a requirement and is not the last backup",
                 "[SqlMigration][BackupRequirement]")
{
    auto& manager = MigrationManager::GetInstance();
    auto const applied = EmptyMigration<20990201000001> { "applied", NoChange };
    manager.CreateMigrationHistory();
    REQUIRE(manager.ApplyPendingMigrations() == 1);
    auto const pending = EmptyMigration<20990201000002> { "needs backup", NoChange };
    manager.RegisterRelease("3.0.0", MigrationTimestamp { 20990201000002 }, { .requiresBackup = true });

    // A waiver of the current state, written after the last migration: it describes the state exactly, yet counts for
    // nothing.
    REQUIRE(WriteBackupRecord(manager.GetDataMapper(), MakeRecord(CurrentState(), BackupRecordKind::Waived)).has_value());
    auto const requirement = manager.BackupRequirementFor(PendingTimestamps());
    CHECK(requirement.required);
    CHECK_FALSE(requirement.satisfied);
    CHECK_FALSE(requirement.lastBackup.has_value());
}

// --- The record --------------------------------------------------------------------------------

TEST_CASE_METHOD(BackupRequirementFixture,
                 "WriteBackupRecord round-trips every field, a non-ASCII file name included",
                 "[SqlMigration][BackupRequirement]")
{
    auto& dataMapper = MigrationManager::GetInstance().GetDataMapper();
    auto const wholeSecond = SqlDateTime { std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()) };
    auto record =
        MakeRecord(StateWithDigest('c', MigrationTimestamp { 20990201000001 }), BackupRecordKind::Restore, wholeSecond);
    record.archiveFileName = "Sicherung-M\xC3\xBCller.zip"; // UTF-8 "Sicherung-Müller.zip"
    auto const written = WriteBackupRecord(dataMapper, record);
    if (!written)
    {
        FAIL("written has no value");
        return;
    }
    CHECK((*written).id != 0);
    auto const read = ReadLatestBackupRecord(dataMapper);
    if (!read)
    {
        FAIL("read has no value");
        return;
    }
    auto const& stored = *read;
    CHECK(stored.id == (*written).id);
    CHECK(stored.archiveFileName == record.archiveFileName);
    CHECK(stored.archiveSha256 == record.archiveSha256);
    CHECK(stored.observedState.lastAppliedMigration.has_value());
    CHECK(stored.observedState.lastAppliedMigration.value_or(MigrationTimestamp {}).value
          == record.observedState.lastAppliedMigration.value_or(MigrationTimestamp {}).value);
    CHECK(stored.observedState.digest == record.observedState.digest);
    CHECK(stored.toolVersion == record.toolVersion);
    CHECK(stored.kind == BackupRecordKind::Restore);
    CHECK(stored.createdAt == record.createdAt);
}

TEST_CASE_METHOD(BackupRequirementFixture,
                 "A waived row is stored with its kind and an empty archive",
                 "[SqlMigration][BackupRequirement]")
{
    auto& dataMapper = MigrationManager::GetInstance().GetDataMapper();
    auto waiver = MakeRecord(MigrationHistoryState {}, BackupRecordKind::Waived);
    waiver.archiveFileName.clear();
    waiver.archiveSha256.clear();
    REQUIRE(WriteBackupRecord(dataMapper, waiver).has_value());
    auto stmt = SqlStatement { dataMapper.Connection() };
    CHECK(stmt.ExecuteDirectScalar<std::string>(R"(SELECT "kind" FROM "schema_backups")").value_or("") == "waived");
    CHECK_FALSE(ReadLatestBackupRecord(dataMapper).has_value());
}

TEST_CASE_METHOD(BackupRequirementFixture,
                 "WriteBackupRecord reports a table it cannot write to",
                 "[SqlMigration][BackupRequirement]")
{
    auto& dataMapper = MigrationManager::GetInstance().GetDataMapper();
    auto stmt = SqlStatement { dataMapper.Connection() };
    // A view in its place stands in for a table the user may read but not write.
    (void) stmt.ExecuteDirect(R"(CREATE VIEW "schema_backups" AS SELECT 1 AS "id")");
    auto const written = WriteBackupRecord(dataMapper, MakeRecord(MigrationHistoryState {}));
    CHECK_FALSE(written.has_value());
    (void) stmt.ExecuteDirect(R"(DROP VIEW "schema_backups")");
}

TEST_CASE_METHOD(BackupRequirementFixture, "Missing history tables read as empty", "[SqlMigration][BackupRequirement]")
{
    auto& dataMapper = MigrationManager::GetInstance().GetDataMapper();
    CHECK_FALSE(ReadLatestBackupRecord(dataMapper).has_value());
    CHECK_FALSE(ReadMigrationHistoryState(dataMapper).lastAppliedMigration.has_value());
}

TEST_CASE_METHOD(BackupRequirementFixture,
                 "hard-reset drops schema_backups and never reports it as preserved",
                 "[SqlMigration][BackupRequirement]")
{
    auto& manager = MigrationManager::GetInstance();
    REQUIRE(WriteBackupRecord(manager.GetDataMapper(), MakeRecord(MigrationHistoryState {})).has_value());
    auto const preview = manager.HardReset(ExecutionMode::DryRun);
    CHECK(preview.schemaBackupsDropped);
    CHECK(TableExists(BackupRecordTableName));
    CHECK_FALSE(
        std::ranges::any_of(preview.preservedTables, [](auto const& t) { return t.table == BackupRecordTableName; }));
    CHECK(manager.HardReset(ExecutionMode::Execute).schemaBackupsDropped);
    CHECK_FALSE(TableExists(BackupRecordTableName));
}

TEST_CASE_METHOD(BackupRequirementFixture,
                 "LIGHTWEIGHT_SQL_RELEASE's registrar takes the release options",
                 "[SqlMigration][BackupRequirement]")
{
    auto const registrar = SqlMigration::detail::ReleaseRegistrar { "9.9.9", 20991231235959, { .requiresBackup = true } };
    auto const* release = MigrationManager::GetInstance().FindReleaseByVersion("9.9.9");
    REQUIRE(release != nullptr);
    CHECK(release->requiresBackup);
}

TEST_CASE_METHOD(BackupRequirementFixture,
                 "An older backup followed by a newer waived row still satisfies the requirement",
                 "[SqlMigration][BackupRequirement]")
{
    auto& manager = MigrationManager::GetInstance();
    auto const applied = EmptyMigration<20990201000001> { "applied", NoChange };
    ApplyAndBackUp();
    auto waiver = MakeRecord(CurrentState(), BackupRecordKind::Waived);
    waiver.archiveFileName.clear();
    waiver.archiveSha256.clear();
    REQUIRE(WriteBackupRecord(manager.GetDataMapper(), waiver).has_value());
    auto const pending = EmptyMigration<20990201000002> { "needs backup", NoChange };
    manager.RegisterRelease("3.0.0", MigrationTimestamp { 20990201000002 }, { .requiresBackup = true });

    auto const requirement = manager.BackupRequirementFor(PendingTimestamps());

    CHECK(requirement.satisfied);
    if (!requirement.lastBackup)
    {
        FAIL("requirement.lastBackup has no value");
        return;
    }
    CHECK((*requirement.lastBackup).kind == BackupRecordKind::Backup);
    CHECK((*requirement.lastBackup).archiveFileName == "backup.zip");
}

TEST_CASE_METHOD(BackupRequirementFixture,
                 "A lower migration applied after the highest one makes an earlier backup stale",
                 "[SqlMigration][BackupRequirement]")
{
    auto& manager = MigrationManager::GetInstance();
    auto const first = EmptyMigration<20990201000001> { "first", NoChange };
    auto const second = EmptyMigration<20990201000002> { "second", NoChange };
    auto const third = EmptyMigration<20990201000003> { "third", NoChange };
    manager.RegisterRelease("3.0.0", MigrationTimestamp { 20990201000003 }, { .requiresBackup = true });
    manager.CreateMigrationHistory();
    manager.ApplySingleMigration(second);

    // The highest migration was applied two hours ago; the backup saw exactly that state.
    SetAppliedAt(20990201000002, SqlDateTime { std::chrono::system_clock::now() - 2h });
    REQUIRE(WriteBackupRecord(manager.GetDataMapper(), MakeRecord(CurrentState())).has_value());
    auto const toRun = std::vector { MigrationTimestamp { 20990201000003 } };
    CHECK(manager.BackupRequirementFor(toRun).satisfied);

    manager.ApplySingleMigration(first);

    auto const state = ReadMigrationHistoryState(manager.GetDataMapper());
    if (!state.lastAppliedMigration)
    {
        FAIL("state.lastAppliedMigration has no value");
        return;
    }
    CHECK((*state.lastAppliedMigration).value == 20990201000002);
    CHECK_FALSE(manager.BackupRequirementFor(toRun).satisfied);
}

TEST_CASE_METHOD(BackupRequirementFixture,
                 "Rolling back a lower migration makes an earlier backup stale",
                 "[SqlMigration][BackupRequirement]")
{
    auto& manager = MigrationManager::GetInstance();
    auto const first = Migration<20990201000001> { "first", NoChange, NoChange };
    auto const second = Migration<20990201000002> { "second", NoChange, NoChange };
    auto const third = EmptyMigration<20990201000003> { "third", NoChange };
    manager.RegisterRelease("3.0.0", MigrationTimestamp { 20990201000003 }, { .requiresBackup = true });
    manager.CreateMigrationHistory();
    REQUIRE(manager.ApplyPendingMigrations() == 3);
    REQUIRE(WriteBackupRecord(manager.GetDataMapper(), MakeRecord(CurrentState())).has_value());
    auto const toRun = std::vector { MigrationTimestamp { 20990201000003 } };
    CHECK(manager.BackupRequirementFor(toRun).satisfied);

    manager.RevertSingleMigration(first);

    CHECK(CurrentState().lastAppliedMigration.value_or(MigrationTimestamp {}).value == 20990201000003);
    CHECK_FALSE(manager.BackupRequirementFor(toRun).satisfied);
    std::ignore = second;
}

TEST_CASE_METHOD(BackupRequirementFixture,
                 "Reverting and re-applying a lower migration covers only an identical history",
                 "[SqlMigration][BackupRequirement]")
{
    auto& manager = MigrationManager::GetInstance();
    auto const first = Migration<20990201000001> { "first", NoChange, NoChange };
    auto const second = EmptyMigration<20990201000002> { "second", NoChange };
    manager.RegisterRelease("3.0.0", MigrationTimestamp { 20990201000002 }, { .requiresBackup = true });
    manager.CreateMigrationHistory();
    REQUIRE(manager.ApplyPendingMigrations() == 2);
    auto const firstAppliedAt = AppliedAtOf(20990201000001);
    REQUIRE(WriteBackupRecord(manager.GetDataMapper(), MakeRecord(CurrentState())).has_value());
    auto const toRun = std::vector { MigrationTimestamp { 20990201000002 } };

    manager.RevertSingleMigration(first);
    manager.ApplySingleMigration(first);

    SetAppliedAt(20990201000001, SqlDateTime { firstAppliedAt.value() + 1s });
    CHECK_FALSE(manager.BackupRequirementFor(toRun).satisfied);
    SetAppliedAt(20990201000001, firstAppliedAt);
    CHECK(manager.BackupRequirementFor(toRun).satisfied);
}

TEST_CASE_METHOD(BackupRequirementFixture,
                 "A newer row of an unknown kind is neither the last backup nor a cover",
                 "[SqlMigration][BackupRequirement]")
{
    auto& manager = MigrationManager::GetInstance();
    auto const applied = EmptyMigration<20990201000001> { "applied", NoChange };
    ApplyAndBackUp();
    auto stmt = SqlStatement { manager.GetDataMapper().Connection() };
    stmt.Prepare(R"(INSERT INTO "schema_backups" ("created_at", "archive_file_name", "archive_sha256", "last_migration", )"
                 R"("migration_state_digest", "tool_version", "kind") VALUES (?, ?, ?, ?, ?, ?, ?))");
    (void) stmt.Execute(SqlDateTime::Now(),
                        std::string { "other.zip" },
                        std::string(64, 'b'),
                        std::uint64_t { 20990201000001 },
                        CurrentState().digest,
                        std::string { "test 1.0" },
                        std::string { "mystery" });
    auto const pending = EmptyMigration<20990201000002> { "needs backup", NoChange };
    manager.RegisterRelease("3.0.0", MigrationTimestamp { 20990201000002 }, { .requiresBackup = true });

    auto const requirement = manager.BackupRequirementFor(PendingTimestamps());

    if (!requirement.lastBackup)
    {
        FAIL("requirement.lastBackup has no value");
        return;
    }
    CHECK((*requirement.lastBackup).archiveFileName == "backup.zip");
    CHECK(requirement.satisfied);
}

TEST_CASE_METHOD(BackupRequirementFixture,
                 "The release registrar also takes a MigrationTimestamp",
                 "[SqlMigration][BackupRequirement]")
{
    auto const registrar = SqlMigration::detail::ReleaseRegistrar { "9.9.8",
                                                                    MigrationTimestamp { 20991231235958 },
                                                                    { .requiresBackup = true } };
    auto const* release = MigrationManager::GetInstance().FindReleaseByVersion("9.9.8");
    REQUIRE(release != nullptr);
    CHECK(release->requiresBackup);
    CHECK(release->highestTimestamp.value == 20991231235958);
}
