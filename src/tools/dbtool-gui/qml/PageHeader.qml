// SPDX-License-Identifier: Apache-2.0
//
// Full-width page header: an eyebrow / title / context stack on the left and
// caller-supplied action buttons on the right, over a `bgWindow` band that
// separates the page from the toolbar above it.
//
// The Backups page previously opened with a bare 22px label and two buttons on
// one row, then spent an entire `Card` below the fold restating the backup
// folder. That put a static configuration value — one the user changes rarely
// but needs to *see* constantly to trust what "Back up all" will overwrite —
// into the same visual weight as the live per-profile list. Folding the path
// into the header's context line reclaims the card's vertical space for the
// profile list while keeping the path permanently visible.
//
// Usage:
//     PageHeader {
//         eyebrow: qsTr("Managed archives")
//         title: qsTr("Backups")
//         contextItems: [ … ]        // any Items; laid out in a row
//         actions: [ Button { … } ]  // right-aligned
//     }

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Lightweight.Migrations

Rectangle {
    id: root

    /// Small uppercase label above the title. Names the section the page
    /// belongs to; omit for a page that needs no such grouping.
    property string eyebrow: ""

    /// The page title.
    property string title: ""

    /// Items placed in the context row under the title (path labels, counts,
    /// links). Declared as an item list so callers can mix Labels, Glyphs, and
    /// clickable links without this component knowing their shapes.
    property list<Item> contextItems

    /// Right-aligned action items, typically Buttons. The primary action
    /// should come first so it sits nearest the title.
    property list<Item> actions

    color: Theme.bgWindow
    implicitHeight: headerRow.implicitHeight + 32

    // Hairline separating the header band from the page body below.
    Rectangle {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: 1
        color: Theme.border
    }

    RowLayout {
        id: headerRow
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: 20
        anchors.rightMargin: 20
        spacing: 24

        ColumnLayout {
            Layout.fillWidth: true
            // Bottom-align the text stack with the action row so the title's
            // baseline and the buttons line up on the same optical row.
            Layout.alignment: Qt.AlignBottom
            spacing: 3

            Label {
                visible: root.eyebrow !== ""
                text: root.eyebrow
                color: Theme.textFaint
                font.pixelSize: 10
                font.weight: Font.DemiBold
                font.letterSpacing: 0.9
                font.capitalization: Font.AllUppercase
            }
            Label {
                text: root.title
                color: Theme.text
                font.pixelSize: 21
                font.weight: Font.DemiBold
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            // Context row. `Flow` rather than `RowLayout` so a long path plus
            // several counts wrap instead of forcing the window wider.
            Flow {
                Layout.fillWidth: true
                visible: root.contextItems.length > 0
                spacing: 7
                children: root.contextItems
            }
        }

        Row {
            Layout.alignment: Qt.AlignBottom
            spacing: 8
            children: root.actions
        }
    }
}
