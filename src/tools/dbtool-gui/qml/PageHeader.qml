// SPDX-License-Identifier: Apache-2.0
//
// Kit page header (`k-top` in the Lastrada design): a 52 px white band with
// a breadcrumb over the page title on the left, inline status items next to
// the title (connection chip, run-state pill), and right-aligned actions.
// Every page in the shell (Migrations, Backups, Settings) opens with one.
//
// Usage:
//     PageHeader {
//         crumbs: [ "staging-postgres", qsTr("Migrations") ]
//         title: qsTr("Migrations")
//         contextItems: [ ConnectionChip { } ]   // inline, after the title
//         actions: [ LsButton { … } ]            // right-aligned
//     }

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Lightweight.Migrations

Rectangle {
    id: root

    /// Breadcrumb segments shown above the title, joined by chevrons.
    property var crumbs: []

    /// Legacy single-segment breadcrumb; used when `crumbs` is empty.
    property string eyebrow: ""

    /// The page title.
    property string title: ""

    /// Items placed inline right after the title (pills, connection chip).
    property list<Item> contextItems

    /// Right-aligned action items, typically `LsButton`s. The primary action
    /// should come last so it sits at the trailing edge, as in the kit.
    property list<Item> actions

    readonly property var _crumbs: crumbs.length > 0 ? crumbs : (eyebrow !== "" ? [eyebrow] : [])

    color: Theme.clrCard
    implicitHeight: Theme.topH

    // Hairline separating the header band from the page body below.
    Rectangle {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: 1
        color: Theme.clrContainerHighest
    }

    RowLayout {
        id: headerRow
        anchors.fill: parent
        anchors.leftMargin: Theme.sp4
        anchors.rightMargin: Theme.sp4
        spacing: 10

        ColumnLayout {
            Layout.alignment: Qt.AlignVCenter
            Layout.maximumWidth: implicitWidth
            Layout.minimumWidth: 60
            Layout.fillWidth: true
            spacing: 1

            Row {
                visible: root._crumbs.length > 0
                spacing: 5
                Repeater {
                    model: root._crumbs
                    Row {
                        required property string modelData
                        required property int index
                        spacing: 5
                        Glyph {
                            anchors.verticalCenter: parent.verticalCenter
                            visible: index > 0
                            name: "chevron"
                            size: 10
                            color: Theme.clrOnSurfaceFaint
                        }
                        Label {
                            text: modelData
                            color: Theme.clrOnSurfaceSubtle
                            font.pixelSize: Theme.sizeLabel
                            elide: Text.ElideRight
                        }
                    }
                }
            }
            Label {
                Layout.fillWidth: true
                text: root.title
                color: Theme.clrOnSurface
                font.pixelSize: Theme.sizeTitle
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
        }

        Row {
            Layout.alignment: Qt.AlignVCenter
            visible: root.contextItems.length > 0
            spacing: 8
            children: root.contextItems
        }

        Item { Layout.fillWidth: true }

        Row {
            Layout.alignment: Qt.AlignVCenter
            spacing: 8
            children: root.actions
        }
    }
}
