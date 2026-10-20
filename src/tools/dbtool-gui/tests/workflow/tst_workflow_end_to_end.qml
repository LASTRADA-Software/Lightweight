// SPDX-License-Identifier: Apache-2.0
//
// The whole upgrade as a user does it, under the watchdog: connect, read the backup requirement, dry
// run in the Expert view, back up, run in the Simple view, then a dry run and a run again that find
// nothing left to do. By default it runs against the `unicodeCandidate` fixture: on SQL Server when
// DBTOOL_GUI_TEST_MSSQL_CONNECTION_STRING is set, seeded with the narrow columns a legacy database
// has, else on SQLite. The DBTOOL_GUI_WORKFLOW_* variables (`WorkflowFixture.externalRun`) point it
// at plugins, a profile and expectations of one's own instead; with a profile file of one's own
// it never uses the fixture plugins.

import QtQuick
import QtTest
import Lightweight.Migrations
import DbtoolGuiWorkflow

WorkflowTestCase {
    name: "WorkflowEndToEnd"
    watchdogMode: UiWatchdog.Enforce

    /// The expectations of this run: `pendingBefore`, `backupReasons`, `dryRunLogLine`, `secondDryRunLogLine`.
    property var expected: ({})
    /// The profile the test connects.
    property string profile: ""
    /// The plugins directory the application must load from; empty when the profile file decides.
    property string expectedPluginsDir: ""
    /// The connection string of the SQL Server fixture database; empty otherwise.
    property string mssqlFixtureConnection: ""

    SignalSpy {
        id: finishedSpy
        signalName: "finished"
    }
    SignalSpy {
        id: backupFinishedSpy
        signalName: "finished"
    }

    /// Creates the `unicodeCandidate` database on SQL Server (seeded with narrow columns) or SQLite,
    /// and writes profile `e2e` on it.
    /// @return The expectations of the fixture database.
    function prepareFixtureDatabase(pluginsDir) {
        const onMssql = WorkflowFixture.mssqlAvailable()
        const cs = onMssql ? WorkflowFixture.newMssqlDatabase() : WorkflowFixture.newSqliteDatabase()
        if (onMssql) {
            WorkflowFixture.runDbtool(["--plugins-dir", pluginsDir, "--connection-string", cs, "apply", "20240501000000"],
                                      { LIGHTWEIGHT_TEST_UNICODE_LEGACY_TYPES: "1" }, longRunTimeoutMs)
            WorkflowFixture.runDbtool(["--plugins-dir", pluginsDir, "--connection-string", cs, "rewrite-checksums", "--yes"],
                                      {}, longRunTimeoutMs)
            mssqlFixtureConnection = cs
        }
        const path = WorkflowFixture.writeProfileFile([{ name: "e2e", connectionString: cs, pluginsDir: pluginsDir }],
                                                      "e2e")
        WorkflowFixture.setSetting("config/profileStorePath", path)
        profile = "e2e"
        if (onMssql)
            return { pendingBefore: 1, backupReasons: ['step "Widen text columns"'],
                     dryRunLogLine: "Would upgrade 2 column(s) across 1 table(s) in 1 unit(s).",
                     secondDryRunLogLine: "No text columns to upgrade." }
        // SQLite does not tell narrow from wide text, so the step widens nothing and requires no backup.
        return { pendingBefore: 2, backupReasons: [],
                 dryRunLogLine: "No text columns to upgrade.", secondDryRunLogLine: "No text columns to upgrade." }
    }

    /// Fails unless @p expectations, read from @p file, has the keys and types the test reads.
    function checkExpectations(expectations, file) {
        verify(typeof expectations.pendingBefore === "number", `${file}: "pendingBefore" must be a number`)
        // A JSON array arrives as a QVariantList, which is no JavaScript Array.
        const reasons = expectations.backupReasons
        const isList = reasons !== null && typeof reasons === "object" && typeof reasons.length === "number"
        verify(isList && Array.prototype.every.call(reasons, reason => typeof reason === "string"),
               `${file}: "backupReasons" must be an array of strings`)
        verify(typeof expectations.dryRunLogLine === "string", `${file}: "dryRunLogLine" must be a string`)
        verify(typeof expectations.secondDryRunLogLine === "string", `${file}: "secondDryRunLogLine" must be a string`)
    }

    /// Applies the DBTOOL_GUI_WORKFLOW_* settings, or prepares the fixture database, before the window opens.
    function prepare() {
        const external = WorkflowFixture.externalRun()
        if (external.profileFile !== "") {
            verify(external.profile !== "", "DBTOOL_GUI_WORKFLOW_PROFILE_FILE needs DBTOOL_GUI_WORKFLOW_PROFILE")
            // The fixture migrations must never reach a database of one's own.
            verify(external.pluginsDir !== "" || external.skipFixtures,
                   "DBTOOL_GUI_WORKFLOW_PROFILE_FILE needs DBTOOL_GUI_WORKFLOW_PLUGINS_DIR or DBTOOL_GUI_WORKFLOW_SKIP_FIXTURES=1")
            verify(external.expectations !== undefined,
                   "DBTOOL_GUI_WORKFLOW_PROFILE_FILE needs DBTOOL_GUI_WORKFLOW_EXPECTATIONS")
            checkExpectations(external.expectations, external.expectationsFile)
            let profileFile = external.profileFile
            if (external.pluginsDir !== "") {
                // Profiles name their own plugins directories, which win over the Settings page's.
                profileFile = WorkflowFixture.copyProfileFileWithoutPluginsDirs(external.profileFile)
                WorkflowFixture.setSetting("config/pluginsDir", external.pluginsDir)
                expectedPluginsDir = external.pluginsDir
            }
            WorkflowFixture.setSetting("config/profileStorePath", profileFile)
            profile = external.profile
            expected = external.expectations
        } else {
            verify(!external.skipFixtures, "DBTOOL_GUI_WORKFLOW_SKIP_FIXTURES=1 needs DBTOOL_GUI_WORKFLOW_PROFILE_FILE")
            const pluginsDir = external.pluginsDir !== "" ? external.pluginsDir
                                                          : WorkflowFixture.newPluginsDir(["unicodeCandidate"])
            expectedPluginsDir = pluginsDir
            const fixtureExpectations = prepareFixtureDatabase(pluginsDir)
            // The widened columns are the fixture plugin's; other plugins may define them otherwise.
            if (external.pluginsDir !== "")
                mssqlFixtureConnection = ""
            if (external.expectations !== undefined)
                checkExpectations(external.expectations, external.expectationsFile)
            expected = external.expectations !== undefined ? external.expectations : fixtureExpectations
        }
        WorkflowFixture.setSetting("connection/mode", "profile")
        WorkflowFixture.setSetting("ui/viewMode", "simple")
        WorkflowFixture.setSetting("backup/folder", WorkflowFixture.tempPath("backups"))
    }

    /// Fails unless the last plugin load read `expectedPluginsDir`; nothing when the profile file decides.
    function checkLoadedPluginsDir() {
        if (expectedPluginsDir === "")
            return
        const marker = "Plugins directory: "
        const lines = AppController.logLines.text().split("\n").filter(line => line.indexOf(marker) >= 0)
        verify(lines.length > 0, "the plugin load must log its directory")
        const last = lines[lines.length - 1]
        compare(last.substring(last.indexOf(marker) + marker.length), expectedPluginsDir,
                "the plugins must load from the requested directory")
    }

    /// @return The data type of column @p column of `unicode_candidate` in the SQL Server fixture database.
    function fixtureColumnType(column) {
        return outsideWatchdog("read the column type", () => WorkflowFixture.queryScalar(mssqlFixtureConnection,
            `SELECT DATA_TYPE FROM INFORMATION_SCHEMA.COLUMNS WHERE TABLE_NAME = 'unicode_candidate' AND COLUMN_NAME = '${column}'`))
    }

    /// @return The row of the model @p model whose name role (257) is @p name; fails when there is none.
    function rowNamed(model, name) {
        for (let row = 0; row < model.rowCount(); ++row) {
            if (model.data(model.index(row, 0), 257) === name)
                return row
        }
        fail(`no profile named ${name}`)
        return -1
    }

    function waitForIdle() {
        tryCompare(AppController, "backupRequirementPending", false, longRunTimeoutMs)
        tryCompare(AppController, "busy", false, longRunTimeoutMs)
    }

    /// Clicks @p objectName and waits for the run it starts to end.
    /// @return The run's `finished` arguments: whether it succeeded, and its summary.
    function runAndWait(objectName) {
        const before = finishedSpy.count
        clickControl(objectName)
        tryVerify(() => finishedSpy.count > before, longRunTimeoutMs, `the run of ${objectName} must finish`)
        waitForIdle()
        return finishedSpy.signalArguments[finishedSpy.count - 1]
    }

    /// Switches to the Simple view and back to its review step, then runs every pending migration.
    /// @return The run's `finished` arguments.
    function simpleRun() {
        clickControl("viewModeControl_simple")
        // An earlier run, the Expert dry run among them, leaves the Simple view on its outcome.
        if (findRequired("doneButton").visible)
            clickControl("doneButton")
        tryVerify(() => findRequired("runButton").visible, 5000, "the Simple view must offer Run")
        compare(findRequired("simpleBackupAlert").visible, false, "no backup may be required any more")
        return runAndWait("runButton")
    }

    /// Clears the log, runs a dry run in the Expert view and checks that it logged @p line.
    function dryRunLogs(line) {
        clickControl("viewModeControl_expert")
        if (!findRequired("optionSwitch_dryRun").checked)
            clickControl("optionSwitch_dryRun")
        compare(findRequired("optionSwitch_dryRun").checked, true)
        AppController.logLines.clear()
        const outcome = runAndWait("primaryActionButton")
        compare(outcome[0], true, "the dry run must succeed: " + outcome[1])
        const log = AppController.logLines.text()
        verify(log.indexOf(line) >= 0, `the dry run must log "${line}":\n${log}`)
    }

    /// The visible task rows as `{ title, state }`, in order.
    function taskRows() {
        const rows = []
        _collect(main.contentItem, "taskProgressRow", rows)
        return rows.filter(row => row.visible).map(row => {
            const bars = []
            const states = []
            _collect(row, "taskProgressBar", bars)
            _collect(row, "taskProgressState", states)
            return { title: bars.length > 0 ? bars[0].title : "", state: states.length > 0 ? states[0].text : "" }
        })
    }

    function test_end_to_end_upgrade_and_idempotent_rerun() {
        prepare()
        let loadingAtFirstFrame = null
        watchdog.firstFrame.connect(() => loadingAtFirstFrame = AppController.pluginsLoading)
        openMain()
        compare(loadingAtFirstFrame, true, "the first frame must come before the plugins finish loading")
        tryCompare(AppController, "pluginsLoading", false, longRunTimeoutMs)
        tryCompare(AppController, "busy", false, longRunTimeoutMs)
        finishedSpy.target = AppController.runner
        backupFinishedSpy.target = AppController.backupRunner

        pickWithKeys("profileCombo", rowNamed(AppController.profiles, profile))
        compare(AppController.currentProfile, profile)
        tryCompare(AppController, "busy", false, longRunTimeoutMs)
        clickControl("connectButton")
        tryCompare(AppController, "connected", true, longRunTimeoutMs)
        waitForIdle()
        checkLoadedPluginsDir()

        compare(AppController.pendingCount, expected.pendingBefore)
        const reasons = Array.prototype.slice.call(AppController.backupRequirementReasons)
        compare(JSON.stringify(reasons), JSON.stringify(expected.backupReasons))
        const backupExpected = expected.backupReasons.length > 0
        compare(findRequired("simpleBackupAlert").visible, backupExpected)
        compare(findRequired("runButton").enabled, !backupExpected, "a required backup blocks the run")

        step("first dry run")
        dryRunLogs(expected.dryRunLogLine)

        if (backupExpected) {
            clickControl("backUpNowButton")
            tryVerify(() => backupFinishedSpy.count > 0, longRunTimeoutMs,
                      "the backup must finish:\n" + AppController.logLines.text())
            compare(backupFinishedSpy.signalArguments[0][0], true,
                    "the backup must succeed: " + backupFinishedSpy.signalArguments[0][1])
            waitForIdle()
            compare(AppController.backupRequired, false, "the backup must satisfy the requirement")
        }
        compare(findRequired("actionsBackupAlert").visible, false)

        step("run")
        const outcome = simpleRun()
        compare(outcome[0], true, "the run must succeed: " + outcome[1])
        const rows = taskRows()
        const summary = rows.map(row => `${row.title}: ${row.state}`).join("\n")
        verify(rows.length > 0, "the run must list its tasks")
        verify(rows.every(row => row.state === "done"), "every task row must end done:\n" + summary)
        compare(AppController.pendingCount, 0)
        tryCompare(findRequired("successTitle"), "text", "Database is up to date", 5000)
        if (mssqlFixtureConnection !== "") {
            compare(fixtureColumnType("name"), "nvarchar", "the run must widen unicode_candidate.name")
            compare(fixtureColumnType("code"), "nchar", "the run must widen unicode_candidate.code")
        }

        step("second dry run")
        dryRunLogs(expected.secondDryRunLogLine)

        step("second run")
        const rerun = simpleRun()
        compare(rerun[0], true, "the second run must succeed: " + rerun[1])
        compare(rerun[1], "Applied 0 migrations.")
        tryCompare(findRequired("simpleView"), "lastSummary", "Applied 0 migrations.", 5000)
        compare(AppController.pendingCount, 0)

        console.log(`End-to-end: max UI gap ${watchdog.maxGapMs} ms (step: ${watchdog.maxGapStep})`)
    }
}
