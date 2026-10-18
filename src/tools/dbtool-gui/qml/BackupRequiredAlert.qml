// SPDX-License-Identifier: Apache-2.0
//
// Shown next to the migrate buttons while a release or a pre-migrate step requires a backup that
// does not exist yet: names what requires it, says a backup is required to run the migrations, and
// offers "Back up now". The migration lock does not stop the application's own writes, so it also
// asks to stop the application during the upgrade.
//
// Usage:
//     BackupRequiredAlert { reasons: AppController.backupRequirementReasons; onBackupRequested: AppController.backUpNow() }

import QtQuick
import QtQuick.Controls
import Lightweight.Migrations

Banner {
    id: root

    /// What requires the backup, e.g. ["release 3.0.0", "step \"Widen text columns\""].
    property var reasons: []
    /// Extra line: why the newest backup does not count, or why the last one could not be recorded.
    property string note: ""
    /// Disables "Back up now" and the checkbox while another run is in flight.
    property bool busy: false
    /// Whether "Migrate without a backup" is ticked (bound to `AppController.migrateWithoutBackup`).
    property bool waived: false

    /// The user asked for a backup.
    signal backupRequested()
    /// The user ticked or cleared "Migrate without a backup".
    signal waivedToggled(bool checked)

    /// The reasons joined with ", " and a final " and ".
    function joinedReasons() {
        const list = root.reasons || []
        if (list.length <= 1)
            return list.join("")
        return list.slice(0, -1).join(", ") + qsTr(" and ") + list[list.length - 1]
    }

    kind: "warn"
    title: qsTr("A backup is required to run the migrations")
    text: qsTr("%1 %2 a backup of this database taken after its last migration.")
              .arg(joinedReasons())
              .arg((root.reasons || []).length === 1 ? qsTr("requires") : qsTr("require"))
          + (root.note !== "" ? " " + root.note : "")
          + " " + qsTr("Stop every application that writes to this database until the migrations have finished: the migration lock does not stop its writes.")
    // Below the text rather than in `actions`: the Expert view's pane is too narrow for both side by side.
    controls: [
        LsButton {
            objectName: "backUpNowButton"
            text: qsTr("Back up now")
            glyph: "archive"
            enabled: !root.busy
            onClicked: root.backupRequested()
        },
        CheckBox {
            id: waiverCheckBox
            objectName: "migrateWithoutBackupCheckBox"
            // Bounded by the banner, wrapping its label rather than overflowing a narrow pane.
            width: parent ? Math.min(implicitWidth, parent.width) : implicitWidth
            text: qsTr("Migrate without a backup")
            contentItem: Label {
                leftPadding: waiverCheckBox.indicator.width + waiverCheckBox.spacing
                text: waiverCheckBox.text
                color: root.foreground
                opacity: waiverCheckBox.enabled ? 1.0 : 0.5
                wrapMode: Text.Wrap
                verticalAlignment: Text.AlignVCenter
            }
            checked: root.waived
            enabled: !root.busy
            ToolTip.visible: hovered
            ToolTip.delay: 500
            ToolTip.text: qsTr("Runs the migrations once without a backup. The run is recorded as waived; the next run needs a backup again.")
            onToggled: root.waivedToggled(checked)
        }
    ]
}
