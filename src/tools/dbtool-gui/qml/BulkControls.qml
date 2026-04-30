// SPDX-License-Identifier: Apache-2.0
//
// Compact two-button segmented control used for the "Expand all / Collapse
// all" and "Select all / Deselect all" bulk actions in the filter bar.

import QtQuick
import QtQuick.Controls
import Lightweight.Migrations

Rectangle {
    id: root

    property string leftLabel: ""
    property string rightLabel: ""
    property string leftTip: ""
    property string rightTip: ""
    property bool activeLeft: false
    property bool activeRight: false

    signal leftClicked()
    signal rightClicked()

    color: Theme.bgSubtle
    border.color: Theme.border
    radius: 6
    implicitWidth: rowLayout.implicitWidth + 8
    implicitHeight: 30

    Row {
        id: rowLayout
        anchors.centerIn: parent
        spacing: 2

        Rectangle {
            width: leftText.implicitWidth + 16
            height: 24
            radius: 4
            color: root.activeLeft ? Theme.bgPanel : "transparent"

            ToolTip.visible: leftHover.hovered && root.leftTip.length > 0
            ToolTip.text: root.leftTip
            ToolTip.delay: 500
            ToolTip.timeout: 10000
            HoverHandler { id: leftHover }

            Text {
                id: leftText
                anchors.centerIn: parent
                text: root.leftLabel
                color: root.activeLeft ? Theme.text : Theme.textMuted
                font.pixelSize: 12
                font.weight: Font.Medium
            }
            MouseArea {
                anchors.fill: parent
                onClicked: root.leftClicked()
                cursorShape: Qt.PointingHandCursor
            }
        }

        Rectangle {
            width: 1
            height: 14
            anchors.verticalCenter: parent.verticalCenter
            color: Theme.border
        }

        Rectangle {
            width: rightText.implicitWidth + 16
            height: 24
            radius: 4
            color: root.activeRight ? Theme.bgPanel : "transparent"

            ToolTip.visible: rightHover.hovered && root.rightTip.length > 0
            ToolTip.text: root.rightTip
            ToolTip.delay: 500
            ToolTip.timeout: 10000
            HoverHandler { id: rightHover }

            Text {
                id: rightText
                anchors.centerIn: parent
                text: root.rightLabel
                color: root.activeRight ? Theme.text : Theme.textMuted
                font.pixelSize: 12
                font.weight: Font.Medium
            }
            MouseArea {
                anchors.fill: parent
                onClicked: root.rightClicked()
                cursorShape: Qt.PointingHandCursor
            }
        }
    }
}
