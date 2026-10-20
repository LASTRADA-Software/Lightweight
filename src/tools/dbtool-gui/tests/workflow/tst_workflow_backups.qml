// SPDX-License-Identifier: Apache-2.0
//
// The Backups page by itself: "Back up all" and a single profile's backup write one archive per
// profile and show its size; a restore puts the archive's tables into a profile, a connection string
// or the connected database; the backup folder of the Settings page moves the archives; "Show in
// folder" asks the desktop to open the archive's folder. The backup requirement is covered by
// tst_workflow_backup_requirement.qml.

import QtQuick
import QtTest
import Lightweight.Migrations
import DbtoolGuiWorkflow

WorkflowTestCase {
    name: "WorkflowBackups"

    SignalSpy {
        id: refreshedSpy
        signalName: "statusRefreshed"
    }

    /// The connection strings of profiles `p` and `q`.
    property string csP: ""
    property string csQ: ""

    /// Writes profiles `p` (table `restore_probe` with two rows) and `q` (table `other_table`) on new SQLite
    /// databases, backing up into `backups/` of the fixture's directory, and opens the Backups page.
    function openBackupsPage() {
        csP = WorkflowFixture.newSqliteDatabase()
        csQ = WorkflowFixture.newSqliteDatabase()
        WorkflowFixture.executeSql(csP, "CREATE TABLE restore_probe (id INTEGER PRIMARY KEY, label VARCHAR(20))")
        WorkflowFixture.executeSql(csP, "INSERT INTO restore_probe (id, label) VALUES (1, 'alpha')")
        WorkflowFixture.executeSql(csP, "INSERT INTO restore_probe (id, label) VALUES (2, 'beta')")
        WorkflowFixture.executeSql(csQ, "CREATE TABLE other_table (id INTEGER PRIMARY KEY)")
        const path = WorkflowFixture.writeProfileFile([
            { name: "p", connectionString: csP, pluginsDir: WorkflowFixture.newPluginsDir(["workflow"]) },
            { name: "q", connectionString: csQ, pluginsDir: WorkflowFixture.newPluginsDir(["workflow"]) },
        ], "p")
        WorkflowFixture.setSetting("config/profileStorePath", path)
        WorkflowFixture.setSetting("ui/viewMode", "simple")
        WorkflowFixture.setSetting("backup/folder", WorkflowFixture.tempPath("backups"))
        openMain()
        tryCompare(AppController, "busy", false, 15000)
    }

    /// Connects profile `p` from the Migrations page, then opens the Backups page.
    function connectThenOpenBackups() {
        clickControl("connectButton")
        tryCompare(AppController, "connected", true, 15000)
        tryCompare(AppController, "backupRequirementPending", false, 15000)
        tryCompare(AppController, "busy", false, 15000)
        clickControl("navItem_backups")
    }

    function count(connectionString, sql) {
        return outsideWatchdog("query the database", () => WorkflowFixture.queryScalar(connectionString, sql))
    }

    /// @return The number of rows of `restore_probe` in the database @p connectionString; 0 while the table is missing.
    function probeRows(connectionString) {
        if (count(connectionString, "SELECT COUNT(*) FROM sqlite_master WHERE type = 'table' AND name = 'restore_probe'") === 0)
            return 0
        return count(connectionString, "SELECT COUNT(*) FROM restore_probe")
    }

    function archivePath(profile) {
        return WorkflowFixture.tempPath(`backups/${profile}.zip`)
    }

    function waitForManagedIdle() {
        tryCompare(AppController.managedBackups, "phase", ManagedBackupController.Idle, 30000)
        tryCompare(AppController, "busy", false, 15000)
    }

    function waitForCustomIdle() {
        tryCompare(AppController.backupRunner, "phase", BackupRunner.Idle, 30000)
        tryCompare(AppController, "busy", false, 15000)
    }

    /// Waits for the profile table to list profile @p profile.
    function waitForRow(profile) {
        tryVerify(() => _rowExists(profile), 10000, `row ${profile} must be listed`)
    }

    function _rowExists(profile) {
        const matches = []
        _collect(main.contentItem, `profileRow_${profile}`, matches)
        return matches.length > 0
    }

    /// Clicks the button @p objectName inside the row of profile @p profile once the row is listed.
    function clickProfileButton(profile, objectName) {
        waitForRow(profile)
        clickInRow(`profileRow_${profile}`, objectName)
    }

    /// @return The size text of the archive file @p path as the page formats it ("12.3 KB").
    function formattedSize(path) {
        const units = ["B", "KB", "MB", "GB", "TB"]
        let value = WorkflowFixture.fileSize(path)
        verify(value > 0, `${path} must exist`)
        let unit = 0
        while (value >= 1024 && unit < units.length - 1) {
            value /= 1024
            ++unit
        }
        return (unit === 0 ? value.toFixed(0) : value.toFixed(1)) + " " + units[unit]
    }

    /// @return The size the row of profile @p profile shows in its size column.
    function shownSize(profile) {
        return findInRow(`profileRow_${profile}`, "profileSizeLabel").text
    }

    /// Types @p name into the open restore dialog's confirmation field @p fieldName and clicks its confirm button.
    function confirmRestore(fieldName, name) {
        tryVerify(() => findRequired(fieldName).visible, 5000, "the restore dialog must open")
        typeText(fieldName, name)
        clickControl("restoreConfirmButton")
    }

    /// Waits until the archive scan after a run shows an archive in the row of profile @p profile.
    function waitForShownArchive(profile) {
        tryVerify(() => shownSize(profile) !== "—", 10000, `the archive scan must list ${profile}'s archive`)
    }

    /// Backs up every profile from the Backups page and waits until the run and its archive scan are over.
    function backUpAll() {
        clickControl("backupAllButton")
        tryVerify(() => WorkflowFixture.fileExists(archivePath("p")) && WorkflowFixture.fileExists(archivePath("q")),
                  30000, "one archive per profile must appear")
        waitForManagedIdle()
        waitForShownArchive("p")
        waitForShownArchive("q")
    }

    function test_back_up_all_writes_one_archive_per_profile() {
        openBackupsPage()
        clickControl("navItem_backups")
        compare(WorkflowFixture.fileExists(archivePath("p")), false)
        backUpAll()
        for (const profile of ["p", "q"])
            compare(shownSize(profile), formattedSize(archivePath(profile)), `size shown for ${profile}`)
    }

    function test_back_up_one_profile() {
        openBackupsPage()
        clickControl("navItem_backups")
        waitForRow("p")
        compare(shownSize("p"), "—")
        clickProfileButton("p", "profileBackupButton")
        tryVerify(() => WorkflowFixture.fileExists(archivePath("p")), 30000, "p's archive must appear")
        waitForManagedIdle()
        waitForShownArchive("p")
        compare(WorkflowFixture.fileExists(archivePath("q")), false, "q must not be backed up")
        compare(shownSize("p"), formattedSize(archivePath("p")))
        compare(shownSize("q"), "—")
    }

    function test_restore_to_a_profile() {
        openBackupsPage()
        clickControl("navItem_backups")
        backUpAll()
        compare(probeRows(csQ), 0)

        clickProfileButton("p", "profileRestoreButton")
        tryVerify(() => findRequired("restoreTargetCombo").visible, 5000, "the restore dialog must open")
        pickWithKeys("restoreTargetCombo", 1)
        compare(findRequired("restoreTargetCombo").currentText, "q")
        confirmRestore("restoreConfirmField", "q")
        tryVerify(() => probeRows(csQ) === 2, 30000, "q must hold p's table")
        waitForManagedIdle()
        compare(count(csQ, "SELECT label FROM restore_probe WHERE id = 2"), "beta")
        compare(probeRows(csP), 2, "p stays as it was")
    }

    function test_restore_to_a_connection_string() {
        openBackupsPage()
        clickControl("navItem_backups")
        backUpAll()
        const target = WorkflowFixture.newSqliteDatabase()

        clickProfileButton("p", "profileRestoreButton")
        tryVerify(() => findRequired("restoreTargetCombo").visible, 5000, "the restore dialog must open")
        pickWithKeys("restoreTargetCombo", 2)
        verify(findRequired("restoreTargetCombo").currentText.startsWith("Custom connection string"))
        typeText("restoreConnectionStringField", target)
        confirmRestore("restoreConfirmField", "p")
        tryVerify(() => probeRows(target) === 2, 30000, "the connection string's database must hold p's table")
        waitForManagedIdle()
        compare(count(target, "SELECT label FROM restore_probe WHERE id = 1"), "alpha")
    }

    function test_custom_backup_and_restore() {
        openBackupsPage()
        connectThenOpenBackups()
        const archive = WorkflowFixture.tempPath("custom.zip")
        typeText("customPathField", archive)
        clickControl("customBackupButton")
        tryVerify(() => WorkflowFixture.fileExists(archive), 30000, "the custom backup must write the archive")
        waitForCustomIdle()

        outsideWatchdog("drop the table", () => WorkflowFixture.executeSql(csP, "DROP TABLE restore_probe"))
        compare(probeRows(csP), 0)
        clickControl("customRestoreButton")
        confirmRestore("customRestoreConfirmField", "p")
        tryVerify(() => probeRows(csP) === 2, 30000, "the restore must bring the table back")
        waitForCustomIdle()
        compare(count(csP, "SELECT label FROM restore_probe WHERE id = 2"), "beta")
    }

    function test_backup_folder_setting_moves_the_archives() {
        openBackupsPage()
        const folder = WorkflowFixture.tempPath("moved-backups")
        clickControl("navItem_settings")
        refreshedSpy.target = AppController.managedBackups
        refreshedSpy.clear()
        typeInto("backupFolderField", folder)
        tryVerify(() => refreshedSpy.count > 0, 10000, "the scan of the new folder must finish")
        compare(AppController.managedBackups.folderProblem, "")
        clickControl("navItem_backups")
        compare(findRequired("backupFolderLabel").text, folder)
        compare(AppController.managedBackups.folderProblem, "")

        clickControl("backupAllButton")
        tryVerify(() => WorkflowFixture.fileExists(`${folder}/p.zip`) && WorkflowFixture.fileExists(`${folder}/q.zip`),
                  30000, "the archives must land in the new folder")
        waitForManagedIdle()
        compare(WorkflowFixture.fileExists(archivePath("p")), false, "nothing is written to the old folder")
    }

    function test_reveal_in_folder_requests_the_archive_folder() {
        openBackupsPage()
        connectThenOpenBackups()
        const archive = WorkflowFixture.tempPath("custom.zip")
        const folder = archive.substring(0, archive.lastIndexOf("/"))
        typeText("customPathField", archive)
        clickControl("customBackupButton")
        tryVerify(() => WorkflowFixture.fileExists(archive), 30000, "the custom backup must write the archive")
        waitForCustomIdle()

        compare(WorkflowFixture.openedUrls(), [])
        tryVerify(() => findRequired("customOutcome").visible, 5000, "the outcome must be shown")
        clickControl("runOutcomeShowInFolder")
        compare(WorkflowFixture.openedUrls(), [WorkflowFixture.localFileUrl(folder).toString()])
    }
}
