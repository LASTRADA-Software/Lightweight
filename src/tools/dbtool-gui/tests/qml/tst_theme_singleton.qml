// SPDX-License-Identifier: Apache-2.0
//
// Verifies the `Theme` singleton resolves and exposes the colour roles every
// other QML component binds against. A regression here (e.g. Theme.qml not
// flagged as a singleton in qmldir) would leave every component falling back
// to red `undefined` palette values at runtime.

import QtQuick
import QtTest
import Lightweight.Migrations

TestCase {
    name: "ThemeSingleton"

    function test_theme_singleton_is_reachable() {
        verify(typeof Theme !== "undefined")
    }

    function test_theme_exposes_core_palette_roles() {
        verify(Theme.bgPage)
        verify(Theme.bgWindow)
        verify(Theme.bgPanel)
        verify(Theme.bgTerminal)
    }

    function test_theme_dark_flag_is_boolean() {
        compare(typeof Theme.dark, "boolean")
    }
}
