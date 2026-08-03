// SPDX-License-Identifier: Apache-2.0
//
// Live per-table detail pane for one profile's backup/restore run. Bound to a
// BackupTableListModel (the profile's `tables` role from BackupStatusListModel)
// and rendered as the right-hand detail region of BackupsPage's master-detail
// layout.
//
// The panel is deliberately two-tier so it stays readable even when a profile
// has hundreds of tables:
//
//   1. A SUMMARY header — the overall "142 / 700 tables" progress, an aggregate
//      progress bar, and per-state chips (running / queued / error / warning).
//      These come straight from the model's cached tallies (totalCount,
//      doneCount, …), so no row scan happens in QML.
//   2. A live list of only the tables that still need attention — running,
//      error, and warning. Completed ("done") and not-yet-started ("queued")
//      tables are collapsed out of the list: the user asked to see progress on
//      what is happening NOW, not a wall of finished rows.
//
// The panel is transparent: it fills a host `Card` in BackupsPage, so it must
// not draw its own competing surface. Its own ListView scrolls independently of
// the profile list on the left (each region owns its scrollbar).

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

    color: "transparent"

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
        spacing: 10

        // ---- Header: profile name + overall state pill + workers chip ----
        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            ColumnLayout {
                // Must be shrinkable: its implicit width is the full
                // untruncated profile name (~50 chars), which would otherwise
                // act as a floor and squeeze the pill and workers chip away.
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                spacing: 3

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Label {
                        // `Layout.fillWidth` + `elide` yields the truncation.
                        // Do NOT compute a `Layout.maximumWidth` from
                        // `parent.width` minus a sibling's width here: the
                        // sibling's width is itself an output of this layout
                        // pass, so the binding feeds the layout its own result
                        // and Qt aborts with "Detected recursive rearrange".
                        // `Layout.minimumWidth: 0` is what lets the layout
                        // shrink this Label instead of the status pill.
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        text: root.profileName === "" ? qsTr("Backup details") : root.profileName
                        color: Theme.text
                        font.pixelSize: 15
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
                }

                // Archive identity line. Names the file the run is writing, so
                // the panel says *which* archive these per-table rows belong to
                // rather than only which profile.
                Label {
                    Layout.fillWidth: true
                    visible: root.profileName !== ""
                    text: root.profileName + ".zip"
                    color: Theme.textFaint
                    font: Theme.monoFont(11)
                    elide: Text.ElideMiddle
                }
            }
            Rectangle {
                visible: root.profileName !== ""
                Layout.alignment: Qt.AlignTop
                implicitWidth: workersRow.implicitWidth + 18
                implicitHeight: workersRow.implicitHeight + 8
                // Keep the chip at its natural size when the profile name is
                // long; the name elides instead.
                Layout.minimumWidth: visible ? implicitWidth : 0
                radius: Theme.radiusPill
                color: Theme.bgSubtle
                border.color: Theme.border

                RowLayout {
                    id: workersRow
                    anchors.centerIn: parent
                    spacing: 5
                    Glyph {
                        name: "workers"
                        size: 11
                        color: Theme.textMuted
                        knockout: Theme.bgSubtle
                    }
                    Label {
                        text: qsTr("%n worker(s)", "", root.workerCount)
                        color: Theme.textMuted
                        font.pixelSize: 11
                    }
                }
                ToolTip.visible: workerHover.hovered
                ToolTip.text: qsTr("Tables are backed up in parallel by up to %n worker thread(s).", "", root.workerCount)
                HoverHandler { id: workerHover }
            }
        }

        // Divider under the header.
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: Theme.divider
        }

        // ---- Summary block (visible once the run has any tables) ----
        ColumnLayout {
            id: summaryBlock
            visible: root._hasRun
            Layout.fillWidth: true
            spacing: 6

            // Aggregate readout. The done-count is the single number a user
            // watching a long run actually wants, so it gets display size and
            // tabular figures (so the digits don't shuffle sideways as it
            // climbs), with the percentage right-aligned in the accent. It was
            // previously a 13px label indistinguishable from the caption below
            // it.
            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Label {
                    text: "" + root._done
                    color: Theme.text
                    font.pixelSize: 22
                    font.weight: Font.DemiBold
                    // Tabular figures: a proportional '1' is narrower than a
                    // '7', so without this the number visibly jitters on every
                    // progress tick.
                    font.features: ({ "tnum": 1 })
                }
                Label {
                    Layout.alignment: Qt.AlignBaseline
                    text: qsTr("/ %n table(s) done", "", root._total)
                    color: Theme.textMuted
                    font.pixelSize: 12
                }
                Item { Layout.fillWidth: true }
                Label {
                    Layout.alignment: Qt.AlignBaseline
                    text: root._total > 0
                          ? Math.floor(100 * root._done / root._total) + "%"
                          : ""
                    color: root._errors > 0 ? Theme.errText
                         : (root._done === root._total ? Theme.okText : Theme.accent)
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                    font.features: ({ "tnum": 1 })
                }
            }

            ProgressTrack {
                Layout.fillWidth: true
                implicitHeight: 6
                from: 0
                to: root._total > 0 ? root._total : 1
                value: root._done
                // Carries the same semantic as the percentage above it: green
                // once every table is in, red once a table has failed.
                fillColor: root._errors > 0 ? Theme.err
                         : (root._done === root._total && root._total > 0 ? Theme.ok : Theme.accent)
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

            // Caption for the active list below. When a run is in flight but
            // nothing is currently active (e.g. between waves), say so rather
            // than leaving a bare heading over empty space.
            Label {
                Layout.fillWidth: true
                Layout.topMargin: 2
                text: activeList.count > 0
                      ? qsTr("Active tables")
                      : qsTr("No tables in flight right now.")
                color: Theme.textMuted
                font.pixelSize: 11
                font.weight: Font.DemiBold
            }
        }

        // ---- Empty state — no profile selected or no run yet ----
        ColumnLayout {
            visible: !root._hasRun
            Layout.fillWidth: true
            Layout.topMargin: 24
            spacing: 6

            // Dashed placeholder box around an archive glyph, rather than an
            // emoji — the emoji ignored `color`, so in dark mode it rendered as
            // a full-colour sticker on a dark panel.
            Rectangle {
                Layout.alignment: Qt.AlignHCenter
                implicitWidth: 34
                implicitHeight: 34
                radius: Theme.radiusMedium
                color: "transparent"
                border.color: Theme.borderStrong
                border.width: 1

                Glyph {
                    anchors.centerIn: parent
                    name: "archive"
                    size: 16
                    color: Theme.textFaint
                }
            }
            Label {
                Layout.alignment: Qt.AlignHCenter
                horizontalAlignment: Text.AlignHCenter
                text: root.profileName === ""
                      ? qsTr("Select a profile to see its backup details.")
                      : qsTr("No backup running for “%1”.\nPress Backup to start one.").arg(root.profileName)
                color: Theme.textMuted
                font.pixelSize: 12
            }
        }

        // ---- Active-table list: running / error / warning only ----
        // Bound to `activeProxy`, which filters the source model in C++ so only
        // in-flight tables reach the view — hundreds of finished rows never
        // appear here. This ListView owns its own vertical scrollbar and
        // scrolls independently of the profile master-list on the left.
        ListView {
            id: activeList
            visible: root._hasRun
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 0
            clip: true
            model: activeProxy
            spacing: 6
            boundsBehavior: Flickable.StopAtBounds

            ScrollBar.vertical: ScrollBar {
                policy: activeList.contentHeight > activeList.height
                        ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
            }

            delegate: Item {
                id: tableRow
                required property string tableName
                required property var currentRows
                required property var totalRows
                required property string state
                required property string message

                readonly property bool hasTotal: tableRow.totalRows > 0

                width: ListView.view ? ListView.view.width : 0
                implicitHeight: rowCol.implicitHeight + 4

                // Error rows get a tinted background so the one table that
                // failed is findable in a scrolling list without reading every
                // status pill.
                Rectangle {
                    anchors.fill: parent
                    anchors.topMargin: 1
                    anchors.bottomMargin: 1
                    radius: Theme.radiusSmall
                    color: tableRow.state === "error" ? Theme.errSoft
                         : (rowHover.hovered ? Theme.bgHover : "transparent")
                    HoverHandler { id: rowHover }
                }

                ColumnLayout {
                    id: rowCol
                    width: parent.width
                    spacing: 3

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: 8
                        Layout.rightMargin: 8
                        Layout.topMargin: 4
                        spacing: 10

                        Label {
                            Layout.fillWidth: true
                            text: tableRow.tableName
                            color: Theme.text
                            font: Theme.monoFont(11)
                            elide: Text.ElideMiddle
                        }
                        // Row counts before the pill, right-aligned in a fixed
                        // column so the numbers line up down the list instead of
                        // shifting with each table name's length.
                        Label {
                            Layout.preferredWidth: 76
                            Layout.minimumWidth: 76
                            horizontalAlignment: Text.AlignRight
                            // Abbreviated ("203M / 320T"), not raw digits — see
                            // _formatRows(). The exact figures are on hover.
                            text: tableRow.hasTotal
                                  ? (root._formatRows(tableRow.currentRows)
                                     + " / " + root._formatRows(tableRow.totalRows))
                                  : root._formatRows(tableRow.currentRows)
                            color: Theme.textMuted
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
                        StatusPill {
                            status: root._pillStatus(tableRow.state)
                            label: tableRow.state
                            // The row's own message line carries the detail, so
                            // the pill does not repeat the generic per-status
                            // explanation on hover (a single space is
                            // StatusPill's documented "no tooltip" value).
                            tooltipText: " "
                        }
                    }

                    ProgressTrack {
                        Layout.fillWidth: true
                        Layout.leftMargin: 8
                        Layout.rightMargin: 8
                        // Indeterminate until we know the total row count.
                        indeterminate: !tableRow.hasTotal && tableRow.state === "running"
                        from: 0
                        to: tableRow.hasTotal ? tableRow.totalRows : 1
                        value: tableRow.hasTotal ? tableRow.currentRows : 0
                        fillColor: tableRow.state === "error" ? Theme.err
                                 : (tableRow.state === "warning" ? Theme.warn : Theme.accent)
                    }

                    Label {
                        Layout.fillWidth: true
                        Layout.leftMargin: 8
                        Layout.rightMargin: 8
                        Layout.bottomMargin: 4
                        visible: tableRow.message !== ""
                        text: tableRow.message
                        color: tableRow.state === "error" ? Theme.errText : Theme.textFaint
                        font.pixelSize: 10
                        elide: Text.ElideRight
                    }
                }
            }
        }
    }
}
