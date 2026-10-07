// SPDX-License-Identifier: Apache-2.0
//
// Outcome of a finished Back up or Restore, shown under the Backup & restore
// panel: one banner naming the database and the archive, the run's facts
// (tables, rows, time), and — when tables were lost — the list of failed
// tables with their reasons.
//
// Back up and Restore share it because they are the same kind of run; only
// the verbs differ. A run that returned normally but lost tables is shown as
// failed ("Restore incomplete"), never as a success.
//
// `result` is `BackupRunner.lastResult`; see BackupRunner.hpp for its keys.
//
// Usage:
//     RunOutcome { result: AppController.backupRunner.lastResult
//                  target: AppController.currentProfile }

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Lightweight.Migrations

ColumnLayout {
    id: root

    /// `BackupRunner.lastResult`.
    property var result: ({})
    /// Name of the database the run was against (the connected profile).
    property string target: ""

    /// Emitted by "Show in folder".
    signal showInFolder()
    /// Emitted by "Dismiss".
    signal dismissed()

    /// Failed tables listed before the rest are summarised as "and N more".
    readonly property int maxListedFailures: 8

    readonly property bool _has: result && result.operation !== undefined
    readonly property bool _ok: _has && result.ok === true
    readonly property bool _isBackup: _has && result.operation === "backup"
    readonly property int _failedCount: _has ? (result.failedCount || 0) : 0
    // Errors that name no table (index restoration, "no tables could be created").
    readonly property int _otherErrors: _has ? (result.otherErrors || 0) : 0
    readonly property var _failures: _has && result.failedTables ? result.failedTables : []
    // The database the run was against, as recorded when it started; falls back to `target`.
    readonly property string _target: _has && result.target ? result.target : target
    readonly property string _archiveName: _has ? RunFormat.baseName(result.archive || "") : ""

    spacing: 8

    /// Banner headline.
    function headline() {
        if (!root._has)
            return ""
        const verb = root._isBackup ? qsTr("Backup") : qsTr("Restore")
        if (root.result.error)
            return qsTr("%1 failed").arg(verb)
        if (root._failedCount > 0)
            return qsTr("%1 incomplete — %2 of %3 tables failed")
                .arg(verb).arg(root._failedCount).arg(root.result.tables)
        if (root._otherErrors > 0)
            return qsTr("%1 finished with errors").arg(verb)
        if (root._isBackup)
            return root._target !== ""
                ? qsTr("Backup written — %1 to %2").arg(root._target).arg(root._archiveName)
                : qsTr("Backup written to %1").arg(root._archiveName)
        return root._target !== ""
            ? qsTr("Restored — %1 from %2").arg(root._target).arg(root._archiveName)
            : qsTr("Restored from %1").arg(root._archiveName)
    }

    /// Banner body: the run's facts on success, the consequence and the fix on failure.
    function body() {
        if (!root._has)
            return ""
        if (root.result.error)
            return root.result.error
        if (root._failedCount > 0) {
            if (root._isBackup)
                return qsTr("The archive is missing these tables. Fix the cause and run Back up again.")
            const restored = root.result.tables - root._failedCount
            return root._target !== ""
                ? qsTr("The other %1 tables were restored, so %2 is in a partial state. Fix the cause and run Restore… again.")
                      .arg(restored).arg(root._target)
                : qsTr("The other %1 tables were restored, so the database is in a partial state. Fix the cause and run Restore… again.")
                      .arg(restored)
        }
        if (root._otherErrors > 0)
            return root._isBackup
                ? qsTr("The archive may be incomplete. Fix the cause and run Back up again.")
                : qsTr("The database may be in a partial state. Fix the cause and run Restore… again.")
        const parts = [root.result.tables === 1 ? qsTr("1 table") : qsTr("%1 tables").arg(root.result.tables),
                       qsTr("%1 rows").arg(RunFormat.formatCount(root.result.rows)),
                       RunFormat.formatDuration(root.result.durationMs)]
        if (root.result.finishedAt)
            parts.push(qsTr("finished %1").arg(Qt.formatDateTime(root.result.finishedAt, "HH:mm")))
        return parts.join(" · ")
    }

    /// Plain-text report for the clipboard.
    function details() {
        const lines = [root.headline()]
        if (root.result.error)
            lines.push(root.result.error)
        for (const f of root._failures)
            lines.push(f.table !== "" ? f.table + ": " + f.reason : f.reason)
        if (root._failedCount > root._failures.length)
            lines.push(qsTr("… and %1 more").arg(root._failedCount - root._failures.length))
        return lines.join("\n")
    }

    Banner {
        objectName: "runOutcomeBanner"
        Layout.fillWidth: true
        kind: root._ok ? "ok" : "err"
        title: root.headline()
        text: root.body()
        actions: [
            LsButton {
                visible: root._ok
                text: qsTr("Show in folder")
                glyph: "folder-outline"
                onClicked: root.showInFolder()
            },
            LsButton {
                objectName: "runOutcomeDismiss"
                variant: "ghost"
                text: qsTr("Dismiss")
                onClicked: root.dismissed()
            }
        ]
    }

    // Failed tables with their reasons.
    Rectangle {
        objectName: "runOutcomeFailures"
        Layout.fillWidth: true
        visible: root._failures.length > 0
        implicitHeight: failureColumn.implicitHeight
        color: Theme.clrCard
        border.color: Theme.clrErrorBorder
        radius: Theme.r2
        clip: true

        Column {
            id: failureColumn
            width: parent.width

            Repeater {
                model: root._failures.slice(0, root.maxListedFailures)

                delegate: Item {
                    required property var modelData
                    width: failureColumn.width
                    implicitHeight: reason.implicitHeight + 14
                    height: implicitHeight

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        spacing: 10

                        Label {
                            Layout.preferredWidth: 150
                            Layout.alignment: Qt.AlignTop
                            Layout.topMargin: 7
                            text: modelData.table !== "" ? modelData.table : "—"
                            color: Theme.clrOnSurface
                            font: Theme.monoFont(11)
                            elide: Text.ElideRight
                        }
                        Label {
                            id: reason
                            Layout.fillWidth: true
                            Layout.topMargin: 7
                            text: modelData.reason
                            color: Theme.clrError
                            font.pixelSize: Theme.sizeBodySm
                            wrapMode: Text.Wrap
                        }
                    }
                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: 1
                        color: Theme.clrDivider
                    }
                }
            }

            Label {
                visible: root._failedCount > root.maxListedFailures
                width: parent.width
                leftPadding: 10
                topPadding: 6
                bottomPadding: 6
                text: qsTr("… and %1 more. The full error text is in the log.")
                          .arg(root._failedCount - root.maxListedFailures)
                color: Theme.clrOnSurfaceSubtle
                font.pixelSize: Theme.sizeLabel
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        visible: !root._ok && root._has

        LsButton {
            objectName: "runOutcomeCopy"
            text: qsTr("Copy details")
            glyph: "copy"
            onClicked: {
                clipboard.text = root.details()
                clipboard.selectAll()
                clipboard.copy()
            }
        }
        Item { Layout.fillWidth: true }
    }

    // Clipboard bridge: QML has no direct clipboard API, a TextEdit copy is the portable route.
    TextEdit {
        id: clipboard
        visible: false
    }
}
