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

#include <Lightweight/SqlBackup.hpp>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QTemporaryDir>
#include <QtTest/QSignalSpy>

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

    CHECK_FALSE(controller.connected());
    CHECK(controller.lastError().isEmpty());
}

TEST_CASE("AppController buffers a startup banner that replays on attachLogSink", "[dbtool-gui][AppController][startup]")
{
    DbtoolGui::AppController controller;
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
    CHECK(joined.contains(QStringLiteral("Theme:")));
    CHECK(joined.contains(QStringLiteral("View mode:")));
    CHECK(joined.contains(QStringLiteral("Profile store:")));
    CHECK(joined.contains(QStringLiteral("Ready")));
}

TEST_CASE("attachLogSink is idempotent — banner does not replay on a second call", "[dbtool-gui][AppController][startup]")
{
    DbtoolGui::AppController controller;
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
    auto const ok = controller.loadProfiles(QString::fromStdString(fixturePath.string()));

    REQUIRE(ok);
    CHECK(controller.profiles()->rowCount() >= 2);
    CHECK(controller.profilePath().toStdString() == fixturePath.string());
}

TEST_CASE("setMigrationSelected on an unknown timestamp is a no-op", "[dbtool-gui][AppController][selection]")
{
    DbtoolGui::AppController controller;
    REQUIRE(controller.selectedMigrationTimestamps().isEmpty());

    controller.setMigrationSelected(QStringLiteral("99999999999999-ghost"), true);

    CHECK(controller.selectedMigrationTimestamps().isEmpty());
    CHECK(controller.selectionCount() == 0);
}

TEST_CASE("selectAllPending on an empty migration list is a no-op", "[dbtool-gui][AppController][selection]")
{
    DbtoolGui::AppController controller;

    controller.selectAllPending(true);
    CHECK(controller.selectionCount() == 0);

    controller.selectAllPending(false);
    CHECK(controller.selectionCount() == 0);
}

TEST_CASE("previewMigrationSql returns empty for an unknown timestamp", "[dbtool-gui][AppController]")
{
    DbtoolGui::AppController controller;
    auto const sql = controller.previewMigrationSql(QStringLiteral("19700101000000-not-a-migration"));
    CHECK(sql.isEmpty());
}

TEST_CASE("buildFailureReport returns a non-empty multi-line bundle even when disconnected",
          "[dbtool-gui][AppController][failure-report]")
{
    DbtoolGui::AppController controller;
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
    auto const ts = controller.releaseHighestTimestamp(QStringLiteral("v999.999.999"));
    CHECK(ts.isEmpty());
}

TEST_CASE("managedBackups is exposed and follows loadProfiles", "[dbtool-gui][AppController][managed-backup]")
{
    auto const fixturePath = FixturesDir() / "dbtool.yml";
    DbtoolGui::AppController controller;
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
