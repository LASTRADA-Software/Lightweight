// SPDX-License-Identifier: Apache-2.0
//
// Pre-migrate steps in a run: they run before the first migration, the task list shows a step and its
// units with their state, a failed unit ends its row failed and applies nothing, and a cancel between
// the units stops the rest from starting.

import QtQuick
import QtTest
import Lightweight.Migrations
import DbtoolGuiWorkflow

WorkflowTestCase {
    name: "WorkflowSteps"

    /// The connection string of profile `p`'s database.
    property string cs: ""
    /// The file the recorder step appends one line to per execution.
    property string stepLog: ""

    SignalSpy {
        id: finishedSpy
        signalName: "finished"
    }

    /// Writes profile `p` on a new SQLite database with the recorder and workflow plugins, opens the
    /// Simple view and connects.
    function openConnected() {
        cs = WorkflowFixture.newSqliteDatabase()
        const plugins = WorkflowFixture.newPluginsDir(["recorder", "workflow"])
        const path = WorkflowFixture.writeProfileFile([{ name: "p", connectionString: cs, pluginsDir: plugins }], "p")
        WorkflowFixture.setSetting("config/profileStorePath", path)
        WorkflowFixture.setSetting("ui/viewMode", "simple")
        stepLog = WorkflowFixture.tempPath("steps.log")
        WorkflowFixture.setEnv("LIGHTWEIGHT_TEST_PRE_MIGRATE_LOG", stepLog)
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

    /// Sets the long step's duration and unit count, and optionally the unit that fails.
    function configureLongStep(milliseconds, units, failAt) {
        WorkflowFixture.setEnv("LIGHTWEIGHT_TEST_LONG_STEP_MS", String(milliseconds))
        WorkflowFixture.setEnv("LIGHTWEIGHT_TEST_LONG_STEP_UNITS", String(units))
        if (failAt !== undefined)
            WorkflowFixture.setEnv("LIGHTWEIGHT_TEST_LONG_STEP_FAIL_AT", String(failAt))
    }

    /// The visible task rows as `{ title, state, reason, value, to }`, in order.
    function taskRows() {
        const rows = []
        _collect(main.contentItem, "taskProgressRow", rows)
        return rows.filter(row => row.visible).map(row => {
            const states = []
            const reasons = []
            const bars = []
            _collect(row, "taskProgressBar", bars)
            _collect(row, "taskProgressState", states)
            _collect(row, "taskProgressFailureReason", reasons)
            return { title: bars[0].title, value: bars[0].value, to: bars[0].to,
                     state: states.length > 0 ? states[0].text : "",
                     reason: reasons.length > 0 && reasons[0].visible ? reasons[0].text : "" }
        })
    }

    /// The row titled @p title, or undefined.
    function rowTitled(title) {
        return taskRows().find(row => row.title === title)
    }

    /// Clicks Run and waits for the run to end.
    function runToTheEnd() {
        const before = finishedSpy.count
        clickControl("runButton")
        tryVerify(() => finishedSpy.count > before, 60000, "the run must finish")
        tryCompare(AppController, "busy", false, 15000)
    }

    function test_steps_run_before_the_migrations() {
        openConnected()
        runToTheEnd()

        compare(WorkflowFixture.lineCount(stepLog), 1)
        verify(WorkflowFixture.fileText(stepLog).startsWith("pre-migrate dryRun=0 pending=3 "),
               "the step must see all three migrations still pending:\n" + WorkflowFixture.fileText(stepLog))
        compare(appliedMigrations(), 3)
    }

    function test_overall_and_sub_task_progress_is_shown() {
        configureLongStep(3000, 3)
        openConnected()
        clickControl("runButton")
        for (const unit of [1, 2, 3]) {
            const title = `Unit ${unit} of 3`
            tryVerify(() => rowTitled(title) !== undefined, 15000, `"${title}" must show while it runs`)
            compare(rowTitled(title).state, "", "a running row has no state")
            compare(rowTitled("Long step").state, "", "the step is not done while its units run")
            tryVerify(() => rowTitled("Long step").to === 3 && rowTitled("Long step").value === unit - 1, 5000,
                      `the step must show ${unit - 1} of 3 units done while unit ${unit} runs`)
        }
        tryVerify(() => finishedSpy.count > 0, 60000, "the run must finish")
        tryCompare(AppController, "busy", false, 15000)

        const rows = taskRows()
        const titles = rows.map(row => row.title)
        verify(!titles.some(title => title.startsWith("Unit ")), "a finished unit's row goes away:\n" + titles.join("\n"))
        compare(rowTitled("Long step").value, 3)
        compare(rowTitled("Long step").state, "done")
        compare(rowTitled("Migrations").state, "done")
        compare(appliedMigrations(), 3)
    }

    function test_a_failed_step_ends_failed_and_applies_nothing() {
        configureLongStep(1500, 3, 2)
        openConnected()
        runToTheEnd()

        const failed = rowTitled("Unit 2 of 3")
        verify(failed !== undefined, "the failing unit must be listed")
        compare(failed.state, "❌ failed")
        compare(failed.reason, "unit 2 failed on purpose")
        compare(rowTitled("Long step").state, "❌ failed")
        compare(rowTitled("Unit 1 of 3"), undefined, "the finished first unit's row is gone")
        compare(finishedSpy.signalArguments[finishedSpy.count - 1][0], false)
        compare(appliedMigrations(), 0)
    }

    function test_cancel_between_units() {
        configureLongStep(6000, 3)
        const unitLog = WorkflowFixture.tempPath("units.log")
        WorkflowFixture.setEnv("LIGHTWEIGHT_TEST_LONG_STEP_LOG", unitLog)
        openConnected()
        clickControl("runButton")
        tryVerify(() => rowTitled("Unit 1 of 3") !== undefined, 15000, "the first unit must be running")
        tryVerify(() => findRequired("cancelButton").visible && findRequired("cancelButton").enabled, 10000)
        clickControl("cancelButton")

        tryVerify(() => finishedSpy.count > 0, 30000, "the run must end")
        tryCompare(AppController, "busy", false, 15000)
        compare(finishedSpy.signalArguments[0][1], "Cancelled during the pre-migrate steps; no migration was applied.")
        compare(WorkflowFixture.fileText(unitLog).trim(), "unit 1 started", "only the first unit may start")
        compare(rowTitled("Long step").state, "cancelled")
        compare(appliedMigrations(), 0)
    }
}
