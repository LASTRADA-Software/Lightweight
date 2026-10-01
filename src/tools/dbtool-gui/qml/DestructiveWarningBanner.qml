// SPDX-License-Identifier: Apache-2.0
//
// The "this destroys data" header of every restore confirmation dialog (the
// `dt-dlg .hd` block of the Lastrada design): a 36 px round error-tinted icon
// beside the dialog's question and a sentence saying plainly what is lost.
//
// Factored out so the managed per-profile restore and the custom-archive
// restore cannot drift apart in wording or prominence — both are equally
// destructive, so both must warn identically. Only the question (`title`) and
// the named target (`subject`) differ between them.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Lightweight.Migrations

Item {
    id: root

    /// The dialog's question, e.g. "Restore prod from its backup?". Empty
    /// shows the warning sentence alone.
    property string title: ""

    /// Name of the database being overwritten, emphasised in the default
    /// wording. Empty falls back to "the target database".
    property string subject: ""

    /// Warning text. Rich text; defaults to the restore wording.
    property string text: subject !== ""
        ? qsTr("<b>Destructive.</b> Tables in <b>%1</b> are dropped and recreated from the archive. Anything written since the backup was taken is lost — this cannot be undone.")
              .arg(subject)
        : qsTr("<b>Destructive.</b> Tables in the target database are dropped and recreated from the archive. Anything written since the backup was taken is lost — this cannot be undone.")

    implicitHeight: row.implicitHeight
    implicitWidth: 320

    RowLayout {
        id: row
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: 14

        Rectangle {
            Layout.alignment: Qt.AlignTop
            implicitWidth: 36
            implicitHeight: 36
            radius: 18
            color: Theme.clrErrorBg

            // Drawn glyph rather than the ⚠ emoji: the emoji ignores `color`,
            // so it rendered as a yellow-and-black sticker instead of picking
            // up `Theme.clrError`.
            Glyph {
                anchors.centerIn: parent
                name: "alert"
                size: 18
                color: Theme.clrError
                knockout: Theme.clrErrorBg
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            spacing: 4

            // Wraps rather than elides: profile names run to ~50 characters,
            // and the name is the one thing the user must read correctly here.
            Label {
                Layout.fillWidth: true
                visible: root.title !== ""
                text: root.title
                color: Theme.clrOnSurface
                font.pixelSize: Theme.sizeTitle
                font.weight: Font.DemiBold
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
            }
            Label {
                Layout.fillWidth: true
                text: root.text
                color: Theme.clrOnSurfaceMed
                font.pixelSize: Theme.sizeBodySm
                lineHeight: 1.2
                wrapMode: Text.WordWrap
                textFormat: Text.RichText
            }
        }
    }
}
