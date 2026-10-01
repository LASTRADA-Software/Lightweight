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
//     Glyph { name: "folder"; size: 13; color: Theme.clrOnSurfaceFaint }

import QtQuick
import QtQuick.Shapes
import Lightweight.Migrations

Item {
    id: root

    /// Which glyph to draw. One of the hand-built "folder", "archive",
    /// "search", "workers", "warning", "check", "cross", "chevron", or any key
    /// of `_svgPaths` (the Lastrada line icons). An unknown name draws nothing.
    property string name: ""

    /// Edge length in pixels. Geometry is authored on a 12×12 grid and scaled
    /// to this, so a glyph stays crisp at any size without a bitmap asset.
    property real size: 12

    /// Stroke/fill colour, taken by every path so one assignment themes the
    /// whole glyph (the reason these replaced emoji).
    property color color: Theme.clrOnSurface

    /// Surface colour used to punch the bang out of the "warning" triangle.
    /// Defaults to the panel background; set it to whatever the glyph actually
    /// sits on (e.g. `Theme.clrWarningBg` inside a warning banner) so the cut-out
    /// matches instead of showing a panel-coloured notch.
    property color knockout: Theme.clrCard

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
            default:         return root._svg !== undefined ? svgGlyph : null;
            }
        }
    }

    // Line icons from the Lastrada icon sprite (`ls-*` symbols in the design
    // project), authored on a 24×24 grid. Data-driven: adding an icon is one
    // table row, not a new Component. `fill: true` marks the solid ones.
    readonly property var _svgPaths: ({
        "layers":         { d: "M12 3 21 8 12 13 3 8 12 3zM3 13l9 5 9-5M3 17l9 5 9-5" },
        "sliders":        { d: "M4 6h9M17 6h3M4 12h3M11 12h9M4 18h11M19 18h1M13 6a2 2 0 1 0 4 0a2 2 0 1 0-4 0M7 12a2 2 0 1 0 4 0a2 2 0 1 0-4 0M15 18a2 2 0 1 0 4 0a2 2 0 1 0-4 0" },
        "refresh":        { d: "M20 12a8 8 0 1 1-2.3-5.7M20 4v5h-5" },
        "play":           { d: "M8 5.5v13l10.5-6.5z", fill: true },
        "server":         { d: "M5 4h14a2 2 0 0 1 2 2v3a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V6a2 2 0 0 1 2-2zM5 13h14a2 2 0 0 1 2 2v3a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-3a2 2 0 0 1 2-2zM7 7.5h.01M7 16.5h.01" },
        "info":           { d: "M3 12a9 9 0 1 0 18 0a9 9 0 1 0-18 0M12 11v5M12 8h.01" },
        "alert":          { d: "M12 3 22 20H2L12 3zM12 9v5M12 17h.01" },
        "check-circle":   { d: "M3 12a9 9 0 1 0 18 0a9 9 0 1 0-18 0M8 12.5l2.5 2.5L16 9.5" },
        "clock":          { d: "M3 12a9 9 0 1 0 18 0a9 9 0 1 0-18 0M12 7v5l3 2" },
        "chart":          { d: "M4 20V4M4 20h16M8 16v-5M12 16V8M16 16v-3M20 16V6" },
        "terminal":       { d: "M5 4h14a2 2 0 0 1 2 2v12a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V6a2 2 0 0 1 2-2zM7 9l3 3-3 3M13 15h4" },
        "eye":            { d: "M2 12s4-7 10-7 10 7 10 7-4 7-10 7S2 12 2 12zM9 12a3 3 0 1 0 6 0a3 3 0 1 0-6 0" },
        "chevron-down":   { d: "M6 9l6 6 6-6" },
        "chevron-left":   { d: "M15 6l-6 6 6 6" },
        "database":       { d: "M4 5.5a8 2.5 0 1 0 16 0a8 2.5 0 1 0-16 0M4 5.5v13c0 1.4 3.6 2.5 8 2.5s8-1.1 8-2.5v-13M4 12c0 1.4 3.6 2.5 8 2.5s8-1.1 8-2.5" },
        "folder-outline": { d: "M3 7a2 2 0 0 1 2-2h4l2 2h8a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V7z" },
        "archive-outline":{ d: "M4 4h16a1 1 0 0 1 1 1v3a1 1 0 0 1-1 1H4a1 1 0 0 1-1-1V5a1 1 0 0 1 1-1zM5 9v10a1 1 0 0 0 1 1h12a1 1 0 0 0 1-1V9M10 13h4" },
        "download":       { d: "M12 3v12M7 10l5 5 5-5M4 20h16" },
        "upload":         { d: "M12 15V3M7 8l5-5 5 5M4 20h16" },
        "document":       { d: "M14 3H6a1 1 0 0 0-1 1v16a1 1 0 0 0 1 1h12a1 1 0 0 0 1-1V8l-5-5zM14 3v5h5M8 13h8M8 17h6" },
        "plug":           { d: "M9 3v5M15 3v5M6 8h12v3a6 6 0 0 1-12 0V8zM12 17v4" },
        "history":        { d: "M3 12a9 9 0 1 0 3-6.7M3 4v5h5M12 7v5l3 2" },
        "copy":           { d: "M11 9h8a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2h-8a2 2 0 0 1-2-2v-8a2 2 0 0 1 2-2zM5 15H4a1 1 0 0 1-1-1V4a1 1 0 0 1 1-1h10a1 1 0 0 1 1 1v1" },
        "external":       { d: "M14 4h6v6M20 4l-9 9M19 14v5a1 1 0 0 1-1 1H5a1 1 0 0 1-1-1V6a1 1 0 0 1 1-1h5" },
        "more":           { d: "M5 10.2a1.8 1.8 0 1 0 0 3.6a1.8 1.8 0 1 0 0-3.6zM12 10.2a1.8 1.8 0 1 0 0 3.6a1.8 1.8 0 1 0 0-3.6zM19 10.2a1.8 1.8 0 1 0 0 3.6a1.8 1.8 0 1 0 0-3.6z", fill: true },
    })
    readonly property var _svg: _svgPaths[name]

    // ---- 24-grid line icon from `_svgPaths` ----
    Component {
        id: svgGlyph
        GlyphShape {
            ShapePath {
                readonly property bool solid: root._svg !== undefined && root._svg.fill === true
                fillColor: solid ? root.color : "transparent"
                strokeColor: solid ? "transparent" : root.color
                // ShapePath.scale scales the geometry but not the pen, so the
                // 1.8-unit stroke of the 24-grid sprite is scaled by hand.
                strokeWidth: solid ? 0 : 1.8 * root.size / 24
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                scale: Qt.size(root.size / 24, root.size / 24)
                PathSvg { path: root._svg !== undefined ? root._svg.d : "" }
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
