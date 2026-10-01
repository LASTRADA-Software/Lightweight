// SPDX-License-Identifier: Apache-2.0
//
// Tabbed bottom panel of the Expert view (`dt-bp` in the Lastrada design),
// hosting `LogPanel` and `SqlQueryPanel`: a white, hairline-bordered panel
// with a 34 px kit tab strip (Log | SQL query) whose right end carries the
// log tools (copy, clear) and the single hide / show toggle for
// `AppController.logVisible`. The bodies stay simple — they no longer need to
// know about `AppController.logVisible`. The setting key remains
// `ui/logVisible` despite now gating both tabs: renaming would invalidate
// every existing user's QSettings entry, and the semantic drift is a one-line
// comment in `AppController.hpp`.
//
// When collapsed the SplitView shrinks this component to `collapsedHeight`,
// which leaves exactly the tab strip visible, so the toggle (and the tabs,
// which also expand the panel) stay discoverable.
//
// Only embedded by `ExpertView.qml`; `SimpleView.qml` does not use a bottom
// panel at all.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Lightweight.Migrations

Rectangle {
    id: root
    // The panel floats in the centre pane's 12 px gutter; the area around it
    // is page background.
    color: Theme.clrBase

    readonly property bool expanded: AppController.logVisible
    /// Currently active tab index. 0 = Log, 1 = SQL Query. Reset to 0 on
    /// each launch (intentionally not persisted).
    property int currentTab: 0

    readonly property int stripHeight: 34
    readonly property int gutter: Theme.sp3
    /// Height that shows the tab strip alone (strip + borders + gutter).
    /// `ExpertView` pins the SplitView item to this while collapsed.
    readonly property int collapsedHeight: stripHeight + 2 + gutter

    readonly property var _tabs: [
        { label: qsTr("Log"), glyph: "terminal" },
        { label: qsTr("SQL query"), glyph: "database" },
    ]

    Rectangle {
        id: panel
        anchors.fill: parent
        anchors.leftMargin: root.gutter
        anchors.rightMargin: root.gutter
        anchors.bottomMargin: root.gutter
        color: Theme.clrCard
        border.color: Theme.clrContainerHighest
        radius: Theme.r3
        clip: true

        // --- Tab strip ---
        Item {
            id: strip
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: 1
            height: root.stripHeight

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: Theme.clrContainerHighest
                visible: root.expanded
            }

            Row {
                anchors.left: parent.left
                anchors.leftMargin: 6
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                spacing: 2

                Repeater {
                    model: root._tabs

                    Item {
                        id: tab
                        required property var modelData
                        required property int index
                        readonly property bool active: root.currentTab === index && root.expanded

                        width: tabContent.implicitWidth + 24
                        height: parent.height

                        Accessible.role: Accessible.PageTab
                        Accessible.name: modelData.label
                        Accessible.selected: active

                        HoverHandler { id: tabHover; cursorShape: Qt.PointingHandCursor }
                        // Picking a tab while collapsed also brings the panel
                        // back — the tab is the obvious thing to click.
                        TapHandler {
                            onTapped: {
                                root.currentTab = tab.index
                                if (!root.expanded)
                                    AppController.setLogVisible(true)
                            }
                        }

                        Row {
                            id: tabContent
                            anchors.centerIn: parent
                            spacing: 7

                            Glyph {
                                anchors.verticalCenter: parent.verticalCenter
                                name: tab.modelData.glyph
                                size: 14
                                color: tab.active ? Theme.clrOnSurfaceMed : Theme.clrOnSurfaceSubtle
                            }
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: tab.modelData.label
                                color: tab.active || tabHover.hovered ? Theme.clrOnSurface : Theme.clrOnSurfaceMed
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.sizeBody
                                font.weight: tab.active ? Font.DemiBold : Font.Normal
                            }
                        }

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

            Row {
                id: tools
                anchors.right: parent.right
                anchors.rightMargin: 4
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2

                // Live run indicator, so activity behind a collapsed panel
                // or the SQL tab is still visible.
                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: AppController.runner.phase !== MigrationRunner.Idle
                    spacing: 6
                    rightPadding: 8
                    Spinner {
                        anchors.verticalCenter: parent.verticalCenter
                        running: parent.visible
                        diameter: 11
                    }
                    Label {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Running…")
                        color: Theme.clrOnSurfaceMed
                        font.pixelSize: Theme.sizeLabel
                    }
                }

                Label {
                    anchors.verticalCenter: parent.verticalCenter
                    rightPadding: 6
                    text: root.currentTab === 0
                        ? (logPanel.lineCount === 1 ? qsTr("1 line") : qsTr("%1 lines").arg(logPanel.lineCount))
                        : (sqlPanel.rowCount === 1 ? qsTr("1 row") : qsTr("%1 rows").arg(sqlPanel.rowCount))
                    color: Theme.clrOnSurfaceFaint
                    font.pixelSize: Theme.sizeLabel
                }

                LsButton {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: root.expanded && root.currentTab === 0
                    variant: "ghost"
                    iconOnly: true
                    glyph: "copy"
                    text: qsTr("Copy log")
                    enabled: logPanel.lineCount > 0
                    onClicked: logPanel.copyLog()
                }
                LsButton {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: root.expanded && root.currentTab === 0
                    variant: "ghost"
                    text: qsTr("Clear")
                    enabled: logPanel.lineCount > 0
                    onClicked: logPanel.clearLog()
                }
                // The chevron points toward where the panel will go.
                LsButton {
                    id: toggleButton
                    anchors.verticalCenter: parent.verticalCenter
                    variant: "ghost"
                    iconOnly: true
                    glyph: "chevron-down"
                    rotation: root.expanded ? 0 : 180
                    text: root.expanded ? qsTr("Hide panel") : qsTr("Show panel")
                    onClicked: AppController.setLogVisible(!root.expanded)
                }
            }
        }

        StackLayout {
            id: stack
            anchors.top: strip.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.leftMargin: 1
            anchors.rightMargin: 1
            anchors.bottomMargin: 1
            currentIndex: root.currentTab
            visible: root.expanded

            LogPanel {
                id: logPanel
            }
            SqlQueryPanel {
                id: sqlPanel
            }
        }
    }
}
