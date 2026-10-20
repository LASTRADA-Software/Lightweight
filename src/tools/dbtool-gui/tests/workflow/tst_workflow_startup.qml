// SPDX-License-Identifier: Apache-2.0
//
// Startup and the first frame: the window shows before the plugins load, the plugins load once per
// cause and off the UI thread, nothing reads a database while disconnected, and a plugins directory
// that does not exist or a plugin that fails to load is logged and still ends the loading state.

import QtQuick
import QtTest
import Lightweight.Migrations
import DbtoolGuiWorkflow

WorkflowTestCase {
    name: "WorkflowStartup"
    watchdogMode: UiWatchdog.Enforce

    /// The file the recorder plugin appends one line to per load.
    property string loadLog: ""

    /// Points the Settings-page plugins directory at a new one holding the recorder and workflow
    /// fixtures, and the recorder's load log at a new file.
    function preparePlugins() {
        const directory = WorkflowFixture.newPluginsDir(["recorder", "workflow"])
        WorkflowFixture.setSetting("config/pluginsDir", directory)
        loadLog = WorkflowFixture.tempPath("plugin-loads.log")
        WorkflowFixture.setEnv("LIGHTWEIGHT_TEST_PLUGIN_LOAD_LOG", loadLog)
        return directory
    }

    /// Writes a profile file with the profiles `p` and `q` on two new SQLite databases, `p` the default.
    function prepareProfiles() {
        const path = WorkflowFixture.writeProfileFile([
            { name: "p", connectionString: WorkflowFixture.newSqliteDatabase() },
            { name: "q", connectionString: WorkflowFixture.newSqliteDatabase() },
        ], "p")
        WorkflowFixture.setSetting("config/profileStorePath", path)
    }

    function waitForIdle() {
        tryCompare(AppController, "busy", false, 15000)
    }

    function test_first_frame_comes_before_a_slow_plugin_load() {
        preparePlugins()
        WorkflowFixture.setEnv("LIGHTWEIGHT_TEST_PLUGIN_LOAD_DELAY_MS", "3000")
        const beginLog = WorkflowFixture.tempPath("plugin-load-begins.log")
        WorkflowFixture.setEnv("LIGHTWEIGHT_TEST_PLUGIN_LOAD_BEGIN_LOG", beginLog)
        let atFirstFrame = null
        watchdog.firstFrame.connect(() => atFirstFrame = {
            loading: AppController.pluginsLoading,
            indicator: findRequired("activityIndicator").visible,
            begun: WorkflowFixture.lineCount(beginLog),
            loads: WorkflowFixture.lineCount(loadLog),
        })
        openMain()
        tryVerify(() => atFirstFrame !== null, 10000)
        compare(atFirstFrame.loading, true)
        compare(atFirstFrame.indicator, true)
        compare(atFirstFrame.begun, 0, "no plugin library may be mapped before the first frame")
        compare(atFirstFrame.loads, 0)
        tryVerify(() => WorkflowFixture.lineCount(beginLog) === 1, 10000, "the load starts after the first frame")
        compare(findRequired("activityText").text, "Loading plugins…")
        tryCompare(AppController, "pluginsLoading", false, 15000)
        compare(AppController.migrationCount, 3)
        compare(findRequired("activityIndicator").visible, false)
    }

    function test_startup_loads_the_plugins_once() {
        preparePlugins()
        openMain()
        waitForIdle()
        compare(WorkflowFixture.lineCount(loadLog), 1)
        compare(AppController.migrationCount, 3)
    }

    function test_one_change_cause_loads_the_plugins_once() {
        preparePlugins()
        prepareProfiles()
        // Copying the plugins is the test's work, not the application's; it happens before the window opens.
        const otherDirectory = WorkflowFixture.newPluginsDir(["recorder", "workflow"])
        openMain()
        waitForIdle()
        compare(WorkflowFixture.lineCount(loadLog), 1)

        // Three changes that each reload the plugins, made in one turn of the event loop.
        step("three changes in one turn")
        AppController.setPluginsDir(otherDirectory)
        AppController.setConnectionMode("dsn")
        AppController.currentProfile = "q"
        compare(AppController.pluginsLoading, true)
        waitForIdle()
        compare(WorkflowFixture.lineCount(loadLog), 2)
        compare(AppController.migrationCount, 3)
    }

    function test_no_database_access_while_disconnected() {
        preparePlugins()
        prepareProfiles()
        WorkflowFixture.startDbAccessProbe()
        openMain()
        waitForIdle()
        compare(AppController.currentProfile, "p")
        compare(WorkflowFixture.dbAccessCount(), 0, "the startup must not read a database")

        // After a connect the default connection still names p's database; switching away must not read it.
        clickControl("connectButton")
        tryCompare(AppController, "connected", true, 15000)
        waitForIdle()
        WorkflowFixture.startDbAccessProbe()
        AppController.currentProfile = "q"
        compare(AppController.connected, false)
        waitForIdle()
        AppController.setConnectionMode("custom")
        waitForIdle()
        compare(AppController.migrationCount, 3)
        compare(WorkflowFixture.dbAccessCount(), 0, "a disconnected reload must not read a database")
    }

    function test_a_missing_plugins_directory_does_not_leave_the_loading_state() {
        WorkflowFixture.setSetting("config/pluginsDir", WorkflowFixture.tempPath("no-such-plugins"))
        const lines = []
        AppController.logLine.connect((line, level) => lines.push(line))
        openMain()
        tryCompare(AppController, "pluginsLoading", false, 15000)
        verify(lines.some(line => line.startsWith("Plugins directory:")),
               "the log must name the plugins directory:\n" + lines.join("\n"))
        compare(lines.filter(line => line.startsWith("Plugins directory not found: ")
                                     && line.endsWith("no-such-plugins")).length, 1,
                "the log must warn once about the missing directory:\n" + lines.join("\n"))
        compare(AppController.busy, false)
        compare(AppController.migrationCount, 0)
        compare(findRequired("activityIndicator").visible, false)
        verify(findRequired("migrationListEmptyTitle").text !== "Loading plugins…")
    }

    function test_a_plugin_that_fails_to_load_is_logged_and_the_others_load() {
        const directory = WorkflowFixture.newPluginsDir(["workflow", "broken"])
        WorkflowFixture.setSetting("config/pluginsDir", directory)
        const lines = []
        AppController.logLine.connect((line, level) => lines.push(line))
        openMain()
        tryCompare(AppController, "pluginsLoading", false, 15000)
        compare(AppController.busy, false)
        verify(lines.some(line => line.startsWith("Plugin failed: ") && line.includes("brokenPlugin")),
               "the log must name the plugin that failed to load:\n" + lines.join("\n"))
        compare(AppController.migrationCount, 3)
        compare(findRequired("activityIndicator").visible, false)
    }
}
