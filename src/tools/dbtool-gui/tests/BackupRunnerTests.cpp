// SPDX-License-Identifier: Apache-2.0
//
// Headless tests for BackupRunner's bookkeeping: the shared progress surface,
// the structured outcome, and the verdict on a run that finishes without
// throwing but lost tables. A fake archive operation drives the runner, so no
// database or ODBC driver is involved.

#include "../BackupRunner.hpp"

#include <Lightweight/SqlBackup.hpp>
#include <Lightweight/SqlConnection.hpp>
#include <Lightweight/SqlStatement.hpp>

#include <catch2/catch_test_macros.hpp>

#include <future>
#include <optional>
#include <stdexcept>
#include <string>

#include <QtCore/QTemporaryDir>
#include <QtCore/QVariantList>
#include <QtTest/QSignalSpy>

using DbtoolGui::BackupRunner;
using DbtoolGui::BackupTableListModel;
using Lightweight::SqlBackup::Progress;
using Lightweight::SqlBackup::ProgressManager;

namespace
{

/// Spins the event loop until `spy` has at least one emission or `ms` ran out.
[[nodiscard]] bool WaitFor(QSignalSpy& spy, int ms = 10000)
{
    return spy.count() > 0 || spy.wait(ms);
}

/// A table that was copied: one `Finished` event carrying its row count.
void Copied(ProgressManager& progress, std::string table, std::size_t rows)
{
    progress.Update(Progress {
        .state = Progress::State::Started, .tableName = table, .currentRows = 0, .totalRows = std::nullopt, .message = {} });
    progress.Update(Progress { .state = Progress::State::Finished,
                               .tableName = table,
                               .currentRows = rows,
                               .totalRows = std::nullopt,
                               .message = {} });
}

/// A table that failed: one `Error` event carrying the reason.
void Failed(ProgressManager& progress, std::string table, std::string reason)
{
    progress.Update(Progress { .state = Progress::State::Error,
                               .tableName = table,
                               .currentRows = 0,
                               .totalRows = std::nullopt,
                               .message = std::move(reason) });
}

[[nodiscard]] QString Text(QVariantMap const& map, char const* key)
{
    return map.value(QString::fromLatin1(key)).toString();
}

} // namespace

TEST_CASE("BackupRunner reports a clean run with its tables, rows and duration", "[dbtool-gui][backup-runner]")
{
    BackupRunner runner;
    runner.setBackupOperation([](std::filesystem::path const&, std::string const&, ProgressManager& progress) {
        progress.SetTotalTables(3);
        Copied(progress, "orders", 100);
        Copied(progress, "invoices", 250);
        Copied(progress, "customers", 50);
    });
    QSignalSpy done(&runner, &BackupRunner::finished);

    runner.runBackup(QStringLiteral("archive.zip"));
    REQUIRE(WaitFor(done));

    CHECK(done.first().at(0).toBool());
    CHECK(runner.phase() == BackupRunner::Phase::Idle);
    CHECK(runner.operation() == BackupRunner::Operation::Backup);

    auto const& result = runner.lastResult();
    CHECK(Text(result, "operation") == "backup");
    CHECK(result.value("ok").toBool());
    CHECK(Text(result, "archive") == "archive.zip");
    CHECK(result.value("tables").toInt() == 3);
    CHECK(result.value("failedCount").toInt() == 0);
    CHECK(result.value("failedTables").toList().isEmpty());
    CHECK(result.value("rows").toULongLong() == 400);
    CHECK(result.value("durationMs").toLongLong() >= 0);
    CHECK(result.value("finishedAt").toDateTime().isValid());
    CHECK(Text(result, "summary").contains("Backup written"));

    CHECK(runner.tablesDone() == 3);
    CHECK(runner.tablesTotal() == 3);
    CHECK(runner.rowsDone() == 400);
}

TEST_CASE("BackupRunner treats a restore that lost tables as failed and lists them", "[dbtool-gui][backup-runner]")
{
    // The library does not throw for a failing table, it only reports it: a
    // runner that looked at exceptions alone would call this a success.
    BackupRunner runner;
    runner.setRestoreOperation([](std::filesystem::path const&, std::string const&, ProgressManager& progress) {
        progress.SetTotalTables(4);
        Copied(progress, "orders", 10);
        Failed(progress, "KUNDE", "FOREIGN KEY constraint failed (547)");
        Failed(progress, "LAB_ITEM", "filegroup PRIMARY is full");
        Copied(progress, "invoices", 20);
    });
    QSignalSpy done(&runner, &BackupRunner::finished);

    runner.runRestore(QStringLiteral("DK_PROD_LATEST.zip"));
    REQUIRE(WaitFor(done));

    CHECK_FALSE(done.first().at(0).toBool());
    auto const& result = runner.lastResult();
    CHECK(Text(result, "operation") == "restore");
    CHECK_FALSE(result.value("ok").toBool());
    CHECK(Text(result, "error").isEmpty()); // it returned normally
    CHECK(result.value("tables").toInt() == 4);
    CHECK(result.value("failedCount").toInt() == 2);
    CHECK(result.value("rows").toULongLong() == 30);

    auto const failed = result.value("failedTables").toList();
    REQUIRE(failed.size() == 2);
    CHECK(failed.at(0).toMap().value("table").toString() == "KUNDE");
    CHECK(failed.at(0).toMap().value("reason").toString().contains("547"));
    CHECK(failed.at(1).toMap().value("table").toString() == "LAB_ITEM");

    auto const summary = Text(result, "summary");
    CHECK(summary.contains("Restore incomplete"));
    CHECK(summary.contains("2 of 4"));
    CHECK(summary.contains("incomplete")); // the database may be left partial
    CHECK(done.first().at(1).toString() == summary);
}

TEST_CASE("BackupRunner words a partial backup differently from a partial restore", "[dbtool-gui][backup-runner]")
{
    BackupRunner runner;
    runner.setBackupOperation([](std::filesystem::path const&, std::string const&, ProgressManager& progress) {
        progress.SetTotalTables(2);
        Copied(progress, "orders", 1);
        Failed(progress, "KUNDE", "locked");
    });
    QSignalSpy done(&runner, &BackupRunner::finished);

    runner.runBackup(QStringLiteral("archive.zip"));
    REQUIRE(WaitFor(done));

    CHECK_FALSE(done.first().at(0).toBool());
    auto const summary = Text(runner.lastResult(), "summary");
    CHECK(summary.contains("Backup incomplete"));
    CHECK(summary.contains("1 of 2"));
}

TEST_CASE("BackupRunner reports an aborted run with the exception text", "[dbtool-gui][backup-runner]")
{
    BackupRunner runner;
    runner.setRestoreOperation([](std::filesystem::path const&, std::string const&, ProgressManager&) {
        throw std::runtime_error("cannot open archive");
    });
    QSignalSpy done(&runner, &BackupRunner::finished);

    runner.runRestore(QStringLiteral("missing.zip"));
    REQUIRE(WaitFor(done));

    CHECK_FALSE(done.first().at(0).toBool());
    CHECK(done.first().at(1).toString() == "cannot open archive");
    CHECK(Text(runner.lastResult(), "error") == "cannot open archive");
    CHECK(runner.phase() == BackupRunner::Phase::Idle);
}

TEST_CASE("BackupRunner counts every failure but keeps the listed ones bounded", "[dbtool-gui][backup-runner]")
{
    BackupRunner runner;
    runner.setRestoreOperation([](std::filesystem::path const&, std::string const&, ProgressManager& progress) {
        progress.SetTotalTables(200);
        for (int i = 0; i < 120; ++i)
            Failed(progress, "T" + std::to_string(i), "boom");
    });
    QSignalSpy done(&runner, &BackupRunner::finished);

    runner.runRestore(QStringLiteral("big.zip"));
    REQUIRE(WaitFor(done));

    auto const& result = runner.lastResult();
    CHECK(result.value("failedCount").toInt() == 120);
    CHECK(result.value("failedTables").toList().size() == 50);
    CHECK(result.value("tables").toInt() == 200);
}

TEST_CASE("BackupRunner exposes live progress and clears the last outcome when a run starts", "[dbtool-gui][backup-runner]")
{
    BackupRunner runner;
    runner.setBackupOperation([](std::filesystem::path const&, std::string const&, ProgressManager& progress) {
        progress.SetTotalTables(1);
        Copied(progress, "orders", 5);
    });
    QSignalSpy firstDone(&runner, &BackupRunner::finished);
    runner.runBackup(QStringLiteral("a.zip"));
    REQUIRE(WaitFor(firstDone));
    REQUIRE_FALSE(runner.lastResult().isEmpty());

    // The second run holds after its first table, so the in-flight state can be inspected.
    std::promise<void> release;
    auto gate = release.get_future().share();
    runner.setRestoreOperation([gate](std::filesystem::path const&, std::string const&, ProgressManager& progress) {
        progress.SetTotalTables(4);
        Copied(progress, "orders", 7);
        gate.wait();
        Copied(progress, "invoices", 9);
    });
    QSignalSpy progress(&runner, &BackupRunner::progressChanged);
    QSignalSpy secondDone(&runner, &BackupRunner::finished);

    runner.runRestore(QStringLiteral("b.zip"));

    // Starting a run discards the previous outcome and resets the counters at once.
    CHECK(runner.phase() == BackupRunner::Phase::Running);
    CHECK(runner.operation() == BackupRunner::Operation::Restore);
    CHECK(runner.lastResult().isEmpty());
    CHECK(runner.tablesDone() == 0);
    CHECK(runner.startedAt().isValid());

    while (runner.tablesDone() < 1)
        REQUIRE(progress.wait(10000));
    CHECK(runner.currentTable() == "orders");
    CHECK(runner.tablesTotal() == 4);
    CHECK(runner.rowsDone() == 7);
    CHECK(runner.phase() == BackupRunner::Phase::Running);

    release.set_value();
    REQUIRE(WaitFor(secondDone));
    CHECK(secondDone.first().at(0).toBool());
    CHECK(runner.tablesDone() == 2);
    CHECK(runner.rowsDone() == 16);
    CHECK(Text(runner.lastResult(), "operation") == "restore");
}

TEST_CASE("BackupRunner without an injected operation can still be constructed idle", "[dbtool-gui][backup-runner]")
{
    BackupRunner runner;
    CHECK(runner.phase() == BackupRunner::Phase::Idle);
    CHECK(runner.operation() == BackupRunner::Operation::None);
    CHECK(runner.lastResult().isEmpty());
    CHECK(runner.tablesTotal() == 0);

    // Clearing an injected operation restores the real one (not a null call).
    runner.setBackupOperation({});
    runner.setRestoreOperation({});
    CHECK(runner.phase() == BackupRunner::Phase::Idle);
}

// What SqlBackup really emits around the tables: a "Scanning schema" phase
// that reports Finished (with a row figure) before the table count is
// announced, and a final Finished carrying no table name. None of that is
// table progress; counting it inflated tables and rows.
TEST_CASE("BackupRunner ignores the library's phase events when tallying tables and rows", "[dbtool-gui][backup-runner]")
{
    BackupRunner runner;
    runner.setBackupOperation([](std::filesystem::path const&, std::string const&, ProgressManager& progress) {
        progress.Update(Progress { .state = Progress::State::InProgress,
                                   .tableName = "Scanning schema",
                                   .currentRows = 1,
                                   .totalRows = 2,
                                   .message = "Scanning table orders" });
        progress.Update(Progress { .state = Progress::State::Finished,
                                   .tableName = "Scanning schema",
                                   .currentRows = 2,
                                   .totalRows = 2,
                                   .message = {} });
        progress.SetTotalTables(2);
        Copied(progress, "orders", 10);
        Copied(progress, "invoices", 20);
        progress.Update(Progress { .state = Progress::State::Finished,
                                   .tableName = "",
                                   .currentRows = 0,
                                   .totalRows = std::nullopt,
                                   .message = "Restore complete" });
    });
    QSignalSpy done(&runner, &BackupRunner::finished);

    runner.runBackup(QStringLiteral("a.zip"));
    REQUIRE(WaitFor(done));

    CHECK(done.first().at(0).toBool());
    CHECK(runner.lastResult().value("tables").toInt() == 2);
    CHECK(runner.lastResult().value("rows").toULongLong() == 30);
    CHECK(runner.tablesDone() == 2);
    CHECK(runner.tablesTotal() == 2);
    CHECK(runner.currentTable() != "Scanning schema");
}

TEST_CASE("BackupRunner counts a table that fails repeatedly once", "[dbtool-gui][backup-runner]")
{
    // One Error per failed chunk: three events, one table.
    BackupRunner runner;
    runner.setRestoreOperation([](std::filesystem::path const&, std::string const&, ProgressManager& progress) {
        progress.SetTotalTables(3);
        Copied(progress, "orders", 1);
        Failed(progress, "KUNDE", "chunk 1 failed");
        Failed(progress, "KUNDE", "chunk 2 failed");
        Failed(progress, "KUNDE", "chunk 3 failed");
        Copied(progress, "invoices", 1);
    });
    QSignalSpy done(&runner, &BackupRunner::finished);

    runner.runRestore(QStringLiteral("a.zip"));
    REQUIRE(WaitFor(done));

    CHECK_FALSE(done.first().at(0).toBool());
    CHECK(runner.lastResult().value("failedCount").toInt() == 1);
    CHECK(runner.lastResult().value("failedTables").toList().size() == 1);
    CHECK(Text(runner.lastResult(), "summary").contains("1 of 3"));
}

TEST_CASE("BackupRunner fails a run on an error that names no table", "[dbtool-gui][backup-runner]")
{
    // "Restore aborted: No tables could be created" returns normally and names
    // no table. It must still be a failed run, and be shown rather than lost.
    BackupRunner runner;
    runner.setRestoreOperation([](std::filesystem::path const&, std::string const&, ProgressManager& progress) {
        Failed(progress, "", "Restore aborted: No tables could be created");
    });
    QSignalSpy done(&runner, &BackupRunner::finished);

    runner.runRestore(QStringLiteral("a.zip"));
    REQUIRE(WaitFor(done));

    CHECK_FALSE(done.first().at(0).toBool());
    auto const& result = runner.lastResult();
    CHECK_FALSE(result.value("ok").toBool());
    CHECK(result.value("failedCount").toInt() == 0);
    CHECK(result.value("otherErrors").toInt() == 1);
    auto const failures = result.value("failedTables").toList();
    REQUIRE(failures.size() == 1);
    CHECK(failures.first().toMap().value("table").toString().isEmpty());
    CHECK(failures.first().toMap().value("reason").toString().contains("No tables could be created"));
    CHECK(Text(result, "summary").contains("Restore finished with 1 error"));
}

// The detail panel on the Backups page reads `tables`, so an ad-hoc run must fill the same
// per-table model a managed run does: its tables and their states, nothing else.
TEST_CASE("BackupRunner fills the per-table model the detail panel shows", "[dbtool-gui][backup-runner]")
{
    BackupRunner runner;
    runner.setRestoreOperation([](std::filesystem::path const&, std::string const&, ProgressManager& progress) {
        progress.Update(Progress { .state = Progress::State::Finished,
                                   .tableName = "Scanning schema",
                                   .currentRows = 2,
                                   .totalRows = 2,
                                   .message = {} });
        progress.SetTotalTables(3);
        Copied(progress, "orders", 10);
        Failed(progress, "KUNDE", "FOREIGN KEY constraint failed (547)");
        Copied(progress, "invoices", 20);
        Failed(progress, "", "Failed to connect for index restoration");
        progress.Update(Progress { .state = Progress::State::Finished,
                                   .tableName = "",
                                   .currentRows = 0,
                                   .totalRows = std::nullopt,
                                   .message = "Restore complete" });
    });
    QSignalSpy done(&runner, &BackupRunner::finished);

    runner.runRestore(QStringLiteral("a.zip"));
    REQUIRE(WaitFor(done));

    auto* tables = runner.tables();
    REQUIRE(tables != nullptr);
    CHECK(tables->totalCount() == 4); // 3 announced + the run-level error row
    CHECK(tables->doneCount() == 2);
    CHECK(tables->errorCount() == 2);
    // No row for the schema-scan phase or the final run-level event.
    bool sawPhaseRow = false;
    bool sawGeneralRow = false;
    for (int row = 0; row < tables->rowCount(); ++row)
    {
        auto const name = tables->data(tables->index(row, 0), BackupTableListModel::TableNameRole).toString();
        sawPhaseRow = sawPhaseRow || name == "Scanning schema" || name.isEmpty();
        sawGeneralRow = sawGeneralRow || name == "(general)";
    }
    CHECK_FALSE(sawPhaseRow);
    CHECK(sawGeneralRow);
}

// SqlBackup reports a failure of the run itself (archive creation, a crashed worker) under the
// placeholder table name "Unknown". That is not a table: it must not become a table row or count as
// a failed table, but it must still fail the run.
TEST_CASE("BackupRunner treats the library's \"Unknown\" table name as a run-level error", "[dbtool-gui][backup-runner]")
{
    BackupRunner runner;
    runner.setBackupOperation([](std::filesystem::path const&, std::string const&, ProgressManager& progress) {
        progress.SetTotalTables(2);
        Copied(progress, "orders", 1);
        Copied(progress, "invoices", 1);
        Failed(progress, "Unknown", "Backup failed: Unknown error");
    });
    QSignalSpy done(&runner, &BackupRunner::finished);

    runner.runBackup(QStringLiteral("a.zip"));
    REQUIRE(WaitFor(done));

    CHECK_FALSE(done.first().at(0).toBool());
    auto const& result = runner.lastResult();
    CHECK(result.value("failedCount").toInt() == 0);
    CHECK(result.value("otherErrors").toInt() == 1);
    CHECK(result.value("tables").toInt() == 2); // not 3: "Unknown" is not a table
    auto const failures = result.value("failedTables").toList();
    REQUIRE(failures.size() == 1);
    CHECK(failures.first().toMap().value("table").toString().isEmpty());
    CHECK(failures.first().toMap().value("reason").toString().contains("Unknown error"));
    CHECK(Text(result, "summary").contains("Backup finished with 1 error"));

    auto* tables = runner.tables();
    bool sawUnknownRow = false;
    for (int row = 0; row < tables->rowCount(); ++row)
        sawUnknownRow = sawUnknownRow
                        || tables->data(tables->index(row, 0), BackupTableListModel::TableNameRole).toString() == "Unknown";
    CHECK_FALSE(sawUnknownRow);
}

TEST_CASE("BackupRunner starts every run with an empty per-table model", "[dbtool-gui][backup-runner]")
{
    BackupRunner runner;
    runner.setBackupOperation([](std::filesystem::path const&, std::string const&, ProgressManager& progress) {
        progress.SetTotalTables(1);
        Copied(progress, "orders", 1);
    });
    QSignalSpy first(&runner, &BackupRunner::finished);
    runner.runBackup(QStringLiteral("a.zip"));
    REQUIRE(WaitFor(first));
    REQUIRE(runner.tables()->totalCount() == 1);

    std::promise<void> release;
    auto gate = release.get_future().share();
    runner.setBackupOperation([gate](std::filesystem::path const&, std::string const&, ProgressManager&) { gate.wait(); });
    QSignalSpy second(&runner, &BackupRunner::finished);
    runner.runBackup(QStringLiteral("b.zip"));

    CHECK(runner.tables()->totalCount() == 0); // the previous run's tables are gone at once
    release.set_value();
    REQUIRE(WaitFor(second));
}

TEST_CASE("BackupRunner records the database each run was against", "[dbtool-gui][backup-runner]")
{
    BackupRunner runner;
    runner.setBackupOperation([](std::filesystem::path const&, std::string const&, ProgressManager&) {});
    QSignalSpy done(&runner, &BackupRunner::finished);

    runner.setTargetLabel(QStringLiteral("staging-postgres"));
    runner.runBackup(QStringLiteral("a.zip"));
    REQUIRE(WaitFor(done));
    CHECK(Text(runner.lastResult(), "target") == "staging-postgres");

    // Connecting elsewhere afterwards does not rewrite what the finished run was against.
    runner.setTargetLabel(QStringLiteral("other-db"));
    CHECK(Text(runner.lastResult(), "target") == "staging-postgres");
}

// The tests above drive the runner with fake operations. This one runs the
// real SqlBackup against SQLite (the same ODBC baseline as the rest of the
// dbtool-gui tests), so what the library actually reports - the announced
// table count, per-table Finished events with row counts - is what the tally
// is checked against.
TEST_CASE("BackupRunner backs up and restores a real SQLite database", "[dbtool-gui][backup-runner][sqlite]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    auto const source = (dir.path() + "/source.db").toStdString();
    auto const target = (dir.path() + "/target.db").toStdString();
    auto const archive = dir.path() + "/archive.zip";

    {
        auto connection =
            Lightweight::SqlConnection { Lightweight::SqlConnectionString { "DRIVER=SQLite3;Database=" + source } };
        auto stmt = Lightweight::SqlStatement { connection };
        (void) stmt.ExecuteDirect("CREATE TABLE items (id INTEGER PRIMARY KEY, label VARCHAR(32) NOT NULL)");
        (void) stmt.ExecuteDirect("INSERT INTO items (id, label) VALUES (1, 'one'), (2, 'two'), (3, 'three')");
        (void) stmt.ExecuteDirect("CREATE TABLE notes (id INTEGER PRIMARY KEY, body VARCHAR(32) NOT NULL)");
        (void) stmt.ExecuteDirect("INSERT INTO notes (id, body) VALUES (1, 'a'), (2, 'b')");
    }

    BackupRunner runner;

    runner.setConnectionString(QString::fromStdString("DRIVER=SQLite3;Database=" + source));
    QSignalSpy backupDone(&runner, &BackupRunner::finished);
    runner.runBackup(archive);
    REQUIRE(WaitFor(backupDone, 30000));
    INFO(Text(runner.lastResult(), "summary").toStdString());
    CHECK(backupDone.first().at(0).toBool());
    CHECK(Text(runner.lastResult(), "operation") == "backup");
    // The library decides what counts as a table (its schema scan may list
    // more than the two seeded here), so assert the invariants the GUI relies
    // on rather than an exact count: our tables are covered, the denominator
    // is fixed and fully consumed, and the rows add up.
    auto const tables = runner.lastResult().value("tables").toInt();
    CHECK(tables >= 2);
    CHECK(runner.lastResult().value("rows").toULongLong() == 5);
    CHECK(runner.lastResult().value("failedCount").toInt() == 0);
    CHECK(runner.tablesTotal() == tables);
    CHECK(runner.tablesDone() == tables);

    runner.setConnectionString(QString::fromStdString("DRIVER=SQLite3;Database=" + target));
    QSignalSpy restoreDone(&runner, &BackupRunner::finished);
    runner.runRestore(archive);
    REQUIRE(WaitFor(restoreDone, 30000));
    INFO(Text(runner.lastResult(), "summary").toStdString());
    CHECK(restoreDone.first().at(0).toBool());
    CHECK(Text(runner.lastResult(), "operation") == "restore");
    CHECK(runner.lastResult().value("tables").toInt() == tables);
    CHECK(runner.lastResult().value("rows").toULongLong() == 5);
    CHECK(runner.lastResult().value("failedCount").toInt() == 0);
}

TEST_CASE("BackupRunner reports restoring a missing archive as a failed run", "[dbtool-gui][backup-runner][sqlite]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    BackupRunner runner;
    runner.setConnectionString(QString::fromStdString("DRIVER=SQLite3;Database=" + (dir.path() + "/t.db").toStdString()));
    QSignalSpy done(&runner, &BackupRunner::finished);

    runner.runRestore(dir.path() + "/does-not-exist.zip");
    REQUIRE(WaitFor(done, 30000));

    CHECK_FALSE(done.first().at(0).toBool());
    CHECK_FALSE(Text(runner.lastResult(), "summary").isEmpty());
    CHECK(runner.phase() == BackupRunner::Phase::Idle);
}
