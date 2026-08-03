// SPDX-License-Identifier: Apache-2.0
//
// Slim, palette-aware progress bar.
//
// Replaces the bare QtQuick.Controls `ProgressBar` used by the backup views.
// The stock control renders through the active QQC2 style, so its height,
// track colour, and fill colour come from the platform style rather than from
// `Theme` — on Windows it drew a chunky Fluent bar that ignored the app's
// accent and looked foreign next to the surrounding pills and cards, and its
// indeterminate animation could not be tinted at all.
//
// This is a plain two-rectangle track with an explicit height, so the backup
// detail panel can put a 4px bar inside a table row and a 6px bar in the
// aggregate readout while both stay on the same palette.
//
// Usage:
//     ProgressTrack { from: 0; to: total; value: done }
//     ProgressTrack { indeterminate: true }          // unknown total

import QtQuick
import Lightweight.Migrations

Item {
    id: root

    /// Lower bound of `value`.
    property real from: 0
    /// Upper bound of `value`. Values <= `from` render an empty track.
    property real to: 1
    /// Current progress, clamped into [`from`, `to`].
    property real value: 0

    /// When true, an accent sliver sweeps the track instead of showing a
    /// proportion — for work whose total is not known yet (e.g. a table whose
    /// row count is still being counted).
    property bool indeterminate: false

    /// Fill colour. Defaults to the accent; pass `Theme.ok` for a completed
    /// run or `Theme.err` for a failed one so the bar carries the same
    /// semantic as the row's status pill.
    property color fillColor: Theme.accent

    /// Track (unfilled) colour.
    property color trackColor: Theme.bgSubtle

    implicitHeight: 4
    implicitWidth: 120

    /// Fraction filled, clamped to [0, 1]. Guards a zero/inverted range so a
    /// not-yet-started run renders an empty track rather than dividing by zero.
    readonly property real _fraction: {
        const span = root.to - root.from;
        if (span <= 0)
            return 0;
        return Math.max(0, Math.min(1, (root.value - root.from) / span));
    }

    Rectangle {
        id: track
        anchors.fill: parent
        radius: Theme.radiusPill
        color: root.trackColor
        clip: true

        // Determinate fill.
        Rectangle {
            visible: !root.indeterminate
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: parent.width * root._fraction
            radius: Theme.radiusPill
            color: root.fillColor

            // Animate growth so a progress tick reads as motion rather than a
            // jump. Short enough not to lag behind a fast run.
            Behavior on width {
                NumberAnimation { duration: 180; easing.type: Easing.OutCubic }
            }
        }

        // Indeterminate sweep.
        Rectangle {
            id: sweep
            visible: root.indeterminate
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: parent.width * 0.34
            radius: Theme.radiusPill
            color: root.fillColor

            // Driven only while visible so an idle panel full of finished
            // tables is not animating dozens of invisible rectangles.
            XAnimator on x {
                running: sweep.visible
                loops: Animation.Infinite
                from: -sweep.width
                to: track.width
                duration: 1300
                easing.type: Easing.InOutQuad
            }
        }
    }
}
