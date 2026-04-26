// SPDX-License-Identifier: Apache-2.0
//
// Rounded, bordered surface used as the container for every sidebar section
// and the Options group in the right pane.
//
// Layout note: Cards are placed inside ColumnLayouts, so they advertise
// `implicitHeight` from a nested `Column` that auto-sizes to its children.
// Children should use `width: parent.width` (or `anchors.left/right`) to span
// the Card's inner width — `ColumnLayout` attachments (`Layout.fillWidth`
// etc.) have no effect inside a `Column`.

import QtQuick
import Lightweight.Migrations

Rectangle {
    id: root
    default property alias contentChildren: inner.data
    property real padding: 12
    property real spacing: 8

    color: Theme.bgPanel
    border.color: Theme.border
    radius: 10
    implicitHeight: inner.implicitHeight + 2 * padding

    Column {
        id: inner
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: root.padding
        spacing: root.spacing
    }
}
