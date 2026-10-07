// SPDX-License-Identifier: Apache-2.0
//
// Progress block of a run in flight (`dt-prog` in the design project): a bold
// status line on the left, a faint figure on the right ("00:23 · 2.1 M rows")
// and a brand-coloured bar underneath.
//
// Migrations, Back up and Restore all show a run this way, so this is the one
// component they share; only the strings differ. While the total is unknown
// the bar sweeps instead of showing a proportion.
//
// Usage:
//     RunProgress { title: qsTr("Table 18 of 38 · orders"); detail: "00:23 · 2.1 M rows"
//                   value: 18; to: 38 }

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Lightweight.Migrations

ColumnLayout {
    id: root

    /// Status line (bold), e.g. "Table 18 of 38 · invoice_lines".
    property string title: ""
    /// Right-aligned figure, e.g. "00:23 · 2.1 M rows".
    property string detail: ""
    /// Work done so far, in the same unit as `to`.
    property real value: 0
    /// Total work; a value <= 0 means "not known yet" and the bar sweeps.
    property real to: 0

    spacing: 6

    RowLayout {
        Layout.fillWidth: true
        spacing: 12

        Label {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            text: root.title
            color: Theme.clrOnSurface
            font.pixelSize: Theme.sizeBody
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }
        Label {
            visible: root.detail !== ""
            text: root.detail
            color: Theme.clrOnSurfaceSubtle
            font.pixelSize: Theme.sizeBody
        }
    }

    ProgressTrack {
        objectName: "runProgressTrack"
        Layout.fillWidth: true
        implicitHeight: 8
        from: 0
        to: root.to
        value: root.value
        indeterminate: root.to <= 0
    }
}
