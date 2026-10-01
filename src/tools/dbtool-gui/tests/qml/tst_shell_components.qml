// SPDX-License-Identifier: Apache-2.0
//
// Behaviour tests for the Lastrada shell pieces that replaced the old top
// ToolBar: the dark NavRail, the Simple | Expert SegmentedControl, the
// numbered StepSection and the kit LsButton.
//
// These are interaction and geometry checks rather than "does it load"
// checks: the rail must report the destination it was clicked on and shrink
// to the narrow width when collapsed, the segmented control must only emit
// for a *different* option, and a collapsed step must give its body's height
// back to the layout.

import QtQuick
import QtTest
import Lightweight.Migrations

TestCase {
    id: root
    name: "ShellComponents"
    when: windowShown
    // TestCase is invisible by default, and an invisible parent makes every
    // created child invisible too — which silently drops all mouse input.
    visible: true
    width: 600
    height: 600

    readonly property var railItems: [
        { page: "migrations", label: "Migrations", glyph: "layers", badge: 3, badgeKind: "warn" },
        { page: "backups", label: "Backups", glyph: "archive-outline" },
    ]
    readonly property var pinned: [
        { page: "settings", label: "Settings", glyph: "sliders" },
    ]

    Component {
        id: railComponent
        NavRail {
            height: 500
            width: implicitWidth
            items: root.railItems
            pinnedItems: root.pinned
            currentPage: "migrations"
        }
    }

    Component {
        id: segComponent
        SegmentedControl {
            model: [ { value: "simple", label: "Simple" }, { value: "expert", label: "Expert" } ]
            current: "simple"
        }
    }

    Component {
        id: stepComponent
        StepSection {
            width: 400
            number: 2
            title: "Review"
            Rectangle { width: 380; height: 120 }
        }
    }

    Component {
        id: buttonComponent
        LsButton { text: "Apply" }
    }

    SignalSpy { id: navigateSpy; signalName: "navigate" }
    SignalSpy { id: activatedSpy; signalName: "activated" }
    SignalSpy { id: clickedSpy; signalName: "clicked" }

    /// Finds the first descendant whose `entry.page` equals `page`.
    function findRailItem(item, page) {
        for (let i = 0; i < item.children.length; ++i) {
            const child = item.children[i]
            if (child.entry !== undefined && child.entry.page === page)
                return child
            const found = findRailItem(child, page)
            if (found)
                return found
        }
        return null
    }

    function test_rail_widths_follow_collapsed_state() {
        const rail = createTemporaryObject(railComponent, root)
        verify(rail !== null)
        compare(rail.implicitWidth, Theme.railW)
        rail.collapsed = true
        // The width animates; wait for it to settle on the narrow rail.
        tryCompare(rail, "implicitWidth", Theme.railNarrowW)
    }

    function test_rail_click_reports_destination() {
        const rail = createTemporaryObject(railComponent, root)
        navigateSpy.target = rail
        navigateSpy.clear()
        waitForRendering(rail)
        const backups = findRailItem(rail, "backups")
        verify(backups !== null)
        mouseClick(backups)
        compare(navigateSpy.count, 1)
        compare(navigateSpy.signalArguments[0][0], "backups")
    }

    function test_rail_hides_invisible_entries() {
        const rail = createTemporaryObject(railComponent, root, {
            pinnedItems: [ { page: "settings", label: "Settings", glyph: "sliders", visible: false } ]
        })
        wait(0)
        const settings = findRailItem(rail, "settings")
        verify(settings !== null)
        verify(!settings.visible)
    }

    function test_segmented_control_emits_only_for_other_option() {
        const seg = createTemporaryObject(segComponent, root)
        activatedSpy.target = seg
        activatedSpy.clear()
        waitForRendering(seg)
        // Click the left (active) chip: no signal.
        mouseClick(seg, 10, seg.height / 2)
        compare(activatedSpy.count, 0)
        // Click the right chip: switches to expert.
        mouseClick(seg, seg.width - 10, seg.height / 2)
        compare(activatedSpy.count, 1)
        compare(activatedSpy.signalArguments[0][0], "expert")
    }

    function test_collapsed_step_drops_body_height() {
        const step = createTemporaryObject(stepComponent, root)
        wait(0)
        const expanded = step.implicitHeight
        verify(expanded > 120)
        step.collapsed = true
        wait(0)
        compare(step.implicitHeight, 42)
    }

    function test_busy_button_swallows_clicks() {
        const button = createTemporaryObject(buttonComponent, root, { variant: "primary", size: "lg" })
        clickedSpy.target = button
        clickedSpy.clear()
        waitForRendering(button)
        compare(button.implicitHeight, Theme.ctlLg)
        mouseClick(button)
        compare(clickedSpy.count, 1)
        button.busy = true
        mouseClick(button)
        compare(clickedSpy.count, 1)
    }
}
