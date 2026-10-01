// SPDX-License-Identifier: Apache-2.0
//
// Kit panel (`k-panel` in the Lastrada design): a white, hairline-bordered
// surface with an optional 40 px header row (glyph, title, right-aligned meta
// text or actions) above the body.
//
// Layout note: Cards are placed inside ColumnLayouts, so they advertise
// `implicitHeight` from a nested `Column` that auto-sizes to its children.
// Children should use `width: parent.width` (or `anchors.left/right`) to span
// the Card's inner width — `ColumnLayout` attachments (`Layout.fillWidth`
// etc.) have no effect inside a `Column`.
//
// Usage:
//     Card { title: qsTr("Status"); glyph: "chart"; meta: "schema_migrations"
//            Label { … } }

import QtQuick
import QtQuick.Controls
import Lightweight.Migrations

Rectangle {
    id: root
    default property alias contentChildren: inner.data
    property real padding: 12
    property real spacing: 10

    /// Header title; the header row is shown only when this is non-empty.
    property string title: ""
    /// Glyph drawn before the title.
    property string glyph: ""
    /// Faint right-aligned header text (counts, source names).
    property string meta: ""
    /// Items placed at the right end of the header (e.g. an `LsButton`).
    property list<Item> headerActions

    readonly property bool _hasHeader: title !== ""

    color: Theme.clrCard
    border.color: Theme.clrContainerHighest
    radius: Theme.r3
    implicitHeight: (_hasHeader ? header.height : 0) + inner.implicitHeight + 2 * padding

    Item {
        id: header
        visible: root._hasHeader
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: root._hasHeader ? 40 : 0

        Row {
            anchors.left: parent.left
            anchors.leftMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8

            Glyph {
                anchors.verticalCenter: parent.verticalCenter
                visible: root.glyph !== ""
                name: root.glyph
                size: 15
                color: Theme.clrOnSurfaceSubtle
            }
            Label {
                anchors.verticalCenter: parent.verticalCenter
                text: root.title
                color: Theme.clrOnSurface
                font.pixelSize: Theme.sizeBody
                font.weight: Font.DemiBold
            }
        }

        Row {
            anchors.right: parent.right
            anchors.rightMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6

            Label {
                anchors.verticalCenter: parent.verticalCenter
                visible: root.meta !== ""
                text: root.meta
                color: Theme.clrOnSurfaceFaint
                font.pixelSize: Theme.sizeLabel
            }
            Row {
                anchors.verticalCenter: parent.verticalCenter
                spacing: 6
                children: root.headerActions
            }
        }

        Rectangle {
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: 1
            anchors.rightMargin: 1
            height: 1
            color: Theme.clrDivider
        }
    }

    Column {
        id: inner
        anchors.top: header.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: root.padding
        anchors.leftMargin: root._hasHeader ? 14 : root.padding
        anchors.rightMargin: root._hasHeader ? 14 : root.padding
        spacing: root.spacing
    }
}
