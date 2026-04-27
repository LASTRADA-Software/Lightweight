// SPDX-License-Identifier: Apache-2.0
//
// Modal dialog that previews the SQL a migration would execute when applied
// against the currently-connected database. Opened from MigrationView when
// the user double-clicks a row.
//
// The dialog is drawn inside the main window (not a top-level OS window) so
// it composites with the app theme and cannot drift to another monitor. A
// manual resize grip in the bottom-right corner lets the user enlarge the
// preview when a migration emits a long script — the default QML `Dialog`
// is otherwise non-resizable.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Lightweight.Migrations

Dialog {
    id: root
    title: qsTr("Migration SQL preview")
    modal: true

    // Opening size — capped relative to the parent window so the dialog
    // always starts fully visible. The user can then drag the resize grip
    // to grow or shrink it within the parent's bounds.
    width: parent ? Math.min(parent.width - 80, 900) : 800
    height: parent ? Math.min(parent.height - 80, 640) : 600

    // Bounds enforced by the resize grip so the dialog stays usable and
    // never exceeds the parent window.
    readonly property int minContentWidth: 420
    readonly property int minContentHeight: 280

    property string migrationTimestamp: ""
    property string migrationTitle: ""

    /// Opens the dialog for a given migration. Resolves the SQL statements
    /// via `AppController.previewMigrationSql()` and joins them with `;\n\n`
    /// separators — the canonical separator between statements in a DDL
    /// script. Some formatters emit statements with a trailing `;` already;
    /// strip any trailing whitespace and `;` per statement before adding our
    /// own terminator so the preview never shows `;;`.
    function showFor(timestamp, title) {
        migrationTimestamp = timestamp;
        migrationTitle = title;
        const statements = AppController.previewMigrationSql(timestamp);
        if (statements.length === 0) {
            sqlText.text = qsTr("-- No SQL statements emitted for this migration.\n")
                         + qsTr("-- (Connect to a database first if you expected output.)");
        } else {
            const trimmed = statements.map(s => s.replace(/[\s;]+$/, ""));
            sqlText.text = trimmed.join(";\n\n") + ";\n";
        }
        open();
    }

    contentItem: ColumnLayout {
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            Label {
                text: qsTr("Timestamp:")
                color: Theme.textFaint
                font.pixelSize: 12
            }
            Label {
                text: root.migrationTimestamp
                color: Theme.text
                font.family: Theme.monoFont
                font.pixelSize: 12
            }
            Item { Layout.fillWidth: true }
        }

        Label {
            Layout.fillWidth: true
            text: root.migrationTitle
            color: Theme.text
            font.pixelSize: 14
            font.weight: Font.DemiBold
            wrapMode: Text.WordWrap
            elide: Text.ElideRight
        }

        Rectangle {
            id: sqlView
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.bgTerminal
            border.color: Theme.border
            radius: 6

            ScrollView {
                anchors.fill: parent
                anchors.margins: 1
                clip: true

                TextArea {
                    id: sqlText
                    readOnly: true
                    selectByMouse: true
                    persistentSelection: true
                    wrapMode: TextEdit.NoWrap
                    textFormat: TextEdit.PlainText
                    font.family: Theme.monoFont
                    font.pixelSize: 12
                    color: "#cbd5e1"
                    background: null
                    leftPadding: 10
                    rightPadding: 10
                    topPadding: 8
                    bottomPadding: 8
                }
            }

        }

        RowLayout {
            Layout.fillWidth: true
            Label {
                text: qsTr("%1 characters").arg(sqlText.length)
                color: Theme.textFaint
                font.pixelSize: 11
            }
            Item { Layout.fillWidth: true }
            Button {
                text: qsTr("Copy All")
                flat: true
                enabled: sqlText.length > 0
                onClicked: {
                    sqlText.selectAll();
                    sqlText.copy();
                    sqlText.deselect();
                }
            }
            Button {
                text: qsTr("Close")
                flat: true
                // Trailing spacer reserves room for the resize grip anchored
                // to the dialog's bottom-right corner, so the grip never
                // overlaps this button's hit area.
                Layout.rightMargin: 20
                onClicked: root.close()
            }
        }
    }

    // Resize grip: diagonal handle pinned to the dialog's bottom-right
    // corner. Reparented to the dialog background so it overlays the
    // entire dialog, not the content margin. The Close button's trailing
    // margin reserves space so the grip never eats its hit area.
    Item {
        id: resizeGrip
        width: 18
        height: 18
        parent: root.background
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        z: 10

        Canvas {
            anchors.fill: parent
            onPaint: {
                const ctx = getContext("2d");
                ctx.reset();
                ctx.strokeStyle = Theme.textFaint;
                ctx.lineWidth = 1;
                for (let i = 0; i < 3; ++i) {
                    const o = 4 + i * 4;
                    ctx.beginPath();
                    ctx.moveTo(width - 2, height - o);
                    ctx.lineTo(width - o, height - 2);
                    ctx.stroke();
                }
            }
        }

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.SizeFDiagCursor
            property real pressX: 0
            property real pressY: 0
            property int startWidth: 0
            property int startHeight: 0
            onPressed: (mouse) => {
                const p = mapToItem(null, mouse.x, mouse.y);
                pressX = p.x;
                pressY = p.y;
                startWidth = root.width;
                startHeight = root.height;
            }
            onPositionChanged: (mouse) => {
                if (!pressed) return;
                const p = mapToItem(null, mouse.x, mouse.y);
                const maxW = root.parent ? root.parent.width - 20 : startWidth;
                const maxH = root.parent ? root.parent.height - 20 : startHeight;
                root.width = Math.max(root.minContentWidth,
                                      Math.min(maxW, startWidth + (p.x - pressX)));
                root.height = Math.max(root.minContentHeight,
                                       Math.min(maxH, startHeight + (p.y - pressY)));
            }
        }
    }
}
