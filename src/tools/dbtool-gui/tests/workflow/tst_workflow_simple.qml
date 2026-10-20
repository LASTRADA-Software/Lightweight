// SPDX-License-Identifier: Apache-2.0
//
// The Simple view's run flow, driven through its controls: apply every pending migration, back up
// first through the file dialog, cancel between pre-migrate units, a failed run and its retry, the
// failure report (copy, save), and Done. The file dialogs are Qt's non-native fallback dialogs,
// which the offscreen platform always uses.

import QtQuick
import QtTest
import Lightweight.Migrations
import DbtoolGuiWorkflow

WorkflowTestCase {
    name: "WorkflowSimple"

    /// The connection string of profile `p`'s database.
    property string cs: ""

    // The target is set once the settings are written: reaching AppController creates it.
    SignalSpy {
        id: finishedSpy
        signalName: "finished"
    }

    function initTestCase() {
        compare(Qt.platform.pluginName, "offscreen", "the file dialogs must be Qt's non-native fallback dialogs")
        compare(WorkflowFixture.qmlWarnings(), [], "QML files must load without warnings")
    }

    /// Writes profile `p` on a new SQLite database with the recorder and workflow plugins, opens the
    /// Simple view and connects.
    function openConnected(pluginKeys) {
        cs = WorkflowFixture.newSqliteDatabase()
        const plugins = WorkflowFixture.newPluginsDir(pluginKeys || ["recorder", "workflow"])
        const path = WorkflowFixture.writeProfileFile([{ name: "p", connectionString: cs, pluginsDir: plugins }], "p")
        WorkflowFixture.setSetting("config/profileStorePath", path)
        WorkflowFixture.setSetting("ui/viewMode", "simple")
        WorkflowFixture.setSetting("backup/folder", WorkflowFixture.tempPath("backups"))
        openMain()
        tryCompare(AppController, "busy", false, 15000)
        clickControl("connectButton")
        tryCompare(AppController, "connected", true, 15000)
        tryCompare(AppController, "busy", false, 15000)
        finishedSpy.target = AppController.runner
    }

    function count(sql) {
        return outsideWatchdog("query the database", () => WorkflowFixture.queryScalar(cs, sql))
    }

    /// @return The rows of `schema_migrations`; 0 while the table does not exist yet.
    function appliedMigrations() {
        if (count("SELECT COUNT(*) FROM sqlite_master WHERE type = 'table' AND name = 'schema_migrations'") === 0)
            return 0
        return count("SELECT COUNT(*) FROM schema_migrations")
    }

    /// Clicks Run and waits for the run to end.
    function runToTheEnd() {
        const before = finishedSpy.count
        clickControl("runButton")
        tryVerify(() => finishedSpy.count > before, 60000, "the run must finish")
        tryCompare(AppController, "busy", false, 15000)
    }

    /// A `file:` URL of the local path @p path.
    function fileUrl(path) {
        return WorkflowFixture.localFileUrl(path)
    }

    /// Picks the file @p path in the open fallback dialog @p dialog as a user does: opens its folder and types the file name.
    /// Typing into `fileNameTextField` depends on the structure of Qt's fallback dialog; recheck it on a Qt upgrade.
    function chooseFile(dialog, path) {
        const separator = path.lastIndexOf("/")
        step("choose " + path)
        dialog.currentFolder = fileUrl(path.substring(0, separator))
        typeText("fileNameTextField", path.substring(separator + 1))
    }

    /// Opens the Failure card with a failed run: the recorder's pre-migrate step throws.
    function failARun() {
        WorkflowFixture.setEnv("LIGHTWEIGHT_TEST_PRE_MIGRATE_LOG", WorkflowFixture.tempPath("pre-migrate.log"))
        WorkflowFixture.setEnv("LIGHTWEIGHT_TEST_PRE_MIGRATE_FAIL", "1")
        openConnected()
        runToTheEnd()
        tryCompare(findRequired("failureBanner"), "title", "Migration run failed", 5000)
    }

    function test_run_applies_every_pending_migration() {
        openConnected()
        runToTheEnd()

        tryCompare(findRequired("runStep"), "title", "Done", 5000)
        tryCompare(findRequired("successTitle"), "text", "Database is up to date", 5000)
        compare(appliedMigrations(), 3)
    }

    function test_back_up_first_chain_writes_the_archive_before_migrating() {
        openConnected()
        clickControl("backupFirstCheckBox")
        compare(findRequired("backupFirstCheckBox").checked, true)
        clickControl("runButton")

        const dialog = findRequired("backupFileDialog")
        tryVerify(() => dialog.visible, 5000, "the backup file dialog must open")
        const archive = WorkflowFixture.tempPath("first.zip")
        chooseFile(dialog, archive)
        step("accept the backup file dialog")
        dialog.accept()

        tryVerify(() => finishedSpy.count > 0, 60000, "the migrations must run after the backup")
        tryCompare(AppController, "busy", false, 15000)
        compare(finishedSpy.signalArguments[0][0], true)
        verify(WorkflowFixture.fileExists(archive), "the archive must exist")
        compare(count("SELECT COUNT(*) FROM schema_backups WHERE kind = 'backup'"), 1)
        compare(appliedMigrations(), 3)
        tryCompare(findRequired("runStep"), "title", "Done", 5000)
    }

    function test_a_refused_second_trigger_keeps_the_first_archive() {
        openConnected()
        clickControl("backupFirstCheckBox")
        clickControl("runButton")
        const dialog = findRequired("backupFileDialog")
        tryVerify(() => dialog.visible, 5000, "the backup file dialog must open")
        const first = WorkflowFixture.tempPath("first.zip")
        const second = WorkflowFixture.tempPath("second.zip")
        chooseFile(dialog, first)
        dialog.accept()
        verify(AppController.chainedBackupPending, "the chain must start")

        // The view offers no second trigger while the backup runs; a second acceptance, in the same turn
        // so that the backup is still in flight, with another file.
        dialog.selectedFile = fileUrl(second)
        compare(dialog.selectedFile, fileUrl(second))
        dialog.accepted()

        tryVerify(() => finishedSpy.count > 0, 60000, "the first chain must still apply")
        tryCompare(AppController, "busy", false, 15000)
        compare(finishedSpy.signalArguments[0][0], true, "the first chain's run must succeed")
        compare(appliedMigrations(), 3)
        verify(AppController.logLines.text().indexOf("another operation is in progress") >= 0,
               "the second trigger must be refused with a log line")
        compare(findRequired("simpleView").lastBackupFile.split("\\").join("/"), first)
        verify(WorkflowFixture.fileExists(first))
        verify(!WorkflowFixture.fileExists(second), "the refused chain wrote nothing")
    }

    function test_cancel_stops_between_units() {
        WorkflowFixture.setEnv("LIGHTWEIGHT_TEST_LONG_STEP_MS", "4000")
        openConnected()
        clickControl("runButton")
        tryVerify(() => findRequired("cancelButton").visible && findRequired("cancelButton").enabled, 10000,
                  "the run must be in flight")
        clickControl("cancelButton")

        tryVerify(() => finishedSpy.count > 0, 30000, "the run must end")
        tryCompare(AppController, "busy", false, 15000)
        compare(finishedSpy.signalArguments[0][1], "Cancelled during the pre-migrate steps; no migration was applied.")
        compare(appliedMigrations(), 0)
    }

    function test_failure_outcome_and_retry() {
        failARun()
        compare(appliedMigrations(), 0)

        WorkflowFixture.unsetEnv("LIGHTWEIGHT_TEST_PRE_MIGRATE_FAIL")
        const before = finishedSpy.count
        clickControl("retryButton")
        tryVerify(() => finishedSpy.count > before, 60000, "the retry must finish")
        tryCompare(AppController, "busy", false, 15000)
        tryCompare(findRequired("successTitle"), "text", "Database is up to date", 5000)
        compare(appliedMigrations(), 3)
    }

    function test_copy_report_puts_the_report_on_the_clipboard() {
        failARun()
        clickControl("copyReportButton")
        verify(WorkflowFixture.clipboardText().startsWith("dbtool-gui - failure report"),
               "the clipboard must hold the report:\n" + WorkflowFixture.clipboardText())
    }

    function test_save_report_writes_the_file() {
        failARun()
        clickControl("saveReportButton")
        const dialog = findRequired("saveReportDialog")
        tryVerify(() => dialog.visible, 5000, "the save dialog must open")
        const report = WorkflowFixture.tempPath("report.txt")
        chooseFile(dialog, report)
        step("accept the save dialog")
        dialog.accept()

        const toast = findRequired("toastLabel")
        tryVerify(() => toast.text.startsWith("Saved report to "), 5000, "the save must be confirmed: " + toast.text)
        verify(WorkflowFixture.fileExists(report))
        verify(WorkflowFixture.fileText(report).startsWith("dbtool-gui - failure report"))
    }

    function test_save_report_to_an_unwritable_path_says_why() {
        failARun()
        clickControl("saveReportButton")
        const dialog = findRequired("saveReportDialog")
        tryVerify(() => dialog.visible, 5000, "the save dialog must open")
        const unwritable = WorkflowFixture.tempPath("no-such-folder/report.txt")
        dialog.selectedFile = fileUrl(unwritable)
        dialog.accepted()

        const toast = findRequired("toastLabel")
        tryVerify(() => toast.text.startsWith("Could not save report: "), 5000, "the failure must be shown: " + toast.text)
        verify(!WorkflowFixture.fileExists(unwritable))
    }

    function test_a_migration_that_fails_partway_shows_the_failure() {
        openConnected(["failing"])
        runToTheEnd()

        tryCompare(findRequired("failureBanner"), "title", "Migration run failed", 5000)
        verify(findRequired("failureBanner").text.indexOf("Applied 0 of 1 migrations before failing.") >= 0,
               findRequired("failureBanner").text)
        compare(findRequired("runStep").title, "Run failed")
        compare(appliedMigrations(), 0)
        clickControl("copyReportButton")
        verify(WorkflowFixture.clipboardText().indexOf("fixture migration failed on purpose") >= 0)
        verify(findRequired("retryButton").visible)
    }

    function test_done_returns_to_review() {
        openConnected()
        runToTheEnd()
        clickControl("doneButton")

        tryCompare(findRequired("successTitle"), "visible", false, 5000)
        compare(findRequired("doneButton").visible, false)
    }
}
