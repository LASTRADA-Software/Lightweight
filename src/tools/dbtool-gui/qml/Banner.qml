// SPDX-License-Identifier: Apache-2.0
//
// Kit banner (`k-banner`): a tinted, bordered message with a leading glyph,
// an optional bold title line and body text. Status is never carried by
// colour alone — every kind has its own glyph.
//
// Usage:
//     Banner { kind: "warn"; title: qsTr("3 migrations pending"); text: qsTr("…") }

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Lightweight.Migrations

Rectangle {
    id: root

    /// "info" | "warn" | "err" | "ok".
    property string kind: "info"
    /// Bold first line; omit for a single-line banner.
    property string title: ""
    /// Body text (wraps).
    property string text: ""
    /// Items placed at the right edge (e.g. a ghost `LsButton` "Clear").
    property list<Item> actions

    // [background, border, foreground, glyph] per kind.
    readonly property var _kinds: ({
        "info": [Theme.clrInfoBg, Theme.clrInfoBorder, Theme.clrInfo, "info"],
        "warn": [Theme.clrWarningBg, Theme.clrWarningBorder, Theme.clrWarning, "alert"],
        "err":  [Theme.clrErrorBg, Theme.clrErrorBorder, Theme.clrError, "alert"],
        "ok":   [Theme.clrSuccessBg, Theme.clrSuccessBorder, Theme.clrSuccess, "check-circle"],
    })
    readonly property var _k: _kinds[kind] || _kinds["info"]
    /// Foreground colour, exposed so callers can colour inline children.
    readonly property color foreground: _k[2]

    color: _k[0]
    border.color: _k[1]
    radius: Theme.r2
    implicitHeight: row.implicitHeight + 20
    implicitWidth: 240

    RowLayout {
        id: row
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        spacing: 10

        Glyph {
            Layout.alignment: Qt.AlignTop
            Layout.topMargin: 1
            name: root._k[3]
            size: 16
            color: root._k[2]
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 1

            Label {
                Layout.fillWidth: true
                visible: root.title !== ""
                text: root.title
                color: root._k[2]
                font.pixelSize: Theme.sizeBodySm + 1
                font.weight: Font.DemiBold
                wrapMode: Text.Wrap
            }
            Label {
                Layout.fillWidth: true
                visible: root.text !== ""
                text: root.text
                color: root._k[2]
                font.pixelSize: Theme.sizeBodySm + 1
                lineHeight: 1.15
                wrapMode: Text.Wrap
            }
        }

        Row {
            Layout.alignment: Qt.AlignVCenter
            visible: root.actions.length > 0
            spacing: 6
            children: root.actions
        }
    }
}
