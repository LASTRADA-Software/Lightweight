// SPDX-License-Identifier: Apache-2.0
//
// The plugins directory typed into the Settings page reloads the plugins once, and a reconnect or a
// refresh runs the plugins' post-init hooks once per click.

import QtQuick
import QtTest
import Lightweight.Migrations
import DbtoolGuiWorkflow

WorkflowTestCase {
    name: "WorkflowPlugins"

    /// The file the recorder plugin appends one line to per load.
    property string loadLog: ""
    /// The file the recorder plugin appends one line to per post-init hook call.
    property string postInitLog: ""

    /// Writes profile `p` on a new SQLite database and a plugins directory holding the workflow and
    /// recorder fixtures, and opens the Expert view.
    function prepare() {
        loadLog = WorkflowFixture.tempPath("plugin-loads.log")
        postInitLog = WorkflowFixture.tempPath("post-init.log")
        WorkflowFixture.setEnv("LIGHTWEIGHT_TEST_PLUGIN_LOAD_LOG", loadLog)
        WorkflowFixture.setEnv("LIGHTWEIGHT_TEST_POST_INIT_LOG", postInitLog)
        WorkflowFixture.setSetting("config/pluginsDir", WorkflowFixture.newPluginsDir(["recorder", "workflow"]))
        const path = WorkflowFixture.writeProfileFile(
            [{ name: "p", connectionString: WorkflowFixture.newSqliteDatabase() }], "p")
        WorkflowFixture.setSetting("config/profileStorePath", path)
        WorkflowFixture.setSetting("ui/viewMode", "expert")
        openMain()
        waitForIdle()
    }

    function waitForIdle() {
        tryCompare(AppController, "busy", false, 15000)
    }

    function test_changing_the_plugins_directory_reloads_once() {
        prepare()
        compare(WorkflowFixture.lineCount(loadLog), 1)
        compare(AppController.migrationCount, 3)
        const otherDirectory = WorkflowFixture.newPluginsDir(["recorder", "backupRequiredRelease"])

        clickControl("navItem_settings")
        typeInto("pluginsDirField", otherDirectory)
        compare(AppController.pluginsDir, otherDirectory)
        waitForIdle()
        compare(WorkflowFixture.lineCount(loadLog), 2)
        compare(AppController.migrationCount, 1)

        // Leaving the field again does not reload.
        clickControl("navItem_migrations")
        waitForIdle()
        compare(WorkflowFixture.lineCount(loadLog), 2)
    }

    function test_reconnect_reruns_post_init_hooks_once() {
        prepare()
        compare(WorkflowFixture.lineCount(postInitLog), 0)
        clickControl("connectButton")
        tryCompare(AppController, "connected", true, 15000)
        waitForIdle()
        compare(WorkflowFixture.lineCount(postInitLog), 1)

        clickControl("connectButton")
        tryVerify(() => WorkflowFixture.lineCount(postInitLog) === 2, 15000)
        waitForIdle()
        compare(WorkflowFixture.lineCount(postInitLog), 2)

        clickControl("refreshButton")
        tryVerify(() => WorkflowFixture.lineCount(postInitLog) === 3, 15000)
        waitForIdle()
        compare(WorkflowFixture.lineCount(postInitLog), 3)
        compare(AppController.connected, true)
    }
}
