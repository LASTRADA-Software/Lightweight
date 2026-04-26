// SPDX-License-Identifier: Apache-2.0
//
// Per-release summary list: version pill + truncated highest-timestamp + aggregate status.
// Scrollable inside its own bounded area so the left sidebar doesn't grow without bound
// when the migration plugin registers hundreds of releases (Lastrada ships ~400).
//
// Uses KineticListView so the list:
//   - gives momentum follow-through on touchpad swipes,
//   - always shows a vertical ScrollBar so users immediately see it's scrollable.

import QtQuick
import QtQuick.Controls
import Lightweight.Migrations

Card {
    id: root
    padding: 8
    spacing: 0

    /// @brief Height of a single release row. Kept as a property so the caller
    /// can tune the maximum visible-rows heuristic if needed.
    property int rowHeight: 30

    /// @brief Maximum number of rows to show before the list starts scrolling.
    /// The Card's content area shrinks to this when there are fewer rows, and
    /// caps at this count when there are more — so the sidebar layout stays
    /// predictable for small plugins while huge plugins (hundreds of releases)
    /// still fit.
    property int maxVisibleRows: 8

    KineticListView {
        id: listView
        width: parent.width
        // Shrink to the exact row count when small, otherwise cap so the
        // release panel doesn't eat the whole sidebar.
        height: Math.min(count, root.maxVisibleRows) * root.rowHeight
        clip: true
        spacing: 0
        model: AppController.releases

        delegate: Rectangle {
            required property string version
            required property string highestTimestamp
            required property string status

            width: ListView.view ? ListView.view.width : 0
            height: root.rowHeight
            color: "transparent"

            Row {
                anchors.fill: parent
                anchors.leftMargin: 4
                anchors.rightMargin: 4
                spacing: 8

                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    color: Theme.accentSoft
                    radius: 4
                    width: versionLabel.implicitWidth + 12
                    height: versionLabel.implicitHeight + 4
                    Label {
                        id: versionLabel
                        anchors.centerIn: parent
                        text: version
                        color: Theme.accent
                        font.family: Theme.monoFont
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                    }
                }

                Label {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "…" + (highestTimestamp.length > 8 ? highestTimestamp.slice(-8) : highestTimestamp)
                    color: Theme.textFaint
                    font.family: Theme.monoFont
                    font.pixelSize: 11
                    width: parent.width - 90 - pill.width
                    elide: Text.ElideRight
                }

                StatusPill {
                    id: pill
                    anchors.verticalCenter: parent.verticalCenter
                    status: parent.parent.status
                }
            }
        }
    }
}
