// SPDX-License-Identifier: Apache-2.0
//
// Central palette used by every GUI component. `dark` is sourced from
// `ThemeController` (C++), not `Qt.styleHints.colorScheme`, because on KDE
// Plasma the platform theme plugin overrides `QStyleHints::setColorScheme`
// and would render `--theme light` / `--theme dark` ineffective.
//
// The palette mirrors the mockup in docs/migrations-gui-mockup.html: a
// Fluent-ish neutral base with a bluish accent. Dark-mode values are picked
// to match Windows 11 / macOS Sonoma defaults without looking washed out.

pragma Singleton

import QtQuick
import Lightweight.Migrations

QtObject {
    readonly property bool dark: ThemeController.dark

    // Surfaces
    readonly property color bgPage:    dark ? "#0f1115" : "#eceef2"
    readonly property color bgWindow:  dark ? "#161a21" : "#ffffff"
    readonly property color bgChrome:  dark ? "#1b2029" : "#f6f7f9"
    readonly property color bgSidebar: dark ? "#12161d" : "#fafbfc"
    readonly property color bgPanel:   dark ? "#1a1f28" : "#ffffff"
    readonly property color bgSubtle:  dark ? "#222832" : "#f2f4f7"
    readonly property color bgHover:   dark ? "#262d38" : "#eef1f6"
    readonly property color bgSelected: dark ? "#153050" : "#e8f1fe"
    readonly property color bgTerminal: "#0f172a"

    // Lines
    readonly property color border:       dark ? "#2a313c" : "#e3e6ec"
    readonly property color borderStrong: dark ? "#3a4351" : "#cfd4dc"
    readonly property color divider:      dark ? "#262d38" : "#eceff4"

    // Text
    readonly property color text:      dark ? "#e6e9ef" : "#1f2430"
    readonly property color textMuted: dark ? "#9ba3b0" : "#5b6372"
    readonly property color textFaint: dark ? "#6b7380" : "#8a93a4"

    // Accent
    readonly property color accent:     "#0a66d6"
    readonly property color accentHover: "#0a5cc2"
    readonly property color accentSoft: dark ? "#1a345a" : "#dbeafe"

    // Semantic
    readonly property color ok:        "#17a34a"
    readonly property color okSoft:    dark ? "#113d22" : "#dcfce7"
    readonly property color okText:    dark ? "#4ade80" : "#0d7a37"
    readonly property color warn:      "#d97706"
    readonly property color warnSoft:  dark ? "#3d2a0e" : "#fef3c7"
    readonly property color warnText:  dark ? "#fbbf24" : "#925005"
    readonly property color err:       "#dc2626"
    readonly property color errSoft:   dark ? "#3e1414" : "#fee2e2"
    readonly property color errText:   dark ? "#f87171" : "#991b1b"
    readonly property color info:      "#0a66d6"
    readonly property color infoSoft:  dark ? "#1a345a" : "#dbeafe"
    readonly property color infoText:  dark ? "#93c5fd" : "#0a66d6"

    // Monospace font fallback chain. Must be assigned via `font.families`
    // (the list-valued property) — `font.family` accepts only a single
    // family name and would treat a comma-joined string as one literal
    // lookup that never matches, silently falling back to the platform
    // default proportional font.
    // Monospace font fallback chain.
    //
    // Nerd Font variants come first — they ship with the same glyph metrics
    // as their upstream face plus a patched icon range, so picking them when
    // installed gives users a richer set of glyphs (e.g. for DB-icon-laden
    // log lines) without changing any column alignment. The `* Nerd Font
    // Mono` variants keep icons single-width, which is what we want in a
    // SQL editor / preview where column alignment matters; the non-`Mono`
    // variants follow as second choice. Plain unpatched faces and the
    // generic `monospace` keyword close out the chain.
    readonly property var monoFamilies: [
        "JetBrainsMono Nerd Font Mono",
        "JetBrainsMono Nerd Font",
        "JetBrainsMonoNL Nerd Font Mono",
        "JetBrainsMonoNL Nerd Font",
        "CaskaydiaMono Nerd Font",
        "CaskaydiaCove Nerd Font Mono",
        "CaskaydiaCove Nerd Font",
        "FiraCode Nerd Font Mono",
        "FiraMono Nerd Font Mono",
        "Hack Nerd Font Mono",
        "Hack Nerd Font",
        "JetBrains Mono",
        "Cascadia Mono",
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
    /// their QML value-type adapter — only `Text`/`Label`/`TextArea` do.
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
