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
                enabled: AppController.backupRestoreEnabled
                         && pathField.text.length > 0
                         && AppController.backupRunner.phase === BackupRunner.Idle
                ToolTip.visible: hovered
                ToolTip.delay: 500
                ToolTip.timeout: 10000
                ToolTip.text: qsTr("Write a schema + data snapshot to the .zip above.")
                onClicked: AppController.backupRunner.runBackup(pathField.text)
            }
            Button {
                text: qsTr("Restore")
                Layout.fillWidth: true
                enabled: AppController.backupRestoreEnabled
                         && pathField.text.length > 0
                         && AppController.backupRunner.phase === BackupRunner.Idle
                ToolTip.visible: hovered
                ToolTip.delay: 500
                ToolTip.timeout: 10000
                ToolTip.text: qsTr("<b>Destructive.</b> Replace the current schema with the .zip contents.")
                onClicked: AppController.backupRunner.runRestore(pathField.text)
            }
        }
    }
}
