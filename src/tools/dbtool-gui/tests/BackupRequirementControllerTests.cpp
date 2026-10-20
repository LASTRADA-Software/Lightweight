// SPDX-License-Identifier: Apache-2.0
//
// AppController blocks the migrations while the pending work requires a backup that does not exist:
// `backupRequired` names the reasons, "Back up now" writes a recorded backup into the backup folder
// (no dialog) and lifts the block, and "Migrate without a backup" waives it for one run only. The
// tests load the backup-required fixture plugin into a scratch plugins directory and connect to a
// scratch SQLite database.

#include "../AppController.hpp"
#include "../BackupRunner.hpp"
#include "../ManagedBackupController.hpp"
#include "../MigrationRunner.hpp"
#include "ControllerTestSupport.hpp"
#include "MigrationRunnerTestSupport.hpp"
#include "SqliteDriver.hpp"

#include <Lightweight/SqlConnection.hpp>
#include <Lightweight/SqlMigration.hpp>
#include <Lightweight/SqlStatement.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <fstream>
#include <ranges>
#include <string>
#include <utility>

#include <BackupRecording.hpp>
#include <QtCore/QSettings>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QTemporaryDir>
#include <QtCore/QVariant>
#include <QtTest/QSignalSpy>

using DbtoolGui::AppController;
using DbtoolGui::BackupRunner;
using DbtoolGui::MigrationRunner;
using DbtoolGui::Tests::ConnectAndWait;
using DbtoolGui::Tests::RecordedKinds;
using DbtoolGui::Tests::ScopedSettingRestore;
using DbtoolGui::Tests::SqliteDriver;
using DbtoolGui::Tests::WaitForIdle;
using Lightweight::SqlMigration::MigrationManager;
using Lightweight::SqlMigration::MigrationTimestamp;

namespace
{

/// The timestamp of the backup-required fixture's migration.
constexpr auto FixtureMigration = MigrationTimestamp { 20240201000000 };

/// A scratch directory with a plugins folder holding one fixture plugin, a `dbtool.yml` with one
/// profile that connects to `db.sqlite` there, and a `backups` folder.
class Workspace
{
  public:
    /// @param fixture The fixture plugin to copy.
    /// @param pluginName The `*Plugin` name discovery loads it under.
    /// @param profileName The profile's name, UTF-8.
    Workspace(std::filesystem::path const& fixture, std::string const& pluginName, std::string const& profileName):
        _root { _directory.path().toStdU16String() },
        _profileName { QString::fromStdString(profileName) }
    {
        std::filesystem::create_directories(_root / "plugins");
        std::filesystem::copy_file(fixture, _root / "plugins" / (pluginName + fixture.extension().string()));
        std::ofstream(_root / "dbtool.yml", std::ios::binary)
            << "profiles:\n  \"" << profileName << "\":\n    connectionString: \"Driver={" << SqliteDriver
            << "};Database=" << (_root / "db.sqlite").generic_string() << "\"\n    pluginsDir: \""
            << (_root / "plugins").generic_string() << "\"\n";
    }

    /// @return The folder "Back up now" writes into.
    [[nodiscard]] std::filesystem::path Backups() const
    {
        return _root / "backups";
    }

    /// Loads the profile file into @p controller, points its backup folder here and connects to the profile.
    void Connect(AppController& controller) const
    {
        controller.managedBackups()->setBackupFolder(QString::fromStdU16String(Backups().u16string()));
        REQUIRE(controller.loadProfiles(QString::fromStdU16String((_root / "dbtool.yml").u16string())));
        controller.setConnectionMode(QStringLiteral("profile"));
        controller.setCurrentProfile(_profileName);
        REQUIRE(ConnectAndWait(controller));
    }

  private:
    QTemporaryDir _directory;
    std::filesystem::path _root;
    QString _profileName;
};

/// The backup-required fixture: one migration in release 3.0.0, which requires a backup.
[[nodiscard]] Workspace BackupRequiredWorkspace()
{
    return Workspace { DBTOOL_GUI_TESTS_BACKUP_REQUIRED_FIXTURE, "BackupRequiredReleasePlugin", "p" };
}

/// Runs "Back up now" and waits for the backup and the requirement check after it to finish.
void BackUpNow(AppController& controller)
{
    QSignalSpy done(controller.backupRunner(), &BackupRunner::finished);
    controller.backUpNow();
    REQUIRE((done.count() > 0 || done.wait(30000)));
    REQUIRE(WaitForIdle(controller));
}

/// The number of `.zip` files in @p folder.
[[nodiscard]] std::ptrdiff_t ArchiveCount(std::filesystem::path const& folder)
{
    if (!std::filesystem::is_directory(folder))
        return 0;
    return std::ranges::count_if(std::filesystem::directory_iterator { folder },
                                 [](auto const& entry) { return entry.path().extension() == ".zip"; });
}

} // namespace

TEST_CASE("a release that requires a backup blocks the migrations until Back up now has run",
          "[dbtool-gui][AppController][backup-requirement]")
{
    ScopedSettingRestore const restoreBackupFolder { QStringLiteral("backup/folder") };
    auto const workspace = BackupRequiredWorkspace();
    AppController controller;
    REQUIRE(WaitForIdle(controller));
    workspace.Connect(controller);

    CHECK(controller.backupRequired());
    CHECK(controller.backupRequirementReasons() == QStringList { QStringLiteral("release 3.0.0") });
    CHECK(controller.backupRequirementNote().isEmpty());

    BackUpNow(controller);

    INFO(controller.backupRunner()->lastResult().value(QStringLiteral("summary")).toString().toStdString());
    CHECK(controller.backupRunner()->lastResult().value(QStringLiteral("recorded")).toBool());
    CHECK_FALSE(controller.backupRequired());
    CHECK(controller.backupRequirementReasons().isEmpty());
    CHECK(ArchiveCount(workspace.Backups()) == 1);
}

TEST_CASE("without declarations nothing is blocked", "[dbtool-gui][AppController][backup-requirement]")
{
    ScopedSettingRestore const restoreBackupFolder { QStringLiteral("backup/folder") };
    auto const workspace = Workspace { DBTOOL_GUI_TESTS_COMPAT_POLICY_FIXTURE, "CompatPolicyPlugin", "p" };
    AppController controller;
    REQUIRE(WaitForIdle(controller));
    workspace.Connect(controller);

    REQUIRE(controller.pendingCount() > 0);
    CHECK_FALSE(controller.backupRequired());
    CHECK(controller.backupRequirementReasons().isEmpty());
}

TEST_CASE("a backup that cannot be recorded leaves the migrations blocked and says why",
          "[dbtool-gui][AppController][backup-requirement]")
{
    ScopedSettingRestore const restoreBackupFolder { QStringLiteral("backup/folder") };
    auto const workspace = BackupRequiredWorkspace();
    AppController controller;
    REQUIRE(WaitForIdle(controller));
    workspace.Connect(controller);
    REQUIRE(controller.backupRequired());

    controller.backupRunner()->setRecorder({
        .captureStart = {},
        .record = [](BackupRunner::Operation,
                     std::filesystem::path const&,
                     std::string const&,
                     std::string const&,
                     Lightweight::Tools::BackupStart const&)
            -> std::expected<Lightweight::SqlMigration::BackupRecord, Lightweight::Tools::BackupRecordError> {
            return std::unexpected(Lightweight::Tools::BackupRecordError {
                .kind = Lightweight::Tools::BackupRecordError::Kind::WriteFailed, .message = "denied" });
        },
    });
    BackUpNow(controller);

    CHECK(controller.backupRunner()->lastResult().value(QStringLiteral("ok")).toBool());
    CHECK(controller.backupRequired());
    CHECK(controller.backupRequirementNote().contains(QStringLiteral("denied")));
}

TEST_CASE("the \"Migrate without a backup\" checkbox enables the buttons for one run only",
          "[dbtool-gui][AppController][backup-requirement]")
{
    ScopedSettingRestore const restoreBackupFolder { QStringLiteral("backup/folder") };
    auto const workspace = BackupRequiredWorkspace();
    AppController controller;
    REQUIRE(WaitForIdle(controller));
    workspace.Connect(controller);
    REQUIRE(controller.backupRequired());

    CHECK_FALSE(controller.migrateWithoutBackup());
    QSignalSpy changed(&controller, &AppController::migrateWithoutBackupChanged);
    controller.setMigrateWithoutBackup(true);
    CHECK(controller.migrateWithoutBackup());
    CHECK(changed.count() == 1);
    auto const persisted = QSettings {}.allKeys();
    CHECK_FALSE(
        std::ranges::any_of(persisted, [](QString const& key) { return key.contains(QStringLiteral("WithoutBackup")); }));

    QSignalSpy finished(controller.runner(), &MigrationRunner::finished);
    controller.runner()->applyUpTo(QString {});
    REQUIRE((finished.count() > 0 || finished.wait(30000)));
    REQUIRE(finished.first().at(0).toBool());
    CHECK_FALSE(controller.migrateWithoutBackup());
    // The history and the requirement are re-read on the worker after the run.
    REQUIRE(WaitForIdle(controller));

    auto& manager = MigrationManager::GetInstance();
    CHECK(manager.GetPending().empty());
    CHECK(RecordedKinds(manager.GetDataMapper()) == "waived");

    // The waiver satisfies nothing: once the migration is pending again, the block is back.
    auto const* migration = manager.GetMigration(FixtureMigration);
    REQUIRE(migration != nullptr);
    manager.RevertSingleMigration(*migration);
    REQUIRE(ConnectAndWait(controller));
    CHECK(controller.backupRequired());
}

TEST_CASE("the \"Migrate without a backup\" checkbox resets on reconnect", "[dbtool-gui][AppController][backup-requirement]")
{
    ScopedSettingRestore const restoreBackupFolder { QStringLiteral("backup/folder") };
    auto const workspace = BackupRequiredWorkspace();
    AppController controller;
    REQUIRE(WaitForIdle(controller));
    workspace.Connect(controller);

    controller.setMigrateWithoutBackup(true);
    REQUIRE(controller.migrateWithoutBackup());
    REQUIRE(ConnectAndWait(controller));
    CHECK_FALSE(controller.migrateWithoutBackup());
}

TEST_CASE("the \"Migrate without a backup\" checkbox resets on disconnect",
          "[dbtool-gui][AppController][backup-requirement]")
{
    ScopedSettingRestore const restoreBackupFolder { QStringLiteral("backup/folder") };
    auto const workspace = BackupRequiredWorkspace();
    AppController controller;
    REQUIRE(WaitForIdle(controller));
    workspace.Connect(controller);

    controller.setMigrateWithoutBackup(true);
    REQUIRE(controller.migrateWithoutBackup());
    // Switching the connection source drops the connection.
    controller.setConnectionMode(QStringLiteral("dsn"));
    REQUIRE_FALSE(controller.connected());
    CHECK_FALSE(controller.migrateWithoutBackup());
    CHECK_FALSE(controller.backupRequired());
}

TEST_CASE("the \"Migrate without a backup\" checkbox resets once a backup satisfies the requirement",
          "[dbtool-gui][AppController][backup-requirement]")
{
    ScopedSettingRestore const restoreBackupFolder { QStringLiteral("backup/folder") };
    auto const workspace = BackupRequiredWorkspace();
    AppController controller;
    REQUIRE(WaitForIdle(controller));
    workspace.Connect(controller);

    controller.setMigrateWithoutBackup(true);
    BackUpNow(controller);
    REQUIRE_FALSE(controller.backupRequired());
    CHECK_FALSE(controller.migrateWithoutBackup());
}

TEST_CASE("connectToProfile is refused while a backup runs", "[dbtool-gui][AppController][backup-requirement]")
{
    ScopedSettingRestore const restoreBackupFolder { QStringLiteral("backup/folder") };
    auto const workspace = BackupRequiredWorkspace();
    AppController controller;
    REQUIRE(WaitForIdle(controller));
    workspace.Connect(controller);

    QSignalSpy done(controller.backupRunner(), &BackupRunner::finished);
    controller.backUpNow();
    // The run reports back through this thread's event loop, so it is still running here.
    REQUIRE(controller.backupRunner()->phase() == BackupRunner::Phase::Running);
    CHECK_FALSE(controller.connectToProfile());
    CHECK(controller.lastWarning().contains(QStringLiteral("backup or restore")));

    REQUIRE((done.count() > 0 || done.wait(30000)));
    CHECK(ConnectAndWait(controller));
}

TEST_CASE("Back up now names the archive after a non-ASCII target correctly",
          "[dbtool-gui][AppController][backup-requirement]")
{
    ScopedSettingRestore const restoreBackupFolder { QStringLiteral("backup/folder") };
    auto const workspace =
        Workspace { DBTOOL_GUI_TESTS_BACKUP_REQUIRED_FIXTURE, "BackupRequiredReleasePlugin", "M\xc3\xbcller" };
    AppController controller;
    REQUIRE(WaitForIdle(controller));
    workspace.Connect(controller);
    REQUIRE(controller.connectedTarget() == QString::fromUtf16(u"M\u00fcller"));

    BackUpNow(controller);

    auto names = QStringList {};
    for (auto const& entry: std::filesystem::directory_iterator { workspace.Backups() })
        names.append(QString::fromStdU16String(entry.path().filename().u16string()));
    INFO(names.join(QStringLiteral(", ")).toStdString());
    REQUIRE(names.size() == 1);
    CHECK(names.first().startsWith(QString::fromUtf16(u"M\u00fcller-")));
    CHECK_FALSE(controller.backupRequired());
}

TEST_CASE("Back up first then migrate: the run starts while the requirement check after the backup is pending",
          "[dbtool-gui][AppController][backup-requirement]")
{
    ScopedSettingRestore const restoreBackupFolder { QStringLiteral("backup/folder") };
    auto const workspace = BackupRequiredWorkspace();
    AppController controller;
    REQUIRE(WaitForIdle(controller));
    workspace.Connect(controller);
    REQUIRE(controller.backupRequired());

    // The Simple view's chain: once the backup finished, start the run. Connected after the controller's
    // own handler, which has just queued the requirement check the backup asks for.
    QSignalSpy runFinished(controller.runner(), &MigrationRunner::finished);
    auto backupOk = false;
    auto checkPendingAtChain = false;
    auto runRefused = true;
    QObject::connect(controller.backupRunner(),
                     &BackupRunner::finished,
                     [&controller, &backupOk, &checkPendingAtChain, &runRefused](bool ok, QString const&) {
                         backupOk = ok;
                         checkPendingAtChain = controller.backupRequirementPending();
                         controller.runner()->applyUpTo(QString {});
                         runRefused = controller.runner()->phase() == MigrationRunner::Phase::Idle;
                     });
    QSignalSpy backupFinished(controller.backupRunner(), &BackupRunner::finished);
    controller.backUpNow();
    REQUIRE((backupFinished.count() > 0 || backupFinished.wait(30000)));
    REQUIRE(backupOk);
    CHECK(checkPendingAtChain);
    REQUIRE_FALSE(runRefused);
    REQUIRE((runFinished.count() > 0 || runFinished.wait(30000)));
    CHECK(runFinished.first().at(0).toBool());
    REQUIRE(WaitForIdle(controller));
    CHECK(MigrationManager::GetInstance().GetPending().empty());
    CHECK(controller.pendingCount() == 0);
}

TEST_CASE("a SQL preview asked for while disconnected replies at once that it needs a connection",
          "[dbtool-gui][AppController][backup-requirement]")
{
    auto const workspace = BackupRequiredWorkspace();
    AppController controller;
    REQUIRE(WaitForIdle(controller));
    QSignalSpy ready(&controller, &AppController::migrationSqlPreviewReady);
    controller.requestMigrationSqlPreview(QString::number(FixtureMigration.value));
    REQUIRE(ready.count() == 1);
    CHECK(ready.first().at(1).toStringList()
          == QStringList { QStringLiteral("-- Connect to a database to preview its SQL.") });
}

TEST_CASE("a SQL preview whose connection changed before it arrived shows no statements",
          "[dbtool-gui][AppController][backup-requirement]")
{
    ScopedSettingRestore const restoreBackupFolder { QStringLiteral("backup/folder") };
    auto const workspace = BackupRequiredWorkspace();
    AppController controller;
    REQUIRE(WaitForIdle(controller));
    workspace.Connect(controller);

    QSignalSpy ready(&controller, &AppController::migrationSqlPreviewReady);
    controller.requestMigrationSqlPreview(QString::number(FixtureMigration.value));
    // The reply is delivered through this thread's event loop, so it cannot arrive before the switch.
    controller.setConnectionMode(QStringLiteral("dsn"));
    REQUIRE(ready.wait(15000));
    CHECK(ready.first().at(1).toStringList()
          == QStringList { QStringLiteral("-- The connection changed; open the preview again.") });
    REQUIRE(WaitForIdle(controller));
}
