// SPDX-License-Identifier: Apache-2.0
//
// Tabbed container hosting `LogPanel` and `SqlQueryPanel`. Owns the
// always-visible footer with the panel-collapse toggle so the two body
// components stay simple — they no longer need to know about
// `AppController.logVisible`. The setting key remains `ui/logVisible`
// despite now gating both tabs: renaming would invalidate every existing
// user's QSettings entry, and the semantic drift is a one-line comment in
// `AppController.hpp`.
//
// Only embedded by `ExpertView.qml`; `SimpleView.qml` does not use a bottom
// panel at all.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Lightweight.Migrations

Rectangle {
    id: root
    color: Theme.bgChrome

    readonly property bool expanded: AppController.logVisible
    /// Currently active tab index. 0 = Log, 1 = SQL Query. Reset to 0 on
    /// each launch (intentionally not persisted).
    property int currentTab: 0

    // Tab bar — only visible when the panel is expanded; collapses fully so
    // the footer toggle is the only thing left in the strip.
    TabBar {
        id: tabs
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 32
        visible: root.expanded
        currentIndex: root.currentTab
        onCurrentIndexChanged: root.currentTab = currentIndex

        TabButton {
            text: qsTr("Log")
            ToolTip.visible: hovered
            ToolTip.delay: 500
            ToolTip.text: qsTr("Live migration log — all output from the most recent run.")
        }
        TabButton {
            text: qsTr("SQL Query")
            ToolTip.visible: hovered
            ToolTip.delay: 500
            ToolTip.text: qsTr("Execute ad-hoc SQL against the connected database.")
        }
    }

    StackLayout {
        id: stack
        anchors.top: tabs.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: footer.top
        currentIndex: tabs.currentIndex
        visible: root.expanded

        LogPanel {
            id: logPanel
        }
        SqlQueryPanel {
            id: sqlPanel
        }
    }

    // Footer strip pinned to the bottom — always visible, regardless of
    // collapsed/expanded state. Hosts the *single* toggle that flips
    // `AppController.logVisible`. When the panel is collapsed the SplitView
    // shrinks the whole component down to this strip, so the user always
    // has a clearly-discoverable affordance to bring the panel back.
    Rectangle {
        id: footer
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 28
        color: Theme.bgChrome

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 1
            color: Theme.border
            visible: root.expanded
        }

        Label {
            anchors.left: parent.left
            anchors.leftMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            text: {
                if (root.expanded)
                    return tabs.currentIndex === 0
                        ? qsTr("Log")
                        : qsTr("SQL Query")
                // Collapsed: surface a count from whichever tab is active so
                // the user has a hint about activity behind the strip.
                return tabs.currentIndex === 0
                    ? qsTr("Log — %1 line(s)").arg(logPanel.lineCount)
                    : qsTr("SQL Query — %1 row(s)").arg(sqlPanel.rowCount)
            }
            color: Theme.textFaint
            font.pixelSize: 11
        }

        Button {
            id: toggleButton
            anchors.right: parent.right
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            flat: true
            // ▼ = "click to collapse downward"; ▲ = "click to expand upward".
            // The arrow always points toward the action that the click takes.
            text: root.expanded
                ? qsTr("▼ Hide panel")
                : qsTr("▲ Show panel")
            ToolTip.visible: hovered
            ToolTip.delay: 500
            ToolTip.timeout: 8000
            ToolTip.text: root.expanded
                ? qsTr("Collapse the bottom panel to a one-line strip, giving the migration list more vertical space. Tab content is preserved — expanding the panel restores whichever tab was active.")
                : qsTr("Expand the bottom panel. The Log tab shows the captured migration output; the SQL Query tab is a scratchpad for ad-hoc queries against the connected database.")
            onClicked: AppController.setLogVisible(!root.expanded)
        }
    }
}
