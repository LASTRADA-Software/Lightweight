// SPDX-License-Identifier: Apache-2.0
//
// Live connection chip shown next to a page title (`dt-conn` in the design):
// a green halo dot plus "Connected · <profile>", or a grey dot and
// "Not connected". Lets the user confirm *which* database they are about to
// change without looking at the Connection panel.

import QtQuick
import QtQuick.Controls
import Lightweight.Migrations

Rectangle {
    id: root

    implicitHeight: 26
    implicitWidth: row.implicitWidth + 20
    radius: Theme.rPill
    color: Theme.clrContainerLow
    border.color: Theme.clrContainerHighest

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 7

        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: 14; height: 14; radius: 7
            color: AppController.connected ? Qt.rgba(23 / 255, 163 / 255, 74 / 255, 0.18) : "transparent"
            Rectangle {
                anchors.centerIn: parent
                width: 8; height: 8; radius: 4
                color: AppController.connected ? Theme.clrSuccessDot : Theme.clrOnSurfaceFaint
            }
        }
        Label {
            anchors.verticalCenter: parent.verticalCenter
            text: AppController.connected ? qsTr("Connected") : qsTr("Not connected")
            color: Theme.clrOnSurface
            font.pixelSize: Theme.sizeBodySm
            font.weight: Font.DemiBold
        }
        Label {
            anchors.verticalCenter: parent.verticalCenter
            visible: AppController.connected && AppController.currentProfile !== ""
            text: AppController.currentProfile
            color: Theme.clrOnSurfaceMed
            font.pixelSize: Theme.sizeBodySm
        }
    }
}
