// SPDX-License-Identifier: Apache-2.0
//
// Centre pane: the migration table as one kit panel (`k-panel` + `dt-tabs` /
// `dt-mr` / `dt-rg` in the Lastrada design). A 42 px toolbar carries the
// underline filter tabs, the search field and the bulk-select menu; under it
// sit a column-header strip and the timeline, grouped by release with
// collapsible section headers. Consumed directly by `ExpertView.qml` and
// exported from the `Lightweight.Migrations 1.0` module so downstream apps
// can embed the whole migrations panel verbatim.

import QtQuick
import QtQuick.Controls
import Lightweight.Migrations

Item {
    id: root

    property string filterTab: "all" // all | pending | applied | issues
    property string search: ""

    // These are bound to notify-enabled AppController properties so the
    // tab badges re-evaluate automatically when the migration model is
    // refreshed. Calling `model.rowCount()` / `model.data(...)` from a
    // QML binding does not trigger on model resets.
    readonly property int countAll: AppController.migrationCount
    readonly property int countPending: AppController.pendingCount
    readonly property int countApplied: AppController.appliedCount
    readonly property int countIssues: AppController.issuesCount

    /// Single source of truth for the tab + search filter, shared by the row
    /// delegates and the per-release tallies so a section header disappears
    /// exactly when none of its rows survive the filter.
    function rowMatches(status, title, timestamp) {
        if (root.filterTab === "pending" && status !== "pending") return false;
        if (root.filterTab === "applied" && status !== "applied") return false;
        if (root.filterTab === "issues"
            && status !== "unknown"
            && status !== "checksum-mismatch")
            return false;
        if (root.search.length > 0
            && title.toLowerCase().indexOf(root.search.toLowerCase()) < 0
            && timestamp.indexOf(root.search) < 0)
            return false;
        return true;
    }

    // --- Release grouping ---------------------------------------------------
    // Rows arrive sorted newest-first and each carries its release version, so
    // releases are contiguous and ListView sections can group them. Tallies
    // per release (total / applied / rows passing the filter / newest
    // timestamp) are rebuilt from the model, because `ReleaseListModel` only
    // carries an aggregate status.
    // Roles on MigrationListModel — keep in sync with the enum in the header
    // (same convention as `TimestampAutocomplete.qml`).
    readonly property int _roleTimestamp: 257
    readonly property int _roleTitle: 258
    readonly property int _roleStatus: 259
    readonly property int _roleRelease: 260

    property var _groups: ({})

    /// User-toggled expand state per release, overriding the default.
    property var _expandOverride: ({})

    function _rebuildGroups() {
        const m = AppController.migrations;
        const out = {};
        for (let i = 0; i < m.rowCount(); ++i) {
            const idx = m.index(i, 0);
            const status = m.data(idx, _roleStatus);
            const ts = m.data(idx, _roleTimestamp);
            const version = m.data(idx, _roleRelease) || "";
            const g = out[version] || (out[version] = { total: 0, applied: 0, visible: 0, newest: "" });
            // Unknown rows are applied-but-unregistered: shown in the group,
            // but not counted towards its declared migration set.
            if (status !== "unknown") {
                g.total += 1;
                if (status === "applied" || status === "checksum-mismatch")
                    g.applied += 1;
            }
            if (rowMatches(status, m.data(idx, _roleTitle), ts))
                g.visible += 1;
            if (ts > g.newest)
                g.newest = ts;
        }
        _groups = out;
    }

    onFilterTabChanged: _rebuildGroups()
    onSearchChanged: _rebuildGroups()
    Component.onCompleted: _rebuildGroups()
    Connections {
        target: AppController.migrations
        function onModelReset() { root._rebuildGroups() }
        function onDataChanged() { root._rebuildGroups() }
    }

    /// Groups start expanded; a click on a header collapses it. Any filter or
    /// search expands every group so matches are never hidden behind a
    /// header. Collapsing fully applied releases by default (as the mockup
    /// shows) is deliberately not done: hidden rows are zero-height
    /// delegates, and ListView instantiates every one of them inside the
    /// viewport, so a default-collapsed plugin with hundreds of releases
    /// would build its whole delegate set up front.
    function isExpanded(version) {
        if (filterTab !== "all" || search.length > 0)
            return true;
        return _expandOverride[version] !== false;
    }

    function toggleExpanded(version) {
        // Filtered views always show every match; ignore the click there so
        // it cannot silently collapse the group in the unfiltered view.
        if (filterTab !== "all" || search.length > 0)
            return;
        const next = Object.assign({}, _expandOverride);
        next[version] = !isExpanded(version);
        _expandOverride = next;
    }

    /// "20260106101833" → "2026-01-06"; anything else is returned as-is.
    function dateOf(ts) {
        return /^\d{14}$/.test(ts) ? `${ts.slice(0, 4)}-${ts.slice(4, 6)}-${ts.slice(6, 8)}` : "";
    }

    /// Column-header caption (`dt-mr.th`): 10 px bold uppercase.
    component HeaderLabel: Label {
        color: Theme.clrOnSurfaceSubtle
        font.pixelSize: Theme.sizeGroup
        font.weight: Font.Bold
        font.letterSpacing: 0.4
        anchors.verticalCenter: parent ? parent.verticalCenter : undefined
        elide: Text.ElideRight
    }

    // --- Table panel ---------------------------------------------------------
    Rectangle {
        id: panel
        anchors.fill: parent
        color: Theme.clrCard
        border.color: Theme.clrContainerHighest
        radius: Theme.r3
        clip: true

        // Toolbar: tabs on the left, search + bulk controls on the right.
        Item {
            id: toolbar
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: 1
            height: 42

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: Theme.clrContainerHighest
            }

            FilterTabs {
                id: filterTabs
                anchors.left: parent.left
                anchors.leftMargin: 6
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                current: root.filterTab
                tabs: [
                    { key: "all",     label: qsTr("All"),     count: root.countAll,
                      tip: qsTr("Every registered migration.") },
                    { key: "pending", label: qsTr("Pending"), count: root.countPending,
                      tip: qsTr("Not yet applied.") },
                    { key: "applied", label: qsTr("Applied"), count: root.countApplied,
                      tip: qsTr("Already in schema_migrations.") },
                    { key: "issues",  label: qsTr("Issues"),  count: root.countIssues,
                      tip: qsTr("Unknown or checksum-mismatch rows.") },
                ]
                onActivated: key => root.filterTab = key
            }

            Row {
                id: tools
                // Above the tabs, so an expanded search box covers them.
                z: 1
                anchors.right: parent.right
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                spacing: 6

                // Kit search control (`k-ctl`). Shrinks with the centre pane;
                // when even its minimum would overlap the tabs it folds to a
                // square search button that expands over the tabs on focus
                // (and stays expanded while it holds a query).
                Rectangle {
                    id: searchBox
                    readonly property real available: toolbar.width - filterTabs.implicitWidth - bulk.width - 32
                    readonly property bool expanded: available >= 140 || searchField.activeFocus
                                                     || searchField.text !== ""
                    anchors.verticalCenter: parent.verticalCenter
                    width: expanded ? Math.max(160, Math.min(220, available)) : Theme.ctlSm
                    height: Theme.ctlSm
                    radius: Theme.r1
                    color: searchField.activeFocus ? Theme.clrFieldFocusBg : Theme.clrCard
                    border.color: searchField.activeFocus ? Theme.clrPrimary : Theme.clrFieldBorder

                    // Focus halo (`--clrFocusRing`), outside the control.
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: -3
                        radius: parent.radius + 3
                        color: "transparent"
                        border.width: 3
                        border.color: Theme.clrFocusRing
                        visible: searchField.activeFocus
                    }

                    Glyph {
                        id: searchGlyph
                        anchors.left: parent.left
                        anchors.leftMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        name: "search"
                        size: 13
                        color: Theme.clrOnSurfaceSubtle
                    }
                    // Collapsed: the whole square focuses the field.
                    MouseArea {
                        anchors.fill: parent
                        visible: !searchBox.expanded
                        cursorShape: Qt.PointingHandCursor
                        onClicked: searchField.forceActiveFocus()
                    }
                    TextField {
                        id: searchField
                        // Kept visible (an invisible item cannot take focus);
                        // just transparent while the box is folded.
                        opacity: searchBox.expanded ? 1 : 0
                        anchors.left: searchGlyph.right
                        anchors.right: parent.right
                        anchors.leftMargin: 4
                        anchors.verticalCenter: parent.verticalCenter
                        placeholderText: qsTr("Search title or timestamp…")
                        placeholderTextColor: Theme.clrOnSurfaceFaint
                        background: null
                        leftPadding: 2
                        rightPadding: 6
                        color: Theme.clrOnSurface
                        font.pixelSize: Theme.sizeBodySm
                        onTextChanged: root.search = text
                    }
                }

                BulkControls {
                    id: bulk
                    anchors.verticalCenter: parent.verticalCenter
                    leftLabel: qsTr("Select all")
                    rightLabel: qsTr("Deselect all")
                    leftTip: qsTr("Tick every pending migration.")
                    rightTip: qsTr("Clear all ticked rows.")
                    activeLeft: AppController.selectionCount > 0
                        && AppController.selectionCount === AppController.pendingCount
                    activeRight: AppController.selectionCount === 0
                    onLeftClicked: AppController.selectAllPending(true)
                    onRightClicked: AppController.selectAllPending(false)
                }
            }
        }

        // `Ctrl+F` jumps to the search field, as the design's hint implies.
        Shortcut {
            sequences: [StandardKey.Find]
            context: Qt.WindowShortcut
            enabled: root.visible
            onActivated: searchField.forceActiveFocus()
        }

        // Column header strip — offsets come from MigrationRow's column
        // constants so the labels line up with the cells on every width.
        Rectangle {
            id: listHeader
            anchors.top: toolbar.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: 1
            anchors.rightMargin: 1
            height: 28
            color: Theme.clrContainerLow

            // Never shown; read only for its column constants.
            MigrationRow { id: geometry; visible: false; width: 0 }

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: Theme.clrContainerHighest
            }

            // Header checkbox: ticks every pending row, clears when all (or
            // some) are ticked — a shortcut for the bulk menu.
            Rectangle {
                id: headerCheck
                readonly property string selKind: AppController.selectionCount === 0 ? "none"
                    : (AppController.selectionCount === AppController.pendingCount ? "all" : "some")
                anchors.left: parent.left
                anchors.leftMargin: geometry.leftInset
                anchors.verticalCenter: parent.verticalCenter
                width: 15
                height: 15
                radius: 3
                opacity: AppController.pendingCount > 0 ? 1.0 : 0.35
                color: selKind === "none" ? Theme.clrCard : Theme.clrPrimary
                border.color: selKind === "none" ? Theme.clrBorderStrong : Theme.clrPrimary

                Accessible.role: Accessible.CheckBox
                Accessible.name: qsTr("Select all pending migrations")
                Accessible.checked: selKind === "all"

                Glyph {
                    anchors.centerIn: parent
                    visible: headerCheck.selKind === "all"
                    name: "check"
                    size: 11
                    color: "#ffffff"
                }
                Rectangle {
                    anchors.centerIn: parent
                    visible: headerCheck.selKind === "some"
                    width: 8
                    height: 2
                    color: "#ffffff"
                }
                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -4
                    enabled: AppController.pendingCount > 0
                    cursorShape: Qt.PointingHandCursor
                    onClicked: AppController.selectAllPending(headerCheck.selKind === "none")
                }
            }

            HeaderLabel {
                id: hdrTimestamp
                anchors.left: parent.left
                anchors.leftMargin: geometry.leftInset + geometry.checkColumn + geometry.gap
                width: geometry.timestampColumn
                text: qsTr("TIMESTAMP")
            }
            HeaderLabel {
                id: hdrStatus
                anchors.right: parent.right
                anchors.rightMargin: geometry.rightInset
                text: qsTr("STATUS")
            }
            HeaderLabel {
                anchors.left: hdrTimestamp.right
                anchors.leftMargin: geometry.gap
                anchors.right: hdrStatus.left
                anchors.rightMargin: geometry.gap
                text: qsTr("MIGRATION")
            }
        }

        KineticListView {
            id: listView
            anchors.top: listHeader.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.leftMargin: 1
            anchors.rightMargin: 1
            anchors.bottomMargin: 1
            clip: true
            spacing: 0

            model: AppController.migrations

            section.property: "releaseVersion"
            section.criteria: ViewSection.FullString
            section.delegate: Item {
                id: sectionItem
                required property string section
                readonly property var group: root._groups[section]
                // Hide the header when the filter leaves nothing under it.
                readonly property bool shown: !!group && group.visible > 0

                width: listView.width
                height: shown ? groupHeader.height : 0
                visible: shown

                ReleaseGroup {
                    id: groupHeader
                    width: parent.width
                    version: sectionItem.section
                    total: sectionItem.group ? sectionItem.group.total : 0
                    applied: sectionItem.group ? sectionItem.group.applied : 0
                    expanded: root.isExpanded(sectionItem.section)
                    meta: sectionItem.section === ""
                          ? qsTr("newer than the latest release")
                          : root.dateOf(sectionItem.group ? sectionItem.group.newest : "")
                    onToggleExpanded: root.toggleExpanded(sectionItem.section)
                }
            }

            delegate: MigrationRow {
                // Bind via `model.<role>` rather than required properties
                // so the delegate is indifferent to whether the inherited
                // MigrationRow property is marked required — the
                // explicit form always picks up the role values
                // reliably on Qt 6's model/view pipeline.
                timestamp: model.timestamp
                title: model.title
                status: model.status
                checksumMismatch: model.checksumMismatch
                selected: model.selected

                onDoubleClicked: (ts, ttl) => previewDialog.showFor(ts, ttl)

                visible: root.rowMatches(status, title, timestamp)
                         && root.isExpanded(model.releaseVersion || "")
                height: visible ? Theme.row : 0
            }
        }

        // Previews the SQL for a migration. Opened by the MigrationRow
        // `doubleClicked` signal — keeping the dialog here (rather than
        // at Main.qml level) scopes it to the migration list view, so
        // the same pattern can be reused if MigrationView is embedded
        // standalone in a downstream app.
        SqlPreviewDialog {
            id: previewDialog
            anchors.centerIn: Overlay.overlay
        }

        // Empty-state overlay
        Column {
            anchors.centerIn: listView
            visible: listView.count === 0
            spacing: 6

            Glyph {
                anchors.horizontalCenter: parent.horizontalCenter
                name: AppController.connected ? "layers" : "plug"
                size: 22
                color: Theme.clrOnSurfaceFaint
            }
            Label {
                anchors.horizontalCenter: parent.horizontalCenter
                text: AppController.connected
                    ? qsTr("No migrations registered")
                    : qsTr("Not connected")
                color: Theme.clrOnSurfaceMed
                font.pixelSize: Theme.sizeBody
                font.weight: Font.DemiBold
            }
            Label {
                anchors.horizontalCenter: parent.horizontalCenter
                text: AppController.connected
                    ? qsTr("Point a profile at a plugin directory to see migrations here.")
                    : qsTr("Pick a profile in the left pane and press Connect.")
                color: Theme.clrOnSurfaceSubtle
                font.pixelSize: Theme.sizeLabel
            }
        }
    }
}
