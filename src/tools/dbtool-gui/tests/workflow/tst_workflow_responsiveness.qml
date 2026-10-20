// SPDX-License-Identifier: Apache-2.0
//
// Connect, refresh, the backup-requirement check and the SQL preview run on the worker: the UI stays
// responsive through a slow post-init hook and a slow backup check, the controls that need a result
// stay disabled until it arrives, a second Connect starts nothing, and a connect whose profile was
// switched away meanwhile never shows the old database.

import QtQuick
import QtTest
import Lightweight.Migrations
import DbtoolGuiWorkflow

WorkflowTestCase {
    name: "WorkflowResponsiveness"
    watchdogMode: UiWatchdog.Enforce

    /// The recorder and backup-required-release plugins.
    property string pluginsDirectory: ""
    /// The connection string of profile `p`'s database.
    property string databaseP: ""
    /// The file the recorder's post-init hook appends one line to per call.
    property string postInitLog: ""

    /// Writes profiles `p` (the default) and `q` on two new SQLite databases and points the plugins
    /// directory at one holding the recorder and the backup-required-release fixtures. With
    /// `qWithoutRecorder`, `q` loads only the backup-required-release fixture from a directory of its own,
    /// so its connect has no slow post-init hook or backup-check step.
    function prepare(qWithoutRecorder) {
        pluginsDirectory = WorkflowFixture.newPluginsDir(["recorder", "backupRequiredRelease"])
        WorkflowFixture.setSetting("config/pluginsDir", pluginsDirectory)
        databaseP = WorkflowFixture.newSqliteDatabase()
        const q = { name: "q", connectionString: WorkflowFixture.newSqliteDatabase() }
        if (qWithoutRecorder)
            q.pluginsDir = WorkflowFixture.newPluginsDir(["backupRequiredRelease"])
        const path = WorkflowFixture.writeProfileFile([
            { name: "p", connectionString: databaseP },
            q,
        ], "p")
        WorkflowFixture.setSetting("config/profileStorePath", path)
        postInitLog = WorkflowFixture.tempPath("post-init.log")
        WorkflowFixture.setEnv("LIGHTWEIGHT_TEST_POST_INIT_LOG", postInitLog)
    }

    /// Applies every migration to `p`'s database with the dbtool CLI, behind the GUI's back.
    function migrateWithDbtool() {
        const exitCode = outsideWatchdog("dbtool migrate", () => WorkflowFixture.runDbtool(
            ["migrate", "--without-backup", "--plugins-dir", pluginsDirectory, "--connection-string", databaseP], {}, longRunTimeoutMs))
        compare(exitCode, 0)
    }

    function waitForIdle() {
        tryCompare(AppController, "busy", false, 15000)
    }

    function openIdle() {
        openMain()
        waitForIdle()
    }

    function connectAndWait() {
        clickControl("connectButton")
        tryCompare(AppController, "connected", true, 15000)
        waitForIdle()
    }

    function test_connect_with_a_slow_post_init_hook_keeps_the_ui_responsive() {
        prepare()
        WorkflowFixture.setEnv("LIGHTWEIGHT_TEST_POST_INIT_DELAY_MS", "3000")
        openIdle()
        clickControl("connectButton")
        compare(AppController.connecting, true)
        compare(findRequired("connectButton").enabled, false)
        compare(findRequired("connectButton").text, "Connecting…")
        compare(AppController.activityText, "Connecting…")
        tryCompare(AppController, "connected", true, 15000)
        waitForIdle()
        compare(AppController.connecting, false)
        compare(WorkflowFixture.lineCount(postInitLog), 1)
        compare(AppController.migrationCount, 1)
    }

    function test_a_twenty_second_backup_check_keeps_the_ui_responsive_and_the_buttons_disabled() {
        prepare()
        WorkflowFixture.setEnv("LIGHTWEIGHT_TEST_PRE_MIGRATE_LOG", WorkflowFixture.tempPath("steps.log"))
        WorkflowFixture.setEnv("LIGHTWEIGHT_TEST_PRE_MIGRATE_REQUIRES_BACKUP_DELAY_MS", "20000")
        openIdle()
        clickControl("connectButton")
        tryCompare(AppController, "backupRequirementPending", true, 15000)
        compare(AppController.connected, true)
        verify(findRequired("backupRequirementChecking").visible)
        compare(findRequired("runButton").enabled, false)
        compare(findRequired("backUpNowButton").enabled, false)
        compare(findRequired("migrateWithoutBackupCheckBox").enabled, false)
        compare(AppController.activityText, "Checking whether a backup is required…")
        tryCompare(AppController, "backupRequirementPending", false, 40000)
        waitForIdle()
        compare(AppController.backupRequired, true)
        compare(AppController.backupRequirementReasons.length, 1)
        compare(AppController.backupRequirementReasons[0], "release 3.0.0")
        compare(findRequired("backupRequirementChecking").visible, false)
        verify(findRequired("runButton").enabled === !AppController.backupRequired)
    }

    function test_a_quick_profile_switch_never_shows_the_old_database() {
        prepare()
        migrateWithDbtool()
        WorkflowFixture.setEnv("LIGHTWEIGHT_TEST_POST_INIT_DELAY_MS", "2000")
        openIdle()
        let leaked = false
        AppController.migrationsChanged.connect(() => leaked = leaked || AppController.appliedCount > 0)
        AppController.connectedChanged.connect(() => leaked = leaked || AppController.connected)

        clickControl("connectButton")
        compare(AppController.connecting, true)
        switchToQ()

        waitForIdle()
        compare(AppController.connecting, false)
        compare(AppController.connected, false)
        compare(leaked, false, "p's applied migration must never show")
        compare(AppController.appliedCount, 0)
        compare(AppController.migrationCount, 1)
    }

    /// Picks `q` in the profile combo, as a user would.
    function switchToQ() {
        step("switch to q")
        const combo = findRequired("profileCombo")
        const indexOfQ = combo.find("q")
        verify(indexOfQ >= 0)
        combo.activated(indexOfQ)
        compare(AppController.currentProfile, "q")
    }

    function test_switching_the_profile_during_the_backup_check_ends_the_connect_at_once() {
        prepare(true)
        WorkflowFixture.setEnv("LIGHTWEIGHT_TEST_PRE_MIGRATE_LOG", WorkflowFixture.tempPath("steps.log"))
        WorkflowFixture.setEnv("LIGHTWEIGHT_TEST_PRE_MIGRATE_REQUIRES_BACKUP_DELAY_MS", "20000")
        openIdle()
        clickControl("connectButton")
        tryCompare(AppController, "backupRequirementPending", true, 15000)
        const switchedAt = Date.now()
        switchToQ()
        compare(AppController.connecting, false, "the connect to p ends with the switch")
        compare(AppController.backupRequirementPending, false)
        compare(AppController.connected, false)
        step("connect to q")
        verify(AppController.connectToProfile(), "a new connect starts while p's check still runs")
        verify(Date.now() - switchedAt < 2000)
        compare(AppController.connecting, true)
        // q's connect queues behind p's check, which the plugin cannot cut short.
        tryCompare(AppController, "connected", true, 40000)
        waitForIdle()
        compare(AppController.connectedTarget, "q")
        compare(AppController.backupRequired, true)
        compare(AppController.backupRequirementReasons.length, 1)
    }

    function test_a_profile_switch_during_the_post_init_hook_skips_the_old_backup_check() {
        prepare(true)
        WorkflowFixture.setEnv("LIGHTWEIGHT_TEST_PRE_MIGRATE_LOG", WorkflowFixture.tempPath("steps.log"))
        WorkflowFixture.setEnv("LIGHTWEIGHT_TEST_POST_INIT_DELAY_MS", "2000")
        WorkflowFixture.setEnv("LIGHTWEIGHT_TEST_PRE_MIGRATE_REQUIRES_BACKUP_DELAY_MS", "20000")
        openIdle()
        clickControl("connectButton")
        compare(AppController.connecting, true)
        const switchedAt = Date.now()
        switchToQ()
        compare(AppController.connecting, false)
        step("connect to q")
        verify(AppController.connectToProfile())
        // p's job stops before its 20 s backup check, so q is connected long before that would end.
        tryCompare(AppController, "connected", true, 15000)
        waitForIdle()
        verify(Date.now() - switchedAt < 15000, `q connected after ${Date.now() - switchedAt} ms`)
        compare(AppController.connectedTarget, "q")
        compare(AppController.backupRequired, true)
    }

    function test_a_second_connect_click_while_connecting_starts_nothing() {
        prepare()
        WorkflowFixture.setEnv("LIGHTWEIGHT_TEST_POST_INIT_DELAY_MS", "1000")
        openIdle()
        clickControl("connectButton")
        compare(AppController.connecting, true)
        compare(findRequired("connectButton").enabled, false)
        compare(findRequired("refreshButton").enabled, false)
        mouseClick(findRequired("connectButton"))
        mouseClick(findRequired("refreshButton"))
        // A click that slips past the disabled button is refused too.
        compare(AppController.connectToProfile(), false)
        tryCompare(AppController, "connected", true, 15000)
        waitForIdle()
        compare(WorkflowFixture.lineCount(postInitLog), 1, "exactly one connect ran")
    }

    function test_a_throwing_post_init_hook_is_logged_and_the_connect_completes() {
        prepare()
        WorkflowFixture.setEnv("LIGHTWEIGHT_TEST_POST_INIT_FAIL", "1")
        const lines = []
        AppController.logLine.connect((line, level) => lines.push(line))
        openIdle()
        connectAndWait()
        compare(AppController.connecting, false)
        compare(WorkflowFixture.lineCount(postInitLog), 1)
        verify(lines.some(line => line.includes("post-init failure requested by LIGHTWEIGHT_TEST_POST_INIT_FAIL")),
               "the log must name the failing hook:\n" + lines.join("\n"))
        compare(AppController.migrationCount, 1)
    }

    function test_refresh_rereads_the_history_on_the_worker() {
        prepare()
        openIdle()
        connectAndWait()
        compare(AppController.appliedCount, 0)

        migrateWithDbtool()
        clickControl("refreshButton")
        compare(AppController.connecting, true)
        compare(AppController.appliedCount, 0, "the history is read on the worker, not during the click")
        waitForIdle()
        compare(AppController.connected, true)
        compare(AppController.appliedCount, 1)
    }

    function test_sql_preview_arrives_asynchronously() {
        prepare()
        openIdle()
        connectAndWait()
        const replies = []
        AppController.migrationSqlPreviewReady.connect((timestamp, statements) => replies.push({ timestamp, statements }))
        AppController.requestMigrationSqlPreview("20240201000000")
        compare(replies.length, 0, "the preview is built on the worker")
        tryVerify(() => replies.length === 1, 15000)
        compare(replies[0].timestamp, "20240201000000")
        verify(replies[0].statements.some(statement => statement.toUpperCase().includes("CREATE TABLE")),
               "the preview must create the fixture's table:\n" + replies[0].statements.join("\n"))
    }
}
