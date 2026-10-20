// SPDX-License-Identifier: Apache-2.0
//
// Updates cost what changed: the application's own write of an encrypted password does not reload the
// profiles, an external edit of the profile file does (after a run, when it came during one), a run's
// per-migration status changes never rebuild the migration and release lists, and the log keeps only
// its newest lines.

import QtQuick
import QtTest
import Lightweight.Migrations
import DbtoolGuiWorkflow

WorkflowTestCase {
    name: "WorkflowCheapUpdates"

    /// The profile file the test wrote.
    property string profileFile: ""
    /// The plugins directory of profile `p`.
    property string pluginsDirectory: ""
    /// The connection string of profile `p`'s database.
    property string databaseP: ""
    /// The reactions to profile file changes, in order.
    property var reactions: []

    /// Writes profile `p` (the default) on a new SQLite database with the plugins @p fixtureKeys, plus
    /// the extra profiles @p others, each `{ name, connectionString }`.
    function prepare(fixtureKeys, extraFields, others) {
        pluginsDirectory = WorkflowFixture.newPluginsDir(fixtureKeys)
        databaseP = WorkflowFixture.newSqliteDatabase()
        writeProfiles(extraFields, others)
        WorkflowFixture.setSetting("config/profileStorePath", profileFile)
    }

    function writeProfiles(extraFields, others) {
        const p = Object.assign({ name: "p", connectionString: databaseP, pluginsDir: pluginsDirectory }, extraFields || {})
        profileFile = WorkflowFixture.writeProfileFile([p].concat(others || []), "p")
    }

    /// Opens the Main window, waits until idle and starts recording the profile file reactions.
    function openIdle() {
        openMain()
        waitForIdle()
        reactions = []
        AppController.profileFileChangeHandled.connect(reaction => reactions.push(reaction))
    }

    function waitForIdle() {
        tryCompare(AppController, "busy", false, 15000)
    }

    function connectAndWait() {
        clickControl("connectButton")
        tryCompare(AppController, "connected", true, 15000)
        waitForIdle()
    }

    function profileNames() {
        const names = []
        const profiles = AppController.profiles
        for (let row = 0; row < profiles.rowCount(); ++row)
            names.push(String(profiles.data(profiles.index(row, 0), Qt.DisplayRole)))
        return names
    }

    function test_own_password_encryption_does_not_reload_twice() {
        const loadLog = WorkflowFixture.tempPath("load.log")
        WorkflowFixture.setEnv("LIGHTWEIGHT_TEST_PLUGIN_LOAD_LOG", loadLog)
        prepare(["recorder", "workflow"], { password: "secret" })
        openIdle()
        compare(WorkflowFixture.lineCount(loadLog), 1)

        clickControl("connectButton")
        tryCompare(AppController, "connected", true, 15000)
        tryVerify(() => reactions.length > 0, 10000, "the encrypt write must reach the file watcher")
        compare(reactions[0], AppController.IgnoredOwnWrite)
        waitForIdle()
        verify(reactions.every(reaction => reaction === AppController.IgnoredOwnWrite), "nothing may reload")
        compare(WorkflowFixture.lineCount(loadLog), 2, "the startup and the connect load the plugins, nothing else")
        verify(String(WorkflowFixture.queryProfileField(profileFile, "p", "password")).startsWith("enc:"))

        // A late, repeated notification of the same content is ignored as well.
        const seen = reactions.length
        outsideWatchdog("rewrite the profile file unchanged", () => WorkflowFixture.rewriteFile(profileFile))
        tryVerify(() => reactions.length > seen, 10000, "the rewrite must reach the file watcher")
        compare(reactions[seen], AppController.IgnoredOwnWrite)
    }

    function test_an_external_profile_file_change_reloads_the_profiles() {
        prepare(["workflow"], {}, [{ name: "q", connectionString: WorkflowFixture.newSqliteDatabase() }])
        openIdle()
        compare(AppController.profiles.rowCount(), 2)

        outsideWatchdog("write the profile file", () => writeProfiles({}, [
            { name: "q", connectionString: WorkflowFixture.newSqliteDatabase() },
            { name: "r", connectionString: WorkflowFixture.newSqliteDatabase() },
        ]))
        tryVerify(() => reactions.length > 0, 10000, "the edit must reach the file watcher")
        compare(reactions[0], AppController.Reloaded)
        compare(AppController.profiles.rowCount(), 3)
    }

    function test_an_external_change_during_a_run_is_deferred_not_lost() {
        WorkflowFixture.setEnv("LIGHTWEIGHT_TEST_LONG_STEP_MS", "2000")
        prepare(["recorder", "workflow"])
        openIdle()
        connectAndWait()

        clickControl("runButton")
        verify(AppController.runner.phase !== MigrationRunner.Idle, "the run must be in flight")
        outsideWatchdog("write the profile file", () => writeProfiles({}, [
            { name: "r", connectionString: WorkflowFixture.newSqliteDatabase() },
        ]))
        tryVerify(() => reactions.length > 0, 10000, "the edit must reach the file watcher")
        compare(reactions[0], AppController.Deferred)
        verify(AppController.runner.phase !== MigrationRunner.Idle, "the edit must arrive during the run")
        compare(AppController.profiles.rowCount(), 1)

        tryCompare(AppController.runner, "phase", MigrationRunner.Idle, 30000)
        tryVerify(() => reactions.length > 1, 15000, "the deferred change must be applied once idle")
        compare(reactions[1], AppController.Reloaded)
        compare(AppController.profiles.rowCount(), 2)
        verify(profileNames().indexOf("r") >= 0)
        compare(AppController.appliedCount, 3)
    }

    function test_an_external_change_during_a_slow_connect_is_deferred_not_lost() {
        WorkflowFixture.setEnv("LIGHTWEIGHT_TEST_POST_INIT_DELAY_MS", "2000")
        prepare(["recorder", "workflow"])
        openIdle()

        clickControl("connectButton")
        compare(AppController.connecting, true)
        outsideWatchdog("write the profile file", () => writeProfiles({}, [
            { name: "r", connectionString: WorkflowFixture.newSqliteDatabase() },
        ]))
        tryVerify(() => reactions.length > 0, 10000, "the edit must reach the file watcher")
        compare(reactions[0], AppController.Deferred)
        verify(AppController.connecting, "the edit must arrive during the connect")
        compare(AppController.profiles.rowCount(), 1)

        tryCompare(AppController, "connected", true, 15000)
        tryVerify(() => reactions.length > 1, 15000, "the deferred change must be applied once idle")
        compare(reactions[1], AppController.Reloaded)
        compare(AppController.profiles.rowCount(), 2)
        verify(profileNames().indexOf("r") >= 0)
    }

    function test_a_run_of_two_thousand_migrations_never_rebuilds_the_lists() {
        WorkflowFixture.setEnv("LIGHTWEIGHT_TEST_BULK_MIGRATIONS", "2000")
        WorkflowFixture.setSetting("ui/viewMode", "expert")
        prepare(["bulk"])
        openIdle()
        connectAndWait()
        compare(AppController.migrationCount, 2000)
        compare(AppController.pendingCount, 2000)
        const before = AppController.migrations.tallyRebuildCount
        const table = findRequired("migrationList")
        let tableResets = 0
        table.filterModel.modelReset.connect(() => ++tableResets)
        table.filterModel.layoutChanged.connect(() => ++tableResets)

        clickControl("optionSwitch_dryRun")
        compare(findRequired("optionSwitch_dryRun").checked, false)
        clickControl("primaryActionButton")
        verify(AppController.runner.phase !== MigrationRunner.Idle, "the run must be in flight")
        tryCompare(AppController.runner, "phase", MigrationRunner.Idle, 120000)
        waitForIdle()

        compare(AppController.appliedCount, 2000)
        compare(AppController.pendingCount, 0)
        compare(AppController.migrations.tallyRebuildCount, before + 1, "only the re-read after the run rebuilds")
        compare(tableResets, 1, "only the re-read after the run resets the table")
        const tally = AppController.migrations.releaseTally("")
        compare(tally.total, 2000)
        compare(tally.applied, 2000)

        // Rows a filter or a collapsed release hides get no delegates.
        const list = findRequired("migrationListView")
        table.filterTab = "pending"
        compare(list.count, 0)
        tryVerify(() => findRequired("migrationListNoMatch").visible, 2000, "a filter that matches nothing must say so")
        table.filterTab = "all"
        compare(list.count, 2000)
        tryCompare(findRequired("migrationListNoMatch"), "visible", false, 2000)
        table.toggleExpanded("")
        compare(list.count, 1, "a collapsed release keeps only the row carrying its header")
        table.toggleExpanded("")
        compare(list.count, 2000)
    }

    function test_the_log_stays_bounded_and_responsive() {
        // The log panel lives in the Expert view.
        WorkflowFixture.setSetting("ui/viewMode", "expert")
        prepare(["workflow"])
        openIdle()
        step("emit 20000 runner log lines")
        WorkflowFixture.emitRunnerLogLines(20000)
        tryCompare(AppController.logLines, "count", 10000, 30000)
        const lines = AppController.logLines.text().split("\n")
        verify(lines[lines.length - 1].endsWith("Runner log line 20000 of 20000"))
        verify(lines.every(line => !line.endsWith("Runner log line 10000 of 20000")), "the oldest lines must be dropped")
        const dropped = AppController.logLines.droppedCount
        verify(dropped > 10000, "every runner line beyond the capacity and the startup lines are dropped")
        compare(lines.length, 10001, "the dropped-lines note and the kept lines")
        compare(lines[0], `… ${dropped} earlier lines dropped`)
        const note = findRequired("logDroppedLines")
        verify(note.visible, "the log must say that lines were dropped")
        compare(note.text, `… ${dropped} earlier lines dropped`)
    }
}
