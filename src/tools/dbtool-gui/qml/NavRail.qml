// SPDX-License-Identifier: Apache-2.0
//
// The Lastrada single dark rail (`r-rail` in the design project): the only
// dark surface in the shell besides the code views. Replaces the old top
// `ToolBar.qml`.
//
// Layout, top to bottom: the brand block, a "Database" group with the main
// destinations, a flexible gap, then the pinned items (Settings) and the
// collapse toggle. Expanded it is `Theme.railW` wide; collapsed it shrinks to
// `Theme.railNarrowW`, hides labels and shows each label as a tooltip.
//
// The rail is data-driven: `items` and `pinnedItems` are arrays of
// `{ page, label, glyph, badge, badgeKind, visible }` and the rail reports
// the user's choice through `navigate(page)`. It owns no navigation state.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Lightweight.Migrations

Rectangle {
    id: root

    /// Main destinations, in display order.
    property var items: []
    /// Destinations pinned to the bottom of the rail (Settings).
    property var pinnedItems: []
    /// `page` of the active destination.
    property string currentPage: ""
    /// Collapsed (icon-only) rail.
    property bool collapsed: false
    /// Version line under the brand name.
    property string versionText: ""

    /// Emitted when the user picks a destination.
    signal navigate(string page)
    /// Emitted when the user clicks the collapse / expand toggle.
    signal toggleCollapsed()

    implicitWidth: collapsed ? Theme.railNarrowW : Theme.railW
    color: Theme.clrSidebarBg

    Behavior on implicitWidth { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }

    // One rail entry: icon, label, optional count badge, active bar.
    component RailItem: Rectangle {
        id: item
        required property var entry
        readonly property bool active: entry.page === root.currentPage

        Layout.fillWidth: true
        Layout.preferredHeight: 34
        visible: entry.visible === undefined || entry.visible
        radius: Theme.r2
        color: active || hover.hovered ? Theme.clrSidebarHi : "transparent"

        // Active marker: 3 px brand bar hugging the rail's left edge.
        Rectangle {
            visible: item.active
            x: -(root.collapsed ? 8 : 10)
            anchors.verticalCenter: parent.verticalCenter
            width: 3
            height: parent.height - 16
            radius: 1.5
            color: Theme.clrPrimary
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: root.collapsed ? 0 : 10
            anchors.rightMargin: root.collapsed ? 0 : 8
            spacing: 10

            Item { Layout.fillWidth: root.collapsed; visible: root.collapsed }
            Glyph {
                Layout.alignment: Qt.AlignVCenter
                name: item.entry.glyph
                size: 16
                color: item.active ? "#ffffff" : (hover.hovered ? Theme.clrSidebarText : Theme.clrSidebarMuted)
            }
            Label {
                Layout.fillWidth: true
                visible: !root.collapsed
                text: item.entry.label
                color: item.active ? "#ffffff" : (hover.hovered ? Theme.clrSidebarText : Theme.clrSidebarMuted)
                font.pixelSize: Theme.sizeBody
                font.weight: item.active ? Font.DemiBold : Font.Normal
                elide: Text.ElideRight
            }
            Rectangle {
                visible: !root.collapsed && (item.entry.badge || 0) > 0
                Layout.preferredHeight: 18
                Layout.preferredWidth: Math.max(18, badgeLabel.implicitWidth + 12)
                radius: 9
                color: item.entry.badgeKind === "warn" ? Theme.clrWarningDot : Theme.clrPrimary
                Label {
                    id: badgeLabel
                    anchors.centerIn: parent
                    text: item.entry.badge || ""
                    color: "#ffffff"
                    font.pixelSize: Theme.sizeLabel - 1
                    font.weight: Font.DemiBold
                }
            }
            Item { Layout.fillWidth: root.collapsed; visible: root.collapsed }
        }

        // Collapsed rail: the badge shrinks to a dot on the icon's corner.
        Rectangle {
            visible: root.collapsed && (item.entry.badge || 0) > 0
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.rightMargin: 8
            anchors.topMargin: 6
            width: 9; height: 9; radius: 4.5
            color: item.entry.badgeKind === "warn" ? Theme.clrWarningDot : Theme.clrPrimary
            border.width: 1.5
            border.color: Theme.clrSidebarBg
        }

        HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
        TapHandler { onTapped: if (item.entry.page !== "__toggle__") root.navigate(item.entry.page) }

        ToolTip.visible: root.collapsed && hover.hovered
        ToolTip.delay: 300
        ToolTip.text: item.entry.label

        Accessible.role: Accessible.PageTab
        Accessible.name: item.entry.label
        Accessible.selected: item.active
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: 14
        anchors.bottomMargin: 12
        anchors.leftMargin: root.collapsed ? 8 : 10
        anchors.rightMargin: root.collapsed ? 8 : 10
        spacing: 2

        // Brand block: red mark + product name.
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: root.collapsed ? 0 : 10
            Layout.bottomMargin: 14
            spacing: 10

            Item { Layout.fillWidth: true; visible: root.collapsed }
            Rectangle {
                Layout.preferredWidth: 24
                Layout.preferredHeight: 24
                radius: Theme.r2
                color: Theme.clrPrimary
                Label {
                    anchors.centerIn: parent
                    text: "LW"
                    color: "#ffffff"
                    font.pixelSize: 10
                    font.weight: Font.Bold
                    font.letterSpacing: 0.3
                }
            }
            ColumnLayout {
                visible: !root.collapsed
                Layout.fillWidth: true
                spacing: 0
                Label {
                    text: "dbtool"
                    color: "#ffffff"
                    font.pixelSize: Theme.sizeBody
                    font.weight: Font.DemiBold
                }
                Label {
                    visible: root.versionText !== ""
                    text: root.versionText
                    color: Theme.clrSidebarMuted
                    font.pixelSize: Theme.sizeLabel - 1
                }
            }
            Item { Layout.fillWidth: true; visible: root.collapsed }
        }

        // Group label; collapses to a hairline in the narrow rail.
        Label {
            visible: !root.collapsed
            Layout.leftMargin: 10
            Layout.topMargin: 4
            Layout.bottomMargin: 6
            text: qsTr("Database")
            color: Theme.clrSidebarMuted
            font.pixelSize: Theme.sizeGroup
            font.weight: Font.Bold
            font.letterSpacing: 0.7
            font.capitalization: Font.AllUppercase
        }
        Rectangle {
            visible: root.collapsed
            Layout.fillWidth: true
            Layout.leftMargin: 6
            Layout.rightMargin: 6
            Layout.topMargin: 4
            Layout.bottomMargin: 6
            height: 1
            color: Theme.clrSidebarHi
        }

        Repeater {
            model: root.items
            delegate: RailItem { required property var modelData; entry: modelData }
        }

        Item { Layout.fillHeight: true }

        Repeater {
            model: root.pinnedItems
            delegate: RailItem { required property var modelData; entry: modelData }
        }

        // Collapse / expand toggle.
        RailItem {
            entry: ({ page: "__toggle__", label: root.collapsed ? qsTr("Expand") : qsTr("Collapse"),
                      glyph: root.collapsed ? "chevron" : "chevron-left" })
            TapHandler { onTapped: root.toggleCollapsed() }
        }
    }
}
