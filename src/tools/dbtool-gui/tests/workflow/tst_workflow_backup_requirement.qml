// SPDX-License-Identifier: Apache-2.0
//
// The backup requirement as the user meets it: no alert when nothing requires a backup; with a
// release and a migration that do, the alert names them and the migrate buttons stay disabled until
// "Back up now" ran, a restore was recorded or the one-run waiver was ticked; and the requirement is
// checked again after a run, a backup and a reconnect.

import QtQuick
import QtTest
import Lightweight.Migrations
import DbtoolGuiWorkflow

WorkflowTestCase {
    name: "WorkflowBackupRequirement"

    /// The connection string of profile `p`'s database.
    property string cs: ""

    SignalSpy {
        id: finishedSpy
        signalName: "finished"
    }

    SignalSpy {
        id: managedFinishedSpy
        signalName: "finished"
    }

    /// Writes profile `p` on a new SQLite database with the plugins @p pluginKeys, plus @p otherProfiles,
    /// opens the window in @p viewMode and connects to `p`, then waits for the requirement check after the connect.
    function openConnected(pluginKeys, viewMode, otherProfiles) {
        cs = WorkflowFixture.newSqliteDatabase()
        const plugins = WorkflowFixture.newPluginsDir(pluginKeys)
        const profiles = [{ name: "p", connectionString: cs, pluginsDir: plugins }].concat(otherProfiles || [])
        const path = WorkflowFixture.writeProfileFile(profiles, "p")
        WorkflowFixture.setSetting("config/profileStorePath", path)
        WorkflowFixture.setSetting("ui/viewMode", viewMode || "simple")
        WorkflowFixture.setSetting("backup/folder", WorkflowFixture.tempPath("backups"))
        openMain()
        tryCompare(AppController, "busy", false, 15000)
        clickControl("connectButton")
        tryCompare(AppController, "connected", true, 15000)
        tryCompare(AppController, "backupRequirementPending", false, 15000)
        tryCompare(AppController, "busy", false, 15000)
        finishedSpy.target = AppController.runner
    }

    function count(sql) {
        return outsideWatchdog("query the database", () => WorkflowFixture.queryScalar(cs, sql))
    }

    /// @return The rows of `schema_backups` of @p kind; 0 while the table does not exist yet.
    function backupRecords(kind) {
        if (count("SELECT COUNT(*) FROM sqlite_master WHERE type = 'table' AND name = 'schema_backups'") === 0)
            return 0
        return count(`SELECT COUNT(*) FROM schema_backups WHERE kind = '${kind}'`)
    }

    /// Clicks "Back up now" and waits until the backup and the requirement check after it are over.
    function backUpNow() {
        clickControl("backUpNowButton")
        tryVerify(() => AppController.backupRunner.phase !== BackupRunner.Idle || !AppController.backupRequired,
                  10000, "the backup must start")
        tryCompare(AppController.backupRunner, "phase", BackupRunner.Idle, 30000)
        tryCompare(AppController, "backupRequirementPending", false, 30000)
        tryCompare(AppController, "busy", false, 15000)
    }

    /// Turns dry run off in the Expert view and targets @p timestamp, so Apply runs up to it.
    function applyUpTo(timestamp) {
        clickControl("optionSwitch_dryRun")
        compare(findRequired("optionSwitch_dryRun").checked, false)
        clickControl("targetOption_timestamp")
        typeText("timestampField", timestamp)
    }

    /// Clicks the primary button and waits for the run it starts to end.
    function runPrimaryAction() {
        const before = finishedSpy.count
        clickControl("primaryActionButton")
        tryVerify(() => finishedSpy.count > before, 60000, "the run must finish")
        tryCompare(AppController, "backupRequirementPending", false, 30000)
        tryCompare(AppController, "busy", false, 15000)
    }

    function test_nothing_required_means_no_alert_and_enabled_buttons() {
        openConnected(["workflow"])
        compare(AppController.backupRequired, false)
        compare(findRequired("simpleBackupAlert").visible, false)
        compare(findRequired("runButton").enabled, true)

        clickControl("viewModeControl_expert")
        compare(findRequired("actionsBackupAlert").visible, false)
        clickControl("optionSwitch_dryRun")
        compare(findRequired("primaryActionButton").enabled, true)
    }

    function test_required_and_missing_disables_the_buttons_and_names_the_reasons() {
        openConnected(["workflow", "backupRequiredRelease", "migrationRequiresBackup"])
        compare(AppController.backupRequired, true)
        verify(findRequired("backUpNowButton").visible)
        const text = findRequired("simpleBackupAlert").text
        verify(text.includes("release 3.0.0"), text)
        verify(text.includes('migration 20240401000000 "Requires a backup"'), text)
        compare(findRequired("runButton").enabled, false)

        clickControl("viewModeControl_expert")
        verify(findRequired("actionsBackupAlert").text.includes("release 3.0.0"))
        clickControl("optionSwitch_dryRun")
        compare(findRequired("primaryActionButton").enabled, false)
        clickControl("optionSwitch_dryRun")
        compare(findRequired("optionSwitch_dryRun").checked, true)
        compare(findRequired("primaryActionButton").enabled, true, "a dry run stays available")
    }

    function test_back_up_now_enables_the_buttons() {
        openConnected(["workflow", "backupRequiredRelease"])
        compare(findRequired("runButton").enabled, false)
        backUpNow()
        compare(AppController.backupRequired, false)
        compare(findRequired("runButton").enabled, true)
        compare(backupRecords("backup"), 1)
        compare(findRequired("backUpNowButton").visible, false)
    }

    function test_the_waiver_enables_one_run_and_records_waived() {
        openConnected(["workflow", "backupRequiredRelease", "migrationRequiresBackup"], "expert")
        applyUpTo("20240201000000")
        compare(findRequired("primaryActionButton").enabled, false)
        clickControl("migrateWithoutBackupCheckBox")
        compare(AppController.migrateWithoutBackup, true)
        compare(findRequired("primaryActionButton").enabled, true)
        runPrimaryAction()

        compare(backupRecords("waived"), 1)
        compare(AppController.migrateWithoutBackup, false)
        compare(findRequired("migrateWithoutBackupCheckBox").checked, false)
        compare(AppController.backupRequired, true, "the next pending migration needs a backup again")
        verify(findRequired("actionsBackupAlert").visible)
        compare(findRequired("primaryActionButton").enabled, false)
    }

    /// Runs "Back up all profiles" on the Backups page and waits for it and its archive scan to end; the profile's
    /// archive is `backups/p.zip`.
    function backUpAllProfiles() {
        clickControl("navItem_backups")
        clickControl("backupAllButton")
        tryVerify(() => WorkflowFixture.fileExists(WorkflowFixture.tempPath("backups/p.zip")), 30000,
                  "the managed backup must write the archive")
        tryCompare(AppController.managedBackups, "phase", ManagedBackupController.Idle, 30000)
        tryVerify(() => findInRow("profileRow_p", "profileRestoreButton").enabled, 10000,
                  "the archive scan after the run must list p's archive")
        tryCompare(AppController, "backupRequirementPending", false, 30000)
        tryCompare(AppController, "busy", false, 15000)
    }

    /// Types @p name into the open restore dialog @p dialog and confirms the restore.
    function confirmRestore(dialog, name) {
        tryVerify(() => dialog.visible, 5000, "the restore dialog must open")
        step("confirm the restore")
        dialog.confirmField.forceActiveFocus()
        for (const character of name)
            keyClick(character)
        clickControl("restoreConfirmButton")
    }

    function test_a_managed_backup_satisfies_the_requirement() {
        openConnected(["workflow", "backupRequiredRelease"])
        compare(AppController.backupRequired, true)
        backUpAllProfiles()
        compare(backupRecords("backup"), 1)
        compare(AppController.backupRequired, false)
        clickControl("navItem_migrations")
        compare(findRequired("backUpNowButton").visible, false)
    }

    function test_a_partly_failed_managed_backup_still_satisfies_the_requirement() {
        const broken = { name: "broken", connectionString: "DRIVER=NoSuchDriver;Database=nothing.db" }
        openConnected(["workflow", "backupRequiredRelease"], "simple", [broken])
        compare(AppController.backupRequired, true)
        managedFinishedSpy.target = AppController.managedBackups
        managedFinishedSpy.clear()
        backUpAllProfiles()
        tryVerify(() => managedFinishedSpy.count > 0, 30000, "the managed run must finish")
        compare(managedFinishedSpy.signalArguments[0][0], false, "the broken profile fails the run")
        tryCompare(AppController, "backupRequirementPending", false, 30000)
        compare(backupRecords("backup"), 1)
        compare(AppController.backupRequired, false, "the connected profile was backed up")
        clickControl("navItem_migrations")
        compare(findRequired("backUpNowButton").visible, false)
    }

    function test_a_managed_restore_record_satisfies_the_requirement() {
        openConnected(["workflow", "backupRequiredRelease"])
        backUpAllProfiles()
        // Roll the database back to a state before the backup record, so the restore alone has to satisfy the requirement.
        clickControl("profileRestoreButton")
        confirmRestore(findRequired("restoreDialog"), "p")
        tryVerify(() => backupRecords("restore") === 1, 30000, "the restore must be recorded")
        tryCompare(AppController.managedBackups, "phase", ManagedBackupController.Idle, 30000)
        tryCompare(AppController, "backupRequirementPending", false, 30000)
        tryCompare(AppController, "busy", false, 15000)

        compare(AppController.backupRequired, false)
        clickControl("navItem_migrations")
        compare(findRequired("backUpNowButton").visible, false)
    }

    function test_a_restore_record_of_a_custom_archive_satisfies_the_requirement() {
        openConnected(["workflow", "backupRequiredRelease"])
        compare(AppController.backupRequired, true)
        const archive = WorkflowFixture.tempPath("custom.zip")
        clickControl("navItem_backups")
        typeText("customPathField", archive)
        clickControl("customBackupButton")
        tryCompare(AppController.backupRunner, "phase", BackupRunner.Idle, 30000)
        verify(WorkflowFixture.fileExists(archive), "the custom backup must write the archive")
        tryCompare(AppController, "backupRequirementPending", false, 30000)
        tryCompare(AppController, "busy", false, 15000)
        compare(backupRecords("backup"), 1)

        clickControl("customRestoreButton")
        confirmRestore(findRequired("customRestoreDialog"), "p")
        tryVerify(() => backupRecords("restore") === 1, 30000, "the restore must be recorded")
        tryCompare(AppController.backupRunner, "phase", BackupRunner.Idle, 30000)
        tryCompare(AppController, "backupRequirementPending", false, 30000)
        tryCompare(AppController, "busy", false, 15000)
        compare(AppController.backupRequired, false)
    }

    function test_the_requirement_is_re_evaluated_after_a_run_a_backup_and_a_reconnect() {
        WorkflowFixture.setEnv("LIGHTWEIGHT_TEST_PRE_MIGRATE_LOG", WorkflowFixture.tempPath("steps.log"))
        WorkflowFixture.setEnv("LIGHTWEIGHT_TEST_PRE_MIGRATE_REQUIRES_BACKUP", "1")
        openConnected(["workflow", "recorder"], "expert")
        compare(AppController.backupRequired, true, "the recorder step requires a backup")

        backUpNow()
        compare(AppController.backupRequired, false, "re-evaluated after the backup")

        clickControl("optionSwitch_dryRun")
        runPrimaryAction()
        compare(AppController.backupRequired, true, "re-evaluated after the run: the backup is now older than the migrations")

        WorkflowFixture.unsetEnv("LIGHTWEIGHT_TEST_PRE_MIGRATE_REQUIRES_BACKUP")
        clickControl("refreshButton")
        tryCompare(AppController, "connecting", false, 15000)
        tryCompare(AppController, "backupRequirementPending", false, 30000)
        tryCompare(AppController, "busy", false, 15000)
        compare(AppController.backupRequired, false, "re-evaluated after the reconnect")
    }
}
