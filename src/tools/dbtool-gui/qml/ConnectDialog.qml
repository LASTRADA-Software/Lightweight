// SPDX-License-Identifier: Apache-2.0
//
// "Connect to a database" dialog (design: Backups turn 2, option 2c): the
// same Profile | DSN | Custom form the Migrations page shows, in the kit dialog
// frame, so a page that needs a connection can offer one without sending the
// user elsewhere. It drives `AppController`, so the connection it opens is the
// one the Migrations page uses and every connection chip updates with it.
//
// Nothing destructive happens here, so the icon is brand-tinted rather than
// the red of the restore confirmations. The dialog closes itself once a
// connection is up.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Lightweight.Migrations

Dialog {
    id: root

    /// One line under the title saying what the connection will be used for.
    property string purpose: ""

    modal: true
    anchors.centerIn: parent
    width: Math.max(360, Math.min(560, (parent ? parent.width : 560) - 48))
    padding: 0
    header: null

    background: Rectangle {
        color: Theme.clrCard
        radius: Theme.r3
        border.color: Theme.clrContainerHighest
    }

    // Close as soon as a connection comes up while the dialog is showing.
    Connections {
        target: AppController
        function onConnectedChanged() {
            if (AppController.connected && root.visible)
                root.close()
        }
    }

    contentItem: ColumnLayout {
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 20
            Layout.bottomMargin: 14
            spacing: 14

            Rectangle {
                Layout.alignment: Qt.AlignTop
                Layout.preferredWidth: 36
                Layout.preferredHeight: 36
                radius: 18
                color: Theme.clrPrimarySoft

                Glyph {
                    anchors.centerIn: parent
                    name: "database"
                    size: 18
                    color: Theme.clrPrimary
                    knockout: parent.color
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4

                Label {
                    Layout.fillWidth: true
                    text: qsTr("Connect to a database")
                    color: Theme.clrOnSurface
                    font.pixelSize: 16
                    font.weight: Font.DemiBold
                }
                Label {
                    Layout.fillWidth: true
                    visible: root.purpose !== ""
                    text: root.purpose
                    color: Theme.clrOnSurfaceMed
                    font.pixelSize: Theme.sizeBodySm + 1
                    wrapMode: Text.Wrap
                }
            }
        }

        ConnectionPanel {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            Layout.bottomMargin: 18
        }
    }

    footer: Rectangle {
        implicitHeight: footerRow.implicitHeight + 24
        color: Theme.clrContainerLow
        radius: Theme.r3

        // Square off the top corners; only the bottom two follow the dialog outline.
        Rectangle {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: Theme.r3
            color: Theme.clrContainerLow
        }
        Rectangle {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: 1
            color: Theme.clrContainerHighest
        }

        RowLayout {
            id: footerRow
            anchors.right: parent.right
            anchors.rightMargin: 20
            anchors.verticalCenter: parent.verticalCenter

            LsButton {
                objectName: "connectDialogClose"
                variant: "secondary"
                size: "md"
                text: AppController.connected ? qsTr("Done") : qsTr("Cancel")
                onClicked: root.close()
            }
        }
    }
}
