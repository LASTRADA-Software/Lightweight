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

    // [background, foreground text, dot, border] per kind — the kit pill
    // palette, so a tally reads the same as the status pills beside it.
    readonly property var _palette: ({
        "running": [Theme.clrInfoBg,    Theme.clrInfo,            Theme.clrInfo,            Theme.clrInfoBorder],
        "queued":  [Theme.clrContainer, Theme.clrOnSurfaceMed,    Theme.clrOnSurfaceFaint,  Theme.clrContainerHighest],
        "error":   [Theme.clrErrorBg,   Theme.clrError,           Theme.clrErrorDot,        Theme.clrErrorBorder],
        "warning": [Theme.clrWarningBg, Theme.clrWarning,         Theme.clrWarningDot,      Theme.clrWarningBorder],
        "done":    [Theme.clrSuccessBg, Theme.clrSuccess,         Theme.clrSuccessDot,      Theme.clrSuccessBorder]
    })
    readonly property var _colours: _palette[kind] || _palette["queued"]

    color: _colours[0]
    border.color: _colours[3]
    radius: Theme.rPill
    implicitWidth: chipRow.implicitWidth + 18
    implicitHeight: 22

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
            font.pixelSize: Theme.sizeLabel
            font.weight: Font.DemiBold
        }
    }
}
