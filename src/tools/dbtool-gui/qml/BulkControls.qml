// SPDX-License-Identifier: Apache-2.0
//
// Compact bulk-action control for the migration table toolbar: a ghost
// "more" icon button (`k-btn ghost icon`) that opens a menu holding the two
// paired actions ("Select all / Deselect all"). The toolbar already carries
// the filter tabs and the search field, so two always-visible text buttons
// no longer fit at the design's ~570 px centre width.
//
// The API is unchanged from the old two-segment control: `activeLeft` /
// `activeRight` now mark the menu entry that matches the current state (it is
// shown checked), and the tips become the entries' tooltips.

import QtQuick
import QtQuick.Controls
import Lightweight.Migrations

Item {
    id: root

    property string leftLabel: ""
    property string rightLabel: ""
    property string leftTip: ""
    property string rightTip: ""
    property bool activeLeft: false
    property bool activeRight: false

    signal leftClicked()
    signal rightClicked()

    implicitWidth: button.implicitWidth
    implicitHeight: button.implicitHeight

    LsButton {
        id: button
        variant: "ghost"
        size: "sm"
        iconOnly: true
        glyph: "more"
        text: root.leftLabel.length > 0 && root.rightLabel.length > 0
              ? qsTr("%1 / %2").arg(root.leftLabel).arg(root.rightLabel)
              : root.leftLabel + root.rightLabel
        onClicked: menu.opened ? menu.close() : menu.popup(button, 0, button.height + 4)
    }

    Menu {
        id: menu

        MenuItem {
            text: root.leftLabel
            checkable: true
            checked: root.activeLeft
            // Re-assert the binding after the click: the menu reflects state
            // owned by the controller, not a local toggle.
            onTriggered: { checked = Qt.binding(() => root.activeLeft); root.leftClicked() }
            ToolTip.visible: hovered && root.leftTip.length > 0
            ToolTip.text: root.leftTip
            ToolTip.delay: 500
        }
        MenuItem {
            text: root.rightLabel
            checkable: true
            checked: root.activeRight
            onTriggered: { checked = Qt.binding(() => root.activeRight); root.rightClicked() }
            ToolTip.visible: hovered && root.rightTip.length > 0
            ToolTip.text: root.rightTip
            ToolTip.delay: 500
        }
    }
}
