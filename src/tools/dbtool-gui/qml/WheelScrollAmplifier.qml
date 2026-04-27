// SPDX-License-Identifier: Apache-2.0
//
// Drop-in pair of `WheelHandler`s that amplifies touchpad pixel deltas and
// converts classic mouse-wheel notches into a sensible pixel step on both
// axes. Attach inside any `Flickable` (or component whose contentItem is a
// Flickable, like `ScrollView` and `TableView`) and assign `target` to that
// Flickable:
//
//     ScrollView {
//         id: sv
//         TextArea {}
//         WheelScrollAmplifier { target: sv.contentItem }
//     }
//
// Mirrors the tuning in `KineticListView.qml` (touchpad scale 2.0×, mouse
// notch 120 px) so every scrollable surface in the GUI feels consistent.
// Qt's default touchpad pixel deltas are physically accurate but typically
// too small to scan a long log buffer or query result set comfortably; the
// amplifier scales them up the same way native apps on macOS / GNOME do.
//
// `WheelHandler.orientation` is a single `Qt::Orientation` value, not a
// flags bitmask — a single handler silently ignores the other axis. We
// instantiate one handler per axis so both vertical and horizontal swipes
// are amplified.

import QtQuick

Item {
    id: root

    /// Flickable to scroll. Both internal handlers reparent themselves
    /// onto it so they receive wheel events directly (a `WheelHandler`
    /// only sees events delivered to its parent item).
    property Item target: null

    /// Pixels per mouse-wheel notch (Qt reports notches as 120-unit angle
    /// deltas).
    property int wheelStepPx: 120

    /// Multiplier applied to touchpad pixel deltas.
    property real touchpadScale: 2.0

    // Zero-size container so the QML scope holds the handlers. The actual
    // event-receiving parent is `target`, set inside each handler's
    // `Component.onCompleted`.
    width: 0
    height: 0
    visible: false

    function _scrollAxis(flickable, dyPixel, dyAngle, isVertical) {
        if (!flickable)
            return
        const max = isVertical
            ? Math.max(0, flickable.contentHeight - flickable.height)
            : Math.max(0, flickable.contentWidth - flickable.width)
        let d = dyPixel
        if (d !== 0)
            d *= root.touchpadScale
        else
            d = (dyAngle / 120) * root.wheelStepPx
        if (d === 0)
            return
        if (isVertical)
            flickable.contentY = Math.max(0, Math.min(max, flickable.contentY - d))
        else
            flickable.contentX = Math.max(0, Math.min(max, flickable.contentX - d))
    }

    WheelHandler {
        id: vHandler
        orientation: Qt.Vertical
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        Component.onCompleted: {
            if (root.target)
                parent = root.target
        }
        onWheel: (event) => {
            root._scrollAxis(root.target, event.pixelDelta.y, event.angleDelta.y, true)
            event.accepted = true
        }
    }

    WheelHandler {
        id: hHandler
        orientation: Qt.Horizontal
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        Component.onCompleted: {
            if (root.target)
                parent = root.target
        }
        onWheel: (event) => {
            root._scrollAxis(root.target, event.pixelDelta.x, event.angleDelta.x, false)
            event.accepted = true
        }
    }
}
