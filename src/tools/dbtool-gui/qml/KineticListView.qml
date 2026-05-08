// SPDX-License-Identifier: Apache-2.0
//
// `ListView` subclass that adds:
//   - Kinetic (momentum) scrolling for pixel-delta wheel devices (touchpads)
//     via a `WheelHandler` that hands the trailing velocity to `flick()` on
//     ScrollEnd, so the list keeps coasting and naturally decelerates via
//     Flickable's own animation curve.
//   - An always-visible vertical `ScrollBar.vertical`, so users have a clear
//     affordance that the area is scrollable (the default auto-hide behaviour
//     hides the bar outside of active scrolling, which surprised testers).
//
// Mouse-wheel events (angle-only, no pixel deltas) are handled explicitly
// inside the WheelHandler: declining an event with `event.accepted = false`
// does not reliably fall back to Flickable's default wheel handling once a
// PointerHandler is attached, so relying on that was silently breaking the
// mouse-wheel scroll. Instead we scroll by a fixed pixels-per-notch step
// derived from the angle delta and accept the event ourselves.
//
// Drop-in replacement for `ListView` — callers set `model` and `delegate` as
// usual; all other ListView properties work unchanged.

import QtQuick
import QtQuick.Controls
import Lightweight.Migrations

ListView {
    id: root

    // Momentum/flick tuning. Explicitly pinned so behaviour is stable across
    // Qt versions and independent of any theme-level Flickable tweaks.
    flickDeceleration: 1500
    maximumFlickVelocity: 5000

    // Always-visible scrollbar.
    //
    // The Basic-style ScrollBar fades its handle to opacity 0 whenever
    // `control.active` is false — and Qt's Flickable integration writes to
    // `active` directly from C++ every time a gesture or wheel event ends,
    // which silently breaks any `active: true` binding we attach from QML.
    // The net effect was an invisible scrollbar on both the migration list
    // and the releases summary.
    //
    // Rebuilding `contentItem` here lets us pin opacity to `size < 1.0`
    // (i.e. "content overflows the view") instead of to `active`. That is a
    // pure function of geometry, so Flickable never touches it, and the
    // handle stays drawn as long as there is something to scroll.
    ScrollBar.vertical: ScrollBar {
        id: vbar
        policy: ScrollBar.AsNeeded
        interactive: true

        contentItem: Rectangle {
            implicitWidth: 8
            radius: width / 2
            color: vbar.pressed ? Theme.textMuted : Theme.textFaint
            opacity: vbar.size < 1.0 ? (vbar.pressed || vbar.hovered ? 0.85 : 0.55) : 0.0
            Behavior on opacity { NumberAnimation { duration: 150 } }
        }
    }

    WheelHandler {
        id: touchpadMomentum
        target: null
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad

        property real _velocity: 0
        property real _lastTs: 0
        property bool _hasPhase: false

        // Pixels per mouse-wheel notch. 120 px ≈ 3 migration rows (rowHeight
        // 40) and ≈ 4 release rows (rowHeight 30) — matches the "three lines
        // per notch" convention used by GTK/macOS/Windows. The previous 40 px
        // stepped only a single row, which felt sluggish when scanning long
        // plugin lists.
        readonly property int _wheelStepPx: 120

        // Multiplier applied to touchpad pixel deltas. Drivers report
        // physically accurate (small) pixel counts; apps typically amplify
        // to make two-finger swipes feel responsive without requiring
        // multiple passes across the trackpad.
        readonly property real _touchpadScale: 2.0

        onWheel: (event) => {
            const dyPixel = event.pixelDelta.y;
            if (dyPixel === 0) {
                // Angle-only delta: classic mouse wheel. Scroll by a
                // fixed pixel step per 120-unit notch (Qt's convention).
                const notches = event.angleDelta.y / 120;
                const step = notches * _wheelStepPx;
                const maxY = Math.max(0, root.contentHeight - root.height);
                root.contentY = Math.max(0, Math.min(maxY, root.contentY - step));
                event.accepted = true;
                return;
            }

            const now = Date.now();
            const dt = Math.max(1, now - _lastTs) / 1000.0;
            _lastTs = now;

            if (event.phase === Qt.ScrollBegin) {
                _velocity = 0;
                _hasPhase = true;
            }

            const dy = dyPixel * _touchpadScale;
            const maxY = Math.max(0, root.contentHeight - root.height);
            root.contentY = Math.max(0, Math.min(maxY, root.contentY - dy));

            // Running velocity in px/sec. Same sign as `dy`: Flickable's
            // flick(0, v) uses v with the same sign convention as angle/pixel
            // delta — see QQuickFlickable::wheelEvent, which forwards
            // `yDelta / elapsed` (no negation) straight into flickY(). The
            // earlier `-(dy/dt)` kicked the flick in the direction opposite
            // to the swipe, producing a visible snap-back at ScrollEnd.
            _velocity = dy / dt;

            if (event.phase === Qt.ScrollEnd) {
                root.flick(0, _velocity);
                _velocity = 0;
                _hasPhase = false;
            } else if (event.phase === Qt.NoScrollPhase
                       || event.phase === Qt.ScrollUpdate
                       || event.phase === undefined) {
                endOfGestureTimer.restart();
            }

            event.accepted = true;
        }
    }

    // Fallback end-of-gesture timer for drivers that deliver pixel-delta
    // wheel events without Qt ScrollPhase markers (some XInput2 setups on
    // X11). If no wheel event arrives for `interval` ms after the last one,
    // treat the gesture as ended and launch a flick using the running
    // velocity the handler accumulated.
    Timer {
        id: endOfGestureTimer
        interval: 80
        repeat: false
        onTriggered: {
            if (!touchpadMomentum._hasPhase
                && Math.abs(touchpadMomentum._velocity) > 50) {
                root.flick(0, touchpadMomentum._velocity);
            }
            touchpadMomentum._velocity = 0;
        }
    }
}
