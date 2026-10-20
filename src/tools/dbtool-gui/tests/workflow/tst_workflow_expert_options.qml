// SPDX-License-Identifier: Apache-2.0
//
// The Expert view's options do what they say. "Acquire lock" decides whether Apply and Rollback take
// the migration lock, and a dry run never takes it. "Back up first" backs up into the backup folder
// before Apply and applies only after a successful backup. The Dry run option says that it changes
// nothing.

import QtQuick
import QtTest
import Lightweight.Migrations
import DbtoolGuiWorkflow

WorkflowTestCase {
    name: "WorkflowExpertOptions"

    /// The connection string of profile `p`'s database.
    property string cs: ""

    // Targets are set once the settings are written: reaching AppController creates it.
    SignalSpy {
        id: finishedSpy
        signalName: "finished"
    }

    SignalSpy {
        id: chainedBackupFailedSpy
        signalName: "chainedBackupFailed"
    }

    SignalSpy {
        id: chainedApplyFailedSpy
        signalName: "chainedApplyFailed"
    }

    /// Writes profile `p` on a new SQLite database with the workflow plugin, using @p backupFolder,
    /// opens the Expert view and connects; runs wait at most 200 ms for the migration lock.
    function openConnected(backupFolder, viewMode) {
        cs = WorkflowFixture.newSqliteDatabase()
        const plugins = WorkflowFixture.newPluginsDir(["workflow"])
        const path = WorkflowFixture.writeProfileFile([{ name: "p", connectionString: cs, pluginsDir: plugins }], "p")
        WorkflowFixture.setSetting("config/profileStorePath", path)
        WorkflowFixture.setSetting("ui/viewMode", viewMode || "expert")
        WorkflowFixture.setSetting("backup/folder", backupFolder || WorkflowFixture.tempPath("backups"))
        openMain()
        tryCompare(AppController, "busy", false, 15000)
        clickControl("connectButton")
        tryCompare(AppController, "connected", true, 15000)
        tryCompare(AppController, "busy", false, 15000)
        WorkflowFixture.setLockTimeoutMs(200)
        finishedSpy.target = AppController.runner
        chainedBackupFailedSpy.target = AppController
        chainedApplyFailedSpy.target = AppController
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

    function holdLock() {
        outsideWatchdog("hold the migration lock", () => WorkflowFixture.holdMigrationLock(cs))
    }

    /// Clicks the primary button and waits for the run it starts to finish; returns `[ok, summary]`.
    function runPrimaryAction() {
        const before = finishedSpy.count
        clickControl("primaryActionButton")
        tryVerify(() => finishedSpy.count > before, 60000, "the run must finish")
        tryCompare(AppController, "busy", false, 15000)
        return finishedSpy.signalArguments[finishedSpy.count - 1]
    }

    function applyEverything() {
        clickControl("optionSwitch_dryRun")
        compare(runPrimaryAction()[0], true)
        compare(appliedMigrations(), 3)
    }

    /// Clicks Rollback and waits for the run to finish; returns `[ok, summary]`.
    function rollBack() {
        const before = finishedSpy.count
        clickControl("rollbackButton")
        tryVerify(() => finishedSpy.count > before, 30000, "the rollback must finish")
        tryCompare(AppController, "busy", false, 15000)
        return finishedSpy.signalArguments[finishedSpy.count - 1]
    }

    function test_apply_without_acquire_lock_succeeds_while_another_migrator_holds_the_lock() {
        openConnected()
        holdLock()
        clickControl("optionSwitch_dryRun")
        clickControl("optionSwitch_acquireLock")
        compare(findRequired("optionSwitch_acquireLock").checked, false)

        compare(runPrimaryAction()[0], true)
        compare(appliedMigrations(), 3)
        verify(AppController.logLines.text().indexOf("Running without the migration lock") >= 0)
    }

    function test_apply_with_acquire_lock_refuses_while_another_migrator_holds_the_lock() {
        openConnected()
        holdLock()
        clickControl("optionSwitch_dryRun")
        compare(findRequired("optionSwitch_acquireLock").checked, true)

        const outcome = runPrimaryAction()
        compare(outcome[0], false)
        compare(outcome[1], "Could not acquire the migration lock.")
        compare(appliedMigrations(), 0)
    }

    function test_rollback_without_acquire_lock_ignores_a_held_lock() {
        openConnected()
        // Switched before the run: afterwards its progress rows widen the pane past the window's edge.
        clickControl("optionSwitch_acquireLock")
        applyEverything()
        holdLock()
        chooseRelease("1.0.0")

        compare(rollBack()[0], true)
        compare(appliedMigrations(), 2)
    }

    function test_rollback_with_acquire_lock_refuses_while_the_lock_is_held() {
        openConnected()
        applyEverything()
        holdLock()
        chooseRelease("1.0.0")

        const outcome = rollBack()
        compare(outcome[0], false)
        compare(outcome[1], "Could not acquire the migration lock.")
        compare(appliedMigrations(), 3)
    }

    function test_dry_run_never_takes_the_lock() {
        openConnected()
        holdLock()
        compare(findRequired("optionSwitch_dryRun").checked, true)
        compare(findRequired("optionSwitch_acquireLock").checked, true)

        const outcome = runPrimaryAction()
        compare(outcome[0], true)
        compare(outcome[1], "Dry-run produced 3 migrations.")
        compare(appliedMigrations(), 0)
    }

    function test_the_simple_view_takes_the_lock_whatever_the_expert_switch_says() {
        openConnected()
        holdLock()
        clickControl("optionSwitch_acquireLock")
        AppController.setViewMode("simple")

        step("apply from the Simple view")
        const before = finishedSpy.count
        AppController.runner.applyUpTo("")
        tryVerify(() => finishedSpy.count > before, 30000, "the run must finish")
        compare(finishedSpy.signalArguments[finishedSpy.count - 1][1], "Could not acquire the migration lock.")
        compare(appliedMigrations(), 0)
    }

    function test_back_up_first_backs_up_then_applies() {
        openConnected()
        clickControl("optionSwitch_dryRun")
        clickControl("optionSwitch_backupBeforeApply")
        clickControl("primaryActionButton")
        verify(AppController.chainedBackupPending, "the backup runs first")
        compare(AppController.runner.phase, MigrationRunner.Idle, "no migration runs during the backup")

        tryVerify(() => finishedSpy.count > 0, 60000, "the migrations must run after the backup")
        compare(finishedSpy.signalArguments[0][0], true)
        tryCompare(AppController, "busy", false, 15000)
        compare(count("SELECT COUNT(*) FROM schema_backups WHERE kind = 'backup'"), 1)
        compare(appliedMigrations(), 3)
        compare(chainedBackupFailedSpy.count, 0)
    }

    function test_a_failed_back_up_first_stops_the_run_and_says_so() {
        // A file where the backup folder should be: the folder cannot be created.
        openConnected(WorkflowFixture.writeProfileFile([], ""))
        clickControl("optionSwitch_dryRun")
        clickControl("optionSwitch_backupBeforeApply")
        clickControl("primaryActionButton")

        tryCompare(chainedBackupFailedSpy, "count", 1, 10000)
        compare(AppController.chainedBackupPending, false)
        verify(chainedBackupFailedSpy.signalArguments[0][0].indexOf("cannot create the backup folder") >= 0)
        tryVerify(() => findRequired("chainedBackupFailedMessage").visible, 2000, "the failure must be shown")
        tryVerify(() => AppController.logLines.text().indexOf("Back up first: cannot create the backup folder") >= 0,
                  2000, "the failure must be logged")
        compare(finishedSpy.count, 0, "no migration run started")
        compare(appliedMigrations(), 0)
    }

    function test_a_backup_that_fails_while_it_runs_applies_nothing() {
        openConnected()
        // The archive path is an existing folder, so the backup starts and then fails on its worker.
        const archive = WorkflowFixture.newPluginsDir([])
        step("back up into an unwritable archive, then apply")
        AppController.applyAfterBackup(archive, "", [])
        verify(AppController.chainedBackupPending, "the backup must start")

        tryCompare(chainedBackupFailedSpy, "count", 1, 30000)
        compare(AppController.chainedBackupPending, false)
        tryCompare(AppController, "busy", false, 15000)
        compare(finishedSpy.count, 0, "no migration run started")
        compare(AppController.runner.phase, MigrationRunner.Idle)
        compare(appliedMigrations(), 0)
        verify(AppController.logLines.text().indexOf("No migration ran.") >= 0)
    }

    function test_the_dry_run_switch_says_it_changes_nothing() {
        openConnected()
        compare(findRequired("optionDesc_dryRun").text, "Show the SQL; change nothing.")
    }

    /// Clicks @p buttonName in the Simple view and checks that it shows the log in the Expert view.
    function checkLogButtonOpensExpertView(buttonName) {
        AppController.setLogVisible(false)
        clickControl(buttonName)
        compare(AppController.viewMode, "expert")
        tryVerify(() => findRequired("expertView").visible, 5000, "the Expert view must be shown")
        compare(AppController.logVisible, true)
    }

    function test_view_log_after_a_successful_simple_run_shows_the_expert_log() {
        openConnected(undefined, "simple")
        clickControl("runButton")
        tryVerify(() => finishedSpy.count > 0, 60000, "the run must finish")
        tryVerify(() => findRequired("viewLogButton").visible, 5000, "the result must be shown")
        checkLogButtonOpensExpertView("viewLogButton")
    }

    function test_show_full_log_after_a_failed_simple_run_shows_the_expert_log() {
        openConnected(undefined, "simple")
        holdLock()
        clickControl("runButton")
        tryVerify(() => finishedSpy.count > 0, 60000, "the run must finish")
        tryVerify(() => findRequired("showFullLogButton").visible, 5000, "the failure must be shown")
        checkLogButtonOpensExpertView("showFullLogButton")
    }

    function test_back_up_first_that_cannot_start_the_apply_says_so() {
        openConnected()
        clickControl("optionSwitch_dryRun")
        clickControl("optionSwitch_backupBeforeApply")
        WorkflowFixture.detachMigrationManager()
        clickControl("primaryActionButton")

        tryCompare(chainedApplyFailedSpy, "count", 1, 60000)
        compare(chainedBackupFailedSpy.count, 0)
        compare(AppController.chainedBackupPending, false)
        tryVerify(() => findRequired("chainedBackupFailedMessage").visible, 2000, "the failure must be shown")
        compare(findRequired("chainedBackupFailedMessage").title, "The backup finished; the migrations did not start")
        verify(AppController.logLines.text().indexOf("No migration ran.") >= 0)
    }

    function test_back_up_first_refuses_a_second_chain_quietly_while_one_is_pending() {
        openConnected()
        AppController.applyAfterBackup("", "", [])
        verify(AppController.chainedBackupPending)
        AppController.applyAfterBackup("", "", [])
        compare(chainedBackupFailedSpy.count, 0, "a refused second trigger is not a failure")
        compare(chainedApplyFailedSpy.count, 0)
        tryVerify(() => finishedSpy.count > 0, 60000, "the first chain must still apply")
        compare(finishedSpy.signalArguments[0][0], true)
        tryCompare(AppController, "busy", false, 15000)
        compare(appliedMigrations(), 3)
        compare(count("SELECT COUNT(*) FROM schema_backups WHERE kind = 'backup'"), 1)
        verify(AppController.logLines.text().indexOf("another operation is in progress") >= 0)
    }

    function test_closing_is_blocked_while_the_backup_folder_is_prepared() {
        openConnected()
        AppController.backUpNow()
        compare(AppController.closeBlocked, true, "the backup is under way before its run starts")
        verify(AppController.closeBlockedReason !== "")
        tryCompare(AppController.backupRunner, "phase", BackupRunner.Idle, 30000)
        tryCompare(AppController, "closeBlocked", false, 15000)
    }
}
