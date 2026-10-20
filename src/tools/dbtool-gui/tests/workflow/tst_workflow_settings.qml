// SPDX-License-Identifier: Apache-2.0
//
// The Settings page's profile file path and backup folder are written to the settings and shown again
// by a window opened afterwards. The cold read of the settings by a fresh AppController is covered by
// each per-process test's QSettings seeding and by ManagedBackupControllerTests.

import QtQuick
import QtTest
import Lightweight.Migrations
import DbtoolGuiWorkflow

WorkflowTestCase {
    name: "WorkflowSettings"

    /// Closes the Main window and opens a new one, as a restart of the application's window.
    function restartWindow() {
        step("close the window")
        main.destroy()
        tryVerify(() => main === null || main === undefined || !main.visible, 5000)
        main = null
        openMain()
        tryCompare(AppController, "busy", false, 15000)
    }

    function prepare() {
        WorkflowFixture.setSetting("config/pluginsDir", WorkflowFixture.newPluginsDir(["workflow"]))
        WorkflowFixture.setSetting("ui/viewMode", "expert")
        openMain()
        tryCompare(AppController, "busy", false, 15000)
        clickControl("navItem_settings")
    }

    function test_profile_file_path_persists_across_a_window_restart() {
        prepare()
        const path = WorkflowFixture.writeProfileFile([
            { name: "p", connectionString: WorkflowFixture.newSqliteDatabase() },
            { name: "q", connectionString: WorkflowFixture.newSqliteDatabase() },
        ], "p")
        typeInto("profileStorePathField", path)
        compare(WorkflowFixture.setting("config/profileStorePath"), path)
        tryCompare(AppController, "profilePath", path, 15000)
        compare(AppController.profiles.rowCount(), 2)

        restartWindow()
        clickControl("navItem_settings")
        compare(findRequired("profileStorePathField").text, path)
        clickControl("navItem_migrations")
        compare(findRequired("profileCombo").count, 2)
    }

    function test_backup_folder_persists_across_a_window_restart() {
        prepare()
        const folder = WorkflowFixture.tempPath("my-backups")
        typeInto("backupFolderField", folder)
        compare(WorkflowFixture.setting("backup/folder"), folder)
        compare(AppController.managedBackups.effectiveBackupFolder, folder)

        restartWindow()
        clickControl("navItem_settings")
        compare(findRequired("backupFolderField").text, folder)
        compare(AppController.managedBackups.effectiveBackupFolder, folder)
    }
}
