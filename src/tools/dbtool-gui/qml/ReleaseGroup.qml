// SPDX-License-Identifier: Apache-2.0
//
// Collapsible group header for a single release (or the "unreleased" bucket).
// Mirrors the mockup's release-divider row with a chevron + tri-state select,
// colour-coded status pill, and a per-release "N of M selected" counter.

import QtQuick
import QtQuick.Controls
import Lightweight.Migrations
import QtQuick.Layouts

Column {
    id: root

    property string version: ""
    property string status: "pending"
    property int total: 0
    property int selected: 0
    property bool expanded: true

    signal toggleExpanded()
    signal toggleSelection()

    width: parent ? parent.width : implicitWidth

    Rectangle {
        id: header
        width: parent.width
        height: 34
        color: hoverArea.containsMouse ? "#eef1f6" : "transparent"

        MouseArea {
            id: hoverArea
            anchors.fill: parent
            hoverEnabled: true
            onClicked: root.toggleExpanded()
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 14
            anchors.rightMargin: 14
            spacing: 8

            Label {
                text: root.expanded ? "▾" : "▸"
                color: "#5b6372"
                Layout.preferredWidth: 14
            }

            // Tri-state select indicator, clicking it fires a separate signal.
            Rectangle {
                id: tri
                readonly property string selKind: root.selected === 0
                    ? "none"
                    : (root.selected === root.total ? "all" : "some")
                width: 14
                height: 14
                radius: 3
                border.width: 1
                border.color: selKind === "none" ? "#cfd4dc" : "#0a66d6"
                color: selKind === "none" ? "white" : "#0a66d6"

                Label {
                    anchors.centerIn: parent
                    visible: tri.selKind === "all"
                    text: "✓"
                    color: "white"
                    font.pixelSize: 11
                }
                Rectangle {
                    visible: tri.selKind === "some"
                    anchors.centerIn: parent
                    width: 8
                    height: 2
                    color: "white"
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: { root.toggleSelection(); mouse.accepted = true }
                }
            }

            Rectangle {
                color: root.version === "" ? "#f2f4f7" : "#dbeafe"
                radius: 4
                Layout.preferredWidth: versionLabel.implicitWidth + 14
                Layout.preferredHeight: versionLabel.implicitHeight + 4
                Label {
                    id: versionLabel
                    anchors.centerIn: parent
                    text: root.version === "" ? "unreleased" : root.version
                    color: root.version === "" ? "#5b6372" : "#0a66d6"
                    font.family: "JetBrains Mono, Consolas, monospace"
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                }
            }

            Label {
                text: `${root.selected} of ${root.total} selected`
                color: "#8a93a4"
                font.pixelSize: 11
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignLeft
            }

            Label {
                text: root.status
                color: root.status === "applied" ? "#0d7a37"
                     : (root.status === "partial" ? "#925005" : "#925005")
                font.pixelSize: 11
            }
        }
    }

    // Caller supplies the body (rows) via default property below.
    property alias contentChildren: contentColumn.data

    Column {
        id: contentColumn
        width: parent.width
        visible: root.expanded
    }
}
