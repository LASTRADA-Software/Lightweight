// SPDX-License-Identifier: Apache-2.0
//
// Live per-table detail panel for one profile's backup/restore run. Bound to a
// BackupTableListModel (the profile's `tables` role from BackupStatusListModel)
// and rendered as the detail region beside (or, on a narrow page, below)
// BackupsPage's profile table.
//
// Drawn as a kit panel (`k-panel`) of its own — a 40 px header with the
// profile name, its run-state pill and the worker count — so the page lays it
// out like any other card. The body is deliberately two-tier so it stays
// readable even when a profile has hundreds of tables:
//
//   1. A SUMMARY — the overall "142 / 700 tables" readout, an aggregate
//      progress bar, and per-state chips (running / queued / error / warning).
//      These come straight from the model's cached tallies (totalCount,
//      doneCount, …), so no row scan happens in QML.
//   2. An "Active tables" section (kit section header + table header row)
//      listing only the tables that still need attention — running, error,
//      and warning. Completed ("done") and not-yet-started ("queued") tables
//      are collapsed out of the list: the user asked to see progress on what
//      is happening NOW, not a wall of finished rows.
//
// The list owns its own scrollbar and scrolls independently of the profile
// table (each region owns its scrollbar).

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Lightweight.Migrations

Rectangle {
    id: root

    /// The selected profile's per-table model (BackupTableListModel*), or null.
    property var tablesModel: null
    /// Profile name shown in the header.
    property string profileName: ""
    /// Overall run state string, drives the header pill ("running", "ok", …).
    property string overallState: "idle"
    /// Number of parallel table workers (BackupConcurrency()), shown as a caption.
    property int workerCount: 1

    color: Theme.clrCard
    border.color: Theme.clrContainerHighest
    radius: Theme.r3
    clip: true

    // Proxy that surfaces only the running/error/warning rows of `tablesModel`,
    // re-filtering automatically as tables transition state. The list below
    // binds to this so hundreds of done/queued rows never reach the view. The
    // SUMMARY still reads its tallies from the unfiltered source model.
    BackupActiveTablesModel {
        id: activeProxy
        sourceModel: root.tablesModel
    }

    // Summary tallies, read reactively off the model's notifying properties.
    // Guarded against a null model so the empty state renders cleanly.
    readonly property int _total: tablesModel ? tablesModel.totalCount : 0
    readonly property int _done: tablesModel ? tablesModel.doneCount : 0
    readonly property int _running: tablesModel ? tablesModel.runningCount : 0
    readonly property int _queued: tablesModel ? tablesModel.queuedCount : 0
    readonly property int _errors: tablesModel ? tablesModel.errorCount : 0
    readonly property int _warnings: tablesModel ? tablesModel.warningCount : 0
    readonly property bool _hasRun: _total > 0

    // Width of the right-aligned ROWS column, shared by the table header and
    // every row so the counts line up under their heading.
    readonly property int _rowsColumnWidth: 76

    /// Compact row count: 4096 → "4.1k", 3_500_000 → "3.5M".
    ///
    /// Row counts run to ten digits on real tables, and the per-table column is
    /// ~76px wide — printing them raw produced "203123123 / 320492304592345",
    /// which overflowed the column, pushed the status pill out, and is unreadable
    /// anyway (nobody counts digit groups to compare two 12-digit numbers).
    /// Three significant digits are all this readout needs: it exists to convey
    /// *proportion*, and the exact figure is in the log pane.
    ///
    /// Thresholds use 1000 (not 1024) because these are row counts, not bytes.
    /// @param n Row count.
    /// @return Compact string, or "" for a negative/undefined input.
    function _formatRows(n) {
        if (n === undefined || n === null || n < 0)
            return ""
        if (n < 1000)
            return "" + Math.floor(n)
        const units = ["k", "M", "G", "T", "P"]
        let value = n / 1000
        let i = 0
        while (value >= 1000 && i < units.length - 1) {
            value /= 1000
            i++
        }
        // One decimal below 10 ("4.1k"), none above ("142k") — keeps every
        // rendering to at most 5 characters so the column never reflows.
        return (value < 10 ? value.toFixed(1) : Math.round(value).toString()) + units[i]
    }

    /// Palette key for the header pill.
    function _pillStatus(s) {
        if (s === "running" || s === "queued")
            return "running"
        if (s === "failed" || s === "error")
            return "unknown"
        if (s === "ok" || s === "done")
            return "applied"
        if (s === "warning")
            return "pending"
        return "empty"
    }

    ColumnLayout {
        id: contentColumn
        anchors.fill: parent
        anchors.margins: 1
        spacing: 0

        // ---- Header (k-panel .hd): glyph, profile name, state pill, workers ----
        Item {
            Layout.fillWidth: true
            implicitHeight: 40

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 12
                spacing: 8

                Glyph {
                    Layout.alignment: Qt.AlignVCenter
                    name: "history"
                    size: 15
                    color: Theme.clrOnSurfaceSubtle
                }
                Label {
                    // `Layout.fillWidth` + `elide` yields the truncation. Do
                    // NOT compute a `Layout.maximumWidth` from `parent.width`
                    // minus a sibling's width here: the sibling's width is
                    // itself an output of this layout pass, so the binding
                    // feeds the layout its own result and Qt aborts with
                    // "Detected recursive rearrange". `Layout.minimumWidth: 0`
                    // is what lets the layout shrink this Label instead of the
                    // status pill.
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    text: root.profileName === "" ? qsTr("Backup details") : root.profileName
                    color: Theme.clrOnSurface
                    font.pixelSize: Theme.sizeBody
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                    ToolTip.visible: titleHover.hovered && truncated
                    ToolTip.delay: 400
                    ToolTip.text: root.profileName
                    HoverHandler { id: titleHover }
                }
                StatusPill {
                    id: statePill
                    visible: root.profileName !== ""
                    // Reserve the pill's width so a long profile name elides
                    // rather than collapsing the status out of the header.
                    Layout.minimumWidth: visible ? implicitWidth : 0
                    Layout.preferredWidth: implicitWidth
                    status: root._pillStatus(root.overallState)
                    label: root.overallState
                }
                // Worker-count chip (neutral kit pill).
                Rectangle {
                    visible: root.profileName !== ""
                    Layout.minimumWidth: visible ? implicitWidth : 0
                    implicitWidth: workersRow.implicitWidth + 16
                    implicitHeight: 22
                    radius: Theme.rPill
                    color: Theme.clrContainer
                    border.color: Theme.clrContainerHighest

                    Row {
                        id: workersRow
                        anchors.centerIn: parent
                        spacing: 5
                        Glyph {
                            anchors.verticalCenter: parent.verticalCenter
                            name: "workers"
                            size: 11
                            color: Theme.clrOnSurfaceSubtle
                            knockout: Theme.clrContainer
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("%n worker(s)", "", root.workerCount)
                            color: Theme.clrOnSurfaceMed
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.sizeLabel
                            font.weight: Font.DemiBold
                        }
                    }
                    ToolTip.visible: workerHover.hovered
                    ToolTip.text: qsTr("Tables are backed up in parallel by up to %n worker thread(s).", "", root.workerCount)
                    HoverHandler { id: workerHover }
                }
            }

            Rectangle {
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                height: 1
                color: Theme.clrDivider
            }
        }

        // Archive identity line. Names the file the run is writing, so the
        // panel says *which* archive these per-table rows belong to rather
        // than only which profile.
        Label {
            Layout.fillWidth: true
            Layout.leftMargin: 14
            Layout.rightMargin: 14
            Layout.topMargin: 10
            visible: root.profileName !== ""
            text: root.profileName + ".zip"
            color: Theme.clrOnSurfaceFaint
            font: Theme.monoFont(11)
            elide: Text.ElideMiddle
        }

        // ---- Summary block (visible once the run has any tables) ----
        ColumnLayout {
            id: summaryBlock
            visible: root._hasRun
            Layout.fillWidth: true
            Layout.leftMargin: 14
            Layout.rightMargin: 14
            Layout.topMargin: 6
            Layout.bottomMargin: 12
            spacing: 8

            // Aggregate readout. The done-count is the single number a user
            // watching a long run actually wants, so it gets display size and
            // tabular figures (so the digits don't shuffle sideways as it
            // climbs), with the percentage right-aligned.
            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Label {
                    text: "" + root._done
                    color: Theme.clrOnSurface
                    font.pixelSize: Theme.sizeValue
                    font.weight: Font.DemiBold
                    // Tabular figures: a proportional '1' is narrower than a
                    // '7', so without this the number visibly jitters on every
                    // progress tick.
                    font.features: ({ "tnum": 1 })
                }
                Label {
                    Layout.alignment: Qt.AlignBaseline
                    text: qsTr("/ %n table(s) done", "", root._total)
                    color: Theme.clrOnSurfaceSubtle
                    font.pixelSize: Theme.sizeBodySm
                }
                Item { Layout.fillWidth: true }
                Label {
                    Layout.alignment: Qt.AlignBaseline
                    text: root._total > 0
                          ? Math.floor(100 * root._done / root._total) + "%"
                          : ""
                    color: root._errors > 0 ? Theme.clrError
                         : (root._done === root._total ? Theme.clrSuccess : Theme.clrOnSurfaceMed)
                    font.pixelSize: Theme.sizeTitleSm
                    font.weight: Font.DemiBold
                    font.features: ({ "tnum": 1 })
                }
            }

            ProgressTrack {
                Layout.fillWidth: true
                implicitHeight: 8
                from: 0
                to: root._total > 0 ? root._total : 1
                value: root._done
                trackColor: Theme.clrContainerHigh
                // Carries the same semantic as the percentage above it: green
                // once every table is in, red once a table has failed.
                fillColor: root._errors > 0 ? Theme.clrErrorDot
                         : (root._done === root._total && root._total > 0 ? Theme.clrSuccessDot : Theme.clrPrimary)
            }

            // Per-state chips — only the ones with a non-zero count show, so a
            // clean run collapses to just "done".
            Flow {
                Layout.fillWidth: true
                spacing: 6

                StatChip {
                    visible: root._running > 0
                    kind: "running"
                    text: qsTr("%n running", "", root._running)
                }
                StatChip {
                    visible: root._queued > 0
                    kind: "queued"
                    text: qsTr("%n queued", "", root._queued)
                }
                StatChip {
                    visible: root._errors > 0
                    kind: "error"
                    text: qsTr("%n error(s)", "", root._errors)
                }
                StatChip {
                    visible: root._warnings > 0
                    kind: "warning"
                    text: qsTr("%n warning(s)", "", root._warnings)
                }
                StatChip {
                    // "All done" affordance so a finished run isn't a blank
                    // panel with an empty active-list below.
                    visible: root._running === 0 && root._queued === 0
                             && root._errors === 0 && root._warnings === 0
                             && root._done > 0
                    kind: "done"
                    text: qsTr("all done")
                }
            }
        }

        // ---- "Active tables" section header (kit `dt-rg`) ----
        // When a run is in flight but nothing is currently active (e.g. between
        // waves), it says so rather than heading an empty list.
        Rectangle {
            visible: root._hasRun
            Layout.fillWidth: true
            implicitHeight: 34
            color: Theme.clrSectionHdr

            Rectangle {
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                height: 1
                color: Theme.clrSectionHdrBorder
            }
            Rectangle {
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                height: 1
                color: Theme.clrSectionHdrBorder
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 14
                spacing: 8

                Glyph {
                    name: "layers"
                    size: 14
                    color: Theme.clrOnSurfaceSubtle
                    knockout: Theme.clrSectionHdr
                }
                Label {
                    Layout.fillWidth: true
                    text: activeList.count > 0
                          ? qsTr("Active tables")
                          : qsTr("No tables in flight right now.")
                    color: activeList.count > 0 ? Theme.clrOnSurface : Theme.clrOnSurfaceSubtle
                    font.pixelSize: Theme.sizeBodySm
                    font.weight: activeList.count > 0 ? Font.DemiBold : Font.Normal
                }
                Label {
                    visible: activeList.count > 0
                    text: "" + activeList.count
                    color: Theme.clrOnSurfaceSubtle
                    font.pixelSize: Theme.sizeLabel
                    font.features: ({ "tnum": 1 })
                }
            }
        }

        // ---- Table header row (kit `dt-mr.th`) ----
        Rectangle {
            visible: root._hasRun && activeList.count > 0
            Layout.fillWidth: true
            implicitHeight: 28
            color: Theme.clrContainerLow

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 14
                spacing: 10

                Repeater {
                    model: [
                        { text: qsTr("Table"), width: -1 },
                        { text: qsTr("Rows"), width: root._rowsColumnWidth },
                        { text: qsTr("State"), width: 78 }
                    ]
                    Label {
                        required property var modelData
                        required property int index
                        Layout.fillWidth: modelData.width < 0
                        Layout.preferredWidth: modelData.width < 0 ? -1 : modelData.width
                        horizontalAlignment: index === 0 ? Text.AlignLeft : Text.AlignRight
                        text: modelData.text.toUpperCase()
                        color: Theme.clrOnSurfaceSubtle
                        font.pixelSize: Theme.sizeGroup
                        font.weight: Font.Bold
                        font.letterSpacing: 0.4
                    }
                }
            }
            Rectangle {
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                height: 1
                color: Theme.clrContainerHighest
            }
        }

        // ---- Empty state — no profile selected or no run yet ----
        ColumnLayout {
            visible: !root._hasRun
            Layout.fillWidth: true
            Layout.topMargin: 28
            Layout.leftMargin: 14
            Layout.rightMargin: 14
            spacing: 8

            // Round placeholder well around an archive glyph, rather than an
            // emoji — the emoji ignored `color` and rendered as a full-colour
            // sticker.
            Rectangle {
                Layout.alignment: Qt.AlignHCenter
                implicitWidth: 40
                implicitHeight: 40
                radius: 20
                color: Theme.clrContainerLow
                border.color: Theme.clrContainerHighest

                Glyph {
                    anchors.centerIn: parent
                    name: "archive-outline"
                    size: 18
                    color: Theme.clrOnSurfaceFaint
                    knockout: Theme.clrContainerLow
                }
            }
            Label {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: root.profileName === ""
                      ? qsTr("Select a profile to see its backup details.")
                      : qsTr("No backup running for “%1”.\nPress Back up to start one.").arg(root.profileName)
                color: Theme.clrOnSurfaceSubtle
                font.pixelSize: Theme.sizeBodySm
            }
        }

        // Absorbs the slack while the list is hidden, so the empty state sits
        // at the top instead of being centred in a tall panel.
        Item {
            visible: !root._hasRun
            Layout.fillHeight: true
        }

        // ---- Active-table list: running / error / warning only ----
        // Bound to `activeProxy`, which filters the source model in C++ so only
        // in-flight tables reach the view — hundreds of finished rows never
        // appear here. This ListView owns its own vertical scrollbar and
        // scrolls independently of the profile table.
        ListView {
            id: activeList
            visible: root._hasRun
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 0
            clip: true
            model: activeProxy
            boundsBehavior: Flickable.StopAtBounds

            ScrollBar.vertical: ScrollBar {
                policy: activeList.contentHeight > activeList.height
                        ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
            }

            delegate: Rectangle {
                id: tableRow
                required property string tableName
                required property var currentRows
                required property var totalRows
                required property string state
                required property string message

                readonly property bool hasTotal: tableRow.totalRows > 0
                readonly property bool isError: tableRow.state === "error"

                width: ListView.view ? ListView.view.width : 0
                implicitHeight: rowCol.implicitHeight + 14
                height: implicitHeight
                // Kit error row: warm tint + 3 px bar, so the one table that
                // failed is findable in a scrolling list without reading every
                // status pill.
                color: isError ? Theme.clrErrorRowBg
                     : (rowHover.hovered ? Theme.clrContainerLow : Theme.clrCard)
                HoverHandler { id: rowHover }

                Rectangle {
                    visible: tableRow.isError
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: 3
                    color: Theme.clrError
                }

                ColumnLayout {
                    id: rowCol
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.leftMargin: 14
                    anchors.rightMargin: 14
                    spacing: 5

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        Label {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            text: tableRow.tableName
                            color: Theme.clrOnSurface
                            font: Theme.monoFont(11)
                            elide: Text.ElideMiddle
                        }
                        // Row counts before the pill, right-aligned in a fixed
                        // column so the numbers line up down the list instead of
                        // shifting with each table name's length.
                        Label {
                            Layout.preferredWidth: root._rowsColumnWidth
                            Layout.minimumWidth: root._rowsColumnWidth
                            horizontalAlignment: Text.AlignRight
                            // Abbreviated ("203M / 320T"), not raw digits — see
                            // _formatRows(). The exact figures are on hover.
                            text: tableRow.hasTotal
                                  ? (root._formatRows(tableRow.currentRows)
                                     + " / " + root._formatRows(tableRow.totalRows))
                                  : root._formatRows(tableRow.currentRows)
                            color: Theme.clrOnSurfaceSubtle
                            // The mono face already has uniform digit widths, so
                            // the counts line up without a `tnum` feature (and
                            // `font: …` cannot be combined with `font.features:`
                            // — QML rejects that as a double assignment).
                            font: Theme.monoFont(10)
                            elide: Text.ElideRight

                            // Exact counts, with thousands separators, for when
                            // the abbreviation is not precise enough.
                            ToolTip.visible: countHover.hovered
                            ToolTip.delay: 400
                            ToolTip.text: tableRow.hasTotal
                                          ? qsTr("%1 of %2 rows")
                                                .arg(Number(tableRow.currentRows).toLocaleString(Qt.locale()))
                                                .arg(Number(tableRow.totalRows).toLocaleString(Qt.locale()))
                                          : qsTr("%1 rows processed")
                                                .arg(Number(tableRow.currentRows).toLocaleString(Qt.locale()))
                            HoverHandler { id: countHover }
                        }
                        // Fixed-width cell so pills right-align under STATE.
                        Item {
                            Layout.preferredWidth: 78
                            Layout.minimumWidth: rowPill.implicitWidth
                            implicitHeight: rowPill.implicitHeight

                            StatusPill {
                                id: rowPill
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                width: implicitWidth
                                status: root._pillStatus(tableRow.state)
                                label: tableRow.state
                                // The row's own message line carries the detail,
                                // so the pill does not repeat the generic
                                // per-status explanation on hover (a single space
                                // is StatusPill's documented "no tooltip" value).
                                tooltipText: " "
                            }
                        }
                    }

                    ProgressTrack {
                        Layout.fillWidth: true
                        // Indeterminate until we know the total row count.
                        indeterminate: !tableRow.hasTotal && tableRow.state === "running"
                        from: 0
                        to: tableRow.hasTotal ? tableRow.totalRows : 1
                        value: tableRow.hasTotal ? tableRow.currentRows : 0
                        trackColor: Theme.clrContainerHigh
                        fillColor: tableRow.isError ? Theme.clrErrorDot
                                 : (tableRow.state === "warning" ? Theme.clrWarningDot : Theme.clrPrimary)
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: tableRow.message !== ""
                        text: tableRow.message
                        color: tableRow.isError ? Theme.clrError : Theme.clrOnSurfaceSubtle
                        font.pixelSize: Theme.sizeLabel
                        elide: Text.ElideRight
                        ToolTip.visible: msgHover.hovered && truncated
                        ToolTip.delay: 400
                        ToolTip.text: tableRow.message
                        HoverHandler { id: msgHover }
                    }
                }

                Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: 1
                    color: Theme.clrDivider
                }
            }
        }
    }
}
