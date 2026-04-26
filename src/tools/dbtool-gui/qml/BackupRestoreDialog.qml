// SPDX-License-Identifier: Apache-2.0
//
// Simple modal dialog for backup / restore. Exposes a file path field and
// two buttons. A "real" version would use QtQuick.Dialogs.FileDialog, but
// keeping this plain-text lets tests drive the field programmatically.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Lightweight.Migrations

Dialog {
    id: root
    title: qsTr("Backup / Restore")
    modal: true
    standardButtons: Dialog.Close
    width: 420

    property alias path: pathField.text

    contentItem: ColumnLayout {
        spacing: 8

        Label {
            text: qsTr("Path to .zip backup file:")
            color: "#5b6372"
        }

        TextField {
            id: pathField
            Layout.fillWidth: true
            placeholderText: qsTr("/path/to/backup.zip")
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            Button {
                text: qsTr("Backup")
                Layout.fillWidth: true
                enabled: pathField.text.length > 0
                         && AppController.backupRunner.phase === BackupRunner.Idle
                ToolTip.visible: hovered
                ToolTip.delay: 500
                ToolTip.timeout: 10000
                ToolTip.text: qsTr("Write a full snapshot of the connected database — schema, data, and <b>schema_migrations</b> bookkeeping — to the .zip file above. Non-destructive: does not touch the live database. Use before risky migrations; keep the file until you have verified the new schema works.")
                onClicked: AppController.backupRunner.runBackup(pathField.text)
            }
            Button {
                text: qsTr("Restore")
                Layout.fillWidth: true
                enabled: pathField.text.length > 0
                         && AppController.backupRunner.phase === BackupRunner.Idle
                ToolTip.visible: hovered
                ToolTip.delay: 500
                ToolTip.timeout: 10000
                ToolTip.text: qsTr("<b>Destructive.</b> Drops the current schema in the connected database and replaces it with the contents of the .zip above. All data written since that backup is lost. Use only to recover from a failed migration or to reproduce a colleague's database state locally.")
                onClicked: AppController.backupRunner.runRestore(pathField.text)
            }
        }
    }
}
