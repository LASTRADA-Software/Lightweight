// SPDX-License-Identifier: Apache-2.0
//
// Behaviour tests for BackupRequiredAlert: it names what requires the backup with the agreeing
// verb, "Back up now" asks for a backup, busy and a pending check disable its controls, and the "Migrate without a
// backup" checkbox starts unchecked and reports a toggle.

import QtQuick
import QtTest
import Lightweight.Migrations

TestCase {
    id: testCase
    name: "BackupRequiredAlert"
    when: windowShown
    visible: true
    width: 900
    height: 400

    Component { id: alertComponent; BackupRequiredAlert { width: 800 } }

    Component {
        id: spyComponent
        SignalSpy {}
    }

    function test_one_reason_requires() {
        const alert = createTemporaryObject(alertComponent, testCase, { reasons: ["release 3.0.0"] })
        verify(alert !== null)
        verify(alert.text.indexOf("release 3.0.0 requires a backup") >= 0)
        verify(alert.text.indexOf("Stop every application that writes to this database") >= 0)
        compare(alert.title, "A backup is required to run the migrations")
    }

    function test_two_reasons_require() {
        const alert = createTemporaryObject(alertComponent, testCase,
                                            { reasons: ["release 3.0.0", "step \"X\""] })
        verify(alert.text.indexOf("release 3.0.0 and step \"X\" require a backup") >= 0)
    }

    function test_the_note_is_shown() {
        const alert = createTemporaryObject(alertComponent, testCase,
                                            { reasons: ["release 3.0.0"], note: "Not recorded: denied." })
        verify(alert.text.indexOf("Not recorded: denied.") >= 0)
    }

    function test_back_up_now_asks_for_a_backup() {
        const alert = createTemporaryObject(alertComponent, testCase, { reasons: ["release 3.0.0"] })
        const spy = createTemporaryObject(spyComponent, testCase, { target: alert, signalName: "backupRequested" })
        findChild(alert, "backUpNowButton").clicked()
        compare(spy.count, 1)
    }

    function test_busy_disables_the_controls() {
        const alert = createTemporaryObject(alertComponent, testCase, { reasons: ["release 3.0.0"], busy: true })
        verify(!findChild(alert, "backUpNowButton").enabled)
        const checkBox = findChild(alert, "migrateWithoutBackupCheckBox")
        verify(!checkBox.enabled)
        verify(checkBox.contentItem.opacity < 1.0, "the label dims while disabled")
    }

    function test_pending_says_the_check_runs_and_disables_the_controls() {
        const alert = createTemporaryObject(alertComponent, testCase, { reasons: [], pending: true })
        verify(findChild(alert, "backupRequirementChecking").visible)
        compare(findChild(alert, "backupRequirementChecking").text, "Checking whether a backup is required…")
        compare(alert.title, "")
        compare(alert.text, "")
        verify(!findChild(alert, "backUpNowButton").enabled)
        verify(!findChild(alert, "migrateWithoutBackupCheckBox").enabled)

        alert.pending = false
        alert.reasons = ["release 3.0.0"]
        verify(!findChild(alert, "backupRequirementChecking").visible)
        compare(alert.title, "A backup is required to run the migrations")
    }

    function test_the_checkbox_starts_unchecked_and_reports_a_toggle() {
        const alert = createTemporaryObject(alertComponent, testCase, { reasons: ["release 3.0.0"], waived: false })
        const spy = createTemporaryObject(spyComponent, testCase, { target: alert, signalName: "waivedToggled" })
        const checkBox = findChild(alert, "migrateWithoutBackupCheckBox")
        compare(checkBox.text, "Migrate without a backup")
        verify(!checkBox.checked)
        checkBox.toggle()
        checkBox.toggled()
        compare(spy.count, 1)
        compare(spy.signalArguments[0][0], true)
    }

    Component { id: narrowAlertComponent; BackupRequiredAlert { width: 276 } }

    function test_the_controls_fit_a_narrow_pane() {
        // The Expert view's right pane is about 276 px wide.
        const alert = createTemporaryObject(narrowAlertComponent, testCase,
                                            { reasons: ["release 3.0.0", "step \"Widen text columns\""] })
        waitForRendering(alert)
        const checkBox = findChild(alert, "migrateWithoutBackupCheckBox")
        const button = findChild(alert, "backUpNowButton")
        const text = findChild(alert, "bannerText")
        verify(checkBox.mapToItem(alert, 0, 0).x + checkBox.width <= alert.width, "checkbox x " + checkBox.mapToItem(alert, 0, 0).x + ", width " + checkBox.width)
        verify(button.mapToItem(alert, 0, 0).x + button.width <= alert.width)
        // The body keeps most of the width instead of being squeezed beside the controls.
        verify(text.width >= alert.width * 0.6, "text width " + text.width)
        // The controls sit below the text.
        verify(button.mapToItem(alert, 0, 0).y >= text.mapToItem(alert, 0, 0).y + text.height)
    }
}
