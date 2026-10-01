// SPDX-License-Identifier: Apache-2.0
//
// Verifies the `Theme` singleton resolves and exposes the Lastrada design
// tokens every other QML component binds against. A regression here (e.g.
// Theme.qml not flagged as a singleton in qmldir, or a token renamed away
// from its `_tokens.css` name) would leave components falling back to
// `undefined` palette values at runtime.

import QtQuick
import QtTest
import Lightweight.Migrations

TestCase {
    name: "ThemeSingleton"

    function test_theme_singleton_is_reachable() {
        verify(typeof Theme !== "undefined")
    }

    function test_theme_exposes_core_palette_roles() {
        verify(Theme.clrBase)
        verify(Theme.clrCard)
        verify(Theme.clrContainerLow)
        verify(Theme.clrSidebarBg)
    }

    // The brand colour is the design system's identity; pin it so an
    // accidental edit to the token table shows up as a test failure.
    function test_theme_brand_is_lastrada_red() {
        compare(Theme.clrPrimary.toString(), "#a21928")
    }

    // The palette is light-only by design: the old light/dark switch is
    // gone, and nothing should bind against it any more.
    function test_theme_has_no_dark_flag() {
        compare(typeof Theme.dark, "undefined")
    }

    function test_theme_exposes_shell_metrics() {
        compare(Theme.railW, 216)
        compare(Theme.railNarrowW, 60)
        compare(Theme.topH, 52)
        compare(Theme.statusH, 26)
        verify(Theme.ctlSm < Theme.ctlMd && Theme.ctlMd < Theme.ctlLg)
    }
}
