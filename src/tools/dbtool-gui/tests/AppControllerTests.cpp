// SPDX-License-Identifier: Apache-2.0
//
// Catch2 coverage for `DbtoolGui::AppController` — the C++ view-model that
// holds every piece of long-lived state behind the dbtool-gui QML UI.
// Tests drive the controller through its `Q_INVOKABLE` surface and observe
// state changes via `QSignalSpy`, exactly the same way QML would, so a
// regression here will also be a regression in the live app.
//
// No QML engine is instantiated; `tests/main.cpp` boots a single
// `QGuiApplication` under `QT_QPA_PLATFORM=offscreen` and Catch2 owns the
// rest of the process lifetime.

#include "../AppController.hpp"
#include "../BackupRunner.hpp"
#include "../LogLevel.hpp"
#include "../ManagedBackupController.hpp"
#include "../MigrationRunner.hpp"
#include "../Models/MigrationSnapshot.hpp"
#include "ControllerTestSupport.hpp"
#include "MigrationRunnerTestSupport.hpp"
#include "SqliteDriver.hpp"

#include <Lightweight/SqlBackup.hpp>
#include <Lightweight/SqlMigration.hpp>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <thread>
#include <utility>

#include <QtCore/QCoreApplication>
#include <QtCore/QElapsedTimer>
#include <QtCore/QSettings>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QTemporaryDir>
#include <QtCore/QTimer>
#include <QtCore/QVariant>
#include <QtQuick/QQuickWindow>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>
#include <Secrets/ProfileCipher.hpp>

using DbtoolGui::Tests::ConnectAndWait;
using DbtoolGui::Tests::ScopedSettingRestore;
using DbtoolGui::Tests::SqliteDriver;
using DbtoolGui::Tests::WaitForIdle;

namespace
{

/// Absolute path to the test-fixtures directory next to this source file.
/// `__FILE__` resolves at compile time so the lookup works regardless of
/// where the binary is invoked from (build tree, install tree, IDE).
[[nodiscard]] std::filesystem::path FixturesDir()
{
    return std::filesystem::path(__FILE__).parent_path() / "fixtures";
}

/// Concatenates every captured `logLine` payload into a single newline-
/// separated string for substring assertions. Catch2's spy stores each
/// invocation as a `QList<QVariant>`; we only care about the first arg.
[[nodiscard]] QString JoinLogLines(QSignalSpy const& spy)
{
    QStringList lines;
    lines.reserve(static_cast<int>(spy.count()));
    for (auto const& args: spy)
        lines << args.value(0).toString();
    return lines.join(QLatin1Char('\n'));
}

} // namespace

TEST_CASE("AppController constructs in a disconnected state", "[dbtool-gui][AppController]")
{
    DbtoolGui::AppController controller;
    REQUIRE(WaitForIdle(controller));

    CHECK_FALSE(controller.connected());
    CHECK(controller.lastError().isEmpty());
}

TEST_CASE("preMigrateStepCount reads the snapshot, which counts the steps registered on the manager",
          "[dbtool-gui][AppController]")
{
    auto& manager = Lightweight::SqlMigration::MigrationManager::GetInstance();
    DbtoolGui::AppController controller;
    REQUIRE(WaitForIdle(controller));
    REQUIRE(controller.preMigrateStepCount() == 0);

    manager.AddPreMigrateStep("Widen text columns", {}, [](auto&&...) {});
    CHECK(DbtoolGui::CaptureMigrationSnapshot(manager, DbtoolGui::AppliedState::Unknown).preMigrateStepCount == 1);
    // The GUI thread never reads the manager; the count changes with the next snapshot only.
    CHECK(controller.preMigrateStepCount() == 0);
    manager.RemoveAllPreMigrateSteps();
    CHECK(DbtoolGui::CaptureMigrationSnapshot(manager, DbtoolGui::AppliedState::Unknown).preMigrateStepCount == 0);
}

TEST_CASE("AppController buffers a startup banner that replays on attachLogSink", "[dbtool-gui][AppController][startup]")
{
    DbtoolGui::AppController controller;
    REQUIRE(WaitForIdle(controller));
    QSignalSpy spy(&controller, &DbtoolGui::AppController::logLine);
    REQUIRE(spy.isValid());

    // Before `attachLogSink`, the controller buffers everything — the spy
    // sees nothing because no signal has been emitted yet.
    CHECK(spy.count() == 0);

    controller.attachLogSink();

    // After flushing the buffer, every banner line emerges as a `logLine`
    // signal. We assert on stable substrings rather than full text so the
    // test does not break the next time a banner line is reworded.
    auto const joined = JoinLogLines(spy);
    CHECK(joined.contains(QStringLiteral("dbtool-gui starting")));
    // The GUI is light-only (Lastrada UI); there is no theme choice to report.
    CHECK_FALSE(joined.contains(QStringLiteral("Theme:")));
    CHECK(joined.contains(QStringLiteral("View mode:")));
    CHECK(joined.contains(QStringLiteral("Profile store:")));
    CHECK(joined.contains(QStringLiteral("Ready")));
}

TEST_CASE("attachLogSink is idempotent — banner does not replay on a second call", "[dbtool-gui][AppController][startup]")
{
    DbtoolGui::AppController controller;
    REQUIRE(WaitForIdle(controller));
    QSignalSpy spy(&controller, &DbtoolGui::AppController::logLine);

    controller.attachLogSink();
    auto const firstFlushCount = spy.count();
    REQUIRE(firstFlushCount > 0);

    controller.attachLogSink();
    CHECK(spy.count() == firstFlushCount);
}

TEST_CASE("loadProfiles populates the profiles model from a fixture YAML", "[dbtool-gui][AppController][profiles]")
{
    auto const fixturePath = FixturesDir() / "dbtool.yml";
    REQUIRE(std::filesystem::exists(fixturePath));

    DbtoolGui::AppController controller;
    REQUIRE(WaitForIdle(controller));
    auto const ok = controller.loadProfiles(QString::fromStdString(fixturePath.string()));

    REQUIRE(ok);
    CHECK(controller.profiles()->rowCount() >= 2);
    CHECK(controller.profilePath().toStdString() == fixturePath.string());
}

TEST_CASE("setMigrationSelected on an unknown timestamp is a no-op", "[dbtool-gui][AppController][selection]")
{
    DbtoolGui::AppController controller;
    REQUIRE(WaitForIdle(controller));
    REQUIRE(controller.selectedMigrationTimestamps().isEmpty());

    controller.setMigrationSelected(QStringLiteral("99999999999999-ghost"), true);

    CHECK(controller.selectedMigrationTimestamps().isEmpty());
    CHECK(controller.selectionCount() == 0);
}

TEST_CASE("selectAllPending on an empty migration list is a no-op", "[dbtool-gui][AppController][selection]")
{
    DbtoolGui::AppController controller;
    REQUIRE(WaitForIdle(controller));

    controller.selectAllPending(true);
    CHECK(controller.selectionCount() == 0);

    controller.selectAllPending(false);
    CHECK(controller.selectionCount() == 0);
}

TEST_CASE("a SQL preview of a timestamp that is no number replies empty at once", "[dbtool-gui][AppController]")
{
    DbtoolGui::AppController controller;
    REQUIRE(WaitForIdle(controller));
    QSignalSpy ready(&controller, &DbtoolGui::AppController::migrationSqlPreviewReady);
    controller.requestMigrationSqlPreview(QStringLiteral("19700101000000-not-a-migration"));
    REQUIRE(ready.count() == 1);
    CHECK(ready.first().at(1).toStringList().isEmpty());
}

TEST_CASE("buildFailureReport returns a non-empty multi-line bundle even when disconnected",
          "[dbtool-gui][AppController][failure-report]")
{
    DbtoolGui::AppController controller;
    REQUIRE(WaitForIdle(controller));
    auto const report = controller.buildFailureReport();

    REQUIRE_FALSE(report.isEmpty());
    // The report is the diagnostic bundle the Failure card surfaces — it
    // should always carry the connection summary so support tickets can be
    // routed even when no profile has connected yet.
    CHECK(report.contains(QStringLiteral("Connection")));
}

TEST_CASE("releaseHighestTimestamp returns empty for an unknown release", "[dbtool-gui][AppController]")
{
    DbtoolGui::AppController controller;
    REQUIRE(WaitForIdle(controller));
    auto const ts = controller.releaseHighestTimestamp(QStringLiteral("v999.999.999"));
    CHECK(ts.isEmpty());
}

TEST_CASE("managedBackups is exposed and follows loadProfiles", "[dbtool-gui][AppController][managed-backup]")
{
    auto const fixturePath = FixturesDir() / "dbtool.yml";
    DbtoolGui::AppController controller;
    REQUIRE(WaitForIdle(controller));
    REQUIRE(controller.managedBackups() != nullptr);
    CHECK(controller.managedBackups()->status()->rowCount() == 0);

    REQUIRE(controller.loadProfiles(QString::fromStdString(fixturePath.string())));

    CHECK(controller.managedBackups()->status()->rowCount() == controller.profiles()->rowCount());
}

TEST_CASE("the busy guard between the three runners is mutual", "[dbtool-gui][AppController][managed-backup][busy-guard]")
{
    // AppController wires each runner's busy probe to the other two. Before
    // this, only the managed controller had a probe, so a migration or an
    // ad-hoc restore could start *during* a managed backup and tear the
    // archive that run was about to commit as "ok".
    auto const fixturePath = FixturesDir() / "dbtool.yml";
    QTemporaryDir dir;
    DbtoolGui::AppController controller;
    REQUIRE(WaitForIdle(controller));
    REQUIRE(controller.loadProfiles(QString::fromStdString(fixturePath.string())));

    auto* managed = controller.managedBackups();
    managed->setBackupFolder(dir.path());
    // A stand-in operation keeps the run off any real database; all that
    // matters here is that the managed controller reports Phase::Running.
    managed->setBackupOperation(
        [](std::filesystem::path const& file,
           std::string const& /*connectionString*/,
           std::string const& /*schema*/,
           Lightweight::SqlBackup::ProgressManager& /*progress*/) { std::ofstream(file) << "archive"; });

    QSignalSpy done(managed, &DbtoolGui::ManagedBackupController::finished);
    managed->backupAll();
    // The phase flips synchronously before the worker is dispatched, and the
    // reset back to Idle is posted to this (GUI) thread — which cannot run
    // while this test function does. So the state below is deterministic.
    REQUIRE(managed->phase() == DbtoolGui::ManagedBackupController::Phase::Running);

    QSignalSpy backupLogs(controller.backupRunner(), &DbtoolGui::BackupRunner::logLine);
    controller.backupRunner()->runBackup(dir.path() + QStringLiteral("/adhoc.zip"));
    CHECK(controller.backupRunner()->phase() == DbtoolGui::BackupRunner::Phase::Idle);
    REQUIRE(backupLogs.count() == 1);
    CHECK(backupLogs.first().at(0).toString().contains(QStringLiteral("busy")));

    // The destructive direction: an ad-hoc restore must be refused too.
    controller.backupRunner()->runRestore(dir.path() + QStringLiteral("/adhoc.zip"));
    CHECK(controller.backupRunner()->phase() == DbtoolGui::BackupRunner::Phase::Idle);
    CHECK(backupLogs.count() == 2);

    // ... and so must a migration.
    QSignalSpy migrationLogs(controller.runner(), &DbtoolGui::MigrationRunner::logLine);
    controller.runner()->applyUpTo(QString {});
    CHECK(controller.runner()->phase() == DbtoolGui::MigrationRunner::Phase::Idle);
    REQUIRE(migrationLogs.count() == 1);
    CHECK(migrationLogs.first().at(0).toString().contains(QStringLiteral("busy")));

    REQUIRE((done.count() > 0 || done.wait(30000))); // drain the run before teardown
    managed->setBackupFolder(QString {});
}

namespace
{

/// Reads a whole file as text.
std::string ReadFile(std::filesystem::path const& path)
{
    std::ifstream in(path, std::ios::binary);
    return { std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };
}

} // namespace

TEST_CASE("connectToProfile encrypts a working plaintext password in place", "[dbtool-gui][AppController][password]")
{
    QTemporaryDir dir;
    auto const root = std::filesystem::path { dir.path().toStdString() };
    auto const configPath = root / "dbtool.yml";
    std::ofstream(configPath, std::ios::binary)
        << "# hand-written\nprofiles:\n  p:\n    connectionString: \"Driver={" << SqliteDriver
        << "};Database=" << (root / "gui-password.db").generic_string() << "\"\n    password: hunter2 # old\n";

    DbtoolGui::AppController controller;
    REQUIRE(WaitForIdle(controller));
    QStringList log;
    QObject::connect(&controller, &DbtoolGui::AppController::logLine, [&log](QString const& line, DbtoolGui::LogLevel) {
        log.append(line);
    });
    controller.attachLogSink();
    REQUIRE(controller.loadProfiles(QString::fromStdString(configPath.string())));
    controller.setConnectionMode(QStringLiteral("profile"));
    controller.setCurrentProfile(QStringLiteral("p"));
    REQUIRE(ConnectAndWait(controller));

    INFO("controller log:\n" << log.join(QLatin1Char('\n')).toStdString());
    auto const text = ReadFile(configPath);
    CHECK(text.starts_with("# hand-written\n"));
    CHECK(text.contains("# old"));
    CHECK(text.contains("password: \"enc:"));
    CHECK_FALSE(text.contains("hunter2"));
}

TEST_CASE("startup discovers dbtool.yml above the working directory", "[dbtool-gui][AppController][discovery]")
{
    QSettings().remove(QStringLiteral("config/profileStorePath"));
    QTemporaryDir dir;
    auto const root = std::filesystem::path { dir.path().toStdString() };
    std::filesystem::create_directories(root / "sub");
    std::ofstream(root / "dbtool.yml", std::ios::binary) << "profiles:\n  found:\n    connectionString: \"Driver=x\"\n";

    auto const previous = std::filesystem::current_path();
    std::filesystem::current_path(root / "sub");
    DbtoolGui::AppController controller;
    std::filesystem::current_path(previous);
    REQUIRE(WaitForIdle(controller));

    CHECK(std::filesystem::path { controller.profilePath().toStdString() } == root / "dbtool.yml");
    CHECK(controller.profiles()->rowCount() == 1);
}

TEST_CASE("backup and restore use the connection string with the decrypted password",
          "[dbtool-gui][AppController][password]")
{
    QTemporaryDir dir;
    auto const root = std::filesystem::path { dir.path().toStdString() };
    auto const configPath = root / "dbtool.yml";
    auto const encrypted = Lightweight::Secrets::ProfileCipher::Builtin().Encrypt("s3cr3t");
    REQUIRE(encrypted.has_value());
    std::ofstream(configPath, std::ios::binary)
        << "profiles:\n  p:\n    connectionString: \"Driver={" << SqliteDriver
        << "};Database=" << (root / "gui-backup.db").generic_string() << "\"\n    password: \"" << *encrypted << "\"\n";

    DbtoolGui::AppController controller;
    REQUIRE(WaitForIdle(controller));
    REQUIRE(controller.loadProfiles(QString::fromStdString(configPath.string())));
    controller.setConnectionMode(QStringLiteral("profile"));
    controller.setCurrentProfile(QStringLiteral("p"));
    REQUIRE(ConnectAndWait(controller));

    CHECK(controller.backupRunner()->connectionString().contains(QStringLiteral("PWD=s3cr3t")));
}

TEST_CASE("connectedTarget and connectedMode are empty while nothing is connected", "[dbtool-gui][AppController]")
{
    DbtoolGui::AppController controller;
    REQUIRE(WaitForIdle(controller));

    // Labels that say "connected to …" read these, not the selected profile, so they must
    // not name anything until a connection is actually open.
    CHECK_FALSE(controller.connected());
    CHECK(controller.connectedTarget().isEmpty());
    CHECK(controller.connectedMode().isEmpty());
}

TEST_CASE("addProfile writes an encrypted profile and reloads the list", "[dbtool-gui][AppController][add-profile]")
{
    QTemporaryDir dir;
    auto const root = std::filesystem::path { dir.path().toStdString() };
    auto const configPath = root / "dbtool.yml";
    std::ofstream(configPath, std::ios::binary) << "# team file\nprofiles:\n  existing:\n    dsn: \"OLD\"\n";

    DbtoolGui::AppController controller;
    REQUIRE(WaitForIdle(controller));
    REQUIRE(controller.loadProfiles(QString::fromStdString(configPath.string())));
    REQUIRE(controller.profiles()->rowCount() == 1);

    auto const error = controller.addProfile({ { "name", "  warehouse  " },
                                               { "kind", "dsn" },
                                               { "dsn", "WAREHOUSE" },
                                               { "user", "deploy" },
                                               { "schema", "dbo" },
                                               { "password", "s3cret!" } });

    INFO(error.toStdString());
    CHECK(error.isEmpty());
    CHECK(controller.profiles()->rowCount() == 2);
    auto const text = ReadFile(configPath);
    CHECK(text.starts_with("# team file\n"));
    CHECK_FALSE(text.contains("s3cret!"));
    CHECK(text.contains("enc:"));
    CHECK(text.contains("warehouse"));
    CHECK(controller.profileNameProblem(QStringLiteral("warehouse")).contains(QStringLiteral("already exists")));
}

TEST_CASE("addProfile refuses what cannot become a profile and leaves the file alone",
          "[dbtool-gui][AppController][add-profile]")
{
    QTemporaryDir dir;
    auto const root = std::filesystem::path { dir.path().toStdString() };
    auto const configPath = root / "dbtool.yml";
    std::ofstream(configPath, std::ios::binary) << "profiles:\n  existing:\n    dsn: \"OLD\"\n";
    auto const before = ReadFile(configPath);

    DbtoolGui::AppController controller;
    REQUIRE(WaitForIdle(controller));
    REQUIRE(controller.loadProfiles(QString::fromStdString(configPath.string())));

    CHECK_FALSE(controller.addProfile({ { "name", "" }, { "kind", "dsn" }, { "dsn", "X" } }).isEmpty());
    CHECK_FALSE(controller.addProfile({ { "name", "existing" }, { "kind", "dsn" }, { "dsn", "X" } }).isEmpty());
    CHECK_FALSE(controller.addProfile({ { "name", "n" }, { "kind", "dsn" }, { "dsn", "" } }).isEmpty());
    CHECK_FALSE(
        controller.addProfile({ { "name", "n" }, { "kind", "connectionString" }, { "connectionString", " " } }).isEmpty());

    CHECK(ReadFile(configPath) == before);
    CHECK(controller.profiles()->rowCount() == 1);
}

TEST_CASE("connectionDraft offers a custom connection for saving, without its password",
          "[dbtool-gui][AppController][add-profile]")
{
    QTemporaryDir dir;
    auto const root = std::filesystem::path { dir.path().toStdString() };
    auto const configPath = root / "dbtool.yml";
    std::ofstream(configPath, std::ios::binary) << "profiles:\n  scratch:\n    dsn: \"OLD\"\n";

    DbtoolGui::AppController controller;
    REQUIRE(WaitForIdle(controller));
    REQUIRE(controller.loadProfiles(QString::fromStdString(configPath.string())));

    // Nothing open: nothing to save.
    CHECK_FALSE(controller.connectionDraft().value(QStringLiteral("available")).toBool());

    auto const database = (root / "gui-draft.db").generic_string();
    controller.setConnectionMode(QStringLiteral("custom"));
    controller.setConnectionStringOverride(
        QString::fromStdString("Driver={" + std::string { SqliteDriver } + "};Database=" + database + ";PWD=hunter2"));
    REQUIRE(ConnectAndWait(controller));

    auto const draft = controller.connectionDraft();
    CHECK(draft.value(QStringLiteral("available")).toBool());
    CHECK(draft.value(QStringLiteral("kind")).toString() == QStringLiteral("connectionString"));
    CHECK(draft.value(QStringLiteral("hasPassword")).toBool());
    auto const connectionString = draft.value(QStringLiteral("connectionString")).toString();
    CHECK_FALSE(connectionString.contains(QStringLiteral("hunter2")));
    CHECK_FALSE(connectionString.contains(QStringLiteral("PWD")));
    CHECK(connectionString.contains(QStringLiteral("gui-draft.db")));
    CHECK_FALSE(draft.value(QStringLiteral("suggestedName")).toString().isEmpty());
    CHECK(draft.value(QStringLiteral("targetFile")).toString() == QString::fromStdString(configPath.string()));

    // Saving it takes the password from the open connection, encrypted, and never changes the connection.
    auto const error = controller.addProfile({ { "name", draft.value(QStringLiteral("suggestedName")) },
                                               { "kind", draft.value(QStringLiteral("kind")) },
                                               { "connectionString", connectionString },
                                               { "useOpenConnectionPassword", true } });
    INFO(error.toStdString());
    CHECK(error.isEmpty());
    CHECK(controller.connected());
    CHECK(controller.connectedMode() == QStringLiteral("custom"));
    auto const text = ReadFile(configPath);
    CHECK_FALSE(text.contains("hunter2"));
    CHECK(text.contains("enc:"));
    CHECK(controller.profiles()->rowCount() == 2);
    // The new profile is in the store now, so the suggestion moves on.
    CHECK(controller.connectionDraft().value(QStringLiteral("suggestedName")).toString()
          != draft.value(QStringLiteral("suggestedName")).toString());
}

TEST_CASE("a profile connection is already saved, so there is no draft to offer", "[dbtool-gui][AppController][add-profile]")
{
    QTemporaryDir dir;
    auto const root = std::filesystem::path { dir.path().toStdString() };
    auto const configPath = root / "dbtool.yml";
    std::ofstream(configPath, std::ios::binary) << "profiles:\n  p:\n    connectionString: \"Driver={" << SqliteDriver
                                                << "};Database=" << (root / "gui-saved.db").generic_string() << "\"\n";

    DbtoolGui::AppController controller;
    REQUIRE(WaitForIdle(controller));
    REQUIRE(controller.loadProfiles(QString::fromStdString(configPath.string())));
    controller.setConnectionMode(QStringLiteral("profile"));
    controller.setCurrentProfile(QStringLiteral("p"));
    REQUIRE(ConnectAndWait(controller));

    CHECK(controller.connectedMode() == QStringLiteral("profile"));
    CHECK(controller.connectedTarget() == QStringLiteral("p"));
    CHECK_FALSE(controller.connectionDraft().value(QStringLiteral("available")).toBool());
}

TEST_CASE("connectedTarget names the open connection, not the selected profile", "[dbtool-gui][AppController][add-profile]")
{
    QTemporaryDir dir;
    auto const root = std::filesystem::path { dir.path().toStdString() };
    auto const configPath = root / "dbtool.yml";
    std::ofstream(configPath, std::ios::binary) << "profiles:\n  p:\n    connectionString: \"Driver={" << SqliteDriver
                                                << "};Database=" << (root / "gui-profile.db").generic_string() << "\"\n";

    DbtoolGui::AppController controller;
    REQUIRE(WaitForIdle(controller));
    REQUIRE(controller.loadProfiles(QString::fromStdString(configPath.string())));
    controller.setCurrentProfile(QStringLiteral("p"));
    controller.setConnectionMode(QStringLiteral("custom"));
    controller.setConnectionStringOverride(QString::fromStdString("Driver={" + std::string { SqliteDriver }
                                                                  + "};Database=" + (root / "other.db").generic_string()));
    REQUIRE(ConnectAndWait(controller));

    // The selected profile is still "p", but the open connection is the custom one.
    CHECK(controller.currentProfile() == QStringLiteral("p"));
    CHECK(controller.connectedMode() == QStringLiteral("custom"));
    CHECK(controller.connectedTarget() != QStringLiteral("p"));
    CHECK_FALSE(controller.connectedTarget().isEmpty());
    CHECK(controller.backupRunner()->lastResult().isEmpty());
}

TEST_CASE("loading a dbtool.yml with defaultBackupDir sets the default backup folder",
          "[dbtool-gui][AppController][backup-dir]")
{
    QTemporaryDir dir;
    auto const root = std::filesystem::path { dir.path().toStdString() };
    auto const configPath = root / "dbtool.yml";
    auto const backups = (root / "installer-backups").generic_string();
    std::ofstream(configPath, std::ios::binary)
        << "defaultBackupDir: \"" << backups << "\"\nprofiles:\n  p:\n    dsn: \"X\"\n";

    DbtoolGui::AppController controller;
    REQUIRE(WaitForIdle(controller));
    controller.managedBackups()->setBackupFolder(QString {}); // nothing chosen in Settings
    REQUIRE(controller.loadProfiles(QString::fromStdString(configPath.string())));

    CHECK(controller.managedBackups()->defaultBackupFolder() == QString::fromStdString(backups));
    CHECK(controller.managedBackups()->effectiveBackupFolder() == QString::fromStdString(backups));

    // A file without the key goes back to the platform default.
    std::ofstream(configPath, std::ios::binary | std::ios::trunc) << "profiles:\n  p:\n    dsn: \"X\"\n";
    REQUIRE(controller.loadProfiles(QString::fromStdString(configPath.string())));
    CHECK(controller.managedBackups()->defaultBackupFolder() != QString::fromStdString(backups));
}

TEST_CASE("a non-ASCII defaultBackupDir keeps its spelling as the default backup folder",
          "[dbtool-gui][AppController][backup-dir]")
{
    QTemporaryDir dir;
    auto const configPath = std::filesystem::path { dir.path().toStdU16String() } / "dbtool.yml";
    // UTF-8 "D:/Sicherungen-Müller", as dbtool.yml stores it.
    std::ofstream(configPath, std::ios::binary)
        << "defaultBackupDir: \"D:/Sicherungen-M\xC3\xBCller\"\nprofiles:\n  p:\n    dsn: \"X\"\n";

    DbtoolGui::AppController controller;
    REQUIRE(WaitForIdle(controller));
    controller.managedBackups()->setBackupFolder(QString {}); // nothing chosen in Settings
    REQUIRE(controller.loadProfiles(QString::fromStdU16String(configPath.u16string())));

    CHECK(controller.managedBackups()->defaultBackupFolder() == QString::fromUtf16(u"D:/Sicherungen-M\u00fcller"));
}

TEST_CASE("switching to an empty plugins dir drops the unloaded plugins' compat policy, migrations and releases",
          "[dbtool-gui][AppController][plugins]")
{
    // setPluginsDir persists under the test application's QSettings, never the real app's.
    ScopedSettingRestore const restorePluginsDir { QStringLiteral("config/pluginsDir") };

    QTemporaryDir dir;
    auto const root = std::filesystem::path { dir.path().toStdString() };
    auto const fixture = std::filesystem::path { DBTOOL_GUI_TESTS_COMPAT_POLICY_FIXTURE };
    std::filesystem::create_directories(root / "plugins");
    std::filesystem::create_directories(root / "empty");
    std::filesystem::copy_file(fixture, root / "plugins" / ("CompatPolicyPlugin" + fixture.extension().string()));
    // A profile file without plugin directories, so only the Settings-page plugins dir applies.
    std::ofstream(root / "dbtool.yml", std::ios::binary) << "profiles:\n  p:\n    dsn: \"X\"\n";

    auto const& manager = Lightweight::SqlMigration::MigrationManager::GetInstance();
    {
        DbtoolGui::AppController controller;
        REQUIRE(WaitForIdle(controller));
        REQUIRE(controller.loadProfiles(QString::fromStdString((root / "dbtool.yml").string())));

        controller.setPluginsDir(QString::fromStdString((root / "plugins").string()));
        REQUIRE(WaitForIdle(controller));
        REQUIRE(static_cast<bool>(manager.GetCompatPolicy()));
        REQUIRE(manager.GetAllMigrations().size() == 3);
        REQUIRE(manager.GetAllReleases().size() == 1);

        controller.setPluginsDir(QString::fromStdString((root / "empty").string()));
        REQUIRE(WaitForIdle(controller));
        CHECK_FALSE(static_cast<bool>(manager.GetCompatPolicy()));
        CHECK(manager.GetAllMigrations().empty());
        CHECK(manager.GetAllReleases().empty());
    }
}

TEST_CASE("a plugin reload requested during a migration run waits until the run finishes",
          "[dbtool-gui][AppController][plugins][busy-guard]")
{
    ScopedSettingRestore const restorePluginsDir { QStringLiteral("config/pluginsDir") };

    QTemporaryDir dir;
    auto const root = std::filesystem::path { dir.path().toStdString() };
    auto const fixture = std::filesystem::path { DBTOOL_GUI_TESTS_COMPAT_POLICY_FIXTURE };
    std::filesystem::create_directories(root / "plugins");
    std::filesystem::create_directories(root / "empty");
    std::filesystem::copy_file(fixture, root / "plugins" / ("CompatPolicyPlugin" + fixture.extension().string()));
    std::ofstream(root / "dbtool.yml", std::ios::binary)
        << "profiles:\n  p:\n    connectionString: \"Driver={" << SqliteDriver
        << "};Database=" << (root / "gui-reload.db").generic_string() << "\"\n";

    auto const& manager = Lightweight::SqlMigration::MigrationManager::GetInstance();
    {
        DbtoolGui::AppController controller;
        REQUIRE(WaitForIdle(controller));
        REQUIRE(controller.loadProfiles(QString::fromStdString((root / "dbtool.yml").string())));
        controller.setConnectionMode(QStringLiteral("profile"));
        controller.setCurrentProfile(QStringLiteral("p"));
        controller.setPluginsDir(QString::fromStdString((root / "plugins").string()));
        REQUIRE(ConnectAndWait(controller));
        REQUIRE(manager.GetAllMigrations().size() == 3);

        QSignalSpy finished(controller.runner(), &DbtoolGui::MigrationRunner::finished);
        controller.runner()->dryRunUpTo(QString {});
        // The reset back to Idle is posted to this thread, so the run stays Running until the spy waits.
        REQUIRE(controller.runner()->phase() == DbtoolGui::MigrationRunner::Phase::Running);

        controller.setPluginsDir(QString::fromStdString((root / "empty").string()));
        CHECK(manager.GetAllMigrations().size() == 3);
        CHECK(controller.lastWarning().contains(QStringLiteral("after the current run")));

        REQUIRE((finished.count() > 0 || finished.wait(30000)));
        REQUIRE(WaitForIdle(controller));
        CHECK(manager.GetAllMigrations().empty());
        CHECK(controller.lastWarning().isEmpty());
    }
}

namespace
{

/// Sets a process environment variable for its lifetime, then removes it.
class ScopedTestVariable
{
  public:
    ScopedTestVariable(char const* name, std::filesystem::path const& value):
        _name { name }
    {
        qputenv(_name, QByteArray::fromStdString(value.string()));
    }

    ScopedTestVariable(ScopedTestVariable const&) = delete;
    ScopedTestVariable(ScopedTestVariable&&) = delete;
    ScopedTestVariable& operator=(ScopedTestVariable const&) = delete;
    ScopedTestVariable& operator=(ScopedTestVariable&&) = delete;

    ~ScopedTestVariable()
    {
        qunsetenv(_name);
    }

  private:
    char const* _name;
};

/// Writes a `dbtool.yml` under @p root whose profile `p` connects to `<root>/<database>` with the
/// post-migrate recorder plugin, copied to `<root>/plugins`.
void WriteRecorderWorkspace(std::filesystem::path const& root, std::string const& database)
{
    auto const plugin = std::filesystem::path { DBTOOL_GUI_TESTS_PRE_MIGRATE_RECORDER_PLUGIN };
    std::filesystem::create_directories(root / "plugins");
    std::filesystem::copy_file(plugin, root / "plugins" / plugin.filename());
    std::ofstream(root / "dbtool.yml", std::ios::binary)
        << "profiles:\n  p:\n    connectionString: \"Driver={" << SqliteDriver
        << "};Database=" << (root / database).generic_string() << "\"\n    pluginsDir: \""
        << (root / "plugins").generic_string() << "\"\n";
}

/// The number of lines in @p file; 0 when it does not exist.
[[nodiscard]] int LineCount(std::filesystem::path const& file)
{
    auto stream = std::ifstream { file };
    auto lines = 0;
    for (auto line = std::string {}; std::getline(stream, line);)
        ++lines;
    return lines;
}

} // namespace

TEST_CASE("a plugin reload while connected keeps the pre-migrate steps the plugins register on connect",
          "[dbtool-gui][AppController][plugins][pre-migrate]")
{
    QTemporaryDir dir;
    auto const root = std::filesystem::path { dir.path().toStdString() };
    WriteRecorderWorkspace(root, "gui-steps.db");
    // The fixture's post-init hook registers its "Recorder step" only while this names a log file.
    auto const stepLog = ScopedTestVariable { "LIGHTWEIGHT_TEST_PRE_MIGRATE_LOG", root / "steps.log" };

    DbtoolGui::AppController controller;
    REQUIRE(WaitForIdle(controller));
    REQUIRE(controller.loadProfiles(QString::fromStdString((root / "dbtool.yml").string())));
    controller.setConnectionMode(QStringLiteral("profile"));
    controller.setCurrentProfile(QStringLiteral("p"));
    REQUIRE(ConnectAndWait(controller));
    REQUIRE(controller.preMigrateStepCount() == 1);

    // A profile file naming another plugins directory reloads the plugins, which unloads the steps
    // registered on connect; an unchanged directory would not reload at all.
    auto const plugin = std::filesystem::path { DBTOOL_GUI_TESTS_PRE_MIGRATE_RECORDER_PLUGIN };
    std::filesystem::create_directories(root / "plugins2");
    std::filesystem::copy_file(plugin, root / "plugins2" / plugin.filename());
    std::ofstream(root / "dbtool.yml", std::ios::binary | std::ios::trunc)
        << "profiles:\n  p:\n    connectionString: \"Driver={" << SqliteDriver
        << "};Database=" << (root / "gui-steps.db").generic_string() << "\"\n    pluginsDir: \""
        << (root / "plugins2").generic_string() << "\"\n";
    QSignalSpy migrationsChanged(&controller, &DbtoolGui::AppController::migrationsChanged);
    REQUIRE(controller.loadProfiles(QString::fromStdString((root / "dbtool.yml").string())));
    CHECK(controller.pluginsLoading());
    REQUIRE(WaitForIdle(controller));
    CHECK(controller.connected());
    CHECK(controller.preMigrateStepCount() == 1);
    CHECK(migrationsChanged.count() > 0);
}

TEST_CASE("a reconnect runs the plugins' post-init hooks once and drops the last run's rows",
          "[dbtool-gui][AppController][plugins][pre-migrate]")
{
    QTemporaryDir dir;
    auto const root = std::filesystem::path { dir.path().toStdString() };
    WriteRecorderWorkspace(root, "gui-reconnect.db");
    auto const stepLog = ScopedTestVariable { "LIGHTWEIGHT_TEST_PRE_MIGRATE_LOG", root / "steps.log" };
    auto const postInitLog = ScopedTestVariable { "LIGHTWEIGHT_TEST_POST_INIT_LOG", root / "post-init.log" };

    DbtoolGui::AppController controller;
    REQUIRE(WaitForIdle(controller));
    REQUIRE(controller.loadProfiles(QString::fromStdString((root / "dbtool.yml").string())));
    controller.setConnectionMode(QStringLiteral("profile"));
    controller.setCurrentProfile(QStringLiteral("p"));
    REQUIRE(ConnectAndWait(controller));
    REQUIRE(LineCount(root / "post-init.log") == 1);

    QSignalSpy finished(controller.runner(), &DbtoolGui::MigrationRunner::finished);
    controller.runner()->dryRunUpTo(QString {});
    REQUIRE((finished.count() > 0 || finished.wait(30000)));
    QCoreApplication::processEvents();
    REQUIRE(controller.runner()->tasks()->rowCount() > 0);

    // The reload inside connect leaves the hooks to connect, which runs them once, on the new connection.
    REQUIRE(ConnectAndWait(controller));
    CHECK(LineCount(root / "post-init.log") == 2);
    CHECK(controller.preMigrateStepCount() == 1);
    CHECK(controller.runner()->tasks()->rowCount() == 0);
}

TEST_CASE("connectToProfile is refused while a migration run is active", "[dbtool-gui][AppController][busy-guard]")
{
    QTemporaryDir dir;
    auto const root = std::filesystem::path { dir.path().toStdString() };
    std::ofstream(root / "dbtool.yml", std::ios::binary)
        << "profiles:\n  p:\n    connectionString: \"Driver={" << SqliteDriver
        << "};Database=" << (root / "gui-connect.db").generic_string() << "\"\n";

    DbtoolGui::AppController controller;
    REQUIRE(WaitForIdle(controller));
    REQUIRE(controller.loadProfiles(QString::fromStdString((root / "dbtool.yml").string())));
    controller.setConnectionMode(QStringLiteral("profile"));
    controller.setCurrentProfile(QStringLiteral("p"));
    REQUIRE(ConnectAndWait(controller));

    QSignalSpy finished(controller.runner(), &DbtoolGui::MigrationRunner::finished);
    controller.runner()->dryRunUpTo(QString {});
    REQUIRE(controller.runner()->phase() == DbtoolGui::MigrationRunner::Phase::Running);

    // Reconnecting would replace the connection the running dry run reads through.
    CHECK_FALSE(controller.connectToProfile());
    CHECK(controller.lastWarning().contains(QStringLiteral("before connecting")));
    CHECK(controller.connected());

    REQUIRE((finished.count() > 0 || finished.wait(30000)));
    CHECK(ConnectAndWait(controller));
}

TEST_CASE("the constructor returns before the plugins load", "[dbtool-gui][AppController][startup]")
{
    ScopedSettingRestore const restorePluginsDir { QStringLiteral("config/pluginsDir") };
    QTemporaryDir dir;
    auto const root = std::filesystem::path { dir.path().toStdString() };
    auto const plugin = std::filesystem::path { DBTOOL_GUI_TESTS_PRE_MIGRATE_RECORDER_PLUGIN };
    std::filesystem::create_directories(root / "plugins");
    std::filesystem::copy_file(plugin, root / "plugins" / plugin.filename());
    QSettings().setValue(QStringLiteral("config/pluginsDir"), QString::fromStdString((root / "plugins").string()));
    auto const loadDelay = ScopedTestVariable { "LIGHTWEIGHT_TEST_PLUGIN_LOAD_DELAY_MS", "2000" };
    auto const loadLog = ScopedTestVariable { "LIGHTWEIGHT_TEST_PLUGIN_LOAD_LOG", root / "load.log" };

    auto timer = QElapsedTimer {};
    timer.start();
    DbtoolGui::AppController controller;
    CHECK(timer.elapsed() < 500);
    CHECK(LineCount(root / "load.log") == 0);
    CHECK(controller.pluginsLoading());
    CHECK(controller.busy());
    CHECK(controller.activityText() == QStringLiteral("Loading plugins…"));

    REQUIRE(WaitForIdle(controller));
    CHECK_FALSE(controller.pluginsLoading());
    CHECK(controller.activityText().isEmpty());
    CHECK(LineCount(root / "load.log") == 1);
}

TEST_CASE("plugin reload requests in one event-loop turn coalesce into one load", "[dbtool-gui][AppController][startup]")
{
    ScopedSettingRestore const restorePluginsDir { QStringLiteral("config/pluginsDir") };
    ScopedSettingRestore const restoreMode { QStringLiteral("connection/mode") };
    ScopedSettingRestore const restoreProfile { QStringLiteral("connection/profile") };
    QTemporaryDir dir;
    auto const root = std::filesystem::path { dir.path().toStdString() };
    auto const plugin = std::filesystem::path { DBTOOL_GUI_TESTS_PRE_MIGRATE_RECORDER_PLUGIN };
    for (auto const* const name: { "plugins", "plugins2" })
    {
        std::filesystem::create_directories(root / name);
        std::filesystem::copy_file(plugin, root / name / plugin.filename());
    }
    std::ofstream(root / "dbtool.yml", std::ios::binary) << "profiles:\n  p:\n    dsn: \"X\"\n  q:\n    dsn: \"Y\"\n";
    QSettings().setValue(QStringLiteral("config/pluginsDir"), QString::fromStdString((root / "plugins").string()));
    auto const loadLog = ScopedTestVariable { "LIGHTWEIGHT_TEST_PLUGIN_LOAD_LOG", root / "load.log" };

    DbtoolGui::AppController controller;
    // A store loaded before the startup ran is kept, and its reload joins the startup's.
    REQUIRE(controller.loadProfiles(QString::fromStdString((root / "dbtool.yml").string())));
    REQUIRE(WaitForIdle(controller));
    REQUIRE(LineCount(root / "load.log") == 1);

    controller.setPluginsDir(QString::fromStdString((root / "plugins2").string()));
    controller.setConnectionMode(QStringLiteral("dsn"));
    controller.setCurrentProfile(controller.currentProfile() == QStringLiteral("p") ? QStringLiteral("q")
                                                                                    : QStringLiteral("p"));
    CHECK(controller.pluginsLoading());
    REQUIRE(WaitForIdle(controller));
    CHECK(LineCount(root / "load.log") == 2);

    // Reloading the profile file with the same effective plugins directory loads nothing.
    REQUIRE(controller.loadProfiles(QString::fromStdString((root / "dbtool.yml").string())));
    CHECK_FALSE(controller.pluginsLoading());
    REQUIRE(WaitForIdle(controller));
    CHECK(LineCount(root / "load.log") == 2);
}

namespace
{

/// A plugins directory with the recorder fixture as the Settings-page plugins directory, whose load
/// logs its begin and end to `begin.log` and `load.log` and takes @p delayMs in between.
class SlowPluginWorkspace
{
  public:
    explicit SlowPluginWorkspace(char const* delayMs):
        _root { _directory.path().toStdString() },
        _loadDelay { "LIGHTWEIGHT_TEST_PLUGIN_LOAD_DELAY_MS", delayMs },
        _beginLog { "LIGHTWEIGHT_TEST_PLUGIN_LOAD_BEGIN_LOG", _root / "begin.log" },
        _loadLog { "LIGHTWEIGHT_TEST_PLUGIN_LOAD_LOG", _root / "load.log" }
    {
        auto const plugin = std::filesystem::path { DBTOOL_GUI_TESTS_PRE_MIGRATE_RECORDER_PLUGIN };
        std::filesystem::create_directories(_root / "plugins");
        std::filesystem::copy_file(plugin, _root / "plugins" / plugin.filename());
        QSettings().setValue(QStringLiteral("config/pluginsDir"), QString::fromStdString((_root / "plugins").string()));
    }

    /// @return Whether the plugin load has begun.
    [[nodiscard]] bool LoadBegun() const
    {
        return LineCount(_root / "begin.log") > 0;
    }

    /// @return The plugin loads that have finished.
    [[nodiscard]] int LoadsFinished() const
    {
        return LineCount(_root / "load.log");
    }

  private:
    ScopedSettingRestore _restorePluginsDir { QStringLiteral("config/pluginsDir") };
    QTemporaryDir _directory;
    std::filesystem::path _root;
    ScopedTestVariable _loadDelay;
    ScopedTestVariable _beginLog;
    ScopedTestVariable _loadLog;
};

} // namespace

TEST_CASE("quitting while the plugins load waits for the load and leaves cleanly", "[dbtool-gui][AppController][startup]")
{
    auto const workspace = SlowPluginWorkspace { "2000" };
    auto controller = std::make_unique<DbtoolGui::AppController>();

    // The real quit path: the application's event loop ends while the worker is inside the load.
    auto poll = QTimer {};
    QObject::connect(&poll, &QTimer::timeout, [&workspace] {
        if (workspace.LoadBegun())
            QCoreApplication::quit();
    });
    poll.start(10);
    auto guard = QTimer {};
    guard.setSingleShot(true);
    QObject::connect(&guard, &QTimer::timeout, [] { QCoreApplication::exit(1); });
    guard.start(std::chrono::seconds { 30 });
    REQUIRE(QCoreApplication::exec() == 0);
    poll.stop();

    // aboutToQuit drained the worker: the load has finished, not merely begun.
    CHECK(workspace.LoadsFinished() == 1);
    controller.reset();
    CHECK(workspace.LoadsFinished() == 1);
}

TEST_CASE("a controller told to await the first frame loads no plugin before it", "[dbtool-gui][AppController][startup]")
{
    auto const workspace = SlowPluginWorkspace { "0" };
    auto window = QQuickWindow {};
    DbtoolGui::AppController controller;
    controller.awaitFirstFrame(&window);

    // Deliver the queued startup and anything it queues in turn, then let the worker run what it got.
    for (auto turn = 0; turn < 3; ++turn)
        QCoreApplication::processEvents();
    REQUIRE(controller.runner()->Worker().WaitForDone(std::chrono::seconds { 30 }));
    CHECK_FALSE(workspace.LoadBegun());
    CHECK(controller.pluginsLoading());

    window.show();
    REQUIRE(WaitForIdle(controller));
    CHECK(workspace.LoadsFinished() == 1);
}

TEST_CASE("destroying the controller while a connect is in flight waits for the worker",
          "[dbtool-gui][AppController][lifetime]")
{
    QTemporaryDir dir;
    auto const root = std::filesystem::path { dir.path().toStdString() };
    WriteRecorderWorkspace(root, "gui-quit.db");
    auto const postInitDelay = ScopedTestVariable { "LIGHTWEIGHT_TEST_POST_INIT_DELAY_MS", "1000" };
    auto const postInitLog = ScopedTestVariable { "LIGHTWEIGHT_TEST_POST_INIT_LOG", root / "post-init.log" };

    auto controller = std::make_unique<DbtoolGui::AppController>();
    REQUIRE(WaitForIdle(*controller));
    REQUIRE(controller->loadProfiles(QString::fromStdString((root / "dbtool.yml").string())));
    controller->setConnectionMode(QStringLiteral("profile"));
    controller->setCurrentProfile(QStringLiteral("p"));
    REQUIRE(WaitForIdle(*controller));
    REQUIRE(controller->connectToProfile());
    REQUIRE(controller->connecting());

    // The real quit path: the application's event loop ends while the connect job is in flight.
    QTimer::singleShot(0, [] { QCoreApplication::quit(); });
    auto guard = QTimer {};
    guard.setSingleShot(true);
    QObject::connect(&guard, &QTimer::timeout, [] { QCoreApplication::exit(1); });
    guard.start(std::chrono::seconds { 30 });
    REQUIRE(QCoreApplication::exec() == 0);

    // aboutToQuit drained the worker: the post-init hook has run, not merely begun.
    CHECK(LineCount(root / "post-init.log") == 1);
    controller.reset();
    CHECK(LineCount(root / "post-init.log") == 1);
}

TEST_CASE("quitting stops waiting for a job after the drain limit", "[dbtool-gui][AppController][lifetime]")
{
    auto const workspace = SlowPluginWorkspace { "3000" };
    DbtoolGui::AppController controller;
    REQUIRE(QTest::qWaitFor([&workspace] { return workspace.LoadBegun(); }, 15000));

    auto exceeded = false;
    controller.SetShutdownDrainLimit(std::chrono::milliseconds { 200 }, [&exceeded] { exceeded = true; });
    auto timer = QElapsedTimer {};
    timer.start();
    controller.Shutdown();
    CHECK(exceeded);
    CHECK(timer.elapsed() < 2000);
    CHECK(workspace.LoadsFinished() == 0);

    // The members the load uses must outlive it.
    REQUIRE(controller.runner()->Worker().WaitForDone(std::chrono::milliseconds::max()));
}

TEST_CASE("quitting while a managed backup runs waits for the backup", "[dbtool-gui][AppController][lifetime]")
{
    QTemporaryDir dir;
    // Connected around the controller's own aboutToQuit handler: one runs before its drain, one after.
    auto const quitting = std::make_shared<std::atomic<bool>>(false);
    auto const drainReturned = std::make_shared<std::atomic<bool>>(false);
    auto before = QObject {};
    QObject::connect(qApp, &QCoreApplication::aboutToQuit, &before, [quitting] { *quitting = true; });
    auto controller = std::make_unique<DbtoolGui::AppController>();
    auto after = QObject {};
    QObject::connect(qApp, &QCoreApplication::aboutToQuit, &after, [drainReturned] { *drainReturned = true; });

    REQUIRE(WaitForIdle(*controller));
    REQUIRE(controller->loadProfiles(QString::fromStdString((FixturesDir() / "dbtool.yml").string())));
    auto* managed = controller->managedBackups();
    managed->setBackupFolder(dir.path());
    auto const archivesWritten = std::make_shared<std::atomic<int>>(0);
    auto const drainReturnedWhileRunning = std::make_shared<std::atomic<bool>>(false);
    // The backup runs on through the quit; a drain that waits for it cannot return before it ends,
    // so it gives up looking for that return after a second.
    managed->setBackupOperation([=](std::filesystem::path const& file,
                                    std::string const& /*connectionString*/,
                                    std::string const& /*schema*/,
                                    Lightweight::SqlBackup::ProgressManager& /*progress*/) {
        auto const quitDeadline = std::chrono::steady_clock::now() + std::chrono::seconds { 30 };
        while (!*quitting && std::chrono::steady_clock::now() < quitDeadline)
            std::this_thread::yield();
        auto const drainDeadline = std::chrono::steady_clock::now() + std::chrono::seconds { 1 };
        while (!*drainReturned && std::chrono::steady_clock::now() < drainDeadline)
            std::this_thread::yield();
        *drainReturnedWhileRunning = drainReturned->load();
        std::ofstream(file) << "archive";
        ++*archivesWritten;
    });
    managed->backupProfile(QStringLiteral("sqlite-temp"));
    REQUIRE(managed->phase() == DbtoolGui::ManagedBackupController::Phase::Running);
    // The backup outlasts this limit; a run in flight is waited for to its end regardless.
    auto exceeded = false;
    controller->SetShutdownDrainLimit(std::chrono::milliseconds { 200 }, [&exceeded] { exceeded = true; });

    QTimer::singleShot(0, [] { QCoreApplication::quit(); });
    auto guard = QTimer {};
    guard.setSingleShot(true);
    QObject::connect(&guard, &QTimer::timeout, [] { QCoreApplication::exit(1); });
    guard.start(std::chrono::seconds { 30 });
    REQUIRE(QCoreApplication::exec() == 0);

    CHECK(*archivesWritten == 1);
    CHECK_FALSE(*drainReturnedWhileRunning);
    CHECK_FALSE(exceeded);
    managed->setBackupFolder(QString {});
    controller.reset();
}

TEST_CASE("editing the connection string while connecting with it ends that connect", "[dbtool-gui][AppController]")
{
    ScopedSettingRestore const restoreMode { QStringLiteral("connection/mode") };
    ScopedSettingRestore const restoreString { QStringLiteral("connection/overrideString") };
    ScopedSettingRestore const restorePluginsDir { QStringLiteral("config/pluginsDir") };
    QTemporaryDir dir;
    auto const root = std::filesystem::path { dir.path().toStdString() };
    WriteRecorderWorkspace(root, "gui-custom.db");
    QSettings().setValue(QStringLiteral("config/pluginsDir"), QString::fromStdString((root / "plugins").string()));
    auto const postInitDelay = ScopedTestVariable { "LIGHTWEIGHT_TEST_POST_INIT_DELAY_MS", "1000" };

    DbtoolGui::AppController controller;
    REQUIRE(WaitForIdle(controller));
    controller.setConnectionMode(QStringLiteral("custom"));
    auto const connectionString =
        std::string { "Driver={" } + SqliteDriver + "};Database=" + (root / "gui-custom.db").generic_string();
    controller.setConnectionStringOverride(QString::fromStdString(connectionString));
    REQUIRE(WaitForIdle(controller));
    REQUIRE(controller.connectToProfile());
    REQUIRE(controller.connecting());

    // The connect targets the old string; the edit ends it, and its result never shows.
    controller.setConnectionStringOverride(QString::fromStdString(connectionString + ";Timeout=5"));
    CHECK_FALSE(controller.connecting());
    REQUIRE(WaitForIdle(controller));
    REQUIRE(controller.runner()->Worker().WaitForDone(std::chrono::seconds { 30 }));
    QCoreApplication::processEvents();
    CHECK_FALSE(controller.connected());
}

TEST_CASE("typing a DSN or connection string during a plugin load keeps the load's result", "[dbtool-gui][AppController]")
{
    ScopedSettingRestore const restoreMode { QStringLiteral("connection/mode") };
    ScopedSettingRestore const restoreDsn { QStringLiteral("connection/dsn") };
    ScopedSettingRestore const restoreString { QStringLiteral("connection/overrideString") };
    QSettings().setValue(QStringLiteral("connection/mode"), QStringLiteral("dsn"));
    auto const workspace = SlowPluginWorkspace { "1000" };
    auto const fixture = std::filesystem::path { DBTOOL_GUI_TESTS_COMPAT_POLICY_FIXTURE };
    auto const pluginsDir =
        std::filesystem::path { QSettings().value(QStringLiteral("config/pluginsDir")).toString().toStdString() };
    std::filesystem::copy_file(fixture, pluginsDir / ("CompatPolicyPlugin" + fixture.extension().string()));

    DbtoolGui::AppController controller;
    QStringList log;
    QObject::connect(&controller, &DbtoolGui::AppController::logLine, [&log](QString const& line, DbtoolGui::LogLevel) {
        log.append(line);
    });
    controller.attachLogSink();
    REQUIRE(QTest::qWaitFor([&workspace] { return workspace.LoadBegun(); }, 15000));

    // The startup load is in flight; the fields of the active mode and of the other one are edited.
    controller.setSelectedDsn(QStringLiteral("SomeDsn"));
    controller.setDsnUser(QStringLiteral("someone"));
    controller.setConnectionStringOverride(QStringLiteral("Driver=X;Database=y"));
    REQUIRE(WaitForIdle(controller));

    INFO("controller log:\n" << log.join(QLatin1Char('\n')).toStdString());
    CHECK(workspace.LoadsFinished() == 1);
    CHECK(controller.migrationCount() == 3);
    CHECK(log.filter(QStringLiteral("Ready — not connected")).size() == 1);
}
