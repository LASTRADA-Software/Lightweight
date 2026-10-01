// SPDX-License-Identifier: Apache-2.0
//
// Left-pane "Status" kit panel: two small readouts (`k-readout sm`) for the
// release the database is at and the latest declared release, a compact row
// of per-status counts, and a banner that says in words whether anything is
// left to apply.
//
// Bindings read the notify-enabled counters on `AppController` so they
// refresh whenever the migration model is rebuilt (as opposed to reading
// `model.rowCount()` directly, which does not notify QML bindings on model
// resets).

import QtQuick
import QtQuick.Controls
import Lightweight.Migrations

Card {
    id: root
    title: qsTr("Status")
    glyph: "chart"
    meta: "schema_migrations"

    /// Splits a controller release label such as "v2.4.0 (+1 unreleased)"
    /// into the headline value and the parenthesised remainder, so the
    /// readout can show the version large and the qualifier as its sub-line.
    function splitLabel(label) {
        const at = label.indexOf(" (");
        if (at <= 0)
            return { value: label, detail: "" };
        return { value: label.slice(0, at), detail: label.slice(at + 2).replace(/\)$/, "") };
    }

    readonly property var _current: splitLabel(AppController.currentReleaseLabel)
    readonly property var _latest: splitLabel(AppController.targetReleaseLabel)

    Row {
        id: readouts
        width: parent.width
        spacing: Theme.sp2

        Repeater {
            model: [
                { label: qsTr("Current release"), value: root._current.value,
                  detail: root._current.detail.length > 0
                          ? root._current.detail
                          : qsTr("%1 applied").arg(AppController.appliedCount) },
                { label: qsTr("Latest release"), value: root._latest.value,
                  detail: root._latest.detail.length > 0
                          ? root._latest.detail
                          : qsTr("%1 registered").arg(AppController.migrationCount) },
            ]

            Rectangle {
                required property var modelData
                width: (readouts.width - readouts.spacing) / 2
                height: tile.implicitHeight + 18
                radius: Theme.r3
                color: Theme.clrCard
                border.color: Theme.clrContainerHighest

                Column {
                    id: tile
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.leftMargin: 11
                    anchors.rightMargin: 11
                    spacing: 3

                    Label {
                        width: parent.width
                        text: modelData.label
                        color: Theme.clrOnSurfaceSubtle
                        font.pixelSize: Theme.sizeLabel
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }
                    Label {
                        width: parent.width
                        text: modelData.value.length > 0 ? modelData.value : "—"
                        color: Theme.clrOnSurface
                        font.pixelSize: 17
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }
                    Label {
                        width: parent.width
                        text: modelData.detail
                        color: Theme.clrOnSurfaceSubtle
                        font.pixelSize: Theme.sizeLabel
                        elide: Text.ElideRight
                    }
                }
            }
        }
    }

    // Per-status counts. Kept to one dense line: the banner below carries the
    // headline, these are for the user who wants the exact numbers. Unknown
    // and checksum rows only appear when non-zero, since they are anomalies.
    Flow {
        width: parent.width
        spacing: Theme.sp3

        Repeater {
            model: [
                { label: qsTr("applied"),    count: AppController.appliedCount,          dot: Theme.clrSuccessDot, always: true },
                { label: qsTr("pending"),    count: AppController.pendingCount,          dot: Theme.clrWarningDot, always: true },
                { label: qsTr("unknown"),    count: AppController.unknownCount,          dot: Theme.clrErrorDot,   always: false },
                { label: qsTr("checksum ≠"), count: AppController.checksumMismatchCount, dot: Theme.clrErrorDot,   always: false },
            ]

            Row {
                required property var modelData
                visible: modelData.always || modelData.count > 0
                spacing: 5

                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 7
                    height: 7
                    radius: 3.5
                    color: modelData.dot
                }
                Label {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "<b>%1</b> %2".arg(modelData.count).arg(modelData.label)
                    textFormat: Text.StyledText
                    color: Theme.clrOnSurfaceMed
                    font.pixelSize: Theme.sizeBodySm
                }
            }
        }
    }

    Banner {
        width: parent.width
        visible: AppController.pendingCount > 0
        kind: "warn"
        title: AppController.pendingCount === 1
               ? qsTr("1 migration pending")
               : qsTr("%1 migrations pending").arg(AppController.pendingCount)
        text: AppController.pendingUnreleasedCount > 0
              ? qsTr("%1 of them not part of a release yet.").arg(AppController.pendingUnreleasedCount)
              : ""
    }

    Banner {
        width: parent.width
        visible: AppController.connected && AppController.pendingCount === 0
                 && AppController.migrationCount > 0
        kind: "ok"
        title: qsTr("Up to date")
        text: qsTr("All %1 migrations are applied.").arg(AppController.appliedCount)
    }
}
