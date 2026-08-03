// SPDX-License-Identifier: Apache-2.0
//
// Small monochrome vector glyph, drawn with QtQuick.Shapes rather than
// rendered from an emoji code point.
//
// The GUI previously used emoji (🔍 📁 🗄 ⚙ ⚠ ✓ ✕) as inline icons. Emoji are
// rendered by the platform's colour-emoji font, which means they:
//   * ignore `color`, so they cannot follow the light/dark Theme palette — a
//     dark-mode warning banner got a full-colour sticker on a dark surface;
//   * differ per platform (Segoe UI Emoji on Windows, Apple Color Emoji on
//     macOS, Noto on Linux), so metrics and weight shift across the very
//     platforms this project's CI matrix builds for;
//   * carry no fallback — a missing face draws the tofu box.
//
// Each glyph here is built from strokes and fills that take `color`, so one
// assignment themes it and every platform gets identical geometry. Geometry is
// authored on a nominal 12×12 grid and scaled by `size`.
//
// Only the selected glyph's paths are instantiated: each shape lives in its own
// `Component` and a `Loader` picks one by name. `ShapePath` is not an `Item`,
// so it has no `visible` property — gating the paths with `visible:` silently
// fails to compile the type, which is why selection happens at the Loader
// level rather than per path.
//
// Usage:
//     Glyph { name: "folder"; size: 13; color: Theme.textFaint }

import QtQuick
import QtQuick.Shapes
import Lightweight.Migrations

Item {
    id: root

    /// Which glyph to draw. One of: "folder", "archive", "search", "workers",
    /// "warning", "check", "cross", "chevron". An unknown name draws nothing.
    property string name: ""

    /// Edge length in pixels. Geometry is authored on a 12×12 grid and scaled
    /// to this, so a glyph stays crisp at any size without a bitmap asset.
    property real size: 12

    /// Stroke/fill colour, taken by every path so one assignment themes the
    /// whole glyph (the reason these replaced emoji).
    property color color: Theme.text

    /// Surface colour used to punch the bang out of the "warning" triangle.
    /// Defaults to the panel background; set it to whatever the glyph actually
    /// sits on (e.g. `Theme.warnSoft` inside a warning banner) so the cut-out
    /// matches instead of showing a panel-coloured notch.
    property color knockout: Theme.bgPanel

    /// Relative stroke weight on the 12-unit grid, scaled with `size`.
    property real strokeWidth: 1.6

    implicitWidth: size
    implicitHeight: size

    // Scale factor from the authoring grid to the requested pixel size.
    readonly property real u: size / 12.0
    readonly property real sw: strokeWidth * u

    Loader {
        anchors.fill: parent
        sourceComponent: {
            switch (root.name) {
            case "folder":   return folderGlyph;
            case "archive":  return archiveGlyph;
            case "search":   return searchGlyph;
            case "workers":  return workersGlyph;
            case "warning":  return warningGlyph;
            case "check":    return checkGlyph;
            case "cross":    return crossGlyph;
            case "chevron":  return chevronGlyph;
            default:         return null;
            }
        }
    }

    // Shared Shape settings. The curve renderer keeps small radii and
    // diagonals smooth without multisampling the whole window.
    component GlyphShape: Shape {
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer
        antialiasing: true
    }

    // A filled path with no stroke — used for the solid box/bar glyphs.
    component FillPath: ShapePath {
        fillColor: root.color
        strokeColor: "transparent"
        strokeWidth: 0
    }

    // A stroked path with no fill — used for the line-art glyphs.
    component StrokePath: ShapePath {
        fillColor: "transparent"
        strokeColor: root.color
        strokeWidth: root.sw
        capStyle: ShapePath.RoundCap
        joinStyle: ShapePath.RoundJoin
    }

    // ---- folder: a tab bar over a body ----
    Component {
        id: folderGlyph
        GlyphShape {
            FillPath {
                PathRectangle {
                    x: 0; y: 2 * root.u
                    width: 5 * root.u; height: 2.4 * root.u
                    topLeftRadius: root.u; topRightRadius: root.u
                }
            }
            FillPath {
                PathRectangle {
                    x: 0; y: 4 * root.u
                    width: 12 * root.u; height: 7 * root.u
                    radius: 1.5 * root.u
                }
            }
        }
    }

    // ---- archive: a lid bar over a lighter body ----
    Component {
        id: archiveGlyph
        GlyphShape {
            FillPath {
                PathRectangle {
                    x: 0; y: 2 * root.u
                    width: 12 * root.u; height: 3 * root.u
                    radius: root.u
                }
            }
            ShapePath {
                // Same hue at reduced alpha, so the lid reads as a separate
                // plane without introducing a second palette entry.
                fillColor: Qt.rgba(root.color.r, root.color.g, root.color.b,
                                   root.color.a * 0.55)
                strokeColor: "transparent"
                strokeWidth: 0
                PathRectangle {
                    x: root.u; y: 5.8 * root.u
                    width: 10 * root.u; height: 5.2 * root.u
                    bottomLeftRadius: 1.5 * root.u
                    bottomRightRadius: 1.5 * root.u
                }
            }
        }
    }

    // ---- search: a ring plus a diagonal handle ----
    Component {
        id: searchGlyph
        GlyphShape {
            StrokePath {
                PathAngleArc {
                    centerX: 4.6 * root.u; centerY: 4.6 * root.u
                    radiusX: 3.6 * root.u; radiusY: 3.6 * root.u
                    startAngle: 0
                    sweepAngle: 360
                }
            }
            StrokePath {
                startX: 7.4 * root.u; startY: 7.4 * root.u
                PathLine { x: 10.8 * root.u; y: 10.8 * root.u }
            }
        }
    }

    // ---- workers: three lanes, the middle one short ----
    // Reads as parallel lanes of work, which is what the worker count means.
    Component {
        id: workersGlyph
        GlyphShape {
            StrokePath {
                startX: 0.8 * root.u; startY: 2.6 * root.u
                PathLine { x: 11.2 * root.u; y: 2.6 * root.u }
            }
            StrokePath {
                startX: 0.8 * root.u; startY: 6 * root.u
                PathLine { x: 7.6 * root.u; y: 6 * root.u }
            }
            StrokePath {
                startX: 0.8 * root.u; startY: 9.4 * root.u
                PathLine { x: 11.2 * root.u; y: 9.4 * root.u }
            }
        }
    }

    // ---- warning: a filled triangle with a knocked-out bang ----
    Component {
        id: warningGlyph
        GlyphShape {
            ShapePath {
                fillColor: root.color
                strokeColor: root.color
                strokeWidth: root.sw
                joinStyle: ShapePath.RoundJoin
                capStyle: ShapePath.RoundCap
                startX: 6 * root.u; startY: 1.2 * root.u
                PathLine { x: 11.4 * root.u; y: 10.6 * root.u }
                PathLine { x: 0.6 * root.u;  y: 10.6 * root.u }
                PathLine { x: 6 * root.u;    y: 1.2 * root.u }
            }
            // Drawn in the host surface colour so the bang reads as a cut-out
            // rather than a second ink colour.
            ShapePath {
                fillColor: root.knockout
                strokeColor: "transparent"
                strokeWidth: 0
                PathRectangle {
                    x: 5.2 * root.u; y: 4.4 * root.u
                    width: 1.6 * root.u; height: 3.4 * root.u
                    radius: 0.8 * root.u
                }
            }
            ShapePath {
                fillColor: root.knockout
                strokeColor: "transparent"
                strokeWidth: 0
                PathRectangle {
                    x: 5.2 * root.u; y: 8.5 * root.u
                    width: 1.6 * root.u; height: 1.6 * root.u
                    radius: 0.8 * root.u
                }
            }
        }
    }

    // ---- check: a two-segment tick ----
    Component {
        id: checkGlyph
        GlyphShape {
            StrokePath {
                startX: 1.6 * root.u; startY: 6.4 * root.u
                PathLine { x: 4.6 * root.u;  y: 9.4 * root.u }
                PathLine { x: 10.4 * root.u; y: 2.8 * root.u }
            }
        }
    }

    // ---- cross ----
    Component {
        id: crossGlyph
        GlyphShape {
            StrokePath {
                startX: 2.2 * root.u; startY: 2.2 * root.u
                PathLine { x: 9.8 * root.u; y: 9.8 * root.u }
            }
            StrokePath {
                startX: 9.8 * root.u; startY: 2.2 * root.u
                PathLine { x: 2.2 * root.u; y: 9.8 * root.u }
            }
        }
    }

    // ---- chevron (pointing right) ----
    Component {
        id: chevronGlyph
        GlyphShape {
            StrokePath {
                startX: 4.6 * root.u; startY: 2.6 * root.u
                PathLine { x: 8.4 * root.u; y: 6 * root.u }
                PathLine { x: 4.6 * root.u; y: 9.4 * root.u }
            }
        }
    }
}
