// SPDX-License-Identifier: Apache-2.0
//
// The tasks of a run in flight — pre-migrate steps, their units and tables, the
// migrations — one `RunProgress` row per task, indented by depth. A task without a
// total sweeps; a finished top-level task stays full until the next run; a failed
// task stays red with its reason underneath. A state line under each row reads
// "done", "cancelled" or "❌ failed", and nothing while the task runs.
//
// Usage:
//     TaskProgressList { model: AppController.runner.tasks }

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Lightweight.Migrations

ColumnLayout {
    id: root

    /// A `ProgressTaskModel` (or any model with its roles).
    property var model: null
    /// Rows shown.
    readonly property int count: repeater.count

    spacing: Theme.sp2

    Repeater {
        id: repeater
        model: root.model
        delegate: ColumnLayout {
            objectName: "taskProgressRow"
            Layout.fillWidth: true
            Layout.leftMargin: model.depth * Theme.sp4
            spacing: Theme.sp1

            RunProgress {
                objectName: "taskProgressBar"
                Layout.fillWidth: true
                title: model.title
                detail: (model.detail !== "" ? model.detail + " · " : "") + RunFormat.clock(model.elapsedMs)
                // A row with a total shows how far it got, also once it ended (failed or cancelled); a row
                // without one sweeps while running, then reads full when done and empty when it failed.
                to: model.indeterminate ? (model.finished ? 1 : 0) : model.total
                value: model.indeterminate ? (model.finished && !model.failed ? 1 : 0) : model.done
                failed: model.failed
            }

            Label {
                objectName: "taskProgressState"
                Layout.fillWidth: true
                visible: text !== ""
                text: model.failed ? "❌ " + qsTr("failed") : (model.cancelled ? qsTr("cancelled") : (model.finished ? qsTr("done") : ""))
                color: model.failed ? Theme.clrError : Theme.clrOnSurfaceSubtle
                font.pixelSize: Theme.sizeLabel
            }

            Label {
                objectName: "taskProgressFailureReason"
                Layout.fillWidth: true
                visible: model.failed && model.failureReason !== ""
                text: model.failureReason
                color: Theme.clrError
                font.pixelSize: Theme.sizeLabel
                wrapMode: Text.Wrap
            }
        }
    }
}
