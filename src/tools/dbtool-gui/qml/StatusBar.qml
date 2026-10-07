// SPDX-License-Identifier: Apache-2.0
//
// Kit status bar (`k-status`): a 26 px strip along the bottom of the content
// area with the connection state and migration counts on the left and the
// effective plugins directory on the right. Read-only; every value here is
// actionable elsewhere in the UI.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Lightweight.Migrations

Rectangle {
    id: root

    /// Show the plugins directory on the right (Expert view only — it is
    /// noise for the Simple-view user).
    property bool showPluginsDir: true

    implicitHeight: Theme.statusH
    color: Theme.clrContainerLow

    Rectangle {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 1
        color: Theme.clrContainerHighest
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 14
        anchors.rightMargin: 14
        spacing: 16

        Row {
            spacing: 7
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: 8; height: 8; radius: 4
                color: AppController.connected ? Theme.clrSuccessDot : Theme.clrOnSurfaceFaint
            }
            Label {
                anchors.verticalCenter: parent.verticalCenter
                text: AppController.connected ? qsTr("Connected") : qsTr("Not connected")
                color: Theme.clrOnSurfaceSubtle
                font.pixelSize: Theme.sizeLabel
            }
        }
        Label {
            // The open connection when there is one, else the selected profile.
            readonly property string target: AppController.connected ? AppController.connectedTarget
                                                                     : AppController.currentProfile
            visible: target !== ""
            text: target
            color: Theme.clrOnSurfaceSubtle
            font.pixelSize: Theme.sizeLabel
        }
        Label {
            visible: AppController.migrationCount > 0
            text: qsTr("%1 applied · %2 pending").arg(AppController.appliedCount).arg(AppController.pendingCount)
            color: Theme.clrOnSurfaceSubtle
            font.pixelSize: Theme.sizeLabel
        }
        Item { Layout.fillWidth: true }
        Label {
            visible: root.showPluginsDir && AppController.pluginsDir !== ""
            Layout.maximumWidth: root.width * 0.45
            text: qsTr("Plugins: ") + AppController.pluginsDir
            color: Theme.clrOnSurfaceSubtle
            font.pixelSize: Theme.sizeLabel
            elide: Text.ElideMiddle
        }
    }
}
