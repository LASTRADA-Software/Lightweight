// SPDX-License-Identifier: Apache-2.0
//
// Kit segmented control (`k-seg`): a grey track holding mutually exclusive
// options, the active one raised as a white chip. Used for the Simple | Expert
// view switch and the Profile | DSN | Custom connection mode — places where
// the *current* value must be readable at a glance, which tab-style toggles
// failed at.
//
// Usage:
//     SegmentedControl {
//         model: [ { value: "simple", label: qsTr("Simple") },
//                  { value: "expert", label: qsTr("Expert") } ]
//         current: AppController.viewMode
//         onActivated: (value) => AppController.setViewMode(value)
//     }

import QtQuick
import QtQuick.Controls
import Lightweight.Migrations

Rectangle {
    id: root

    /// Array of `{ value, label }` options, in display order.
    property var model: []
    /// The `value` of the active option.
    property string current: ""
    /// Stretch the options to fill the control's width (connection mode);
    /// otherwise each option sizes to its label.
    property bool fill: false
    /// "sm" (22 px chips) or "md" (24 px chips).
    property string size: "md"

    /// Emitted when the user picks an option other than the current one.
    signal activated(string value)

    readonly property int _chipH: size === "sm" ? 22 : 24

    color: Theme.clrContainer
    radius: Theme.r2
    implicitHeight: _chipH + 4
    implicitWidth: options.implicitWidth + 4

    Row {
        id: options
        anchors.fill: parent
        anchors.margins: 2
        spacing: 2

        Repeater {
            id: repeater
            model: root.model

            delegate: Rectangle {
                id: chip
                required property var modelData
                required property int index
                readonly property bool active: modelData.value === root.current

                height: root._chipH
                width: root.fill
                       ? (options.width - (repeater.count - 1) * options.spacing) / Math.max(1, repeater.count)
                       : label.implicitWidth + (root.size === "sm" ? 20 : 24)
                radius: Theme.r1
                color: active ? Theme.clrCard : (hover.hovered ? Qt.lighter(Theme.clrContainer, 1.02) : "transparent")
                border.width: active ? 1 : 0
                border.color: Qt.rgba(0, 0, 0, 0.06)

                Label {
                    id: label
                    anchors.centerIn: parent
                    text: chip.modelData.label
                    color: chip.active ? Theme.clrOnSurface : Theme.clrOnSurfaceMed
                    font.pixelSize: root.size === "sm" ? Theme.sizeLabel + 1 : Theme.sizeBodySm
                    font.weight: chip.active ? Font.DemiBold : Font.Normal
                }

                HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
                TapHandler {
                    onTapped: if (!chip.active) root.activated(chip.modelData.value)
                }

                Accessible.role: Accessible.RadioButton
                Accessible.name: chip.modelData.label
                Accessible.checked: chip.active
            }
        }
    }
}
