// SPDX-License-Identifier: Apache-2.0
//
// Summary card — total registered migrations + 2×2 grid of Applied / Pending
// / Unknown / Checksum-mismatch counts. Bindings read the notify-enabled
// counters on `AppController` so they refresh whenever the migration model
// is rebuilt (as opposed to reading `model.rowCount()` directly, which does
// not notify QML bindings on model resets).

import QtQuick
import QtQuick.Controls
import Lightweight.Migrations

Card {
    id: root
    padding: 10
    spacing: 10

    Row {
        width: parent.width
        Label {
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Registered migrations")
            color: Theme.textMuted
            font.pixelSize: 12
        }
        Item { width: parent.width - 200; height: 1 }
        Label {
            anchors.verticalCenter: parent.verticalCenter
            text: AppController.migrationCount
            color: Theme.text
            font.pixelSize: 15
            font.weight: Font.Bold
        }
    }

    Grid {
        id: grid
        width: parent.width
        columns: 2
        columnSpacing: 8
        rowSpacing: 8

        Repeater {
            model: [
                { label: qsTr("Applied"),    count: AppController.appliedCount,           colour: Theme.ok   },
                { label: qsTr("Pending"),    count: AppController.pendingCount,           colour: Theme.warn },
                { label: qsTr("Unknown"),    count: AppController.unknownCount,           colour: Theme.err  },
                { label: qsTr("Checksum ≠"), count: AppController.checksumMismatchCount,  colour: Theme.err  },
            ]
            Rectangle {
                required property var modelData
                width: (grid.width - grid.columnSpacing) / 2
                height: 56
                radius: 6
                color: Theme.bgSubtle
                border.color: Theme.border
                Column {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 2
                    Label {
                        text: modelData.count
                        font.pixelSize: 20
                        font.weight: Font.Bold
                        color: modelData.colour
                    }
                    Label {
                        text: modelData.label
                        color: Theme.textMuted
                        font.pixelSize: 11
                    }
                }
            }
        }
    }
}
