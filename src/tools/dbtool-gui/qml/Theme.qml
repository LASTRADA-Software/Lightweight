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

    readonly property string monoFont: "JetBrains Mono, Consolas, monospace"
}
