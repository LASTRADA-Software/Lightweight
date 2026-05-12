// SPDX-License-Identifier: Apache-2.0
//
// Expert view: the original three-pane body of `Main.qml` — connection +
// status + releases (left), migration timeline + log panel (centre),
// actions panel (right). Extracted verbatim so `Main.qml` can swap
// between this and `SimpleView.qml` via the toolbar toggle.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Lightweight.Migrations

SplitView {
    id: mainSplit
    orientation: Qt.Horizontal

    // --- Size hints (consumed by `Main.qml` on view-mode switch) ---
    // These mirror the original window defaults tuned for the three-pane
    // layout: below the minimums the right pane eats the centre, which is
    // harder to read than a scrolled single column.
    readonly property int preferredViewWidth: 1380
    readonly property int preferredViewHeight: 860
    readonly property int minimumViewWidth: 900
    readonly property int minimumViewHeight: 520
    handle: Rectangle {
        implicitWidth: 4
        color: SplitHandle.pressed ? Theme.accent
             : SplitHandle.hovered ? Theme.borderStrong
             : Theme.border
    }

    // Left pane — stacked cards, scrolls when the window is too short
    Rectangle {
        SplitView.preferredWidth: 300
        SplitView.minimumWidth: 260
        color: Theme.bgSidebar

        ScrollView {
            id: leftScroll
            anchors.fill: parent
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            contentWidth: availableWidth

            ColumnLayout {
                width: leftScroll.availableWidth
                spacing: 10

                Label {
                    text: qsTr("CONNECTION")
                    color: Theme.textFaint
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                    font.letterSpacing: 1
                    Layout.leftMargin: 14
                    Layout.topMargin: 14
                    Layout.fillWidth: true
                }
                ConnectionPanel {
                    Layout.fillWidth: true
                    Layout.leftMargin: 14
                    Layout.rightMargin: 14
                }

                Label {
                    text: qsTr("STATUS")
                    color: Theme.textFaint
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                    font.letterSpacing: 1
                    Layout.leftMargin: 14
                    Layout.topMargin: 4
                    Layout.fillWidth: true
                }
                StatusCard {
                    Layout.fillWidth: true
                    Layout.leftMargin: 14
                    Layout.rightMargin: 14
                }

                Label {
                    text: qsTr("RELEASES")
                    color: Theme.textFaint
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                    font.letterSpacing: 1
                    Layout.leftMargin: 14
                    Layout.topMargin: 4
                    Layout.fillWidth: true
                }
                ReleasesSummary {
                    Layout.fillWidth: true
                    Layout.leftMargin: 14
                    Layout.rightMargin: 14
                    Layout.bottomMargin: 14
                }
            }
        }
    }

    // Centre pane: migration list on top, log panel below.
    SplitView {
        id: centreSplit
        SplitView.fillWidth: true
        SplitView.minimumWidth: 320
        orientation: Qt.Vertical
        handle: Rectangle {
            implicitHeight: 7
            color: SplitHandle.pressed ? Theme.accent
                 : SplitHandle.hovered ? Theme.borderStrong
                 : Theme.border

            Row {
                anchors.centerIn: parent
                spacing: 4
                Repeater {
                    model: 3
                    Rectangle {
                        width: 3
                        height: 3
                        radius: 1.5
                        color: SplitHandle.pressed || SplitHandle.hovered
                            ? Theme.bgPage : Theme.textFaint
                        opacity: 0.7
                    }
                }
            }

            HoverHandler {
                cursorShape: Qt.SizeVerCursor
            }
        }

        Rectangle {
            SplitView.fillHeight: true
            SplitView.minimumHeight: 160
            color: Theme.bgPage
            MigrationView {
                anchors.fill: parent
                anchors.margins: 16
            }
        }

        BottomPanel {
            id: bottomPanel
            readonly property int defaultExpandedHeight: 280
            readonly property int expandedMinHeight: 160
            readonly property int collapsedHeight: 28

            property int rememberedExpandedHeight: defaultExpandedHeight

            function applyExpanded() {
                var h = Math.max(expandedMinHeight, rememberedExpandedHeight)
                SplitView.minimumHeight = h
                SplitView.maximumHeight = h
                SplitView.preferredHeight = h
                Qt.callLater(function() {
                    SplitView.minimumHeight = expandedMinHeight
                    SplitView.maximumHeight = Number.POSITIVE_INFINITY
                })
            }
            function applyCollapsed() {
                if (height > collapsedHeight)
                    rememberedExpandedHeight = height
                SplitView.minimumHeight = collapsedHeight
                SplitView.maximumHeight = collapsedHeight
                SplitView.preferredHeight = collapsedHeight
            }

            Component.onCompleted: {
                if (AppController.logVisible)
                    applyExpanded()
                else
                    applyCollapsed()
            }

            Connections {
                target: AppController
                function onLogVisibleChanged() {
                    if (AppController.logVisible)
                        bottomPanel.applyExpanded()
                    else
                        bottomPanel.applyCollapsed()
                }
            }
        }
    }

    // Right pane — fixed toolbar-like column, scrolls vertically when
    // the window is short.
    Rectangle {
        SplitView.preferredWidth: 340
        SplitView.minimumWidth: 300
        color: Theme.bgSidebar

        ScrollView {
            id: rightScroll
            anchors.fill: parent
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            contentWidth: availableWidth

            ActionsPanel {
                width: rightScroll.availableWidth - 28
                x: 14
                y: 14
            }
        }
    }
}
