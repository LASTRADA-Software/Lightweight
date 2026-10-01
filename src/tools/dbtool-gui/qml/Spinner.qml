// SPDX-License-Identifier: Apache-2.0
//
// Small indeterminate spinner (`dt-spin` in the design): a faint ring with a
// coloured quarter that rotates. Used inside buttons, pills and progress rows
// where a full `BusyIndicator` would be far too large.

import QtQuick
import QtQuick.Shapes
import Lightweight.Migrations

Item {
    id: root

    /// Outer diameter in pixels.
    property real diameter: 11
    /// Colour of the moving arc; the track is the same hue at low alpha.
    property color color: Theme.clrPrimary
    /// Whether the arc rotates. Stop it when hidden so it costs nothing.
    property bool running: true

    implicitWidth: diameter
    implicitHeight: diameter

    readonly property real _stroke: Math.max(1.5, diameter / 6)

    Shape {
        id: shape
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer
        antialiasing: true

        ShapePath {
            fillColor: "transparent"
            strokeColor: Qt.rgba(root.color.r, root.color.g, root.color.b, 0.25)
            strokeWidth: root._stroke
            PathAngleArc {
                centerX: root.diameter / 2; centerY: root.diameter / 2
                radiusX: (root.diameter - root._stroke) / 2; radiusY: radiusX
                startAngle: 0; sweepAngle: 360
            }
        }
        ShapePath {
            fillColor: "transparent"
            strokeColor: root.color
            strokeWidth: root._stroke
            capStyle: ShapePath.RoundCap
            PathAngleArc {
                centerX: root.diameter / 2; centerY: root.diameter / 2
                radiusX: (root.diameter - root._stroke) / 2; radiusY: radiusX
                startAngle: -90; sweepAngle: 90
            }
        }

        RotationAnimation on rotation {
            from: 0; to: 360
            duration: 900
            loops: Animation.Infinite
            running: root.running && root.visible
        }
    }
}
