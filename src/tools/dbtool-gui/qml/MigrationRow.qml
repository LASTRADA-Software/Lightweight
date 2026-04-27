// SPDX-License-Identifier: Apache-2.0
//
// Single row in the migration timeline: checkbox, timestamp, title, status
// pill. Uses explicit anchors instead of a RowLayout so the status pill is
// reliably glued to the right edge on every window width — the RowLayout
// pattern was intermittently leaving the pill off-screen on wide windows
// when the Label with `Layout.fillWidth` misallocated space.

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

    /// Fired when the row is double-clicked anywhere except the checkbox.
    /// The centre MigrationView catches this to open the SQL preview
    /// dialog — kept as a signal (rather than calling the dialog directly)
    /// so the row stays a reusable presentation component.
    signal doubleClicked(string timestamp, string title)

    height: 40
    width: ListView.view ? ListView.view.width : implicitWidth
    color: hoverArea.containsMouse ? Theme.bgHover : "transparent"

    MouseArea {
        id: hoverArea
        anchors.fill: parent
        hoverEnabled: true
        // The CheckBox sits on top in z-order, so its clicks do not reach
        // here — double-clicking the checkbox toggles it twice, not the
        // preview, which is the expected UX.
        onDoubleClicked: root.doubleClicked(root.timestamp, root.title)
    }

    // Bottom divider matching the mockup's hairline separator between rows.
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Theme.divider
    }

    CheckBox {
        id: selectBox
        anchors.left: parent.left
        anchors.leftMargin: 10
        anchors.verticalCenter: parent.verticalCenter
        enabled: root.status === "pending"
        checked: root.selected
        ToolTip.visible: hovered
        ToolTip.delay: 500
        ToolTip.timeout: 10000
        ToolTip.text: root.status === "pending"
            ? qsTr("Tick to include this pending migration in the next <b>Apply</b> or <b>Dry-run</b>. As soon as at least one row is ticked the target picker on the right is bypassed — only the ticked rows run. Double-click the row (not the checkbox) to preview its SQL.")
            : qsTr("Only pending migrations can be selected for running. This row is already applied (or in an error state) so the checkbox is disabled to prevent accidentally re-running committed work.")
        // Two-way sync: the CheckBox writes through to the model via the
        // controller, and the binding above pulls the model's current value
        // back into `checked` when the selection is cleared elsewhere (bulk
        // controls, refresh).
        onToggled: AppController.setMigrationSelected(root.timestamp, checked)
    }

    Label {
        id: timestampLabel
        anchors.left: selectBox.right
        anchors.leftMargin: 4
        anchors.verticalCenter: parent.verticalCenter
        text: root.timestamp
        font.family: Theme.monoFont
        color: Theme.textMuted
        font.pixelSize: 12
        width: 130
        elide: Text.ElideLeft
    }

    StatusPill {
        id: pill
        anchors.right: parent.right
        anchors.rightMargin: 14
        anchors.verticalCenter: parent.verticalCenter
        status: root.checksumMismatch && root.status === "applied"
            ? "checksum-mismatch"
            : root.status
    }

    Label {
        anchors.left: timestampLabel.right
        anchors.leftMargin: 10
        anchors.right: pill.left
        anchors.rightMargin: 10
        anchors.verticalCenter: parent.verticalCenter
        text: root.title
        color: Theme.text
        elide: Text.ElideRight
    }
}
