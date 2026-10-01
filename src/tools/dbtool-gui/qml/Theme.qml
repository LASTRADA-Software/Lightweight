// SPDX-License-Identifier: Apache-2.0
//
// Central design tokens used by every GUI component.
//
// dbtool-gui follows the Lastrada UI design system (the "Lastrada UI" Claude
// Design project, `_tokens.css` + the `dbtool/` pages). Token names mirror
// that file one-to-one so a value looked up in the design can be found here
// by the same name: `--clrPrimary` is `Theme.clrPrimary`, `--sp3` is
// `Theme.sp3`, and so on.
//
// The palette is light-only by design: the dark navigation rail and the code
// surfaces (log, SQL editor) are the only dark areas, and they are dark in
// every configuration. `main.cpp` pins the platform colour scheme to light so
// the Fusion controls never pick up a dark system palette underneath us.

pragma Singleton

import QtQuick

QtObject {
    // ---- Brand ----
    readonly property color clrPrimary:           "#a21928"
    readonly property color clrPrimaryHover:      "#8a1522"
    readonly property color clrPrimaryActive:     "#70101a"
    readonly property color clrPrimarySoft:       "#fbe9eb"
    readonly property color clrPrimarySoftBorder: "#f0bfc5"
    readonly property color clrFocusRing:         Qt.rgba(162 / 255, 25 / 255, 40 / 255, 0.20)

    // ---- Neutrals: page -> card ----
    readonly property color clrBase:             "#f2f2f4"
    readonly property color clrContainerLow:     "#f7f7f8"
    readonly property color clrContainer:        "#ececee"
    readonly property color clrContainerHigh:    "#e4e5e8"
    readonly property color clrContainerHighest: "#d2d3d8"
    readonly property color clrCard:             "#ffffff"

    readonly property color clrOnSurface:       "#15171c"
    readonly property color clrOnSurfaceMed:    "#3a3f49"
    readonly property color clrOnSurfaceSubtle: "#6b717e"
    readonly property color clrOnSurfaceFaint:  "#9a9fab"

    readonly property color clrBorderStrong: "#aeb2bb"
    readonly property color clrDivider:      "#e8e9ec"

    // ---- Dark rail and code surfaces ----
    readonly property color clrSidebarBg:    "#1a1718"
    readonly property color clrSidebarHi:    "#2a2526"
    readonly property color clrSidebarText:  "#ebe6e7"
    readonly property color clrSidebarMuted: "#a89fa1"
    readonly property color clrSidebarDash:  "#4a4345"
    /// Foreground on code surfaces (log, SQL editor, SQL preview).
    readonly property color clrCodeText:     "#ebe6e7"
    /// Timestamps and comments on code surfaces.
    readonly property color clrCodeMuted:    "#857b7d"

    // ---- Fields ----
    readonly property color clrFieldBorder:  "#c6c9d0"
    readonly property color clrFieldRoBg:    "#f5f5f6"
    readonly property color clrFieldRoText:  "#7c828d"
    readonly property color clrFieldRoBorder: "#e3e4e8"
    readonly property color clrFieldFocusBg: "#fffbfb"

    // ---- Section headers ----
    readonly property color clrSectionHdr:       "#faf5f5"
    readonly property color clrSectionHdrBorder: "#e8dcdd"

    // ---- Semantic. Error is warmer than brand and always carries an icon or text. ----
    readonly property color clrSuccess:       "#0e6b44"
    readonly property color clrSuccessBg:     "#d9f2e4"
    readonly property color clrSuccessBorder: "#8fd8b3"
    readonly property color clrSuccessDot:    "#17a34a"
    readonly property color clrWarning:       "#8f4e00"
    readonly property color clrWarningBg:     "#fdecc8"
    readonly property color clrWarningBorder: "#f2c56d"
    readonly property color clrWarningDot:    "#d97706"
    readonly property color clrError:         "#b42318"
    readonly property color clrErrorBg:       "#fee4e2"
    readonly property color clrErrorBorder:   "#fda29b"
    readonly property color clrErrorRowBg:    "#fff8f7"
    readonly property color clrErrorDot:      "#dc2626"
    readonly property color clrInfo:          "#1a56a8"
    readonly property color clrInfoBg:        "#e4eefb"
    readonly property color clrInfoBorder:    "#a8c4ee"

    /// Status colours on dark code surfaces.
    readonly property color clrCodeOk:   "#4ade80"
    readonly property color clrCodeWarn: "#fbbf24"
    readonly property color clrCodeErr:  "#f87171"

    // ---- Typography (pixel sizes; QML pixelSize is integral) ----
    readonly property string fontFamily: "Segoe UI"
    readonly property int sizeDisplay: 22
    readonly property int sizeValue:   20
    readonly property int sizeTitle:   16
    readonly property int sizeTitleSm: 14
    readonly property int sizeBody:    13
    readonly property int sizeBodySm:  12
    readonly property int sizeLabel:   11
    readonly property int sizeMono:    12
    readonly property int sizeGroup:   10

    // ---- Spacing ----
    readonly property int sp1: 4
    readonly property int sp2: 8
    readonly property int sp3: 12
    readonly property int sp4: 16
    readonly property int sp5: 20
    readonly property int sp6: 24
    readonly property int sp8: 32

    // ---- Shape ----
    readonly property real r1: 4      // buttons, inputs
    readonly property real r2: 6      // option cards, banners, rail items
    readonly property real r3: 8      // panels
    readonly property real r4: 12
    readonly property real rPill: 999 // pills and progress tracks

    // ---- Control and chrome sizes ----
    readonly property int ctlSm: 28
    readonly property int ctlMd: 32
    readonly property int ctlLg: 40
    readonly property int rowDense: 28
    readonly property int row: 32
    readonly property int railW: 216
    readonly property int railNarrowW: 60
    readonly property int topH: 52
    readonly property int statusH: 26

    /// Single elevation step, used to lift popups off the page.
    readonly property color shadow: Qt.rgba(21 / 255, 23 / 255, 28 / 255, 0.10)

    // Monospace font fallback chain. Must be assigned via `font.families`
    // (the list-valued property) — `font.family` accepts only a single
    // family name and would treat a comma-joined string as one literal
    // lookup that never matches, silently falling back to the platform
    // default proportional font.
    //
    // Cascadia Mono comes first to match the design system's `--mono`. Nerd
    // Font variants follow — they share upstream glyph metrics plus a patched
    // icon range, so column alignment is unchanged; plain faces and the generic
    // `monospace` keyword close out the chain.
    readonly property var monoFamilies: [
        "Cascadia Mono",
        "CaskaydiaMono Nerd Font",
        "CaskaydiaCove Nerd Font Mono",
        "JetBrainsMono Nerd Font Mono",
        "JetBrains Mono",
        "Consolas",
        "Menlo",
        "DejaVu Sans Mono",
        "Courier New",
        "monospace",
    ]

    /// Build a complete monospace `font` value with the project's family
    /// fallback chain baked in. Use this as `font: Theme.monoFont(12)`
    /// rather than `font.family: …` because QtQuick.Controls 2 elements
    /// (`TextField`, `ComboBox`, …) do not expose `font.families` through
    /// their QML value-type adapter — and on Qt 6.11 neither do `Text` or
    /// `Label`, so whole-value assignment is the only portable route.
    /// Assigning the whole `font` property side-steps that limitation by
    /// copying an underlying `QFont` that already has `setFamilies()`
    /// applied. Mixing `font: X` with `font.pixelSize: Y` on the same
    /// element is rejected by the QML parser as a double-assignment, which
    /// is why the size (and optional weight) are passed in here instead of
    /// being layered on at the call site.
    /// @param pixelSize Pixel size for the resulting font.
    /// @param weight Optional Qt font weight (e.g. `Font.DemiBold`); omit
    ///        for the default `Font.Normal`.
    function monoFont(pixelSize, weight) {
        return Qt.font({
            families: monoFamilies,
            pixelSize: pixelSize,
            weight: weight !== undefined ? weight : Font.Normal,
        })
    }
}
