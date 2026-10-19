// SPDX-License-Identifier: Apache-2.0
//
// Behaviour tests for TaskProgressList, fed a ListModel with the roles of
// ProgressTaskModel: one row per task, sub-tasks indented, a task without a
// total sweeping and a failed task drawn red with its reason.

import QtQuick
import QtTest
import Lightweight.Migrations

TestCase {
    id: testCase
    name: "TaskProgressList"
    when: windowShown
    visible: true
    width: 700
    height: 700

    Component { id: listComponent; TaskProgressList { width: 600 } }

    Component {
        id: modelComponent
        ListModel {
            ListElement { title: "Step"; depth: 0; total: 0; done: 0; detail: "planning"; elapsedMs: 1000
                          indeterminate: true; finished: false; failed: false; failureReason: "" }
            ListElement { title: "Table a"; depth: 1; total: 10; done: 5; detail: ""; elapsedMs: 0
                          indeterminate: false; finished: false; failed: false; failureReason: "" }
        }
    }

    /// The list's row delegates, in model order.
    function rows(list) {
        const found = []
        for (let i = 0; i < list.children.length; ++i)
            if (list.children[i].objectName === "taskProgressRow")
                found.push(list.children[i])
        return found
    }

    function test_one_row_per_task_with_sub_tasks_indented() {
        const listModel = createTemporaryObject(modelComponent, testCase)
        const list = createTemporaryObject(listComponent, testCase, { model: listModel })
        verify(list !== null)
        compare(list.count, 2)
        const shown = rows(list)
        compare(shown.length, 2)
        verify(findChild(shown[0], "runProgressTrack").indeterminate)
        verify(!findChild(shown[1], "runProgressTrack").indeterminate)
        tryVerify(() => shown[1].x > shown[0].x)
    }

    function test_a_failed_task_is_red_with_its_reason() {
        const listModel = createTemporaryObject(modelComponent, testCase)
        listModel.setProperty(1, "finished", true)
        listModel.setProperty(1, "failed", true)
        listModel.setProperty(1, "failureReason", "disk full")
        const list = createTemporaryObject(listComponent, testCase, { model: listModel })
        const failedRow = rows(list)[1]
        const track = findChild(failedRow, "runProgressTrack")
        compare(track.fillColor, Theme.clrErrorDot)
        compare(findChild(rows(list)[0], "runProgressTrack").fillColor, Theme.clrPrimary)
        // It keeps how far it got (5 of 10) instead of reading as complete.
        verify(!track.indeterminate)
        compare(track.value, 5)
        compare(track.to, 10)
        let reasonShown = false
        for (let i = 0; i < failedRow.children.length; ++i)
            if (failedRow.children[i].text === "disk full" && failedRow.children[i].visible)
                reasonShown = true
        verify(reasonShown, "the failure reason is shown under the row")
    }

    function test_a_finished_task_with_a_total_shows_how_far_it_got() {
        // A cancelled run ends its rows without failing them.
        const listModel = createTemporaryObject(modelComponent, testCase)
        listModel.setProperty(1, "finished", true)
        const list = createTemporaryObject(listComponent, testCase, { model: listModel })
        const track = findChild(rows(list)[1], "runProgressTrack")
        compare(track.value, 5)
        compare(track.to, 10)
        compare(track.fillColor, Theme.clrPrimary)
    }

    function test_a_failed_task_without_a_total_stops_sweeping() {
        const listModel = createTemporaryObject(modelComponent, testCase)
        listModel.setProperty(0, "finished", true)
        listModel.setProperty(0, "failed", true)
        const list = createTemporaryObject(listComponent, testCase, { model: listModel })
        const track = findChild(rows(list)[0], "runProgressTrack")
        verify(!track.indeterminate)
        compare(track.value, 0)
    }
}
