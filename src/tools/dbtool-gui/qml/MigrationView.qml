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
    objectName: "migrationList"

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

    // --- Release grouping ---------------------------------------------------
    // Rows arrive sorted newest-first and each carries its release version, so
    // releases are contiguous and ListView sections can group them. The filter
    // model drops the rows the tab, the search or a collapsed release hide, so
    // the view creates delegates for shown rows only; the per-release tallies
    // come from `MigrationListModel`, which keeps them current per row.
    MigrationFilterModel {
        id: shownRows
        sourceModel: AppController.migrations
        filterTab: root.filterTab
        search: root.search
    }
    /// The rows the table shows.
    readonly property alias filterModel: shownRows

    /// Bumped whenever a release tally changes, so the section headers read it again.
    property int _tallyRevision: 0
    Connections {
        target: AppController.migrations
        function onReleaseTallyChanged() { root._tallyRevision += 1 }
        function onTallyRebuilt() { root._tallyRevision += 1 }
    }

    /// Groups start expanded; a click on a header collapses it. Any filter or
    /// search expands every group so matches are never hidden behind a header.
    function isExpanded(version) {
        return shownRows.expansionRevision >= 0 && shownRows.isExpanded(version);
    }

    function toggleExpanded(version) {
        shownRows.toggleExpanded(version);
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
                    { key: "all",     label: qsTr("All"),
                      tip: qsTr("Every registered migration.") },
                    { key: "pending", label: qsTr("Pending"),
                      tip: qsTr("Not yet applied.") },
                    { key: "applied", label: qsTr("Applied"),
                      tip: qsTr("Already in schema_migrations.") },
                    { key: "issues",  label: qsTr("Issues"),
                      tip: qsTr("Unknown or checksum-mismatch rows.") },
                ]
                counts: ({ all: root.countAll, pending: root.countPending, applied: root.countApplied,
                           issues: root.countIssues })
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
            objectName: "migrationListView"
            anchors.top: listHeader.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.leftMargin: 1
            anchors.rightMargin: 1
            anchors.bottomMargin: 1
            clip: true
            spacing: 0

            model: shownRows

            section.property: "releaseVersion"
            section.criteria: ViewSection.FullString
            section.delegate: Item {
                id: sectionItem
                required property string section
                readonly property var group: root._tallyRevision >= 0 ? AppController.migrations.releaseTally(section)
                                                                         : ({})

                width: listView.width
                height: groupHeader.height

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
                objectName: "migrationRow_" + model.timestamp
                timestamp: model.timestamp
                title: model.title
                status: model.status
                checksumMismatch: model.checksumMismatch
                selected: model.selected

                onDoubleClicked: (ts, ttl) => previewDialog.showFor(ts, ttl)

                // A collapsed release keeps only its first row, which carries the header, at no height.
                readonly property bool shown: root.isExpanded(model.releaseVersion || "")
                visible: shown
                height: shown ? Theme.row : 0
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

        // A filter or search that leaves no row.
        Label {
            objectName: "migrationListNoMatch"
            anchors.centerIn: listView
            visible: listView.count === 0 && AppController.migrationCount > 0
            text: qsTr("No migrations match this filter")
            color: Theme.clrOnSurfaceMed
            font.pixelSize: Theme.sizeBody
        }

        // Empty-state overlay
        Column {
            anchors.centerIn: listView
            // The list is empty, not just filtered down to nothing.
            visible: AppController.migrationCount === 0
            spacing: 6

            Spinner {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: AppController.pluginsLoading
                running: visible
                diameter: 22
            }
            Glyph {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: !AppController.pluginsLoading
                name: AppController.connected ? "layers" : "plug"
                size: 22
                color: Theme.clrOnSurfaceFaint
            }
            Label {
                objectName: "migrationListEmptyTitle"
                anchors.horizontalCenter: parent.horizontalCenter
                text: AppController.pluginsLoading
                    ? qsTr("Loading plugins…")
                    : AppController.connected
                    ? qsTr("No migrations registered")
                    : qsTr("Not connected")
                color: Theme.clrOnSurfaceMed
                font.pixelSize: Theme.sizeBody
                font.weight: Font.DemiBold
            }
            Label {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: !AppController.pluginsLoading
                text: AppController.connected
                    ? qsTr("Point a profile at a plugin directory to see migrations here.")
                    : qsTr("Pick a profile in the left pane and press Connect.")
                color: Theme.clrOnSurfaceSubtle
                font.pixelSize: Theme.sizeLabel
            }
        }
    }
}
