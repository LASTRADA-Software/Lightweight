// SPDX-License-Identifier: Apache-2.0
//
// Compact count chip for the backup detail summary ("3 running", "1 error").
// Distinct from StatusPill: StatusPill is a single-item status badge with a
// hover explanation, whereas StatChip is a tiny tally marker — a coloured dot
// plus a count phrase — meant to sit in a row of siblings. Colour is keyed by
// `kind` so the summary reads at a glance (blue running, amber warning, red
// error, green done, muted queued).

import QtQuick
import QtQuick.Controls
import Lightweight.Migrations

Rectangle {
    id: root

    /// One of "running" / "queued" / "error" / "warning" / "done".
    property string kind: "queued"
    /// The already-pluralised caption, e.g. "3 running".
    property string text: ""

    // [background, foreground text, dot] per kind.
    readonly property var _palette: ({
        "running": [Theme.accentSoft, Theme.accent,   Theme.accent],
        "queued":  [Theme.bgSubtle,   Theme.textMuted, Theme.textFaint],
        "error":   [Theme.errSoft,    Theme.errText,   Theme.err],
        "warning": [Theme.warnSoft,   Theme.warnText,  Theme.warn],
        "done":    [Theme.okSoft,     Theme.okText,    Theme.ok]
    })
    readonly property var _colours: _palette[kind] || [Theme.bgSubtle, Theme.textMuted, Theme.textFaint]

    color: _colours[0]
    radius: 999
    implicitWidth: chipRow.implicitWidth + 16
    implicitHeight: chipRow.implicitHeight + 5

    Row {
        id: chipRow
        anchors.centerIn: parent
        spacing: 5

        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: 6
            height: 6
            radius: 3
            color: root._colours[2]
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: root.text
            color: root._colours[1]
            font.pixelSize: 11
            font.weight: Font.Medium
        }
    }
}
