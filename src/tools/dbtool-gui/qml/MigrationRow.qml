// SPDX-License-Identifier: Apache-2.0
//
// Single 32 px row in the migration table (`dt-mr` in the Lastrada design):
// checkbox, mono timestamp, elided title and a right-aligned status pill,
// over a hairline divider. A ticked row takes the selection treatment — soft
// brand tint plus a 3 px brand bar on the left edge — so the rows the next
// Apply will act on stand out while scrolling.
//
// Uses explicit anchors instead of a RowLayout so the status pill is reliably
// glued to the right edge on every window width — the RowLayout pattern was
// intermittently leaving the pill off-screen on wide windows when the Label
// with `Layout.fillWidth` misallocated space. The column offsets are exposed
// as constants so `MigrationView`'s header strip lines up with them.

import QtQuick
import QtQuick.Controls
import Lightweight.Migrations

Rectangle {
    id: root

    property string timestamp
    property string title
    property string status
    property bool checksumMismatch: false
    property bool selected: false

    // Column geometry shared with the header strip in MigrationView.
    readonly property int leftInset: 10
    readonly property int rightInset: 12
    readonly property int checkColumn: 24
    readonly property int timestampColumn: 128
    readonly property int statusColumn: 96
    readonly property int gap: 8

    /// Fired when the row is double-clicked anywhere except the checkbox.
    /// The centre MigrationView catches this to open the SQL preview
    /// dialog — kept as a signal (rather than calling the dialog directly)
    /// so the row stays a reusable presentation component.
    signal doubleClicked(string timestamp, string title)

    readonly property bool _done: status === "applied" || status === "checksum-mismatch"

    height: Theme.row
    width: ListView.view ? ListView.view.width : implicitWidth
    color: selected ? Theme.clrPrimarySoft
         : hoverArea.containsMouse ? Theme.clrContainerLow : Theme.clrCard

    MouseArea {
        id: hoverArea
        anchors.fill: parent
        hoverEnabled: true
        // The CheckBox sits on top in z-order, so its clicks do not reach
        // here — double-clicking the checkbox toggles it twice, not the
        // preview, which is the expected UX.
        onDoubleClicked: root.doubleClicked(root.timestamp, root.title)
    }

    // Selection bar on the left edge.
    Rectangle {
        visible: root.selected
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 3
        color: Theme.clrPrimary
    }

    // Bottom hairline divider between rows.
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Theme.clrDivider
    }

    CheckBox {
        id: selectBox
        anchors.left: parent.left
        anchors.leftMargin: root.leftInset
        anchors.verticalCenter: parent.verticalCenter
        width: root.checkColumn
        height: root.checkColumn
        padding: 0
        enabled: root.status === "pending"
        checked: root.selected
        ToolTip.visible: hovered
        ToolTip.delay: 500
        ToolTip.timeout: 10000
        ToolTip.text: root.status === "pending"
            ? qsTr("Include in next Apply or Dry-run. Double-click row to preview SQL.")
            : qsTr("Only pending migrations can be selected.")
        // Two-way sync: the CheckBox writes through to the model via the
        // controller, and the binding above pulls the model's current value
        // back into `checked` when the selection is cleared elsewhere (bulk
        // controls, refresh).
        onToggled: AppController.setMigrationSelected(root.timestamp, checked)

        // Kit row checkbox (`dt-cb`): 15 px, brand fill with a white tick
        // when checked; rows that cannot be ticked fade instead of vanishing
        // so the column stays aligned.
        indicator: Rectangle {
            x: 0
            anchors.verticalCenter: parent.verticalCenter
            width: 15
            height: 15
            radius: 3
            opacity: selectBox.enabled ? 1.0 : 0.35
            color: selectBox.checked ? Theme.clrPrimary : Theme.clrCard
            border.color: selectBox.checked ? Theme.clrPrimary
                        : selectBox.hovered ? Theme.clrOnSurfaceSubtle : Theme.clrBorderStrong

            Glyph {
                anchors.centerIn: parent
                visible: selectBox.checked
                name: "check"
                size: 11
                color: "#ffffff"
            }

            Rectangle {
                anchors.fill: parent
                anchors.margins: -3
                radius: 6
                color: "transparent"
                border.width: 3
                border.color: Theme.clrFocusRing
                visible: selectBox.visualFocus
            }
        }
        contentItem: Item {}
    }

    Label {
        id: timestampLabel
        anchors.left: selectBox.right
        anchors.leftMargin: root.gap
        anchors.verticalCenter: parent.verticalCenter
        width: root.timestampColumn
        text: root.timestamp
        font: Theme.monoFont(Theme.sizeMono)
        color: Theme.clrOnSurfaceSubtle
        elide: Text.ElideLeft
    }

    Item {
        id: statusCell
        anchors.right: parent.right
        anchors.rightMargin: root.rightInset
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: Math.max(root.statusColumn, pill.implicitWidth)

        StatusPill {
            id: pill
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            status: root.checksumMismatch && root.status === "applied"
                ? "checksum-mismatch"
                : root.status
        }
    }

    Label {
        anchors.left: timestampLabel.right
        anchors.leftMargin: root.gap
        anchors.right: statusCell.left
        anchors.rightMargin: root.gap
        anchors.verticalCenter: parent.verticalCenter
        text: root.title
        color: root._done ? Theme.clrOnSurfaceMed : Theme.clrOnSurface
        font.pixelSize: Theme.sizeBodySm + 1
        elide: Text.ElideRight
    }
}
