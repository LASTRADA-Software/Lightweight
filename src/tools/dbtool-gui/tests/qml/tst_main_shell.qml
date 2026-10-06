// SPDX-License-Identifier: Apache-2.0
//
// Shell-level test: the real Main window, not the rail in isolation. It pins the
// behaviour that makes Settings reachable from the Simple view (GitHub #659): the rail
// offers Settings in Simple mode, and switching view mode while on it keeps the user there.

import QtQuick
import QtTest
import Lightweight.Migrations

TestCase {
    id: root
    name: "MainShell"
    when: windowShown
    visible: true
    width: 400
    height: 300

    Component { id: mainComponent; Main {} }

    function test_settings_is_offered_in_simple_mode_and_survives_a_mode_switch() {
        const originalMode = AppController.viewMode
        AppController.setViewMode("simple")
        const main = createTemporaryObject(mainComponent, root)
        verify(main !== null, "the Main window must instantiate")

        const rail = findChild(main, "navRail")
        verify(rail !== null)
        compare(rail.pinnedItems.length, 1)
        compare(rail.pinnedItems[0].page, "settings")
        verify(rail.pinnedItems[0].visible === undefined || rail.pinnedItems[0].visible === true,
               "Settings must not be hidden in Simple view")

        main.navigate("settings")
        compare(main.currentPage, "settings")

        AppController.setViewMode("expert")
        compare(main.currentPage, "settings", "switching to Expert keeps the user on Settings")
        AppController.setViewMode("simple")
        compare(main.currentPage, "settings", "switching back to Simple keeps the user on Settings too")

        AppController.setViewMode(originalMode)
    }
}
