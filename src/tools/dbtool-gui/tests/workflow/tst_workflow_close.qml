// SPDX-License-Identifier: Apache-2.0
//
// The main window refuses to close while a run is in flight: it stays open, says why in the window,
// lets the run go on and offers to cancel a migration run, also while it winds down. Ctrl+Q and
// Qt.quit() take the same way. Once the run is over, the window closes.

import QtQuick
import QtTest
import Lightweight.Migrations
import DbtoolGuiWorkflow

WorkflowTestCase {
    name: "WorkflowClose"
    watchdogMode: UiWatchdog.Enforce

    /// Writes profile `p` on a new SQLite database with the recorder and workflow plugins, its
    /// "Long step" pre-migrate step taking @p longStepMs, opens the window and connects.
    function openConnected(longStepMs) {
        WorkflowFixture.setEnv("LIGHTWEIGHT_TEST_LONG_STEP_MS", String(longStepMs))
        const plugins = WorkflowFixture.newPluginsDir(["recorder", "workflow"])
        const path = WorkflowFixture.writeProfileFile([
            { name: "p", connectionString: WorkflowFixture.newSqliteDatabase(), pluginsDir: plugins },
        ], "p")
        WorkflowFixture.setSetting("config/profileStorePath", path)
        openMain()
        tryCompare(AppController, "busy", false, 15000)
        clickControl("connectButton")
        tryCompare(AppController, "connected", true, 15000)
        tryCompare(AppController, "busy", false, 15000)
    }

    function startRun() {
        step("start the run")
        AppController.runner.applyUpTo("")
        verify(AppController.runner.phase !== MigrationRunner.Idle, "the run must be in flight")
        compare(AppController.closeBlocked, true)
    }

    function test_closing_during_a_run_is_refused_and_says_why() {
        openConnected(3000)
        startRun()

        step("close the window")
        main.close()
        verify(main.visible, "the window must stay open during the run")
        const message = findRequired("closeBlockedMessage")
        tryVerify(() => message.visible, 2000, "the window must say why it stays open")
        // The runner announces its phase with a queued signal, so the text follows a moment later.
        tryCompare(message, "text", "A migration is running. Cancel it first or wait until it finishes.", 2000)
        verify(findRequired("closeBlockedCancelButton").visible, "a migration run can be cancelled from the message")
        compare(AppController.runner.phase, MigrationRunner.Running, "the run goes on")

        tryCompare(AppController.runner, "phase", MigrationRunner.Idle, 30000)
        tryCompare(message, "visible", false, 2000)
        compare(AppController.appliedCount, 3, "the run was not cancelled")
        step("close the window after the run")
        main.close()
        compare(main.visible, false, "the window closes once the run is over")
    }

    function test_the_cancel_action_ends_the_run_and_lets_the_window_close() {
        openConnected(20000)
        startRun()
        main.close()
        verify(main.visible)
        tryVerify(() => findRequired("closeBlockedMessage").visible, 2000)

        clickControl("closeBlockedCancelButton")
        // While the run winds down, closing is still refused.
        compare(AppController.runner.phase, MigrationRunner.Cancelling)
        compare(AppController.closeBlockedReason, "A migration is being cancelled. Wait until it stops.")
        main.close()
        verify(main.visible, "the window must stay open while the run is being cancelled")
        tryCompare(AppController.runner, "phase", MigrationRunner.Idle, 10000)
        compare(AppController.appliedCount, 0, "the run stopped before any migration")
        main.close()
        compare(main.visible, false)
    }

    function test_qt_quit_during_a_run_is_refused() {
        openConnected(3000)
        startRun()

        step("call Qt.quit()")
        Qt.quit()
        wait(0)
        verify(main.visible, "Qt.quit() must not close the window during the run")
        tryVerify(() => findRequired("closeBlockedMessage").visible, 2000, "Qt.quit() must say why the window stays")
        compare(AppController.runner.phase, MigrationRunner.Running, "the run goes on")
        tryCompare(AppController.runner, "phase", MigrationRunner.Idle, 30000)
    }

    function test_ctrl_q_goes_through_the_same_guard() {
        openConnected(3000)
        startRun()

        step("press Ctrl+Q")
        main.requestActivate()
        tryCompare(main, "active", true, 2000)
        keySequence("Ctrl+Q")
        verify(main.visible, "Ctrl+Q must not close the window during the run")
        tryVerify(() => findRequired("closeBlockedMessage").visible, 2000, "Ctrl+Q must say why the window stays")

        tryCompare(AppController.runner, "phase", MigrationRunner.Idle, 30000)
        keySequence("Ctrl+Q")
        tryCompare(main, "visible", false, 2000)
    }
}
