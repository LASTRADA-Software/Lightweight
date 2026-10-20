// SPDX-License-Identifier: Apache-2.0
//
// Left-pane "Releases" kit panel (`dt-rel` rows): one row per declared
// release with its version, a slim progress bar (green once complete, amber
// while anything is pending) and a status pill. A synthetic "unreleased" row
// heads the list while migrations newer than the latest release exist.
//
// The list scrolls inside its own bounded area so the left sidebar doesn't
// grow without bound when the migration plugin registers hundreds of
// releases (Lastrada ships ~400). Uses KineticListView for momentum on
// touchpad swipes and an always-visible scrollbar.

import QtQuick
import QtQuick.Controls
import Lightweight.Migrations

Card {
    id: root
    objectName: "releasesSummary"
    title: qsTr("Releases")
    glyph: "layers"
    meta: listView.count === 1 ? qsTr("1 declared") : qsTr("%1 declared").arg(listView.count)
    padding: 6
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

    // Per-release { total, applied } tallies ("" is the unreleased bucket).
    // `ReleaseListModel` only carries an aggregate status, so the bar's fill
    // fraction comes from `MigrationListModel`, which keeps the tallies
    // current per row. Bumped whenever a tally changes, so the rows read it again.
    property int _tallyRevision: 0
    Connections {
        target: AppController.migrations
        function onReleaseTallyChanged() { root._tallyRevision += 1 }
        function onTallyRebuilt() { root._tallyRevision += 1 }
    }

    function _tally(version) {
        return root._tallyRevision >= 0 ? AppController.migrations.releaseTally(version) : ({});
    }

    readonly property var _unreleased: _tally("")

    /// One `dt-rel` row. `label` is the version text (empty → italic
    /// "unreleased"), `total`/`applied` drive the bar and `status` the pill.
    component ReleaseRow: Item {
        id: relRow
        property string label: ""
        property string status: "pending"
        property int total: 0
        property int applied: 0
        readonly property int pending: Math.max(0, total - applied)

        height: root.rowHeight

        Label {
            id: versionText
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            width: 70
            text: relRow.label.length > 0 ? relRow.label : qsTr("unreleased")
            color: relRow.label.length > 0 ? Theme.clrOnSurface : Theme.clrOnSurfaceSubtle
            font.pixelSize: Theme.sizeBodySm + 1
            font.weight: relRow.label.length > 0 ? Font.DemiBold : Font.Normal
            font.italic: relRow.label.length === 0
            elide: Text.ElideRight
        }

        StatusPill {
            id: pill
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            status: relRow.status
            label: (relRow.status === "pending" || relRow.status === "partial") && relRow.pending > 0
                   ? qsTr("%1 pending").arg(relRow.pending)
                   : ""
        }

        ProgressTrack {
            anchors.left: versionText.right
            anchors.right: pill.left
            anchors.leftMargin: 10
            anchors.rightMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            height: 5
            trackColor: Theme.clrContainerHigh
            to: Math.max(1, relRow.total)
            value: relRow.applied
            fillColor: relRow.total > 0 && relRow.applied >= relRow.total
                       ? Theme.clrSuccessDot : Theme.clrWarningDot
        }
    }

    ReleaseRow {
        width: parent.width
        visible: root._unreleased.total > root._unreleased.applied
        label: ""
        status: root._unreleased.applied > 0 ? "partial" : "pending"
        total: root._unreleased.total
        applied: root._unreleased.applied
    }

    KineticListView {
        id: listView
        width: parent.width
        // Shrink to the exact row count when small, otherwise cap so the
        // release panel doesn't eat the whole sidebar.
        height: Math.min(count, root.maxVisibleRows) * root.rowHeight
        clip: true
        spacing: 0
        model: AppController.releases

        delegate: ReleaseRow {
            // `status` already exists on ReleaseRow; `required status`
            // marks it as fed by the model role instead of redeclaring it.
            required property string version
            required status
            required property int migrationCount

            width: ListView.view ? ListView.view.width : 0
            label: version
            readonly property var tally: root._tally(version)
            total: tally.present ? tally.total : migrationCount
            applied: tally.present ? tally.applied : (status === "applied" ? migrationCount : 0)
        }
    }
}
