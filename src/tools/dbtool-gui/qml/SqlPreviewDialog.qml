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
//
// Styled as a Lastrada kit dialog; the SQL body is a dark code surface with
// the same syntax colours as the SQL query editor.

import QtQuick
import QtQuick.Controls
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

    // Kit dialog chrome (`dt-dlg`): white panel with a hairline border and a
    // soft drop shadow, a 16 px title row, the dark code body, and a light
    // footer bar carrying the actions.
    padding: 0
    topPadding: 0
    bottomPadding: 0

    // Kit scrim (`dt-scrim`): a light, cool dim rather than the style's
    // default near-black, so the page stays legible behind the dialog.
    Overlay.modal: Rectangle {
        color: Qt.rgba(21 / 255, 23 / 255, 28 / 255, 0.38)
    }

    background: Rectangle {
        color: Theme.clrCard
        radius: Theme.r3
        border.color: Theme.clrContainerHighest

        // Two-step shadow approximating `--shadowDlg` without an effect
        // pass: wide faint halo plus a tighter contact edge.
        Rectangle {
            z: -1
            anchors.fill: parent
            anchors.margins: -6
            anchors.topMargin: -2
            anchors.bottomMargin: -12
            radius: parent.radius + 6
            color: Theme.shadow
            opacity: 0.6
        }
        Rectangle {
            z: -1
            anchors.fill: parent
            anchors.margins: -1
            anchors.bottomMargin: -3
            radius: parent.radius + 1
            color: Theme.shadow
        }
    }

    header: Item {
        implicitHeight: headerColumn.implicitHeight + 34

        Glyph {
            id: headerGlyph
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.leftMargin: 20
            anchors.topMargin: 21
            name: "document"
            size: 18
            color: Theme.clrOnSurfaceSubtle
        }

        Column {
            id: headerColumn
            anchors.left: headerGlyph.right
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.leftMargin: 12
            anchors.rightMargin: 20
            anchors.topMargin: 18
            spacing: 4

            Label {
                width: parent.width
                text: root.title
                color: Theme.clrOnSurface
                font.pixelSize: Theme.sizeTitle
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
            Row {
                width: parent.width
                spacing: 8
                Label {
                    id: tsLabel
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.migrationTimestamp
                    color: Theme.clrOnSurfaceSubtle
                    font: Theme.monoFont(Theme.sizeMono)
                }
                Label {
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - tsLabel.width - parent.spacing
                    text: root.migrationTitle
                    color: Theme.clrOnSurfaceMed
                    font.pixelSize: Theme.sizeBodySm + 1
                    elide: Text.ElideRight
                }
            }
        }
    }

    contentItem: Item {
        Rectangle {
            id: sqlView
            anchors.fill: parent
            anchors.leftMargin: 20
            anchors.rightMargin: 20
            anchors.bottomMargin: 16
            color: Theme.clrSidebarBg
            radius: Theme.r2

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
                    font: Theme.monoFont(Theme.sizeMono)
                    color: Theme.clrCodeText
                    selectionColor: Qt.rgba(Theme.clrPrimary.r, Theme.clrPrimary.g, Theme.clrPrimary.b, 0.55)
                    selectedTextColor: "#ffffff"
                    background: null
                    leftPadding: 14
                    rightPadding: 14
                    topPadding: 10
                    bottomPadding: 10
                    SqlSyntaxHighlighter {
                        textDocument: sqlText.textDocument
                    }
                }
            }
        }
    }

    footer: Rectangle {
        implicitHeight: 52
        color: Theme.clrContainerLow
        // Only the bottom corners follow the dialog's radius.
        bottomLeftRadius: Theme.r3
        bottomRightRadius: Theme.r3

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 1
            color: Theme.clrContainerHighest
        }

        Label {
            anchors.left: parent.left
            anchors.leftMargin: 20
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("%1 characters").arg(sqlText.length)
            color: Theme.clrOnSurfaceSubtle
            font.pixelSize: Theme.sizeLabel
        }

        Row {
            anchors.right: parent.right
            // Trailing room for the resize grip anchored to the dialog's
            // bottom-right corner, so the grip never overlaps Close.
            anchors.rightMargin: 28
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8

            LsButton {
                text: qsTr("Copy all")
                variant: "secondary"
                size: "md"
                glyph: "copy"
                enabled: sqlText.length > 0
                onClicked: {
                    sqlText.selectAll();
                    sqlText.copy();
                    sqlText.deselect();
                }
            }
            LsButton {
                text: qsTr("Close")
                variant: "secondary"
                size: "md"
                onClicked: root.close()
            }
        }
    }

    // Resize grip: diagonal handle pinned to the dialog's bottom-right
    // corner. Reparented to the footer bar — the topmost item in that
    // corner — so it stays visible and clickable. The action row's trailing
    // margin reserves space so the grip never eats the Close button's hit
    // area.
    Item {
        id: resizeGrip
        width: 18
        height: 18
        parent: root.footer
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.rightMargin: 2
        anchors.bottomMargin: 2
        z: 10

        Canvas {
            anchors.fill: parent
            onPaint: {
                const ctx = getContext("2d");
                ctx.reset();
                ctx.strokeStyle = Theme.clrOnSurfaceFaint;
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
