// SPDX-License-Identifier: Apache-2.0
//
// Headless tests for ManagedBackupController. SQLite-only: every profile
// points at a database file inside a QTemporaryDir, so the suite runs on
// any machine with the SQLite3 ODBC driver — same baseline as the rest of
// the dbtool-gui tests.

#include "../BackupRunner.hpp"
#include "../CloseGuard.hpp"
#include "../ManagedBackupController.hpp"
#include "../MigrationRunner.hpp"
#include "../Models/BackupStatusListModel.hpp"
#include "../Models/BackupTableListModel.hpp"
#include "../SettingsLocation.hpp"
#include "SqliteDriver.hpp"

#include <Lightweight/SqlBackup.hpp>
#include <Lightweight/SqlConnection.hpp>
#include <Lightweight/SqlStatement.hpp>
#include <Lightweight/Zip/ZipArchive.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <expected>
#include <filesystem>
#include <fstream>
#include <future>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <BackupRecording.hpp>
#include <Config/ProfileStore.hpp>
#include <QtCore/QEventLoop>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QTemporaryDir>
#include <QtCore/QTimer>
#include <QtCore/QtEnvironmentVariables>
#include <QtTest/QSignalSpy>
#include <Secrets/ProfileCipher.hpp>

using DbtoolGui::ManagedBackupController;
using DbtoolGui::Tests::SqliteConnectionString;

namespace
{

/// Builds a profile with a raw SQLite connection string into `dir`.
[[nodiscard]] Lightweight::Config::Profile SqliteProfile(QTemporaryDir const& dir, std::string const& name)
{
    Lightweight::Config::Profile profile;
    profile.name = name;
    profile.connectionString =
        SqliteConnectionString((dir.path() + "/" + QString::fromStdString(name)).toStdString() + ".db");
    return profile;
}

/// Sets an environment variable for its lifetime and restores the previous value, also when a test fails.
class ScopedEnvironmentVariable
{
  public:
    ScopedEnvironmentVariable(char const* name, QByteArray const& value):
        _name { name },
        _previous { qgetenv(name) },
        _wasSet { qEnvironmentVariableIsSet(name) }
    {
        qputenv(name, value);
    }
    ScopedEnvironmentVariable(ScopedEnvironmentVariable const&) = delete;
    ScopedEnvironmentVariable(ScopedEnvironmentVariable&&) = delete;
    ScopedEnvironmentVariable& operator=(ScopedEnvironmentVariable const&) = delete;
    ScopedEnvironmentVariable& operator=(ScopedEnvironmentVariable&&) = delete;
    ~ScopedEnvironmentVariable()
    {
        if (_wasSet)
            qputenv(_name, _previous);
        else
            qunsetenv(_name);
    }

  private:
    char const* _name;
    QByteArray _previous;
    bool _wasSet;
};

/// One call the injected recorder saw.
struct RecorderCall
{
    std::string what; ///< "capture", "backup" (a record) or "restore" (a record)
    std::string connectionString;
    std::string schema;
    std::filesystem::path archive;
};

/// A recorder that notes its calls in @p calls, in order; a record fails with @p recordFailure when it is set.
/// The calls come from the worker, and the test reads them only after `finished`.
[[nodiscard]] DbtoolGui::BackupRunner::Recorder NotingRecorder(
    std::vector<RecorderCall>& calls, std::optional<Lightweight::Tools::BackupRecordError> recordFailure = std::nullopt)
{
    using DbtoolGui::BackupRunner;
    return {
        .captureStart =
            [&calls](std::string const& connectionString, std::string const& schema) {
                calls.push_back({ "capture", connectionString, schema, {} });
                return Lightweight::Tools::BackupStart {};
            },
        .verifyArchive =
            [](std::filesystem::path const&) { return std::optional<Lightweight::Tools::BackupRecordError> {}; },
        .record = [&calls, recordFailure](BackupRunner::Operation operation,
                                          std::filesystem::path const& archive,
                                          std::string const& connectionString,
                                          std::string const& schema,
                                          Lightweight::Tools::BackupStart const& /*start*/)
            -> std::expected<Lightweight::SqlMigration::BackupRecord, Lightweight::Tools::BackupRecordError> {
            calls.push_back(
                { operation == BackupRunner::Operation::Backup ? "backup" : "restore", connectionString, schema, archive });
            if (recordFailure)
                return std::unexpected(*recordFailure);
            return Lightweight::SqlMigration::BackupRecord {};
        },
    };
}

/// A recorder that records nothing, for tests whose archives are not real.
[[nodiscard]] DbtoolGui::BackupRunner::Recorder IgnoringRecorder()
{
    using DbtoolGui::BackupRunner;
    return {
        .captureStart = [](std::string const&, std::string const&) { return Lightweight::Tools::BackupStart {}; },
        .verifyArchive =
            [](std::filesystem::path const&) { return std::optional<Lightweight::Tools::BackupRecordError> {}; },
        .record = [](BackupRunner::Operation,
                     std::filesystem::path const&,
                     std::string const&,
                     std::string const&,
                     Lightweight::Tools::BackupStart const&)
            -> std::expected<Lightweight::SqlMigration::BackupRecord, Lightweight::Tools::BackupRecordError> {
            return Lightweight::SqlMigration::BackupRecord {};
        },
    };
}

/// A recorder whose verification of every archive fails.
[[nodiscard]] DbtoolGui::BackupRunner::Recorder FailingVerificationRecorder()
{
    auto recorder = IgnoringRecorder();
    recorder.verifyArchive = [](std::filesystem::path const&) {
        return std::optional { Lightweight::Tools::BackupRecordError {
            .kind = Lightweight::Tools::BackupRecordError::Kind::VerificationFailed, .message = "not a zip" } };
    };
    return recorder;
}

/// Spins the event loop until `spy` has at least one emission or `ms` ran out.
[[nodiscard]] bool WaitFor(QSignalSpy& spy, int ms = 30000)
{
    return spy.count() > 0 || spy.wait(ms);
}

} // namespace

TEST_CASE("ManagedBackupController defaults: idle, default folder, empty model", "[dbtool-gui][managed-backup-controller]")
{
    ManagedBackupController controller;
    controller.setBackupFolder(QString {}); // do not inherit a persisted value from a previous run

    CHECK(controller.phase() == ManagedBackupController::Phase::Idle);
    CHECK(controller.backupFolder().isEmpty());
    CHECK(controller.effectiveBackupFolder() == controller.defaultBackupFolder());
    CHECK_FALSE(controller.defaultBackupFolder().isEmpty());
    CHECK(controller.status()->rowCount() == 0);
}

TEST_CASE("the default backup folder lies in LIGHTWEIGHT_DBTOOL_GUI_SETTINGS_DIR when it is set",
          "[dbtool-gui][managed-backup-controller]")
{
    QTemporaryDir dir;
    auto const defaultFolder = [&] {
        auto const settingsDirectory =
            ScopedEnvironmentVariable { DbtoolGui::SettingsDirectoryVariable, QFile::encodeName(dir.path()) };
        ManagedBackupController controller;
        controller.setBackupFolder(QString {});
        return controller.defaultBackupFolder();
    }();

    CHECK(defaultFolder == dir.path() + QStringLiteral("/backups"));
}

TEST_CASE("setBackupFolder persists, notifies, and feeds effectiveBackupFolder", "[dbtool-gui][managed-backup-controller]")
{
    QTemporaryDir dir;
    ManagedBackupController controller;
    QSignalSpy spy(&controller, &ManagedBackupController::backupFolderChanged);

    controller.setBackupFolder(dir.path());

    CHECK(spy.count() == 1);
    CHECK(controller.backupFolder() == dir.path());
    CHECK(controller.effectiveBackupFolder() == dir.path());

    // A second controller picks the value up from QSettings.
    ManagedBackupController second;
    CHECK(second.backupFolder() == dir.path());

    controller.setBackupFolder(QString {}); // reset for other tests
}

TEST_CASE("setProfiles populates the status model and requestStatusRefresh scans archives",
          "[dbtool-gui][managed-backup-controller]")
{
    QTemporaryDir dir;
    ManagedBackupController controller;
    QSignalSpy refreshed(&controller, &ManagedBackupController::statusRefreshed);
    controller.setBackupFolder(dir.path());
    controller.setProfiles({ SqliteProfile(dir, "alpha"), SqliteProfile(dir, "beta") });

    REQUIRE(controller.status()->rowCount() == 2);
    // The scan runs in the background; the two requests above deliver one result, the later one.
    REQUIRE(WaitFor(refreshed));
    CHECK(refreshed.count() == 1);
    CHECK(controller.folderProblem().isEmpty());

    // Drop a fake archive for alpha and re-scan.
    QFile file(dir.path() + "/alpha.zip");
    REQUIRE(file.open(QIODevice::WriteOnly));
    file.write("xxxx");
    file.close();
    refreshed.clear();
    controller.requestStatusRefresh();
    REQUIRE(WaitFor(refreshed));

    auto* model = controller.status();
    CHECK(model->data(model->index(0, 0), DbtoolGui::BackupStatusListModel::ArchiveExistsRole).toBool());
    CHECK_FALSE(model->data(model->index(1, 0), DbtoolGui::BackupStatusListModel::ArchiveExistsRole).toBool());

    controller.setBackupFolder(QString {});
}

TEST_CASE("folderProblem is set when the folder path is unusable", "[dbtool-gui][managed-backup-controller]")
{
    QTemporaryDir dir;
    // A *file* at the configured folder path is unusable as a directory.
    QFile blocker(dir.path() + "/blocker");
    REQUIRE(blocker.open(QIODevice::WriteOnly));
    blocker.write("x");
    blocker.close();

    ManagedBackupController controller;
    QSignalSpy refreshed(&controller, &ManagedBackupController::statusRefreshed);
    controller.setBackupFolder(dir.path() + "/blocker");
    controller.setProfiles({ SqliteProfile(dir, "alpha") });

    // Scanned in the background: nothing is known before the result arrives.
    CHECK(controller.folderProblem().isEmpty());
    REQUIRE(WaitFor(refreshed));
    CHECK_FALSE(controller.folderProblem().isEmpty());
    controller.setBackupFolder(QString {});
}

namespace
{

/// Creates the SQLite database behind `profile` with one table and rows so
/// a backup has real content. Uses the Lightweight API directly.
void SeedDatabase(Lightweight::Config::Profile const& profile)
{
    auto connection = Lightweight::SqlConnection { Lightweight::SqlConnectionString { profile.connectionString } };
    auto stmt = Lightweight::SqlStatement { connection };
    (void) stmt.ExecuteDirect("CREATE TABLE items (id INTEGER PRIMARY KEY, label VARCHAR(32) NOT NULL)");
    (void) stmt.ExecuteDirect("INSERT INTO items (id, label) VALUES (1, 'one'), (2, 'two')");
}

} // namespace

TEST_CASE("backupAll writes one archive per profile and reports success", "[dbtool-gui][managed-backup-controller][run]")
{
    QTemporaryDir dir;
    ManagedBackupController controller;
    controller.setBackupFolder(dir.path() + "/backups");
    auto const alpha = SqliteProfile(dir, "alpha");
    auto const beta = SqliteProfile(dir, "beta");
    SeedDatabase(alpha);
    SeedDatabase(beta);
    QSignalSpy refreshed(&controller, &ManagedBackupController::statusRefreshed);
    controller.setProfiles({ alpha, beta });
    REQUIRE(WaitFor(refreshed));
    refreshed.clear();
    QSignalSpy done(&controller, &ManagedBackupController::finished);

    controller.backupAll();

    REQUIRE(WaitFor(done));
    CHECK(done.first().at(0).toBool());
    CHECK(done.first().at(1).toString() == "Backed up 2 profile(s).");
    // The archives are re-scanned in the background, never on the calling thread.
    REQUIRE(WaitFor(refreshed));
    CHECK(QFile::exists(dir.path() + "/backups/alpha.zip"));
    CHECK(QFile::exists(dir.path() + "/backups/beta.zip"));
    CHECK_FALSE(QFile::exists(dir.path() + "/backups/alpha.zip.tmp"));
    CHECK(controller.phase() == ManagedBackupController::Phase::Idle);

    auto* model = controller.status();
    CHECK(model->data(model->index(0, 0), DbtoolGui::BackupStatusListModel::RunStateRole).toString() == "ok");
    CHECK(model->data(model->index(0, 0), DbtoolGui::BackupStatusListModel::ArchiveExistsRole).toBool());

    controller.setBackupFolder(QString {});
}

TEST_CASE("backupAll re-run safely overwrites and leaves no tmp file", "[dbtool-gui][managed-backup-controller][run]")
{
    QTemporaryDir dir;
    ManagedBackupController controller;
    controller.setBackupFolder(dir.path() + "/backups");
    auto const alpha = SqliteProfile(dir, "alpha");
    SeedDatabase(alpha);
    controller.setProfiles({ alpha });

    QSignalSpy done(&controller, &ManagedBackupController::finished);
    controller.backupAll();
    REQUIRE(WaitFor(done));
    auto const firstSize = QFileInfo(dir.path() + "/backups/alpha.zip").size();
    REQUIRE(firstSize > 0);

    QSignalSpy done2(&controller, &ManagedBackupController::finished);
    controller.backupAll();
    REQUIRE(WaitFor(done2));
    CHECK(done2.first().at(0).toBool());
    CHECK(QFileInfo(dir.path() + "/backups/alpha.zip").size() > 0);
    CHECK_FALSE(QFile::exists(dir.path() + "/backups/alpha.zip.tmp"));

    controller.setBackupFolder(QString {});
}

TEST_CASE("backupAll continues past a broken profile and reports partial failure",
          "[dbtool-gui][managed-backup-controller][run]")
{
    QTemporaryDir dir;
    ManagedBackupController controller;
    controller.setBackupFolder(dir.path() + "/backups");
    auto broken = Lightweight::Config::Profile {};
    broken.name = "broken";
    broken.connectionString = "DRIVER=NoSuchDriver;Database=/nonexistent/nope.db";
    auto const good = SqliteProfile(dir, "good");
    SeedDatabase(good);
    controller.setProfiles({ broken, good });
    QSignalSpy done(&controller, &ManagedBackupController::finished);

    controller.backupAll();

    REQUIRE(WaitFor(done));
    CHECK_FALSE(done.first().at(0).toBool());
    CHECK(done.first().at(1).toString() == "Backed up 1 profile(s), 1 failed.");
    CHECK(QFile::exists(dir.path() + "/backups/good.zip"));
    auto* model = controller.status();
    CHECK(model->data(model->index(0, 0), DbtoolGui::BackupStatusListModel::RunStateRole).toString() == "failed");
    CHECK_FALSE(model->data(model->index(0, 0), DbtoolGui::BackupStatusListModel::ErrorTextRole).toString().isEmpty());
    CHECK(model->data(model->index(1, 0), DbtoolGui::BackupStatusListModel::RunStateRole).toString() == "ok");

    controller.setBackupFolder(QString {});
}

TEST_CASE("backupAll refuses while busy probe reports another run", "[dbtool-gui][managed-backup-controller][run]")
{
    QTemporaryDir dir;
    ManagedBackupController controller;
    controller.setBackupFolder(dir.path());
    controller.setProfiles({ SqliteProfile(dir, "alpha") });
    controller.setBusyProbe([] { return true; });
    QSignalSpy logs(&controller, &ManagedBackupController::logLine);

    controller.backupAll();

    CHECK(controller.phase() == ManagedBackupController::Phase::Idle);
    REQUIRE(logs.count() >= 1);
    CHECK(logs.first().at(0).toString().contains("busy"));

    controller.setBackupFolder(QString {});
}

TEST_CASE("backupAll refuses while a run is already in progress", "[dbtool-gui][managed-backup-controller][run]")
{
    QTemporaryDir dir;
    ManagedBackupController controller;
    controller.setBackupFolder(dir.path() + "/backups");
    SeedDatabase(SqliteProfile(dir, "alpha"));
    controller.setProfiles({ SqliteProfile(dir, "alpha") });
    QSignalSpy logs(&controller, &ManagedBackupController::logLine);
    QSignalSpy done(&controller, &ManagedBackupController::finished);

    // Phase flips to Running synchronously before the worker is dispatched,
    // so a second call made before the event loop spins sees it and refuses.
    controller.backupAll();
    REQUIRE(controller.phase() == ManagedBackupController::Phase::Running);
    controller.backupAll();

    REQUIRE(logs.count() >= 1);
    CHECK(logs.first().at(0).toString().contains("already in progress"));

    REQUIRE(WaitFor(done)); // let the first run finish so the pool is idle again
    controller.setBackupFolder(QString {});
}

TEST_CASE("backupAll fails when the backup folder is unusable", "[dbtool-gui][managed-backup-controller][run]")
{
    QTemporaryDir dir;
    QFile blocker(dir.path() + "/blocker");
    REQUIRE(blocker.open(QIODevice::WriteOnly));
    blocker.write("x");
    blocker.close();

    ManagedBackupController controller;
    QSignalSpy refreshed(&controller, &ManagedBackupController::statusRefreshed);
    controller.setBackupFolder(dir.path() + "/blocker"); // a file, not a directory
    controller.setProfiles({ SqliteProfile(dir, "alpha") });
    REQUIRE(WaitFor(refreshed));
    refreshed.clear();
    QSignalSpy logs(&controller, &ManagedBackupController::logLine);
    QSignalSpy done(&controller, &ManagedBackupController::finished);

    // The folder is checked by the run, off the calling thread, which then fails.
    controller.backupAll();
    REQUIRE(WaitFor(done));
    // The failed run re-scans the folder in the background, never on the calling thread.
    REQUIRE(WaitFor(refreshed));

    CHECK_FALSE(done.first().at(0).toBool());
    CHECK(controller.phase() == ManagedBackupController::Phase::Idle);
    REQUIRE(logs.count() >= 1);
    // The failure must name the folder problem (not the busy-probe path):
    // "blocker exists but is not a directory." is what CheckWritableFolder
    // reports for a file where a directory is expected.
    CHECK(logs.last().at(0).toString().contains("not a directory"));
    CHECK(done.first().at(1).toString().contains("not a directory"));
    CHECK_FALSE(controller.folderProblem().isEmpty());

    controller.setBackupFolder(QString {});
}

TEST_CASE("backupProfile backs up exactly one profile", "[dbtool-gui][managed-backup-controller][run]")
{
    QTemporaryDir dir;
    ManagedBackupController controller;
    controller.setBackupFolder(dir.path() + "/backups");
    auto const alpha = SqliteProfile(dir, "alpha");
    auto const beta = SqliteProfile(dir, "beta");
    SeedDatabase(alpha);
    SeedDatabase(beta);
    controller.setProfiles({ alpha, beta });
    QSignalSpy done(&controller, &ManagedBackupController::finished);

    controller.backupProfile("beta");

    REQUIRE(WaitFor(done));
    CHECK(done.first().at(0).toBool());
    CHECK(QFile::exists(dir.path() + "/backups/beta.zip"));
    CHECK_FALSE(QFile::exists(dir.path() + "/backups/alpha.zip"));

    controller.setBackupFolder(QString {});
}

TEST_CASE("backupProfile rejects an unknown profile name", "[dbtool-gui][managed-backup-controller][run]")
{
    QTemporaryDir dir;
    ManagedBackupController controller;
    controller.setBackupFolder(dir.path() + "/backups");
    controller.setProfiles({ SqliteProfile(dir, "alpha") });
    QSignalSpy logs(&controller, &ManagedBackupController::logLine);

    controller.backupProfile("ghost");

    CHECK(controller.phase() == ManagedBackupController::Phase::Idle);
    REQUIRE(logs.count() >= 1);
    CHECK(logs.first().at(0).toString().contains("ghost"));

    controller.setBackupFolder(QString {});
}

TEST_CASE("backupAll fails a profile whose sanitized archive name collides, without touching its database",
          "[dbtool-gui][managed-backup-controller][run]")
{
    QTemporaryDir dir;
    ManagedBackupController controller;
    controller.setBackupFolder(dir.path() + "/backups");
    // "acme prod" and "acme_prod" both sanitize to "acme_prod.zip"; the
    // later profile is the collision (see ManagedBackupCore::PlanArchiveNames).
    auto first = SqliteProfile(dir, "acme prod");
    auto second = SqliteProfile(dir, "acme_prod");
    SeedDatabase(first);
    SeedDatabase(second);
    controller.setProfiles({ first, second });
    QSignalSpy done(&controller, &ManagedBackupController::finished);

    controller.backupAll();

    REQUIRE(WaitFor(done));
    CHECK_FALSE(done.first().at(0).toBool());
    CHECK(done.first().at(1).toString() == "Backed up 1 profile(s), 1 failed.");
    auto* model = controller.status();
    CHECK(model->data(model->index(0, 0), DbtoolGui::BackupStatusListModel::RunStateRole).toString() == "ok");
    CHECK(model->data(model->index(1, 0), DbtoolGui::BackupStatusListModel::RunStateRole).toString() == "failed");
    CHECK(model->data(model->index(1, 0), DbtoolGui::BackupStatusListModel::ErrorTextRole).toString().contains("collides"));

    controller.setBackupFolder(QString {});
}

TEST_CASE("backupAll fails a profile whose connection string cannot be resolved",
          "[dbtool-gui][managed-backup-controller][run]")
{
    QTemporaryDir dir;
    ManagedBackupController controller;
    controller.setBackupFolder(dir.path() + "/backups");
    auto badSource = Lightweight::Config::Profile {};
    badSource.name = "badsource";
    badSource.dsn = "NoSuchDsn";
    badSource.secretRef = "env:MBCTRL_MISSING_PWD_2";
    controller.setProfiles({ badSource });
    QSignalSpy done(&controller, &ManagedBackupController::finished);

    controller.backupAll();

    REQUIRE(WaitFor(done));
    CHECK_FALSE(done.first().at(0).toBool());
    auto* model = controller.status();
    CHECK(model->data(model->index(0, 0), DbtoolGui::BackupStatusListModel::RunStateRole).toString() == "failed");
    CHECK(model->data(model->index(0, 0), DbtoolGui::BackupStatusListModel::ErrorTextRole).toString().contains("secret"));

    controller.setBackupFolder(QString {});
}

TEST_CASE("restoreArchive round-trips data into another profile's database",
          "[dbtool-gui][managed-backup-controller][restore]")
{
    QTemporaryDir dir;
    ManagedBackupController controller;
    controller.setBackupFolder(dir.path() + "/backups");
    auto const alpha = SqliteProfile(dir, "alpha");
    auto const beta = SqliteProfile(dir, "beta"); // target: file does not even exist yet
    SeedDatabase(alpha);
    controller.setProfiles({ alpha, beta });

    QSignalSpy backupDone(&controller, &ManagedBackupController::finished);
    controller.backupProfile("alpha");
    REQUIRE(WaitFor(backupDone));
    REQUIRE(backupDone.first().at(0).toBool());

    QSignalSpy restoreDone(&controller, &ManagedBackupController::finished);
    controller.restoreArchive("alpha", "beta");
    REQUIRE(WaitFor(restoreDone));
    CHECK(restoreDone.first().at(0).toBool());

    // beta's database now contains alpha's rows.
    auto connection = Lightweight::SqlConnection { Lightweight::SqlConnectionString { beta.connectionString } };
    auto stmt = Lightweight::SqlStatement { connection };
    CHECK(stmt.ExecuteDirectScalar<int>("SELECT COUNT(*) FROM items").value_or(-1) == 2);

    controller.setBackupFolder(QString {});
}

TEST_CASE("restoreArchiveToConnectionString restores into a raw target", "[dbtool-gui][managed-backup-controller][restore]")
{
    QTemporaryDir dir;
    ManagedBackupController controller;
    controller.setBackupFolder(dir.path() + "/backups");
    auto const alpha = SqliteProfile(dir, "alpha");
    SeedDatabase(alpha);
    controller.setProfiles({ alpha });

    QSignalSpy backupDone(&controller, &ManagedBackupController::finished);
    controller.backupProfile("alpha");
    REQUIRE(WaitFor(backupDone));

    auto const targetCs = QString::fromStdString(SqliteConnectionString((dir.path() + "/raw-target.db").toStdString()));
    QSignalSpy restoreDone(&controller, &ManagedBackupController::finished);
    controller.restoreArchiveToConnectionString("alpha", targetCs, QString {});
    REQUIRE(WaitFor(restoreDone));
    CHECK(restoreDone.first().at(0).toBool());

    auto connection = Lightweight::SqlConnection { Lightweight::SqlConnectionString { targetCs.toStdString() } };
    auto stmt = Lightweight::SqlStatement { connection };
    CHECK(stmt.ExecuteDirectScalar<int>("SELECT COUNT(*) FROM items").value_or(-1) == 2);

    controller.setBackupFolder(QString {});
}

TEST_CASE("restoreArchive fails cleanly when the archive is missing", "[dbtool-gui][managed-backup-controller][restore]")
{
    QTemporaryDir dir;
    ManagedBackupController controller;
    controller.setBackupFolder(dir.path());
    auto const alpha = SqliteProfile(dir, "alpha");
    controller.setProfiles({ alpha });
    QSignalSpy logs(&controller, &ManagedBackupController::logLine);

    controller.restoreArchive("alpha", "alpha");

    // Guard path: no archive on disk -> synchronous error log, stays Idle.
    CHECK(controller.phase() == ManagedBackupController::Phase::Idle);
    REQUIRE(logs.count() >= 1);
    CHECK(logs.first().at(0).toString().contains("archive"));

    controller.setBackupFolder(QString {});
}

TEST_CASE("restoreArchive rejects an unknown target profile", "[dbtool-gui][managed-backup-controller][restore]")
{
    QTemporaryDir dir;
    ManagedBackupController controller;
    controller.setBackupFolder(dir.path());
    controller.setProfiles({ SqliteProfile(dir, "alpha") });
    QSignalSpy logs(&controller, &ManagedBackupController::logLine);

    controller.restoreArchive("alpha", "ghost");

    CHECK(controller.phase() == ManagedBackupController::Phase::Idle);
    REQUIRE(logs.count() >= 1);
    CHECK(logs.first().at(0).toString().contains("ghost"));

    controller.setBackupFolder(QString {});
}

TEST_CASE("restoreArchive resolves the target profile's secret off the calling thread and fails cleanly "
          "when it cannot be resolved",
          "[dbtool-gui][managed-backup-controller][restore]")
{
    QTemporaryDir dir;
    ManagedBackupController controller;
    controller.setBackupFolder(dir.path() + "/backups");
    auto const alpha = SqliteProfile(dir, "alpha");
    SeedDatabase(alpha);

    // Target profile whose secretRef points at an environment variable that
    // is guaranteed not to be set — ResolveConnectionString() must fail, and
    // that failure must surface via a queued finished(false, ...) emitted
    // from the worker thread, not a synchronous call on the GUI thread.
    auto badTarget = Lightweight::Config::Profile {};
    badTarget.name = "badtarget";
    badTarget.dsn = "NoSuchDsn";
    badTarget.secretRef = "env:MBCTRL_MISSING_PWD";
    controller.setProfiles({ alpha, badTarget });

    QSignalSpy backupDone(&controller, &ManagedBackupController::finished);
    controller.backupProfile("alpha");
    REQUIRE(WaitFor(backupDone));
    REQUIRE(backupDone.first().at(0).toBool());

    QSignalSpy restoreDone(&controller, &ManagedBackupController::finished);
    controller.restoreArchive("alpha", "badtarget");
    REQUIRE(WaitFor(restoreDone));
    CHECK_FALSE(restoreDone.first().at(0).toBool());
    CHECK(controller.phase() == ManagedBackupController::Phase::Idle);

    controller.setBackupFolder(QString {});
}

TEST_CASE("restoreArchive refuses while a run is already in progress", "[dbtool-gui][managed-backup-controller][restore]")
{
    QTemporaryDir dir;
    ManagedBackupController controller;
    controller.setBackupFolder(dir.path() + "/backups");
    auto const alpha = SqliteProfile(dir, "alpha");
    SeedDatabase(alpha);
    controller.setProfiles({ alpha });

    QSignalSpy backupDone(&controller, &ManagedBackupController::finished);
    controller.backupProfile("alpha");
    REQUIRE(controller.phase() == ManagedBackupController::Phase::Running);

    QSignalSpy logs(&controller, &ManagedBackupController::logLine);
    controller.restoreArchive("alpha", "alpha");
    REQUIRE(logs.count() >= 1);
    CHECK(logs.first().at(0).toString().contains("already in progress"));

    REQUIRE(WaitFor(backupDone)); // let the backup finish so the pool is idle again
    controller.setBackupFolder(QString {});
}

TEST_CASE("backupProfile refuses a colliding profile but backs up the earlier owner of the name",
          "[dbtool-gui][managed-backup-controller][run]")
{
    QTemporaryDir dir;
    ManagedBackupController controller;
    controller.setBackupFolder(dir.path() + "/backups");
    // "acme prod" and "acme_prod" both sanitize to "acme_prod.zip"; the later
    // profile is the collision (see ManagedBackupCore::PlanArchiveNames).
    auto const first = SqliteProfile(dir, "acme prod");
    auto const second = SqliteProfile(dir, "acme_prod");
    SeedDatabase(first);
    SeedDatabase(second);
    controller.setProfiles({ first, second });

    // The later, colliding profile is refused synchronously, staying Idle and
    // never touching the shared archive name.
    QSignalSpy logs(&controller, &ManagedBackupController::logLine);
    controller.backupProfile("acme_prod");
    CHECK(controller.phase() == ManagedBackupController::Phase::Idle);
    REQUIRE(logs.count() >= 1);
    CHECK(logs.first().at(0).toString().contains("acme_prod"));
    CHECK(logs.first().at(0).toString().contains("collides"));

    // The earlier profile owns the sanitized name and still backs up cleanly.
    QSignalSpy done(&controller, &ManagedBackupController::finished);
    controller.backupProfile("acme prod");
    REQUIRE(WaitFor(done));
    CHECK(done.first().at(0).toBool());
    CHECK(QFile::exists(dir.path() + "/backups/acme_prod.zip"));

    controller.setBackupFolder(QString {});
}

TEST_CASE("restoreArchive refuses a profile whose sanitized archive name collides",
          "[dbtool-gui][managed-backup-controller][restore]")
{
    QTemporaryDir dir;
    ManagedBackupController controller;
    controller.setBackupFolder(dir.path() + "/backups");
    auto const first = SqliteProfile(dir, "acme prod");
    auto const second = SqliteProfile(dir, "acme_prod");
    SeedDatabase(first);
    SeedDatabase(second);
    controller.setProfiles({ first, second });

    // Give the shared "acme_prod.zip" real content so a missing-archive check
    // cannot be what refuses the restore below — only the collision guard can.
    QSignalSpy backupDone(&controller, &ManagedBackupController::finished);
    controller.backupProfile("acme prod");
    REQUIRE(WaitFor(backupDone));
    REQUIRE(backupDone.first().at(0).toBool());

    QSignalSpy logs(&controller, &ManagedBackupController::logLine);
    controller.restoreArchive("acme_prod", "acme_prod");
    CHECK(controller.phase() == ManagedBackupController::Phase::Idle);
    REQUIRE(logs.count() >= 1);
    CHECK(logs.first().at(0).toString().contains("acme_prod"));
    CHECK(logs.first().at(0).toString().contains("collides"));

    controller.setBackupFolder(QString {});
}

TEST_CASE("backupProfile emits tableProgress and populates the profile's table model",
          "[dbtool-gui][managed-backup-controller][run]")
{
    QTemporaryDir dir;
    QTemporaryDir folder;
    REQUIRE(dir.isValid());
    REQUIRE(folder.isValid());

    // A profile whose SQLite database has one table with a few rows, so the
    // backup emits per-table progress.
    auto profile = SqliteProfile(dir, "alpha");
    {
        auto conn = Lightweight::SqlConnection(Lightweight::SqlConnectionString { profile.connectionString });
        auto stmt = Lightweight::SqlStatement(conn);
        (void) stmt.ExecuteDirect("CREATE TABLE widgets (id INTEGER PRIMARY KEY, name TEXT)");
        (void) stmt.ExecuteDirect("INSERT INTO widgets (name) VALUES ('a'), ('b'), ('c')");
    }

    ManagedBackupController controller;
    controller.setBackupFolder(folder.path());
    controller.setProfiles({ profile });

    QSignalSpy progress(&controller, &ManagedBackupController::tableProgress);
    QSignalSpy done(&controller, &ManagedBackupController::finished);

    controller.backupProfile("alpha");
    REQUIRE(WaitFor(done));

    // At least one tableProgress arrived, and its profile field is "alpha".
    REQUIRE(progress.count() > 0);
    CHECK(progress.first().at(0).toString() == "alpha");

    // The per-profile table model ended up with at least one table row.
    auto* tables = controller.status()->tablesFor("alpha");
    REQUIRE(tables != nullptr);
    CHECK(tables->rowCount() >= 1);

    controller.setBackupFolder(QString {});
}

// ---------------------------------------------------------------------------
// Table-level failure semantics.
//
// Neither `SqlBackup::Backup` nor `SqlBackup::Restore` throws when an
// individual table fails: `Backup.cpp` catches the per-chunk exception, marks
// the chunk failed and reports `Progress::State::Error`, then returns
// normally; `Restore.cpp`'s `CreateTablesInOrder` drops each archived table
// and, when the re-`CREATE` fails, reports `State::Error` and simply omits the
// table. `ProgressManager::ErrorCount()` is the only signal that the run was
// partial — which is exactly what the dbtool CLI gates its exit code on.
//
// Reproducing a real per-table failure on demand is not possible through a
// live SQLite database, so the operations are injected: the fake stands in for
// `SqlBackup::Backup`/`Restore` and reproduces precisely that contract —
// report an Error event, then return without throwing.
// ---------------------------------------------------------------------------

namespace
{

/// Backup/restore stand-in that reports `count` table-level errors through the
/// progress manager and returns normally, exactly like the real API does when
/// individual tables fail.
/// @param count Number of Error events to report.
/// @param writeOutput Whether to create the output file first (a backup writes
///        its temporary archive before any table is processed).
/// @return An operation usable as both BackupOperation and RestoreOperation.
[[nodiscard]] auto FailingOperation(std::size_t count, bool writeOutput)
{
    return [count, writeOutput](std::filesystem::path const& file,
                                std::string const& /*connectionString*/,
                                std::string const& /*schema*/,
                                Lightweight::SqlBackup::ProgressManager& progress) {
        if (writeOutput)
            std::ofstream(file) << "partial archive";
        for (std::size_t i = 0; i < count; ++i)
            progress.Update({ .state = Lightweight::SqlBackup::Progress::State::Error,
                              .tableName = "items",
                              .currentRows = 0,
                              .totalRows = 0,
                              .message = "Backup failed: table is locked" });
    };
}

/// Reads a whole file into a string (used to prove an archive was untouched).
[[nodiscard]] std::string ReadAll(QString const& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll().toStdString();
}

} // namespace

TEST_CASE("a backup whose tables failed is reported as failed and never committed",
          "[dbtool-gui][managed-backup-controller][run]")
{
    QTemporaryDir dir;
    ManagedBackupController controller;
    controller.setBackupFolder(dir.path() + "/backups");
    auto const alpha = SqliteProfile(dir, "alpha");
    SeedDatabase(alpha);
    controller.setProfiles({ alpha });
    controller.setBackupOperation(FailingOperation(2, /*writeOutput*/ true));

    QSignalSpy done(&controller, &ManagedBackupController::finished);
    controller.backupAll();
    REQUIRE(WaitFor(done));

    // The run must be reported as a failure, not as "Backed up 1 profile(s).".
    CHECK_FALSE(done.first().at(0).toBool());
    CHECK(done.first().at(1).toString() == "Backed up 0 profile(s), 1 failed.");

    // Neither the archive nor its temporary sibling may survive.
    CHECK_FALSE(QFile::exists(dir.path() + "/backups/alpha.zip"));
    CHECK_FALSE(QFile::exists(dir.path() + "/backups/alpha.zip.tmp"));

    auto* model = controller.status();
    CHECK(model->data(model->index(0, 0), DbtoolGui::BackupStatusListModel::RunStateRole).toString() == "failed");
    CHECK(
        model->data(model->index(0, 0), DbtoolGui::BackupStatusListModel::ErrorTextRole).toString().contains("2 table(s)"));
    CHECK(model->data(model->index(0, 0), DbtoolGui::BackupStatusListModel::ErrorTextRole).toString().contains("discarded"));

    controller.setBackupFolder(QString {});
}

TEST_CASE("a failed backup leaves the previous good archive in place", "[dbtool-gui][managed-backup-controller][run]")
{
    QTemporaryDir dir;
    ManagedBackupController controller;
    controller.setBackupFolder(dir.path() + "/backups");
    auto const alpha = SqliteProfile(dir, "alpha");
    SeedDatabase(alpha);
    controller.setProfiles({ alpha });

    // 1. A real, complete backup produces the "last good" archive.
    QSignalSpy firstRun(&controller, &ManagedBackupController::finished);
    controller.backupAll();
    REQUIRE(WaitFor(firstRun));
    REQUIRE(firstRun.first().at(0).toBool());
    auto const goodArchive = ReadAll(dir.path() + "/backups/alpha.zip");
    REQUIRE_FALSE(goodArchive.empty());

    // 2. The next run loses tables. The archive on disk must not change: this
    //    is the only copy the user has.
    controller.setBackupOperation(FailingOperation(1, /*writeOutput*/ true));
    QSignalSpy secondRun(&controller, &ManagedBackupController::finished);
    controller.backupAll();
    REQUIRE(WaitFor(secondRun));

    CHECK_FALSE(secondRun.first().at(0).toBool());
    CHECK(ReadAll(dir.path() + "/backups/alpha.zip") == goodArchive);
    CHECK_FALSE(QFile::exists(dir.path() + "/backups/alpha.zip.tmp"));

    controller.setBackupFolder(QString {});
}

TEST_CASE("a restore whose tables failed is reported as failed and warns about the target",
          "[dbtool-gui][managed-backup-controller][restore]")
{
    QTemporaryDir dir;
    ManagedBackupController controller;
    controller.setBackupFolder(dir.path() + "/backups");
    auto const alpha = SqliteProfile(dir, "alpha");
    SeedDatabase(alpha);
    controller.setProfiles({ alpha });

    QSignalSpy backupDone(&controller, &ManagedBackupController::finished);
    controller.backupAll();
    REQUIRE(WaitFor(backupDone));
    REQUIRE(backupDone.first().at(0).toBool());

    controller.setRestoreOperation(FailingOperation(3, /*writeOutput*/ false));
    QSignalSpy restoreDone(&controller, &ManagedBackupController::finished);
    QSignalSpy logs(&controller, &ManagedBackupController::logLine);
    controller.restoreArchive("alpha", "alpha");
    REQUIRE(WaitFor(restoreDone));

    CHECK_FALSE(restoreDone.first().at(0).toBool());
    auto const summary = restoreDone.first().at(1).toString();
    // Three errors of the same table are one failed table.
    CHECK(summary.contains("1 table(s)"));
    // A restore is not transactional: tables are dropped and recreated one at a
    // time, so a failed run must tell the user the target is not usable.
    CHECK(summary.contains("INCOMPLETE"));
    CHECK(controller.phase() == ManagedBackupController::Phase::Idle);
    // The failure also reaches the log panel, not just the return value.
    REQUIRE(logs.count() >= 1);
    CHECK(logs.last().at(0).toString().contains("INCOMPLETE"));

    controller.setBackupFolder(QString {});
}

TEST_CASE("a managed restore counts failed tables from its report, not error updates",
          "[dbtool-gui][managed-backup-controller][run]")
{
    QTemporaryDir dir;
    ManagedBackupController controller;
    controller.setBackupFolder(dir.path() + "/backups");
    auto const alpha = SqliteProfile(dir, "alpha");
    SeedDatabase(alpha);
    controller.setProfiles({ alpha });

    QSignalSpy backupDone(&controller, &ManagedBackupController::finished);
    controller.backupAll();
    REQUIRE(WaitFor(backupDone));
    REQUIRE(backupDone.first().at(0).toBool());

    // "items" reports two unreadable chunks and its terminal error; "lines" fails without any error update.
    controller.setRestoreOperation([](std::filesystem::path const&,
                                      std::string const&,
                                      std::string const&,
                                      Lightweight::SqlBackup::ProgressManager& progress) {
        for (auto const* message: { "Unreadable chunk a", "Unreadable chunk b", "2 rows failed, 0 cells set to NULL" })
            progress.Update({ .state = Lightweight::SqlBackup::Progress::State::Error,
                              .tableName = "items",
                              .currentRows = 0,
                              .totalRows = 0,
                              .message = message });
        auto items = Lightweight::SqlBackup::TableRestoreReport {};
        items.tableName = "items";
        items.failedRowCount = 2;
        auto lines = Lightweight::SqlBackup::TableRestoreReport {};
        lines.tableName = "lines";
        lines.failedRowCount = 1;
        auto report = Lightweight::SqlBackup::RestoreReport {};
        report.tables = { items, lines };
        report.totalFailedRows = 3;
        return report;
    });
    QSignalSpy restoreDone(&controller, &ManagedBackupController::finished);
    controller.restoreArchive("alpha", "alpha");
    REQUIRE(WaitFor(restoreDone));

    CHECK_FALSE(restoreDone.first().at(0).toBool());
    CHECK(restoreDone.first().at(1).toString().contains("2 table(s)"));

    controller.setBackupFolder(QString {});
}

TEST_CASE("a clean injected run still commits and reports success", "[dbtool-gui][managed-backup-controller][run]")
{
    // Counterpart to the failure cases above: the ErrorCount() gate must not
    // reject a run that reported only non-Error progress.
    QTemporaryDir dir;
    ManagedBackupController controller;
    controller.setRecorder(IgnoringRecorder());
    controller.setBackupFolder(dir.path() + "/backups");
    auto const alpha = SqliteProfile(dir, "alpha");
    SeedDatabase(alpha);
    controller.setProfiles({ alpha });
    controller.setBackupOperation([](std::filesystem::path const& file,
                                     std::string const& /*connectionString*/,
                                     std::string const& /*schema*/,
                                     Lightweight::SqlBackup::ProgressManager& progress) {
        std::ofstream(file) << "complete archive";
        progress.Update({ .state = Lightweight::SqlBackup::Progress::State::Warning,
                          .tableName = "items",
                          .currentRows = 2,
                          .totalRows = 2,
                          .message = "a warning is not an error" });
    });

    QSignalSpy done(&controller, &ManagedBackupController::finished);
    controller.backupAll();
    REQUIRE(WaitFor(done));

    CHECK(done.first().at(0).toBool());
    CHECK(ReadAll(dir.path() + "/backups/alpha.zip") == "complete archive");

    // Handing back an empty callable restores the real SqlBackup::Backup.
    controller.setBackupOperation({});
    QSignalSpy realRun(&controller, &ManagedBackupController::finished);
    controller.backupAll();
    REQUIRE(WaitFor(realRun));
    CHECK(realRun.first().at(0).toBool());
    CHECK(ReadAll(dir.path() + "/backups/alpha.zip") != "complete archive");

    controller.setBackupFolder(QString {});
}

namespace
{

/// A backup operation that writes a complete archive without touching any database.
void WriteArchiveOperation(std::filesystem::path const& file,
                           std::string const& /*connectionString*/,
                           std::string const& /*schema*/,
                           Lightweight::SqlBackup::ProgressManager& /*progress*/)
{
    std::ofstream(file) << "complete archive";
}

/// A restore operation that succeeds without touching any database.
void NoOpRestoreOperation(std::filesystem::path const& /*archiveFile*/,
                          std::string const& /*connectionString*/,
                          std::string const& /*schema*/,
                          Lightweight::SqlBackup::ProgressManager& /*progress*/)
{
}

} // namespace

TEST_CASE("a plaintext password is handed over for encryption as soon as a backup connects with it",
          "[dbtool-gui][managed-backup-controller][password]")
{
    QTemporaryDir dir;
    ManagedBackupController controller;
    controller.setRecorder(IgnoringRecorder());
    controller.setBackupFolder(dir.path() + "/backups");
    auto plaintext = SqliteProfile(dir, "plaintext");
    plaintext.password = "hunter2";
    auto encrypted = SqliteProfile(dir, "encrypted");
    auto const cipherText = Lightweight::Secrets::ProfileCipher::Builtin().Encrypt("hunter2");
    REQUIRE(cipherText.has_value());
    encrypted.password = cipherText.value_or(std::string {});
    auto const passwordless = SqliteProfile(dir, "passwordless");
    controller.setProfiles({ plaintext, encrypted, passwordless });
    controller.setBackupOperation(&WriteArchiveOperation);

    // Only a plaintext password needs proving, so only its connection is probed.
    auto probed = std::vector<std::string> {};
    controller.setConnectionProbe([&](std::string const& connectionString) {
        probed.push_back(connectionString);
        return true;
    });
    auto verified = std::vector<Lightweight::Config::Profile> {};
    controller.setPlaintextPasswordVerifiedHandler(
        [&](Lightweight::Config::Profile const& profile) { verified.push_back(profile); });

    QSignalSpy done(&controller, &ManagedBackupController::finished);
    controller.backupAll();
    REQUIRE(WaitFor(done));
    REQUIRE(done.first().at(0).toBool());

    // The handler ran (queued on this thread) before `finished`, with the password it was verified with.
    REQUIRE(probed.size() == 1);
    CHECK(probed.front().find("hunter2") != std::string::npos);
    REQUIRE(verified.size() == 1);
    CHECK(verified.front().name == "plaintext");
    CHECK(verified.front().password == "hunter2");

    controller.setBackupFolder(QString {});
}

TEST_CASE("a plaintext password is encrypted once it connects, even if the backup then fails",
          "[dbtool-gui][managed-backup-controller][password]")
{
    QTemporaryDir dir;
    ManagedBackupController controller;
    controller.setBackupFolder(dir.path() + "/backups");
    auto plaintext = SqliteProfile(dir, "plaintext");
    plaintext.password = "hunter2";
    controller.setProfiles({ plaintext });
    controller.setConnectionProbe([](std::string const& /*connectionString*/) { return true; });
    controller.setBackupOperation(
        [](std::filesystem::path const& /*file*/,
           std::string const& /*connectionString*/,
           std::string const& /*schema*/,
           Lightweight::SqlBackup::ProgressManager& /*progress*/) { throw std::runtime_error("disk full"); });

    auto verifiedCount = 0;
    controller.setPlaintextPasswordVerifiedHandler([&](Lightweight::Config::Profile const&) { ++verifiedCount; });

    QSignalSpy done(&controller, &ManagedBackupController::finished);
    controller.backupAll();
    REQUIRE(WaitFor(done));
    CHECK_FALSE(done.first().at(0).toBool());
    CHECK(verifiedCount == 1);

    controller.setBackupFolder(QString {});
}

TEST_CASE("a plaintext password whose connection fails is not handed over",
          "[dbtool-gui][managed-backup-controller][password]")
{
    QTemporaryDir dir;
    ManagedBackupController controller;
    controller.setBackupFolder(dir.path() + "/backups");
    auto plaintext = SqliteProfile(dir, "plaintext");
    plaintext.password = "wrong";
    controller.setProfiles({ plaintext });
    controller.setConnectionProbe([](std::string const& /*connectionString*/) { return false; });
    controller.setBackupOperation(
        [](std::filesystem::path const& /*file*/,
           std::string const& /*connectionString*/,
           std::string const& /*schema*/,
           Lightweight::SqlBackup::ProgressManager& /*progress*/) { throw std::runtime_error("login failed"); });

    auto verifiedCount = 0;
    controller.setPlaintextPasswordVerifiedHandler([&](Lightweight::Config::Profile const&) { ++verifiedCount; });

    QSignalSpy done(&controller, &ManagedBackupController::finished);
    controller.backupAll();
    REQUIRE(WaitFor(done));
    CHECK_FALSE(done.first().at(0).toBool());
    CHECK(verifiedCount == 0);

    controller.setBackupFolder(QString {});
}

TEST_CASE("a plaintext password is handed over as soon as a restore connects to its target profile",
          "[dbtool-gui][managed-backup-controller][password]")
{
    QTemporaryDir dir;
    ManagedBackupController controller;
    controller.setRecorder(IgnoringRecorder());
    controller.setBackupFolder(dir.path() + "/backups");
    auto const source = SqliteProfile(dir, "source");
    auto target = SqliteProfile(dir, "target");
    target.password = "hunter2";
    controller.setProfiles({ source, target });
    controller.setBackupOperation(&WriteArchiveOperation);
    controller.setRestoreOperation(&NoOpRestoreOperation);
    controller.setConnectionProbe([](std::string const& /*connectionString*/) { return true; });

    QSignalSpy backupDone(&controller, &ManagedBackupController::finished);
    controller.backupProfile("source");
    REQUIRE(WaitFor(backupDone));
    REQUIRE(backupDone.first().at(0).toBool());

    auto verified = std::vector<std::string> {};
    controller.setPlaintextPasswordVerifiedHandler(
        [&](Lightweight::Config::Profile const& profile) { verified.push_back(profile.name); });

    QSignalSpy restoreDone(&controller, &ManagedBackupController::finished);
    controller.restoreArchive("source", "target");
    REQUIRE(WaitFor(restoreDone));
    REQUIRE(restoreDone.first().at(0).toBool());

    CHECK(verified == std::vector<std::string> { "target" });

    controller.setBackupFolder(QString {});
}

TEST_CASE("a managed restore in flight blocks closing and is named as a restore",
          "[dbtool-gui][managed-backup-controller][close-guard]")
{
    QTemporaryDir dir;
    ManagedBackupController controller;
    controller.setRecorder(IgnoringRecorder());
    controller.setBackupFolder(dir.path() + "/backups");
    auto const source = SqliteProfile(dir, "source");
    auto const target = SqliteProfile(dir, "target");
    controller.setProfiles({ source, target });
    controller.setBackupOperation(&WriteArchiveOperation);
    controller.setConnectionProbe([](std::string const& /*connectionString*/) { return true; });

    QSignalSpy backupDone(&controller, &ManagedBackupController::finished);
    controller.backupProfile("source");
    REQUIRE(WaitFor(backupDone));
    REQUIRE(backupDone.first().at(0).toBool());

    // The restore holds until the test has looked at the close guard.
    auto release = std::promise<void> {};
    auto released = release.get_future().share();
    auto started = std::promise<void> {};
    auto startedFuture = started.get_future();
    controller.setRestoreOperation([released, &started](std::filesystem::path const& /*archiveFile*/,
                                                        std::string const& /*connectionString*/,
                                                        std::string const& /*schema*/,
                                                        Lightweight::SqlBackup::ProgressManager& /*progress*/) {
        started.set_value();
        released.wait();
    });

    auto const runner = DbtoolGui::MigrationRunner {};
    auto const backupRunner = DbtoolGui::BackupRunner {};
    CHECK(DbtoolGui::CloseBlockedReason(runner, backupRunner, controller).isEmpty());

    QSignalSpy restoreDone(&controller, &ManagedBackupController::finished);
    controller.restoreArchive("source", "target");
    REQUIRE(startedFuture.wait_for(std::chrono::seconds { 30 }) == std::future_status::ready);
    CHECK(controller.phase() == ManagedBackupController::Phase::Running);
    CHECK(DbtoolGui::CloseBlockedReason(runner, backupRunner, controller)
          == QStringLiteral("A restore is running. Wait until it finishes."));

    release.set_value();
    REQUIRE(WaitFor(restoreDone));
    CHECK(DbtoolGui::CloseBlockedReason(runner, backupRunner, controller).isEmpty());
    controller.setBackupFolder(QString {});
}

TEST_CASE("the folder dbtool.yml configures is the default backup folder, and Settings still wins",
          "[dbtool-gui][managed-backup-controller][backup-dir]")
{
    QTemporaryDir dir;
    ManagedBackupController controller;
    controller.setBackupFolder(QString {}); // do not inherit a persisted value from a previous run
    controller.setConfiguredBackupFolder(QString {});
    auto const platformDefault = controller.defaultBackupFolder();

    QSignalSpy changed(&controller, &ManagedBackupController::backupFolderChanged);
    auto const configured = dir.path() + "/from-yml";
    controller.setConfiguredBackupFolder(configured);

    CHECK(changed.count() >= 1);
    CHECK(controller.defaultBackupFolder() == configured);
    CHECK(controller.effectiveBackupFolder() == configured);

    // A folder chosen in Settings wins; the configured one stays the default.
    auto const chosen = dir.path() + "/chosen";
    controller.setBackupFolder(chosen);
    CHECK(controller.effectiveBackupFolder() == chosen);
    CHECK(controller.defaultBackupFolder() == configured);

    // Clearing both goes back to the platform default.
    controller.setBackupFolder(QString {});
    controller.setConfiguredBackupFolder(QString {});
    CHECK(controller.effectiveBackupFolder() == platformDefault);
}

TEST_CASE("a multi-schema archive drops only a profile schema it holds; a typed schema goes to the restore",
          "[dbtool-gui][managed-backup-controller][restore]")
{
    QTemporaryDir dir;
    ManagedBackupController controller;
    controller.setBackupFolder(dir.path() + "/backups");
    auto const source = SqliteProfile(dir, "source");
    auto inArchive = SqliteProfile(dir, "in-archive");
    inArchive.schema = "SALES";
    auto notInArchive = SqliteProfile(dir, "not-in-archive");
    notInArchive.schema = "restore_check";
    controller.setProfiles({ source, inArchive, notInArchive });
    controller.setBackupOperation([](std::filesystem::path const& file,
                                     std::string const& /*connectionString*/,
                                     std::string const& /*schema*/,
                                     Lightweight::SqlBackup::ProgressManager& /*progress*/) {
        // Runs on the pool thread, so a failure throws (and fails the backup) instead of asserting.
        auto archive = Lightweight::Zip::ZipArchive::CreateOrTruncate(file);
        if (!archive
            || !archive->AddString("metadata.json",
                                   R"({"format_version":"2.0","schema_name":"","schema_names":["dbo","sales"],"schema":[]})")
            || !archive->Close())
            throw std::runtime_error("cannot write the multi-schema test archive");
    });
    auto restoredSchemas = std::vector<std::string> {};
    controller.setRestoreOperation(
        [&restoredSchemas](std::filesystem::path const& /*archiveFile*/,
                           std::string const& /*connectionString*/,
                           std::string const& schema,
                           Lightweight::SqlBackup::ProgressManager& /*progress*/) { restoredSchemas.push_back(schema); });

    QSignalSpy backupDone(&controller, &ManagedBackupController::finished);
    controller.backupProfile("source");
    REQUIRE(WaitFor(backupDone));
    REQUIRE(backupDone.first().at(0).toBool());

    auto const restore = [&](auto const& start) {
        QSignalSpy restoreDone(&controller, &ManagedBackupController::finished);
        start();
        REQUIRE(WaitFor(restoreDone));
    };
    restore([&] { controller.restoreArchive("source", "in-archive"); });
    restore([&] { controller.restoreArchive("source", "not-in-archive"); });
    restore([&] {
        controller.restoreArchiveToConnectionString(
            "source", QString::fromStdString(notInArchive.connectionString), QStringLiteral("sales"));
    });

    CHECK(restoredSchemas == std::vector<std::string> { "", "restore_check", "sales" });

    controller.setBackupFolder(QString {});
}

TEST_CASE("a managed backup records every successful profile once, with its final archive and schema",
          "[dbtool-gui][managed-backup-controller][recording]")
{
    QTemporaryDir dir;
    ManagedBackupController controller;
    controller.setBackupFolder(dir.path() + "/backups");
    auto alpha = SqliteProfile(dir, "alpha");
    auto beta = SqliteProfile(dir, "beta");
    beta.schema = "sales";
    auto both = SqliteProfile(dir, "both");
    both.schema = "a,b";
    controller.setProfiles({ alpha, beta, both });
    auto calls = std::vector<RecorderCall> {};
    controller.setBackupOperation(&WriteArchiveOperation);
    controller.setRecorder(NotingRecorder(calls));

    QSignalSpy done(&controller, &ManagedBackupController::finished);
    controller.backupAll();
    REQUIRE(WaitFor(done));
    REQUIRE(done.first().at(0).toBool());

    REQUIRE(calls.size() == 6);
    auto const folder = std::filesystem::path { (dir.path() + "/backups").toStdString() };
    CHECK(calls[0].what == "capture");
    CHECK(calls[0].connectionString == alpha.connectionString);
    CHECK(calls[1].what == "backup");
    CHECK(calls[1].archive == folder / "alpha.zip");
    CHECK(calls[1].schema.empty());
    CHECK(calls[3].archive == folder / "beta.zip");
    CHECK(calls[3].schema == "sales");
    CHECK(calls[5].archive == folder / "both.zip");
    CHECK(calls[5].schema.empty());

    controller.setBackupFolder(QString {});
}

TEST_CASE("a managed backup that failed, collided or lost tables records nothing",
          "[dbtool-gui][managed-backup-controller][recording]")
{
    QTemporaryDir dir;
    ManagedBackupController controller;
    controller.setBackupFolder(dir.path() + "/backups");
    auto const alpha = SqliteProfile(dir, "alpha");
    controller.setProfiles({ alpha });
    auto calls = std::vector<RecorderCall> {};
    controller.setBackupOperation(FailingOperation(2, /*writeOutput*/ true));
    controller.setRecorder(NotingRecorder(calls));

    QSignalSpy done(&controller, &ManagedBackupController::finished);
    controller.backupAll();
    REQUIRE(WaitFor(done));

    CHECK_FALSE(done.first().at(0).toBool());
    CHECK(std::ranges::none_of(calls, [](RecorderCall const& call) { return call.what == "backup"; }));

    controller.setBackupOperation(&WriteArchiveOperation);
    controller.setProfiles({ SqliteProfile(dir, "a b"), SqliteProfile(dir, "a_b") });
    calls.clear();
    QSignalSpy collided(&controller, &ManagedBackupController::finished);
    controller.backupAll();
    REQUIRE(WaitFor(collided));
    CHECK_FALSE(collided.first().at(0).toBool());
    CHECK(std::ranges::count_if(calls, [](RecorderCall const& call) { return call.what == "backup"; }) == 1);

    controller.setBackupFolder(QString {});
}

TEST_CASE("a managed backup whose record cannot be written keeps the new archive and warns",
          "[dbtool-gui][managed-backup-controller][recording]")
{
    QTemporaryDir dir;
    ManagedBackupController controller;
    controller.setBackupFolder(dir.path() + "/backups");
    controller.setProfiles({ SqliteProfile(dir, "alpha") });
    controller.setBackupOperation(&WriteArchiveOperation);
    auto calls = std::vector<RecorderCall> {};
    controller.setRecorder(
        NotingRecorder(calls,
                       Lightweight::Tools::BackupRecordError {
                           .kind = Lightweight::Tools::BackupRecordError::Kind::WriteFailed, .message = "disk full" }));
    QSignalSpy warned(&controller, &ManagedBackupController::logLine);
    QSignalSpy done(&controller, &ManagedBackupController::finished);
    controller.backupAll();
    REQUIRE(WaitFor(done));

    CHECK(done.first().at(0).toBool());
    CHECK(ReadAll(dir.path() + "/backups/alpha.zip") == "complete archive");
    REQUIRE(warned.count() >= 1);
    CHECK(warned.last().at(0).toString().contains("disk full"));
    CHECK(warned.last().at(1).value<DbtoolGui::LogLevel>() == DbtoolGui::LogLevel::Warning);

    controller.setBackupFolder(QString {});
}

TEST_CASE("a managed restore records into the target database, and a failed restore does not",
          "[dbtool-gui][managed-backup-controller][recording]")
{
    QTemporaryDir dir;
    ManagedBackupController controller;
    controller.setBackupFolder(dir.path() + "/backups");
    auto const source = SqliteProfile(dir, "source");
    auto target = SqliteProfile(dir, "target");
    target.schema = "sales";
    controller.setProfiles({ source, target });
    controller.setBackupOperation(&WriteArchiveOperation);
    controller.setRestoreOperation(&NoOpRestoreOperation);
    controller.setRecorder(IgnoringRecorder());
    QSignalSpy backupDone(&controller, &ManagedBackupController::finished);
    controller.backupProfile("source");
    REQUIRE(WaitFor(backupDone));
    REQUIRE(backupDone.first().at(0).toBool());

    auto calls = std::vector<RecorderCall> {};
    controller.setRecorder(NotingRecorder(calls));
    QSignalSpy restoreDone(&controller, &ManagedBackupController::finished);
    controller.restoreArchive("source", "target");
    REQUIRE(WaitFor(restoreDone));
    REQUIRE(restoreDone.first().at(0).toBool());
    REQUIRE(calls.size() == 1);
    CHECK(calls[0].what == "restore");
    CHECK(calls[0].connectionString == target.connectionString);
    CHECK(calls[0].schema == "sales");
    CHECK(calls[0].archive == std::filesystem::path { (dir.path() + "/backups/source.zip").toStdString() });

    calls.clear();
    controller.setRestoreOperation(FailingOperation(1, /*writeOutput*/ false));
    QSignalSpy failedDone(&controller, &ManagedBackupController::finished);
    controller.restoreArchive("source", "target");
    REQUIRE(WaitFor(failedDone));
    CHECK_FALSE(failedDone.first().at(0).toBool());
    CHECK(calls.empty());

    controller.setBackupFolder(QString {});
}

TEST_CASE("a managed restore whose record cannot be written still succeeds and warns",
          "[dbtool-gui][managed-backup-controller][recording]")
{
    QTemporaryDir dir;
    ManagedBackupController controller;
    controller.setBackupFolder(dir.path() + "/backups");
    auto const source = SqliteProfile(dir, "source");
    controller.setProfiles({ source });
    controller.setBackupOperation(&WriteArchiveOperation);
    controller.setRestoreOperation(&NoOpRestoreOperation);
    controller.setRecorder(IgnoringRecorder());
    QSignalSpy backupDone(&controller, &ManagedBackupController::finished);
    controller.backupProfile("source");
    REQUIRE(WaitFor(backupDone));

    auto calls = std::vector<RecorderCall> {};
    controller.setRecorder(NotingRecorder(
        calls,
        Lightweight::Tools::BackupRecordError { .kind = Lightweight::Tools::BackupRecordError::Kind::WriteFailed,
                                                .message = "read-only database" }));
    QSignalSpy warned(&controller, &ManagedBackupController::logLine);
    QSignalSpy restoreDone(&controller, &ManagedBackupController::finished);
    controller.restoreArchive("source", "source");
    REQUIRE(WaitFor(restoreDone));
    CHECK(restoreDone.first().at(0).toBool());
    REQUIRE(warned.count() >= 1);
    CHECK(warned.last().at(0).toString().contains("read-only database"));

    controller.setBackupFolder(QString {});
}

TEST_CASE("a managed restore into a connection string records into it under the typed schema",
          "[dbtool-gui][managed-backup-controller][recording]")
{
    QTemporaryDir dir;
    ManagedBackupController controller;
    controller.setBackupFolder(dir.path() + "/backups");
    auto const source = SqliteProfile(dir, "source");
    controller.setProfiles({ source });
    controller.setBackupOperation(&WriteArchiveOperation);
    controller.setRestoreOperation(&NoOpRestoreOperation);
    controller.setRecorder(IgnoringRecorder());
    QSignalSpy backupDone(&controller, &ManagedBackupController::finished);
    controller.backupProfile("source");
    REQUIRE(WaitFor(backupDone));
    REQUIRE(backupDone.first().at(0).toBool());

    auto calls = std::vector<RecorderCall> {};
    controller.setRecorder(NotingRecorder(calls));
    auto const custom = SqliteProfile(dir, "custom");
    QSignalSpy typedDone(&controller, &ManagedBackupController::finished);
    controller.restoreArchiveToConnectionString("source", QString::fromStdString(custom.connectionString), "sales");
    REQUIRE(WaitFor(typedDone));
    REQUIRE(typedDone.first().at(0).toBool());
    REQUIRE(calls.size() == 1);
    CHECK(calls[0].what == "restore");
    CHECK(calls[0].connectionString == custom.connectionString);
    CHECK(calls[0].schema == "sales");

    calls.clear();
    QSignalSpy listDone(&controller, &ManagedBackupController::finished);
    controller.restoreArchiveToConnectionString("source", QString::fromStdString(custom.connectionString), "a,b");
    REQUIRE(WaitFor(listDone));
    REQUIRE(calls.size() == 1);
    CHECK(calls[0].schema.empty());

    controller.setBackupFolder(QString {});
}

TEST_CASE("a managed backup whose archive fails verification leaves no archive when there was none",
          "[dbtool-gui][managed-backup-controller][recording]")
{
    QTemporaryDir dir;
    ManagedBackupController controller;
    controller.setBackupFolder(dir.path() + "/backups");
    controller.setProfiles({ SqliteProfile(dir, "alpha") });
    controller.setBackupOperation(&WriteArchiveOperation);
    controller.setRecorder(FailingVerificationRecorder());
    QSignalSpy done(&controller, &ManagedBackupController::finished);
    controller.backupAll();
    REQUIRE(WaitFor(done));

    CHECK_FALSE(done.first().at(0).toBool());
    CHECK_FALSE(QFile::exists(dir.path() + "/backups/alpha.zip"));
    CHECK_FALSE(QFile::exists(dir.path() + "/backups/alpha.zip.tmp"));

    controller.setBackupFolder(QString {});
}

TEST_CASE("a managed backup whose archive fails verification leaves the last good archive byte-identical",
          "[dbtool-gui][managed-backup-controller][recording]")
{
    QTemporaryDir dir;
    ManagedBackupController controller;
    controller.setBackupFolder(dir.path() + "/backups");
    controller.setProfiles({ SqliteProfile(dir, "alpha") });
    controller.setBackupOperation(&WriteArchiveOperation);
    controller.setRecorder(IgnoringRecorder());
    QSignalSpy goodDone(&controller, &ManagedBackupController::finished);
    controller.backupAll();
    REQUIRE(WaitFor(goodDone));
    REQUIRE(goodDone.first().at(0).toBool());
    auto const archive = dir.path() + "/backups/alpha.zip";
    auto const goodArchive = ReadAll(archive);
    REQUIRE_FALSE(goodArchive.empty());

    controller.setBackupOperation(
        [](std::filesystem::path const& file,
           std::string const&,
           std::string const&,
           Lightweight::SqlBackup::ProgressManager&) { std::ofstream(file) << "corrupt archive"; });
    controller.setRecorder(FailingVerificationRecorder());
    QSignalSpy badDone(&controller, &ManagedBackupController::finished);
    controller.backupAll();
    REQUIRE(WaitFor(badDone));

    CHECK_FALSE(badDone.first().at(0).toBool());
    CHECK(ReadAll(archive) == goodArchive);
    CHECK_FALSE(QFile::exists(archive + ".tmp"));

    controller.setBackupFolder(QString {});
}
