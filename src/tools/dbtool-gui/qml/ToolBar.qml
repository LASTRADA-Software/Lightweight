// SPDX-License-Identifier: Apache-2.0
//
// Toolbar along the top of the window: gradient logo, "Database Migrations /
// <profile>" heading, and right-aligned ghost buttons. The profile name
// elides so the toolbar survives narrow windows without pushing the action
// buttons off-screen.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Lightweight.Migrations

Rectangle {
    id: root
    height: 48
    color: Theme.bgWindow

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Theme.border
    }

    signal refreshClicked()
    signal backupRestoreClicked()
    signal settingsClicked()

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 16
        anchors.rightMargin: 16
        spacing: 10

        Rectangle {
            width: 28
            height: 28
            radius: 6
            gradient: Gradient {
                orientation: Gradient.Vertical
                GradientStop { position: 0.0; color: "#0a66d6" }
                GradientStop { position: 1.0; color: "#7c3aed" }
            }
            Text {
                anchors.centerIn: parent
                text: "L"
                color: "white"
                font.bold: true
                font.pixelSize: 14
            }
        }

        Label {
            text: qsTr("Database Migrations")
            color: Theme.text
            font.pixelSize: 15
            font.weight: Font.DemiBold
        }

        Label {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            text: qsTr("/ ") + (AppController.currentProfile || qsTr("(no profile)"))
            color: Theme.textFaint
            font.pixelSize: 14
            elide: Text.ElideRight
        }

        // View-mode toggle: a single button whose label names the mode the
        // user would switch *to*. Two tabs were confusing — the "active"
        // tab was indistinguishable from a toggleable option. One button
        // with "Switch to X" is unambiguous.
        //
        // The button is wide enough to fit the longer of the two possible
        // labels ("Switch to Expert view") so toggling never causes the
        // toolbar to reflow left/right.
        ToolButton {
            id: viewModeToggle
            Layout.alignment: Qt.AlignVCenter
            // Reserve width for the longer label. `fontMetrics` gives us
            // a font-accurate width that survives font-size / DPI changes
            // without guessing a magic pixel count.
            readonly property string expandLabel: qsTr("Switch to Expert view")
            readonly property string collapseLabel: qsTr("Switch to Simple view")
            Layout.minimumWidth: toggleMetrics.advanceWidth(
                expandLabel.length > collapseLabel.length ? expandLabel : collapseLabel) + 32

            text: AppController.viewMode === "expert" ? collapseLabel : expandLabel
            onClicked: AppController.setViewMode(
                AppController.viewMode === "expert" ? "simple" : "expert")

            ToolTip.visible: hovered
            ToolTip.delay: 500
            ToolTip.timeout: 10000
            ToolTip.text: AppController.viewMode === "expert"
                ? qsTr("Switch to the Simple view: a one-click Run button for end-users who just need their database up to date.")
                : qsTr("Switch to the Expert view: full migration timeline, per-migration selection, dry-run, rollback, log panel.")

            FontMetrics {
                id: toggleMetrics
                font: viewModeToggle.font
            }
        }

        ToolButton {
            text: qsTr("Refresh")
            ToolTip.visible: hovered
            ToolTip.delay: 500
            ToolTip.timeout: 10000
            ToolTip.text: qsTr("Re-query the connected database for applied migrations, re-scan the plugin directory for registered migrations, and rebuild the timeline, status card and release summary. Use this after an external migration run (dbtool, CI) changed the database behind the GUI's back.")
            onClicked: root.refreshClicked()
        }
        ToolButton {
            text: qsTr("Backup / Restore")
            ToolTip.visible: hovered
            ToolTip.delay: 500
            ToolTip.timeout: 10000
            ToolTip.text: qsTr("Open the snapshot dialog. <b>Backup</b> writes the current schema + data to a .zip file; <b>Restore</b> wipes the target database and reloads it from a previously written .zip. Intended for rollback safety nets — take a backup before risky migrations and keep the file until you have verified the new schema in production.")
            onClicked: root.backupRestoreClicked()
        }
        ToolButton {
            text: qsTr("Settings")
            ToolTip.visible: hovered
            ToolTip.delay: 500
            ToolTip.timeout: 10000
            ToolTip.text: qsTr("Application settings (placeholder). Will host GUI preferences such as profile-file path, keychain service name, log verbosity, and theme.")
            onClicked: root.settingsClicked()
        }
    }
}
