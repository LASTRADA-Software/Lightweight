// SPDX-License-Identifier: Apache-2.0
//
// Pill-style tab switcher: All / Pending / Applied / Issues, with per-tab
// count badges. Drives `MigrationView.filterTab` (bound from the parent).

import QtQuick
import QtQuick.Controls
import Lightweight.Migrations
import QtQuick.Layouts

Rectangle {
    id: root

    property string current: "all"
    signal activated(string key)

    property var tabs: []

    color: Theme.bgSubtle
    border.color: Theme.border
    radius: 999
    implicitWidth: row.implicitWidth + 6
    implicitHeight: 30

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 2

        Repeater {
            model: root.tabs
            Rectangle {
                id: tabRect
                required property var modelData
                readonly property bool active: root.current === modelData.key

                height: 24
                width: label.implicitWidth + (countLabel.visible ? countLabel.implicitWidth + 14 : 0) + 18
                radius: 999
                color: active ? Theme.bgPanel : "transparent"
                border.color: active ? Theme.border : "transparent"

                ToolTip.visible: tabHover.hovered
                ToolTip.text: modelData.tip || ""
                ToolTip.delay: 500
                ToolTip.timeout: 10000
                HoverHandler { id: tabHover }

                Row {
                    anchors.centerIn: parent
                    spacing: 6

                    Text {
                        id: label
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData.label
                        color: tabRect.active ? Theme.text : Theme.textMuted
                        font.pixelSize: 12
                        font.weight: Font.Medium
                    }

                    Rectangle {
                        id: countPill
                        anchors.verticalCenter: parent.verticalCenter
                        visible: countLabel.text.length > 0
                        color: tabRect.active ? Theme.accentSoft : Theme.bgHover
                        radius: 999
                        width: countLabel.implicitWidth + 10
                        height: countLabel.implicitHeight + 2
                        Text {
                            id: countLabel
                            anchors.centerIn: parent
                            text: modelData.count !== undefined ? String(modelData.count) : ""
                            color: tabRect.active ? Theme.accent : Theme.textMuted
                            font.pixelSize: 11
                        }
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: root.activated(modelData.key)
                    cursorShape: Qt.PointingHandCursor
                }
            }
        }
    }
}
