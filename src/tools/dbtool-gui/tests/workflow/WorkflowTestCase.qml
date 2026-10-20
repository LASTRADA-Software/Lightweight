// SPDX-License-Identifier: Apache-2.0
//
// Base of the dbtool-gui workflow tests: opens the real Main window, finds and clicks its controls
// by objectName, and runs a UI-thread watchdog from the window's first frame on. `cleanup()` runs
// the file's `cleanupWorkflow()`, then releases the held migration lock, removes the user DSNs,
// restores the environment, and reports the longest stall, failing the test in Enforce mode when
// it exceeds the budget, or when a QML file logged a warning. Test files override `cleanupWorkflow()`,
// never `cleanup()`. The report names the step of the longest stall: `openMain`, `clickControl` and
// `step()` name steps, and fixture work the test does while the window is open runs through
// `outsideWatchdog`, so it does not count.

import QtQuick
import QtQuick.Controls
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
    property int watchdogMode: UiWatchdog.Enforce
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

    /// Fails when a QML file logged a warning while the module loaded, such as a type that shadows a signal of its base.
    function initTestCase() {
        compare(WorkflowFixture.qmlWarnings(), [], "QML files must load without warnings")
    }

    /// Creates the Main window, arms the watchdog on its first frame and waits for that frame.
    function openMain() {
        step("open the Main window")
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
        _collect(main.contentItem.Overlay.overlay, objectName, matches)
        const visible = matches.filter(item => item.visible)
        const found = visible.length > 0 ? visible[0] : (matches.length > 0 ? matches[0] : findChild(main, objectName))
        verify(found !== null && found !== undefined, `no control named "${objectName}"`)
        return found
    }

    /// Scrolls every Flickable around `control` so that it is in view, as a user would; a click outside
    /// the window is lost. Fails when a Flickable the user cannot scroll hides it.
    function _scrollIntoView(control, objectName) {
        for (let item = control.parent; item; item = item.parent) {
            if (item.contentY === undefined || item.contentItem === undefined || item.flickableDirection === undefined)
                continue
            const top = control.mapToItem(item.contentItem, 0, 0).y
            if (top >= item.contentY && top + control.height <= item.contentY + item.height)
                continue
            verify(item.interactive !== false, `"${objectName}" is out of view in a Flickable the user cannot scroll`)
            step(`scroll ${objectName} into view`)
            const lowest = Math.max(item.originY, item.originY + item.contentHeight - item.height)
            item.contentY = Math.min(Math.max(item.originY, top - (item.height - control.height) / 2), lowest)
        }
    }

    /// Clicks the control named `objectName` after checking that it is enabled, letting the layout settle
    /// and scrolling it into view.
    function clickControl(objectName) {
        step(`click ${objectName}`)
        return _clickItem(findRequired(objectName), objectName)
    }

    /// The control named `objectName` inside the item named `rowName` (a list row, say); fails the test
    /// unless there is exactly one.
    function findInRow(rowName, objectName) {
        const row = findRequired(rowName)
        const matches = []
        _collect(row, objectName, matches)
        compare(matches.length, 1, `"${rowName}" must hold exactly one "${objectName}"`)
        return matches[0]
    }

    /// Like `clickControl`, for the control named `objectName` inside the row named `rowName`.
    function clickInRow(rowName, objectName) {
        step(`click ${objectName} of ${rowName}`)
        return _clickItem(findInRow(rowName, objectName), `${rowName}/${objectName}`)
    }

    function _clickItem(control, objectName) {
        verify(control.enabled, `"${objectName}" must be enabled to be clicked`)
        // Layouts size their items on the next polish; a click before it can miss a control that just appeared.
        verify(waitForPolish(main), "the window must finish its layout")
        _scrollIntoView(control, objectName)
        step(`click ${objectName}`)
        const centre = control.mapToItem(main.contentItem, control.width / 2, control.height / 2)
        verify(centre.x >= 0 && centre.y >= 0 && centre.x < main.width && centre.y < main.height,
               `"${objectName}" must be inside the window to be clicked`)
        mouseClick(control)
        return control
    }

    /// Replaces the text of the field named `objectName` by typing @p text key by key, as a user does.
    function typeText(objectName, text) {
        const field = clickControl(objectName)
        keyClick(Qt.Key_A, Qt.ControlModifier)
        keyClick(Qt.Key_Delete)
        for (const character of text)
            keyClick(character)
        return field
    }

    /// Like `typeText`, then presses Enter, which fires the field's `editingFinished` and `accepted`.
    function typeInto(objectName, text) {
        const field = typeText(objectName, text)
        keyClick(Qt.Key_Return)
        return field
    }

    /// Picks row @p row of the ComboBox named `objectName` with the arrow keys, which emits its `activated`.
    function pickWithKeys(objectName, row) {
        const combo = findRequired(objectName)
        combo.forceActiveFocus()
        verify(combo.activeFocus, `"${objectName}" must take the focus`)
        step(`pick row ${row} of ${objectName}`)
        for (let guard = 0; combo.currentIndex !== row && guard < 100; ++guard)
            keyClick(combo.currentIndex < row ? Qt.Key_Down : Qt.Key_Up)
        compare(combo.currentIndex, row)
    }

    /// Picks release @p version as the migration target with key input in the release picker.
    function chooseRelease(version) {
        clickControl("targetOption_release")
        const releases = AppController.releases
        for (let row = 0; row < releases.rowCount(); ++row) {
            if (releases.data(releases.index(row, 0), ReleaseListModel.VersionRole) === version) {
                pickWithKeys("releasePicker", row)
                return
            }
        }
        fail(`no release ${version}`)
    }

    /// Names what the test does from now on; the watchdog report names the step of the longest stall.
    function step(name) {
        watchdog.step = name
    }

    /// Runs `work`, fixture work of the test itself such as copying plugins or running dbtool, with the
    /// watchdog paused, so the GUI-thread time it takes does not count against the application.
    function outsideWatchdog(name, work) {
        step(name)
        watchdog.pause()
        try {
            return work()
        } finally {
            watchdog.resume()
        }
    }

    /// Stops the watchdog, logs the longest stall and, in Enforce mode, fails the test when it exceeds the budget.
    function finishWatchdog() {
        if (!_watchdogArmed)
            return
        _watchdogArmed = false
        watchdog.stop()
        const report = `UI watchdog: max gap ${watchdog.maxGapMs} ms of ${watchdog.budgetMs} ms`
                       + (watchdog.maxGapStep !== "" ? ` (step: ${watchdog.maxGapStep})` : "")
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
            compare(WorkflowFixture.qmlWarnings(), [], "the workflow must log no QML warning")
        }
    }
}
