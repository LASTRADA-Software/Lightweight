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

    // Per-release { total, applied } tallies, keyed by release version ("" is
    // the unreleased bucket). `ReleaseListModel` only carries an aggregate
    // status, so the bar's fill fraction is derived from the migration rows.
    // Roles on MigrationListModel — keep in sync with the enum in the header
    // (same convention as `TimestampAutocomplete.qml`).
    readonly property int _roleStatus: 259
    readonly property int _roleRelease: 260
    property var _stats: ({})

    function _rebuildStats() {
        const m = AppController.migrations;
        const out = {};
        for (let i = 0; i < m.rowCount(); ++i) {
            const idx = m.index(i, 0);
            const status = m.data(idx, _roleStatus);
            // Unknown rows are applied-but-unregistered: they belong to no
            // release's declared migration set, matching ReleaseListModel.
            if (status === "unknown")
                continue;
            const version = m.data(idx, _roleRelease) || "";
            const s = out[version] || (out[version] = { total: 0, applied: 0 });
            s.total += 1;
            if (status === "applied" || status === "checksum-mismatch")
                s.applied += 1;
        }
        _stats = out;
    }

    Component.onCompleted: _rebuildStats()
    Connections {
        target: AppController.migrations
        function onModelReset() { root._rebuildStats() }
        function onDataChanged() { root._rebuildStats() }
    }

    readonly property var _unreleased: _stats[""] || ({ total: 0, applied: 0 })

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
            total: root._stats[version] ? root._stats[version].total : migrationCount
            applied: root._stats[version] ? root._stats[version].applied
                                          : (status === "applied" ? migrationCount : 0)
        }
    }
}
