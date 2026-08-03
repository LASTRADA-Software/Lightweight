// SPDX-License-Identifier: Apache-2.0
//
// The red "this destroys data" banner shown at the top of every restore
// confirmation dialog. Factored out so the managed per-profile restore and the
// custom-archive restore cannot drift apart in wording or prominence — both are
// equally destructive, so both must warn identically.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Lightweight.Migrations

Rectangle {
    id: root

    /// Warning text. Rich text; defaults to the restore wording.
    property string text: qsTr("<b>Destructive.</b> Tables in the target database are dropped and recreated from the archive. This cannot be undone.")

    implicitHeight: warnRow.implicitHeight + 18
    color: Theme.errSoft
    radius: Theme.radiusSmall

    RowLayout {
        id: warnRow
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: 11
        anchors.rightMargin: 11
        spacing: 9

        // Drawn glyph rather than the ⚠ emoji: the emoji ignores `color`, so it
        // rendered as a yellow-and-black sticker instead of picking up
        // `Theme.errText` — visually contradicting the red banner it sits in.
        Glyph {
            name: "warning"
            size: 13
            color: Theme.errText
            knockout: Theme.errSoft
            Layout.alignment: Qt.AlignTop
            Layout.topMargin: 1
        }
        Label {
            Layout.fillWidth: true
            text: root.text
            color: Theme.errText
            font.pixelSize: 12
            wrapMode: Text.WordWrap
            textFormat: Text.RichText
        }
    }
}
