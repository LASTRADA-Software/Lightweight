// SPDX-License-Identifier: Apache-2.0
//
// Underline tab strip (`k-tab` in the Lastrada design): All / Pending /
// Applied / Issues, each with a count chip. The active tab is darker,
// semibold and underlined in brand red, and its chip turns brand-filled.
// Drives `MigrationView.filterTab` (bound from the parent).
//
// The strip only draws the tabs; the hairline under them belongs to the
// containing toolbar so the active underline can sit on top of it.

import QtQuick
import QtQuick.Controls
import Lightweight.Migrations

Item {
    id: root

    property string current: "all"
    signal activated(string key)

    property var tabs: []

    implicitWidth: row.implicitWidth
    implicitHeight: 42

    Row {
        id: row
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        spacing: 2

        Repeater {
            model: root.tabs

            Item {
                id: tab
                required property var modelData
                readonly property bool active: root.current === modelData.key

                height: row.height
                width: content.implicitWidth + 24

                Accessible.role: Accessible.PageTab
                Accessible.name: modelData.label
                Accessible.selected: active

                ToolTip.visible: tabHover.hovered && (modelData.tip || "").length > 0
                ToolTip.text: modelData.tip || ""
                ToolTip.delay: 500
                ToolTip.timeout: 10000
                HoverHandler { id: tabHover; cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: root.activated(tab.modelData.key) }

                Row {
                    id: content
                    anchors.centerIn: parent
                    spacing: 7

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: tab.modelData.label
                        color: tab.active || tabHover.hovered ? Theme.clrOnSurface : Theme.clrOnSurfaceMed
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.sizeBody
                        font.weight: tab.active ? Font.DemiBold : Font.Normal
                    }

                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        visible: tab.modelData.count !== undefined
                        height: 17
                        width: Math.max(height, countLabel.implicitWidth + 12)
                        radius: Theme.rPill
                        color: tab.active ? Theme.clrPrimary : Theme.clrContainer

                        Text {
                            id: countLabel
                            anchors.centerIn: parent
                            text: tab.modelData.count !== undefined ? String(tab.modelData.count) : ""
                            color: tab.active ? "#ffffff" : Theme.clrOnSurfaceMed
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.sizeLabel - 1
                            font.weight: Font.DemiBold
                        }
                    }
                }

                // Active underline, 2 px brand red flush with the bottom edge.
                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 2
                    color: Theme.clrPrimary
                    visible: tab.active
                }
            }
        }
    }
}
