// SPDX-License-Identifier: Apache-2.0
//
// One row of the Backups page profile master-list: an accent selection spine,
// the profile name with its status pill, a per-row metadata line, hover-
// revealed Backup / Restore actions, and an inline progress bar while a run is
// in flight.
//
// Three things changed from the inline delegate this replaces:
//
//  1. PER-ROW METADATA IS BACK ON THE ROW. The previous design hoisted the
//     archive timestamp and size out of the delegate into a single shared line
//     under the whole list, showing only the *selected* profile's values. That
//     made every row look identical, so the list — the one surface whose job
//     is comparing profiles — could not be scanned: finding the stale archive
//     among twelve meant clicking all twelve.
//
//  2. ACTIONS REVEAL ON HOVER OR SELECTION. Two always-visible buttons per row
//     is 24 competing affordances on a twelve-profile list (600 on the 300-row
//     list the search field exists to serve), and one of them per row drops
//     and recreates every table of a database. Revealing them on the row the
//     pointer is actually on keeps the list readable and puts the destructive
//     control behind a deliberate act of pointing at its row.
//
//  3. SELECTION IS A SPINE PLUS A TINT, not a flat `accentSoft` fill. The flat
//     fill sat directly behind the "running" status pill, which uses
//     `accentSoft` as its own background — the pill dissolved into the row
//     exactly when the row mattered most.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Lightweight.Migrations

Rectangle {
    id: root

    /// Profile name, shown as the row's primary label.
    property string name: ""
    /// Palette key for the status pill (see StatusPill).
    property string pillStatus: "empty"
    /// Caption shown inside the status pill.
    property string pillLabel: ""
    /// Secondary line: archive mtime + size, an error message, or a progress
    /// caption. Rendered monospace unless `metaIsError` is set.
    property string meta: ""
    /// When true, `meta` is an error string — rendered in the error colour and
    /// the proportional font (error prose is not tabular data).
    property bool metaIsError: false
    /// True while this row is the detail region's subject.
    property bool selected: false
    /// True while this profile's backup/restore is running — shows the inline
    /// progress bar.
    property bool running: false
    /// Progress fraction [0, 1] for the inline bar.
    property real progress: 0
    /// Whether the inline bar should sweep instead of fill (unknown total).
    property bool progressIndeterminate: false
    /// Enables the Backup action.
    property bool canBackup: false
    /// Enables the Restore action.
    property bool canRestore: false
    /// Label for the left action — "Backup" normally, "Retry" after a failure.
    property string backupLabel: qsTr("Backup")

    /// Emitted when the row body is clicked (pin / unpin the detail region).
    signal activated()
    /// Emitted by the Backup (or Retry) action.
    signal backupRequested()
    /// Emitted by the Restore action.
    signal restoreRequested()

    // Content-driven: the name wraps to as many as 3 lines, so rows are not a
    // uniform height. 18 = the layout's 9px top anchor margin + 9px below it.
    implicitHeight: rowContent.implicitHeight + 18
    color: selected ? Theme.bgSelected
                    : (hover.hovered ? Theme.bgHover : "transparent")

    HoverHandler {
        id: hover
        cursorShape: Qt.PointingHandCursor
    }
    TapHandler {
        // Bound to the row background only; the action buttons above it take
        // their own presses, so clicking Restore… never also re-pins the row.
        onTapped: root.activated()
    }

    // Selection spine. Also turns red on a failed row so a failure is visible
    // from the list's left edge without reading any text.
    Rectangle {
        id: spine
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 3
        radius: 1.5
        color: root.metaIsError ? Theme.err
             : (root.selected ? Theme.accent : "transparent")
    }

    ColumnLayout {
        id: rowContent
        // Anchored top-down and given an explicit height from its own implicit
        // height, NOT anchored to the row's bottom. The row's `implicitHeight`
        // is derived from this layout (the name wraps to 1-3 lines, so row
        // height varies), and anchoring the layout's bottom back to the row
        // would close that loop — Qt would report a binding loop and the last
        // wrapped line would clip.
        anchors.left: spine.right
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: 9
        anchors.rightMargin: 12
        anchors.topMargin: 9
        height: implicitHeight
        spacing: 5

        // ---- Line 1: the profile name, on its own full-width line ----
        //
        // The name is NOT elided and shares its line with nothing. Profile names
        // run to ~50 characters and differ in their *tails*
        // ("…-prod-eu-west-1" vs "…-prod-eu-west-2"), so eliding to fit a pill
        // and two buttons cut off precisely the part that tells two rows apart —
        // the list showed several visually identical entries. Wrapping instead
        // makes the row taller, which costs far less than an unusable list.
        Label {
            Layout.fillWidth: true
            text: root.name
            color: Theme.text
            font.pixelSize: 13
            font.weight: Font.DemiBold
            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
            // WrapAnywhere matters: these names are single unbroken tokens with
            // no spaces, so word-boundary wrapping alone would not break them.
            maximumLineCount: 3
            elide: Text.ElideRight
        }

        // ---- Line 2: status + actions, beneath the name ----
        // Everything here has a natural size and is laid out left-to-right, so
        // no item can be squeezed by the name any more — they no longer share a
        // line with it.
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            StatusPill {
                id: statusPill
                status: root.pillStatus
                label: root.pillLabel
                // The row's own meta line already carries the error text, so
                // suppress the pill's tooltip (a single space is StatusPill's
                // documented "no tooltip" value) rather than saying the same
                // thing twice on hover.
                tooltipText: " "
            }

            // Archive mtime + size, or the error text. Sits next to the pill
            // rather than on its own line: both are short, and pairing them
            // keeps the row to two lines in the common case.
            Label {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                visible: root.meta !== ""
                text: root.meta
                color: root.metaIsError ? Theme.errText : Theme.textFaint
                // Timestamps and sizes are tabular, so they get the mono face
                // and line up down the column; error prose does not.
                font: root.metaIsError ? Qt.font({ pixelSize: 11 })
                                       : Theme.monoFont(11)
                elide: Text.ElideRight
                ToolTip.visible: metaHover.hovered && truncated
                ToolTip.delay: 400
                ToolTip.text: root.meta
                HoverHandler { id: metaHover }
            }

            // Spacer for when the meta line is empty, so the buttons stay
            // right-aligned instead of hugging the pill.
            Item {
                Layout.fillWidth: true
                visible: root.meta === ""
            }

            // Hover/selection-revealed actions. Kept in the layout at all times
            // (only `opacity` changes) so revealing them never reflows the row
            // under the pointer.
            Row {
                id: actions
                spacing: 6
                Layout.alignment: Qt.AlignVCenter
                opacity: (hover.hovered || root.selected) ? 1 : 0
                // Non-interactive while hidden, so a click on an invisible
                // Restore… cannot land.
                enabled: opacity > 0

                Behavior on opacity {
                    NumberAnimation { duration: 90 }
                }

                Button {
                    text: root.backupLabel
                    enabled: root.canBackup
                    onClicked: root.backupRequested()
                }
                Button {
                    text: qsTr("Restore…")
                    enabled: root.canRestore
                    ToolTip.visible: hovered
                    ToolTip.delay: 500
                    ToolTip.timeout: 10000
                    ToolTip.text: qsTr("<b>Destructive.</b> Replaces every table of the target database.")
                    onClicked: root.restoreRequested()
                }
            }
        }

        // Inline progress for the running profile, so the list shows how far
        // along the run is without switching to the detail region.
        ProgressTrack {
            Layout.fillWidth: true
            visible: root.running
            indeterminate: root.progressIndeterminate
            from: 0
            to: 1
            value: root.progress
        }
    }

    // Row separator.
    Rectangle {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: 9
        height: 1
        color: Theme.divider
    }
}
