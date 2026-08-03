// SPDX-License-Identifier: Apache-2.0
//
// Headless tests for ManagedBackupController. SQLite-only: every profile
// points at a database file inside a QTemporaryDir, so the suite runs on
// any machine with the SQLite3 ODBC driver — same baseline as the rest of
// the dbtool-gui tests.

#include "../ManagedBackupController.hpp"
#include "../Models/BackupStatusListModel.hpp"
#include "../Models/BackupTableListModel.hpp"

#include <Lightweight/SqlBackup.hpp>
#include <Lightweight/SqlConnection.hpp>
#include <Lightweight/SqlStatement.hpp>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

#include <Config/ProfileStore.hpp>
#include <QtCore/QEventLoop>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QTemporaryDir>
#include <QtCore/QTimer>
#include <QtTest/QSignalSpy>

using DbtoolGui::ManagedBackupController;

namespace
{

/// Builds a profile with a raw SQLite connection string into `dir`.
[[nodiscard]] Lightweight::Config::Profile SqliteProfile(QTemporaryDir const& dir, std::string const& name)
{
    Lightweight::Config::Profile profile;
    profile.name = name;
    profile.connectionString =
        "DRIVER=SQLite3;Database=" + (dir.path() + "/" + QString::fromStdString(name)).toStdString() + ".db";
    return profile;
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

TEST_CASE("setProfiles populates the status model and refreshStatus scans archives",
          "[dbtool-gui][managed-backup-controller]")
{
    QTemporaryDir dir;
    ManagedBackupController controller;
    controller.setBackupFolder(dir.path());
    controller.setProfiles({ SqliteProfile(dir, "alpha"), SqliteProfile(dir, "beta") });

    REQUIRE(controller.status()->rowCount() == 2);
    CHECK(controller.folderProblem().isEmpty());

    // Drop a fake archive for alpha and re-scan.
    QFile file(dir.path() + "/alpha.zip");
    REQUIRE(file.open(QIODevice::WriteOnly));
    file.write("xxxx");
    file.close();
    controller.refreshStatus();

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
    controller.setBackupFolder(dir.path() + "/blocker");
    controller.setProfiles({ SqliteProfile(dir, "alpha") });

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
    controller.setProfiles({ alpha, beta });
    QSignalSpy done(&controller, &ManagedBackupController::finished);

    controller.backupAll();

    REQUIRE(WaitFor(done));
    CHECK(done.first().at(0).toBool());
    CHECK(done.first().at(1).toString() == "Backed up 2 profile(s).");
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

TEST_CASE("backupAll refuses when the backup folder is unusable", "[dbtool-gui][managed-backup-controller][run]")
{
    QTemporaryDir dir;
    QFile blocker(dir.path() + "/blocker");
    REQUIRE(blocker.open(QIODevice::WriteOnly));
    blocker.write("x");
    blocker.close();

    ManagedBackupController controller;
    controller.setBackupFolder(dir.path() + "/blocker"); // a file, not a directory
    controller.setProfiles({ SqliteProfile(dir, "alpha") });
    QSignalSpy logs(&controller, &ManagedBackupController::logLine);

    controller.backupAll();

    CHECK(controller.phase() == ManagedBackupController::Phase::Idle);
    REQUIRE(logs.count() >= 1);
    // The refusal must name the folder problem (not the busy-probe path):
    // "blocker exists but is not a directory." is what CheckWritableFolder
    // reports for a file where a directory is expected.
    CHECK(logs.first().at(0).toString().contains("not a directory"));

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

    auto const targetCs = QString("DRIVER=SQLite3;Database=%1/raw-target.db").arg(dir.path());
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
    CHECK(summary.contains("3 table(s)"));
    // A restore is not transactional: tables are dropped and recreated one at a
    // time, so a failed run must tell the user the target is not usable.
    CHECK(summary.contains("INCOMPLETE"));
    CHECK(controller.phase() == ManagedBackupController::Phase::Idle);
    // The failure also reaches the log panel, not just the return value.
    REQUIRE(logs.count() >= 1);
    CHECK(logs.last().at(0).toString().contains("INCOMPLETE"));

    controller.setBackupFolder(QString {});
}

TEST_CASE("a clean injected run still commits and reports success", "[dbtool-gui][managed-backup-controller][run]")
{
    // Counterpart to the failure cases above: the ErrorCount() gate must not
    // reject a run that reported only non-Error progress.
    QTemporaryDir dir;
    ManagedBackupController controller;
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
