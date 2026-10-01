// SPDX-License-Identifier: Apache-2.0
//
// Expert view (`migrations.html` option 1a in the Lastrada design): three
// panes in a resizable SplitView —
//   * left (`dt-side`): Connection, Status and Releases kit panels on the
//     light container tone;
//   * centre (`dt-center`): the migration table panel above the Log / SQL
//     bottom panel, with a 12 px gutter that doubles as the vertical resize
//     handle;
//   * right (`dt-act`): target, options and the primary action anchored to
//     the bottom of a white pane.
//
// The page header (title, connection chip, Simple | Expert switch, Refresh)
// and the status bar belong to `Main.qml`, so this view draws neither.

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

    // Pane separators are 1 px hairlines (the design's pane borders), but a
    // 1 px target is unusable with a mouse — the containment mask widens the
    // grab area to 9 px without taking layout space from the panes.
    handle: Rectangle {
        id: paneHandle
        implicitWidth: 1
        color: SplitHandle.pressed ? Theme.clrPrimary
             : SplitHandle.hovered ? Theme.clrBorderStrong
             : Theme.clrContainerHighest
        containmentMask: Item {
            x: -4
            width: 9
            height: paneHandle.height
        }
    }

    // Left pane — stacked kit panels; scrolls when the window is too short.
    Rectangle {
        SplitView.preferredWidth: 272
        SplitView.minimumWidth: 240
        color: Theme.clrContainerLow

        ScrollView {
            id: leftScroll
            anchors.fill: parent
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            contentWidth: availableWidth
            contentHeight: leftColumn.implicitHeight + 2 * Theme.sp3

            ColumnLayout {
                id: leftColumn
                x: Theme.sp3
                y: Theme.sp3
                width: leftScroll.availableWidth - 2 * Theme.sp3
                spacing: Theme.sp3

                Card {
                    Layout.fillWidth: true
                    title: qsTr("Connection")
                    glyph: "server"

                    ConnectionPanel {
                        width: parent.width
                    }
                }

                StatusCard {
                    Layout.fillWidth: true
                }

                ReleasesSummary {
                    Layout.fillWidth: true
                }
            }
        }
    }

    // Centre pane: migration table on top, Log / SQL panel below.
    SplitView {
        id: centreSplit
        SplitView.fillWidth: true
        SplitView.minimumWidth: 320
        orientation: Qt.Vertical

        // The 12 px gutter between the two panels *is* the handle, so the
        // resize affordance costs no extra space. It stays invisible until
        // hovered, when a hairline appears to show what will move.
        handle: Rectangle {
            implicitHeight: Theme.sp3
            color: Theme.clrBase

            Rectangle {
                anchors.centerIn: parent
                width: Math.min(parent.width - 2 * Theme.sp3, 48)
                height: 3
                radius: 1.5
                color: SplitHandle.pressed ? Theme.clrPrimary : Theme.clrBorderStrong
                visible: SplitHandle.pressed || SplitHandle.hovered
            }

            HoverHandler {
                cursorShape: Qt.SizeVerCursor
            }
        }

        Rectangle {
            SplitView.fillHeight: true
            SplitView.minimumHeight: 160
            color: Theme.clrBase
            MigrationView {
                anchors.fill: parent
                anchors.leftMargin: Theme.sp3
                anchors.rightMargin: Theme.sp3
                anchors.topMargin: Theme.sp3
            }
        }

        BottomPanel {
            id: bottomPanel
            readonly property int defaultExpandedHeight: 240
            readonly property int expandedMinHeight: 140

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

    // Right pane — target, options and the action buttons. Scrolls when the
    // window is short; when there is spare height the panel is stretched to
    // the viewport so its spacer can pin the buttons to the bottom edge.
    Rectangle {
        SplitView.preferredWidth: 304
        SplitView.minimumWidth: 280
        color: Theme.clrCard

        ScrollView {
            id: rightScroll
            anchors.fill: parent
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            contentWidth: availableWidth
            contentHeight: actionsHost.height + 28

            // The panel fills the pane when there is spare height (so its
            // action buttons sit at the bottom edge) and scrolls otherwise.
            //
            // Binding the height to `max(implicitHeight, viewport)` directly
            // re-enters the ColumnLayout: resizing it re-runs its layout,
            // which re-announces `implicitHeight` while the binding is still
            // being evaluated (a "binding loop for height" at startup). The
            // natural height is therefore tracked one event-loop turn late in
            // `panelNaturalHeight`, which breaks the synchronous cycle without
            // changing the settled result.
            Item {
                id: actionsHost
                x: 14
                y: 14
                width: rightScroll.width - 28
                height: Math.max(panelNaturalHeight, rightScroll.availableHeight - 28)

                property real panelNaturalHeight: 0
                function syncNaturalHeight() { panelNaturalHeight = actionsPanel.implicitHeight }

                ActionsPanel {
                    id: actionsPanel
                    anchors.fill: parent
                    onImplicitHeightChanged: Qt.callLater(actionsHost.syncNaturalHeight)
                    Component.onCompleted: actionsHost.syncNaturalHeight()
                }
            }
        }
    }
}
