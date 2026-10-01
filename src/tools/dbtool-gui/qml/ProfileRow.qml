// SPDX-License-Identifier: Apache-2.0
//
// One row of the Backups page profile table (`dt-br` in the Lastrada design):
//
//   PROFILE                         STATUS      LAST BACKUP      SIZE   actions
//   [PG] staging  (current)         (Running)   Table 3 of 12 ·  —      Back up  Restore…
//        Driver={PostgreSQL…}                   ▬▬▬▬───────
//
// A failed row gets the kit's error row — a warm tint and a 3 px error bar at
// the left edge — with the reason inline in the LAST BACKUP cell, so nobody
// has to open the log to find out why. The row the detail region is showing
// gets the kit's selection treatment (brand-soft tint + brand bar).
//
// Below `compact` width (the page's single-column layout on a narrow window)
// the five fixed columns would leave the profile name a few pixels wide, so the
// row folds into a card: the name on its own line, then status, last backup
// and the actions on the line beneath it.
//
// Two rules carried over from the earlier list design, both learned the hard
// way:
//
//  * The profile name is WRAPPED, never elided to fit. Names run to ~50
//    characters and differ in their tails ("…-eu-west-1" vs "…-eu-west-2"), so
//    eliding cut off precisely the part that tells two rows apart.
//  * Every cell has a fixed or natural width. StatusPill is a plain Rectangle
//    carrying only `implicitWidth`, and a layout shrinks any child below that
//    unless told not to — the fillWidth name once squeezed the pill to zero.
//
// The column widths are mirrored by the table header in BackupsPage.qml; keep
// the two in step.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Lightweight.Migrations

Rectangle {
    id: root

    /// Profile name, shown as the row's primary label.
    property string name: ""
    /// Raw ODBC connection string, shown faint under the name and used to
    /// derive the DBMS badge. Empty for DSN profiles (nothing to show).
    property string connectionString: ""
    /// Two-letter DBMS badge ("MS" / "PG" / "SQ"). Derived from
    /// `connectionString` by default; empty hides the badge.
    property string dbmsBadge: _deriveDbms(connectionString)
    /// True for the profile the app is currently connected through — shown as
    /// a "current" brand tag after the name.
    property bool current: false
    /// Palette key for the status pill (see StatusPill).
    property string pillStatus: "empty"
    /// Caption shown inside the status pill.
    property string pillLabel: ""
    /// LAST BACKUP cell: archive time, an error message, or a progress caption.
    property string meta: ""
    /// Small subtle line under `meta` (table count, date).
    property string metaSub: ""
    /// When true, `meta` is an error string — rendered in the error colour and
    /// the row switches to the kit's error treatment.
    property bool metaIsError: false
    /// When true, `meta` is secondary information ("Never", "Waiting…") and
    /// is drawn in the subtle colour rather than as a value.
    property bool metaMuted: false
    /// SIZE cell text (e.g. "47.7 MB"); empty renders an em-dash.
    property string sizeText: ""
    /// True while this row is the detail region's subject.
    property bool selected: false
    /// True while this profile's backup/restore is running — shows the inline
    /// progress bar under the caption.
    property bool running: false
    /// Progress fraction [0, 1] for the inline bar.
    property real progress: 0
    /// Whether the inline bar should sweep instead of fill (unknown total).
    property bool progressIndeterminate: false
    /// Enables the Backup action.
    property bool canBackup: false
    /// Enables the Restore action.
    property bool canRestore: false
    /// Label for the left action — "Back up" normally, "Retry" after a failure.
    property string backupLabel: qsTr("Back up")
    /// Card layout for narrow widths (see the header comment).
    property bool compact: width < 760

    /// Fixed column widths of the wide layout: status, last backup, size,
    /// actions. The PROFILE column takes the rest.
    readonly property var columnWidths: [124, 150, 70, 176]

    /// Emitted when the row body is clicked (pin / unpin the detail region).
    signal activated()
    /// Emitted by the Backup (or Retry) action.
    signal backupRequested()
    /// Emitted by the Restore action.
    signal restoreRequested()

    /// Best-effort DBMS badge from an ODBC connection string. Only the driver
    /// name is consulted; anything unrecognised (or a DSN) yields "" so the
    /// badge is omitted rather than guessed.
    /// @param cs Connection string.
    /// @return "MS", "PG", "SQ" or "".
    function _deriveDbms(cs) {
        const s = (cs || "").toLowerCase()
        if (s.indexOf("sql server") >= 0 || s.indexOf("sqlncli") >= 0 || s.indexOf("msodbcsql") >= 0)
            return "MS"
        if (s.indexOf("postgres") >= 0 || s.indexOf("psqlodbc") >= 0)
            return "PG"
        if (s.indexOf("sqlite") >= 0)
            return "SQ"
        return ""
    }

    // In card mode the SIZE column folds into the sub-line.
    readonly property string _subText: compact && sizeText !== ""
                                       ? [metaSub, sizeText].filter(s => s !== "").join(" · ")
                                       : metaSub

    implicitHeight: Math.max(52, grid.implicitHeight + 12)
    color: metaIsError ? Theme.clrErrorRowBg
         : selected ? Theme.clrPrimarySoft
         : (hover.hovered ? Theme.clrContainerLow : Theme.clrCard)

    HoverHandler {
        id: hover
        cursorShape: Qt.PointingHandCursor
    }
    TapHandler {
        // Bound to the row background only; the action buttons above it take
        // their own presses, so clicking Restore… never also re-pins the row.
        onTapped: root.activated()
    }

    // Left-edge bar: error red on a failed row (wins over selection — the
    // failure is the more urgent fact), brand red on the selected one.
    Rectangle {
        id: edgeBar
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 3
        color: root.metaIsError ? Theme.clrError
             : (root.selected ? Theme.clrPrimary : "transparent")
    }

    GridLayout {
        id: grid
        // Centred vertically with an explicit height from its own implicit
        // height, NOT anchored top and bottom: the row's height is derived from
        // this layout (the name wraps to 1-3 lines), and anchoring both edges
        // back to the row would close that loop.
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: 14
        anchors.rightMargin: 14
        height: implicitHeight
        columns: root.compact ? 3 : 5
        columnSpacing: 12
        rowSpacing: 8

        // ---- PROFILE: badge, name (+ current tag), connection string ----
        RowLayout {
            id: whoCell
            Layout.row: 0
            Layout.column: 0
            Layout.columnSpan: root.compact ? 3 : 1
            // Zero preferred width + fill: the PROFILE column takes exactly the
            // space the fixed columns leave, whatever the name length, so the
            // STATUS column starts at the same x on every row.
            Layout.preferredWidth: 0
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            spacing: 10

            Rectangle {
                visible: root.dbmsBadge !== ""
                Layout.alignment: Qt.AlignVCenter
                implicitWidth: 28
                implicitHeight: 22
                radius: Theme.r1
                color: Theme.clrContainerHigh

                Text {
                    anchors.centerIn: parent
                    text: root.dbmsBadge
                    color: Theme.clrOnSurfaceMed
                    font.family: Theme.fontFamily
                    font.pixelSize: 9
                    font.weight: Font.Bold
                    font.letterSpacing: 0.3
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                spacing: 2

                RowLayout {
                    id: nameLine
                    Layout.fillWidth: true
                    spacing: 6

                    Label {
                        id: nameLabel
                        // Fill, capped at the natural width: a short name keeps
                        // its width (so the tag sits right after it and the
                        // spacer takes the rest), a long one shrinks and wraps.
                        // Capping at `implicitWidth` rather than at a function
                        // of `nameLine.width` keeps the layout from feeding its
                        // own width back into itself (recursive rearrange).
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        Layout.maximumWidth: implicitWidth
                        text: root.name
                        color: Theme.clrOnSurface
                        font.pixelSize: Theme.sizeBody
                        font.weight: Font.DemiBold
                        // WrapAnywhere matters: names are single unbroken tokens
                        // with no spaces, so word-boundary wrapping alone would
                        // not break them.
                        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                        maximumLineCount: 3
                        elide: Text.ElideRight
                    }
                    Rectangle {
                        id: currentTag
                        visible: root.current
                        Layout.alignment: Qt.AlignTop
                        Layout.topMargin: 1
                        implicitWidth: currentText.implicitWidth + 12
                        implicitHeight: 16
                        radius: Theme.rPill
                        color: Theme.clrPrimarySoft
                        border.color: Theme.clrPrimarySoftBorder

                        Text {
                            id: currentText
                            anchors.centerIn: parent
                            text: qsTr("current")
                            color: Theme.clrPrimary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.sizeGroup
                            font.weight: Font.DemiBold
                        }
                    }
                    Item { Layout.fillWidth: true }
                }

                Label {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    visible: root.connectionString !== ""
                    text: root.connectionString
                    color: Theme.clrOnSurfaceFaint
                    font: Theme.monoFont(11)
                    elide: Text.ElideRight
                    ToolTip.visible: csHover.hovered && truncated
                    ToolTip.delay: 400
                    ToolTip.text: root.connectionString
                    HoverHandler { id: csHover }
                }
            }
        }

        // ---- STATUS ----
        // Wrapped in a fixed-width cell so the pill keeps its natural width
        // instead of being stretched to (or squeezed by) the column.
        Item {
            Layout.row: root.compact ? 1 : 0
            Layout.column: root.compact ? 0 : 1
            Layout.preferredWidth: root.compact ? statusPill.implicitWidth : root.columnWidths[0]
            Layout.minimumWidth: Layout.preferredWidth
            implicitHeight: statusPill.implicitHeight

            StatusPill {
                id: statusPill
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                width: implicitWidth
                status: root.pillStatus
                label: root.pillLabel
                // The LAST BACKUP cell already carries the error text, so
                // suppress the pill's tooltip (a single space is StatusPill's
                // documented "no tooltip" value) rather than saying the same
                // thing twice on hover.
                tooltipText: " "
            }
        }

        // ---- LAST BACKUP (or the failure reason, or run progress) ----
        ColumnLayout {
            Layout.row: root.compact ? 1 : 0
            Layout.column: root.compact ? 1 : 2
            Layout.fillWidth: root.compact
            Layout.preferredWidth: root.compact ? -1 : root.columnWidths[1]
            Layout.maximumWidth: root.compact ? Number.POSITIVE_INFINITY : root.columnWidths[1]
            Layout.minimumWidth: root.compact ? 0 : root.columnWidths[1]
            spacing: 4

            Label {
                Layout.fillWidth: true
                visible: root.meta !== ""
                text: root.meta
                color: root.metaIsError ? Theme.clrError
                     : (root.running || root.metaMuted ? Theme.clrOnSurfaceSubtle : Theme.clrOnSurface)
                font.pixelSize: Theme.sizeBodySm
                // Error prose may need two lines; everything else is one.
                wrapMode: root.metaIsError ? Text.Wrap : Text.NoWrap
                maximumLineCount: root.metaIsError ? 2 : 1
                elide: Text.ElideRight
                ToolTip.visible: metaHover.hovered && truncated
                ToolTip.delay: 400
                ToolTip.text: root.meta
                HoverHandler { id: metaHover }
            }
            Label {
                Layout.fillWidth: true
                visible: root._subText !== "" && !root.running && !root.metaIsError
                text: root._subText
                color: Theme.clrOnSurfaceSubtle
                font.pixelSize: Theme.sizeLabel
                elide: Text.ElideRight
            }
            // Thin brand bar for the running profile, so the table shows how
            // far along the run is without switching to the detail region.
            ProgressTrack {
                Layout.fillWidth: true
                visible: root.running
                implicitHeight: 5
                indeterminate: root.progressIndeterminate
                from: 0
                to: 1
                value: root.progress
                fillColor: Theme.clrPrimary
                trackColor: Theme.clrContainerHigh
            }
        }

        // ---- SIZE (folded into the sub-line in card mode) ----
        Label {
            visible: !root.compact
            Layout.row: 0
            Layout.column: root.compact ? 0 : 3
            Layout.preferredWidth: root.columnWidths[2]
            Layout.maximumWidth: root.columnWidths[2]
            Layout.minimumWidth: root.columnWidths[2]
            text: root.sizeText !== "" ? root.sizeText : "—"
            color: root.sizeText !== "" ? Theme.clrOnSurface : Theme.clrOnSurfaceSubtle
            font.pixelSize: Theme.sizeBodySm
            font.features: ({ "tnum": 1 })
            elide: Text.ElideRight
        }

        // ---- Actions ----
        // Always visible, per the kit table: Restore… is outlined in error red,
        // which is what tells it apart from the quiet Back up — the earlier
        // hover-reveal existed only because both looked alike.
        Item {
            Layout.row: root.compact ? 1 : 0
            Layout.column: root.compact ? 2 : 4
            Layout.preferredWidth: root.compact ? actions.implicitWidth : root.columnWidths[3]
            Layout.minimumWidth: root.compact ? actions.implicitWidth : Math.max(actions.implicitWidth, root.columnWidths[3])
            implicitHeight: actions.implicitHeight

            Row {
                id: actions
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: 6

                LsButton {
                    variant: "secondary"
                    size: "sm"
                    text: root.backupLabel
                    enabled: root.canBackup
                    onClicked: root.backupRequested()
                }
                LsButton {
                    variant: "danger"
                    size: "sm"
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
    }

    // Row divider.
    Rectangle {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: 1
        color: Theme.clrDivider
    }
}
