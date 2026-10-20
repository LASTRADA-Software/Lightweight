// SPDX-License-Identifier: Apache-2.0
//
// The Expert view's migration workflows: the target options (latest, release, timestamp), a dry run,
// applying a selection, the bulk selection menu, rolling back to a release and previewing a
// migration's SQL.

import QtQuick
import QtTest
import Lightweight.Migrations
import DbtoolGuiWorkflow

WorkflowTestCase {
    name: "WorkflowExpert"

    /// The connection string of profile `p`'s database.
    property string cs: ""

    SignalSpy {
        id: finishedSpy
        signalName: "finished"
    }

    /// Writes profile `p` on a new SQLite database with the workflow plugin, opens the Expert view and connects.
    function openConnected() {
        cs = WorkflowFixture.newSqliteDatabase()
        const plugins = WorkflowFixture.newPluginsDir(["workflow"])
        const path = WorkflowFixture.writeProfileFile([{ name: "p", connectionString: cs, pluginsDir: plugins }], "p")
        WorkflowFixture.setSetting("config/profileStorePath", path)
        WorkflowFixture.setSetting("ui/viewMode", "expert")
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

    function isApplied(timestamp) {
        return count(`SELECT COUNT(*) FROM schema_migrations WHERE version = ${timestamp}`) === 1
    }

    /// Clicks the primary button and waits for the run it starts to finish; returns `[ok, summary]`.
    function runPrimaryAction() {
        const before = finishedSpy.count
        clickControl("primaryActionButton")
        tryVerify(() => finishedSpy.count > before, 60000, "the run must finish")
        tryCompare(AppController, "busy", false, 15000)
        return finishedSpy.signalArguments[finishedSpy.count - 1]
    }

    /// Ticks the checkbox of the migration row @p timestamp with a click.
    function tickRow(timestamp) {
        const row = findRequired("migrationRow_" + timestamp)
        const matches = []
        _collect(row, "migrationRowCheck", matches)
        compare(matches.length, 1)
        step(`tick ${timestamp}`)
        verify(waitForPolish(main), "the window must finish its layout")
        _scrollIntoView(matches[0], "migrationRowCheck")
        mouseClick(matches[0])
    }

    function test_target_latest_applies_everything() {
        openConnected()
        clickControl("optionSwitch_dryRun")
        compare(runPrimaryAction()[0], true)
        compare(appliedMigrations(), 3)
    }

    function test_target_release_migrates_to_that_release() {
        openConnected()
        clickControl("optionSwitch_dryRun")
        chooseRelease("1.0.0")
        compare(findRequired("primaryActionButton").text, "Apply → release 1.0.0")
        compare(runPrimaryAction()[0], true)
        compare(appliedMigrations(), 2)
        tryCompare(AppController, "currentReleaseLabel", "1.0.0", 5000)
    }

    function test_target_timestamp_applies_up_to_it() {
        openConnected()
        clickControl("optionSwitch_dryRun")
        clickControl("targetOption_timestamp")
        typeText("timestampField", "20240101000001")
        compare(findRequired("primaryActionButton").text, "Apply → timestamp 20240101000001")
        compare(runPrimaryAction()[0], true)
        compare(appliedMigrations(), 1)
        verify(isApplied("20240101000001"))
    }

    function test_dry_run_changes_nothing_and_lists_the_plan() {
        openConnected()
        compare(findRequired("optionSwitch_dryRun").checked, true)
        compare(runPrimaryAction()[0], true)
        compare(appliedMigrations(), 0)
        verify(AppController.logLines.text().indexOf("-- Dry-run: would apply 3 pending migration(s).") >= 0)
        const logView = findRequired("logView")
        verify(logView.visible, "the log view must be shown")
        compare(logView.count, AppController.logLines.count)
        verify(logView.count > 0, "the log view must list the run")
    }

    function test_selected_rows_apply_only_the_selection() {
        openConnected()
        clickControl("optionSwitch_dryRun")
        tickRow("20240101000001")
        tickRow("20240101000003")
        compare(AppController.selectionCount, 2)
        compare(findRequired("primaryActionButton").text, "Apply 2 selected migration(s)")
        compare(runPrimaryAction()[0], true)
        compare(appliedMigrations(), 2)
        verify(isApplied("20240101000001"))
        verify(isApplied("20240101000003"))
        verify(!isApplied("20240101000002"))
    }

    function test_select_all_and_clear() {
        openConnected()
        clickControl("bulkMenuButton")
        clickControl("selectAllPendingItem")
        compare(AppController.selectionCount, 3)
        compare(findRequired("primaryActionButton").text, "Dry-run 3 selected migration(s)")
        tryVerify(() => findRequired("clearSelectionButton").visible, 2000, "the selection banner must show")
        clickControl("clearSelectionButton")
        compare(AppController.selectionCount, 0)
        compare(findRequired("primaryActionButton").text, "Dry-run pending migrations")
    }

    function test_rollback_to_release_reverts_above_it() {
        openConnected()
        clickControl("optionSwitch_dryRun")
        compare(runPrimaryAction()[0], true)
        compare(appliedMigrations(), 3)
        compare(AppController.currentReleaseLabel, "2.0.0")
        chooseRelease("1.0.0")
        const before = finishedSpy.count
        clickControl("rollbackButton")
        tryVerify(() => finishedSpy.count > before, 30000, "the rollback must finish")
        compare(finishedSpy.signalArguments[finishedSpy.count - 1][0], true)
        tryCompare(AppController, "busy", false, 15000)
        compare(appliedMigrations(), 2)
        verify(!isApplied("20240101000003"))
        tryCompare(AppController, "currentReleaseLabel", "1.0.0", 5000)
        tryCompare(findRequired("migrationRow_20240101000003"), "status", "pending", 5000)
    }

    function test_sql_preview_dialog_shows_the_statements() {
        openConnected()
        const row = findRequired("migrationRow_20240101000001")
        step("double-click the first migration")
        verify(waitForPolish(main), "the window must finish its layout")
        _scrollIntoView(row, "migrationRow_20240101000001")
        mouseDoubleClickSequence(row)
        const dialog = findRequired("sqlPreviewDialog")
        tryVerify(() => dialog.visible, 5000, "the preview must open")
        tryVerify(() => findRequired("sqlPreviewText").text.indexOf("CREATE TABLE") >= 0, 10000,
                  "the preview must show the statements")
    }
}
