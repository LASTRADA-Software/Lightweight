// SPDX-License-Identifier: Apache-2.0
//
// Numbered step for the Simple view's guided flow (kit `.sechdr` / `.secnum`
// / `.secbody`): a 42 px header with a round step badge, the step title and
// right-aligned meta, above a white body. A finished step collapses to its
// header alone, carrying a one-line summary in `meta`.
//
// The badge states follow the design: `active` red with the step number,
// `done` green with a tick, `idle` grey.
//
// Usage:
//     StepSection { number: 2; title: qsTr("Review what will change"); stepState: "active"
//                   meta: qsTr("3 migrations"); Label { … } }

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Lightweight.Migrations

Item {
    id: root

    default property alias contentChildren: body.data

    /// Step number shown in the badge while not done.
    property int number: 1
    /// Header title.
    property string title: ""
    /// Right-aligned header text (a summary for a collapsed step).
    property string meta: ""
    /// "active" | "done" | "idle".
    property string stepState: "active"
    /// Collapse to the header row only (finished steps).
    property bool collapsed: false
    /// Items placed after `meta` in the header (e.g. a "Change" link).
    property list<Item> headerActions

    implicitHeight: header.height + (collapsed ? 0 : bodyFrame.implicitHeight)

    Rectangle {
        id: header
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 42
        radius: Theme.r1
        color: root.collapsed ? Theme.clrCard : Theme.clrSectionHdr
        border.color: root.collapsed ? Theme.clrContainerHighest : Theme.clrSectionHdrBorder

        // Square off the bottom corners while the body hangs below.
        Rectangle {
            visible: !root.collapsed
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.leftMargin: 1
            anchors.rightMargin: 1
            height: Theme.r1
            color: parent.color
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 14
            anchors.rightMargin: 14
            spacing: 10

            Rectangle {
                Layout.preferredWidth: 22
                Layout.preferredHeight: 22
                radius: 11
                color: root.stepState === "done" ? Theme.clrSuccessDot
                     : root.stepState === "idle" ? Theme.clrContainerHighest : Theme.clrPrimary

                Label {
                    anchors.centerIn: parent
                    visible: root.stepState !== "done"
                    text: root.number
                    color: root.stepState === "idle" ? Theme.clrOnSurfaceMed : "#ffffff"
                    font.pixelSize: Theme.sizeLabel
                    font.weight: Font.DemiBold
                }
                Glyph {
                    anchors.centerIn: parent
                    visible: root.stepState === "done"
                    name: "check"
                    size: 12
                    strokeWidth: 2
                    color: "#ffffff"
                }
            }

            Label {
                text: root.title
                color: Theme.clrOnSurface
                font.pixelSize: Theme.sizeBody
                font.weight: Font.DemiBold
            }

            Item { Layout.fillWidth: true }

            Label {
                Layout.maximumWidth: root.width * 0.55
                visible: root.meta !== ""
                text: root.meta
                color: Theme.clrOnSurfaceSubtle
                font.pixelSize: Theme.sizeLabel + 0.5
                elide: Text.ElideRight
            }
            Row {
                visible: root.headerActions.length > 0
                spacing: 6
                children: root.headerActions
            }
        }
    }

    Rectangle {
        id: bodyFrame
        visible: !root.collapsed
        anchors.top: header.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        implicitHeight: body.implicitHeight + 28
        height: implicitHeight
        color: Theme.clrCard
        border.color: Theme.clrContainerHighest
        radius: Theme.r1

        // Hide the top border/radius so the body joins the header seamlessly.
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.leftMargin: 1
            anchors.rightMargin: 1
            height: Theme.r1
            color: parent.color
        }

        Column {
            id: body
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: 14
            spacing: 10
        }
    }
}
