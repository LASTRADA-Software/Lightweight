// SPDX-License-Identifier: Apache-2.0
//
// Base of the dbtool-gui workflow tests: opens the real Main window, finds and clicks its controls
// by objectName, and runs a UI-thread watchdog from the window's first frame on. `cleanup()` runs
// the file's `cleanupWorkflow()`, then releases the held migration lock, removes the user DSNs,
// restores the environment, and reports the longest stall, failing the test in Enforce mode when
// it exceeds the budget. Test files override `cleanupWorkflow()`, never `cleanup()`.

import QtQuick
import QtTest
import Lightweight.Migrations
import DbtoolGuiWorkflow

TestCase {
    id: testCase
    when: windowShown
    visible: true
    width: 400
    height: 300

    /// What a stall over the budget does: UiWatchdog.Record reports it, UiWatchdog.Enforce fails the test.
    property int watchdogMode: UiWatchdog.Record
    /// The heartbeat, armed by `openMain()` on the window's first frame.
    property UiWatchdog watchdog: UiWatchdog { mode: testCase.watchdogMode }
    /// The Main window `openMain()` created; destroyed after each test function.
    property var main: null

    property bool _watchdogArmed: false

    Component { id: mainComponent; Main {} }

    SignalSpy {
        id: firstFrameSpy
        target: testCase.watchdog
        signalName: "firstFrame"
    }

    /// Creates the Main window, arms the watchdog on its first frame and waits for that frame.
    function openMain() {
        firstFrameSpy.clear()
        main = createTemporaryObject(mainComponent, testCase)
        verify(main !== null, "the Main window must instantiate")
        watchdog.armOnFirstFrame(main)
        _watchdogArmed = true
        firstFrameSpy.wait(10000)
        return main
    }

    function _collect(item, objectName, matches) {
        if (!item)
            return
        if (item.objectName === objectName)
            matches.push(item)
        const children = item.children
        if (!children)
            return
        for (let index = 0; index < children.length; ++index)
            _collect(children[index], objectName, matches)
    }

    /// The control named `objectName`, preferring a visible one when the views hold several; fails the test when there is none.
    function findRequired(objectName) {
        verify(main !== null, "openMain() must run first")
        const matches = []
        _collect(main.contentItem, objectName, matches)
        _collect(main.overlay, objectName, matches)
        const visible = matches.filter(item => item.visible)
        const found = visible.length > 0 ? visible[0] : (matches.length > 0 ? matches[0] : findChild(main, objectName))
        verify(found !== null && found !== undefined, `no control named "${objectName}"`)
        return found
    }

    /// Clicks the control named `objectName` after checking that it is enabled.
    function clickControl(objectName) {
        const control = findRequired(objectName)
        verify(control.enabled, `"${objectName}" must be enabled to be clicked`)
        mouseClick(control)
        return control
    }

    /// Stops the watchdog, logs the longest stall and, in Enforce mode, fails the test when it exceeds the budget.
    function finishWatchdog() {
        if (!_watchdogArmed)
            return
        _watchdogArmed = false
        watchdog.stop()
        const report = `UI watchdog: max gap ${watchdog.maxGapMs} ms of ${watchdog.budgetMs} ms`
        console.log(report)
        if (watchdog.mode === UiWatchdog.Enforce && watchdog.exceeded)
            fail(report)
    }

    /// Per-file cleanup, run first by `cleanup()`; override it instead of `cleanup()`.
    function cleanupWorkflow() {
    }

    function cleanup() {
        try {
            cleanupWorkflow()
        } finally {
            WorkflowFixture.releaseMigrationLock()
            WorkflowFixture.removeUserDsns()
            WorkflowFixture.restoreEnv()
            finishWatchdog()
        }
    }
}
