// SPDX-License-Identifier: Apache-2.0
//
// Lastrada kit button (`k-btn` in the design project). One component carries
// every variant the screens use, so a screen picks *intent* (primary,
// secondary, ghost, danger) and *size* (sm 28 / md 32 / lg 40) rather than
// hand-styling a `Button`.
//
// Rules from the design:
//   * Brand red (`primary`) is reserved for the one primary action per screen.
//   * Destructive actions (Restore, Rollback) use `danger` — outlined in error
//     red — or `dangerFill` for the confirming button inside a dialog.
//   * `busy` swaps the icon for a spinner and blocks clicks while keeping the
//     label, so the button does not change width mid-run.
//
// Usage:
//     LsButton { text: qsTr("Apply"); variant: "primary"; size: "lg"; icon.name: "play" }

import QtQuick
import QtQuick.Controls
import Lightweight.Migrations

Button {
    id: root

    /// "primary" | "secondary" | "ghost" | "danger" | "dangerFill".
    property string variant: "secondary"
    /// "sm" (28 px) | "md" (32 px) | "lg" (40 px).
    property string size: "sm"
    /// Glyph name drawn before the label; empty for none.
    property string glyph: ""
    /// Shows a spinner in place of the glyph and ignores clicks.
    property bool busy: false
    /// Draw only the glyph (square button). Text still feeds the tooltip
    /// and accessibility name.
    property bool iconOnly: false

    // Per-variant colours: [background, hover background, border, foreground].
    readonly property var _variants: ({
        "primary":    [Theme.clrPrimary, Theme.clrPrimaryHover, Theme.clrPrimary, "#ffffff"],
        "secondary":  [Theme.clrCard, Theme.clrContainerLow, Theme.clrBorderStrong, Theme.clrOnSurface],
        "ghost":      ["transparent", Theme.clrContainer, "transparent", Theme.clrOnSurfaceMed],
        "danger":     [Theme.clrCard, Theme.clrErrorBg, Theme.clrErrorBorder, Theme.clrError],
        "dangerFill": [Theme.clrError, Qt.darker(Theme.clrError, 1.12), Theme.clrError, "#ffffff"],
    })
    readonly property var _c: _variants[variant] || _variants["secondary"]

    // Per-size metrics: [height, horizontal padding, font px, radius, glyph px].
    readonly property var _sizes: ({
        "sm": [Theme.ctlSm, 10, 12, Theme.r1, 14],
        "md": [Theme.ctlMd, 12, 13, Theme.r2, 15],
        "lg": [Theme.ctlLg, 16, 14, Theme.r3, 16],
    })
    readonly property var _s: _sizes[size] || _sizes["sm"]

    implicitHeight: _s[0]
    implicitWidth: iconOnly ? _s[0] : contentItem.implicitWidth + 2 * _s[1]
    leftPadding: iconOnly ? 0 : _s[1]
    rightPadding: iconOnly ? 0 : _s[1]
    topPadding: 0
    bottomPadding: 0
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    opacity: enabled ? 1.0 : 0.45

    ToolTip.visible: iconOnly && hovered && text !== ""
    ToolTip.delay: 500
    ToolTip.text: text

    // Swallows presses while busy without disabling the button (disabled
    // would fade it and drop keyboard focus mid-run).
    MouseArea {
        anchors.fill: parent
        visible: root.busy
        acceptedButtons: Qt.AllButtons
        cursorShape: Qt.BusyCursor
    }

    background: Rectangle {
        radius: root._s[3]
        color: root.down && !root.busy ? Qt.darker(root._c[1], 1.04)
             : root.hovered && !root.busy ? root._c[1] : root._c[0]
        border.width: root.variant === "ghost" ? 0 : 1
        border.color: root._c[2]

        // Focus ring (`--clrFocusRing`), drawn outside the button.
        Rectangle {
            anchors.fill: parent
            anchors.margins: -3
            radius: parent.radius + 3
            color: "transparent"
            border.width: 3
            border.color: Theme.clrFocusRing
            visible: root.visualFocus
        }
    }

    contentItem: Item {
        implicitWidth: row.implicitWidth
        implicitHeight: row.implicitHeight

        Row {
            id: row
            anchors.centerIn: parent
            spacing: 6

            Item {
                width: root._s[4]
                height: root._s[4]
                anchors.verticalCenter: parent.verticalCenter
                visible: root.busy || root.glyph !== ""

                Glyph {
                    anchors.fill: parent
                    visible: !root.busy
                    name: root.glyph
                    size: root._s[4]
                    color: root._c[3]
                }

                Spinner {
                    anchors.centerIn: parent
                    visible: root.busy
                    running: root.busy
                    diameter: root._s[4] - 3
                    color: root._c[3]
                }
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                visible: !root.iconOnly
                text: root.text
                color: root._c[3]
                font.family: Theme.fontFamily
                font.pixelSize: root._s[2]
                font.weight: root.variant === "primary" || root.variant === "dangerFill"
                             || root.size === "lg" ? Font.DemiBold : Font.Medium
                elide: Text.ElideRight
            }
        }
    }
}
