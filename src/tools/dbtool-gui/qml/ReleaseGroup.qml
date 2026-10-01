// SPDX-License-Identifier: Apache-2.0
//
// Collapsible group header for a single release (or the "unreleased" bucket)
// in the migration table (`dt-rg` in the Lastrada design): a 34 px warm
// section-header band with a chevron, the bold version, faint meta text and,
// right-aligned, a progress bar with an "applied / total" fraction. The bar
// is green once the release is fully applied and amber while anything in it
// is still pending.
//
// `MigrationView` uses it as the ListView section delegate. Rows can also be
// placed below the header through `contentChildren` for standalone use.

import QtQuick
import QtQuick.Controls
import Lightweight.Migrations

Column {
    id: root

    /// Release version; empty for the "unreleased" bucket.
    property string version: ""
    /// Aggregate status ("applied" | "partial" | "pending" | "empty").
    property string status: "pending"
    /// Migrations in the release.
    property int total: 0
    /// Migrations already applied.
    property int applied: 0
    /// Ticked rows in the release (drives the optional tri-state box).
    property int selected: 0
    property bool expanded: true
    /// Faint text after the version (date, "newer than the latest release").
    property string meta: ""
    /// Show the tri-state "select every pending row in this release" box.
    property bool selectable: false

    signal toggleExpanded()
    signal toggleSelection()

    width: parent ? parent.width : implicitWidth

    Rectangle {
        id: header
        width: parent.width
        height: 34
        color: hoverArea.containsMouse ? Qt.darker(Theme.clrSectionHdr, 1.015) : Theme.clrSectionHdr

        Accessible.role: Accessible.Button
        Accessible.name: (root.version === "" ? qsTr("Unreleased") : root.version)
                         + (root.expanded ? qsTr(", expanded") : qsTr(", collapsed"))

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: Theme.clrSectionHdrBorder
        }

        MouseArea {
            id: hoverArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: root.toggleExpanded()
        }

        Row {
            anchors.left: parent.left
            anchors.leftMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8

            Glyph {
                anchors.verticalCenter: parent.verticalCenter
                name: root.expanded ? "chevron-down" : "chevron"
                size: 14
                color: Theme.clrOnSurfaceSubtle
            }

            // Tri-state select indicator; clicking it fires a separate signal.
            Rectangle {
                id: tri
                visible: root.selectable
                anchors.verticalCenter: parent.verticalCenter
                readonly property string selKind: root.selected === 0
                    ? "none"
                    : (root.selected === root.total ? "all" : "some")
                width: 15
                height: 15
                radius: 3
                border.color: selKind === "none" ? Theme.clrBorderStrong : Theme.clrPrimary
                color: selKind === "none" ? Theme.clrCard : Theme.clrPrimary

                Glyph {
                    anchors.centerIn: parent
                    visible: tri.selKind === "all"
                    name: "check"
                    size: 11
                    color: "#ffffff"
                }
                Rectangle {
                    visible: tri.selKind === "some"
                    anchors.centerIn: parent
                    width: 8
                    height: 2
                    color: "#ffffff"
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: mouse => { root.toggleSelection(); mouse.accepted = true }
                }
            }

            Label {
                anchors.verticalCenter: parent.verticalCenter
                text: root.version === "" ? qsTr("Unreleased") : root.version
                color: Theme.clrOnSurface
                font.pixelSize: Theme.sizeBodySm + 1
                font.weight: Font.DemiBold
            }

            Label {
                anchors.verticalCenter: parent.verticalCenter
                visible: text.length > 0
                text: root.meta
                color: Theme.clrOnSurfaceSubtle
                font.pixelSize: Theme.sizeLabel
            }
        }

        Row {
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8

            ProgressTrack {
                anchors.verticalCenter: parent.verticalCenter
                width: 90
                height: 5
                trackColor: Theme.clrContainerHigh
                to: Math.max(1, root.total)
                value: root.applied
                fillColor: root.total > 0 && root.applied >= root.total
                           ? Theme.clrSuccessDot : Theme.clrWarningDot
            }
            Label {
                anchors.verticalCenter: parent.verticalCenter
                width: 40
                horizontalAlignment: Text.AlignRight
                text: "%1 / %2".arg(root.applied).arg(root.total)
                color: Theme.clrOnSurfaceSubtle
                font.pixelSize: Theme.sizeLabel
                font.features: { "tnum": 1 }
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
