// SPDX-License-Identifier: Apache-2.0
//
// The shell around the pages: the navigation rail switches pages, the Simple | Expert switch swaps the
// migrations view and is remembered, the log panel hides and shows, and the window wears the theme.

import QtQuick
import QtTest
import Lightweight.Migrations
import DbtoolGuiWorkflow

WorkflowTestCase {
    name: "WorkflowShell"

    /// Opens the window on a plugins directory of its own, so the pages have something to show.
    function openShell(viewMode) {
        WorkflowFixture.setSetting("config/pluginsDir", WorkflowFixture.newPluginsDir(["workflow"]))
        WorkflowFixture.setSetting("ui/viewMode", viewMode)
        openMain()
        tryCompare(AppController, "busy", false, 15000)
    }

    function test_navigation_rail_switches_pages() {
        openShell("expert")
        compare(main.currentPage, "migrations")
        compare(findRequired("expertView").visible, true)
        compare(findRequired("settingsPage").visible, false)
        compare(findRequired("backupsPage").visible, false)

        clickControl("navItem_settings")
        compare(main.currentPage, "settings")
        compare(findRequired("settingsPage").visible, true)
        compare(findRequired("expertView").visible, false)
        compare(findRequired("backupsPage").visible, false)

        clickControl("navItem_backups")
        compare(main.currentPage, "backups")
        compare(findRequired("backupsPage").visible, true)
        compare(findRequired("settingsPage").visible, false)
        compare(findRequired("expertView").visible, false)

        clickControl("navItem_migrations")
        compare(main.currentPage, "migrations")
        compare(findRequired("expertView").visible, true)
        compare(findRequired("backupsPage").visible, false)
    }

    function test_simple_expert_switch_swaps_the_view_and_persists() {
        openShell("simple")
        compare(findRequired("simpleView").visible, true)
        compare(findRequired("expertView").visible, false)

        clickControl("viewModeControl_expert")
        compare(AppController.viewMode, "expert")
        compare(findRequired("expertView").visible, true)
        compare(findRequired("simpleView").visible, false)
        compare(WorkflowFixture.setting("ui/viewMode"), "expert")

        clickControl("viewModeControl_simple")
        compare(findRequired("simpleView").visible, true)
        compare(findRequired("expertView").visible, false)
        compare(WorkflowFixture.setting("ui/viewMode"), "simple")
    }

    function test_log_toggle_hides_and_shows_the_log() {
        openShell("expert")
        compare(findRequired("logPanel").visible, true)

        clickControl("logToggleButton")
        compare(findRequired("logPanel").visible, false)
        compare(AppController.logVisible, false)
        compare(WorkflowFixture.setting("ui/logVisible"), false)

        clickControl("logToggleButton")
        compare(findRequired("logPanel").visible, true)
        compare(WorkflowFixture.setting("ui/logVisible"), true)
    }

    function test_theme_tokens_are_applied() {
        openShell("expert")
        compare(main.color, Theme.clrBase)
        // The Fusion style is set by the harness's main(), so a check of it would test the harness, not the window.
    }
}
