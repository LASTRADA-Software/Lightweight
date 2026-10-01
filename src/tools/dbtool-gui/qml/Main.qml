// SPDX-License-Identifier: Apache-2.0
//
// Top-level window, laid out as the Lastrada shell:
//   - NavRail (left): Migrations / Backups destinations, Settings pinned at
//     the bottom, collapse toggle. Replaces the old top ToolBar.
//   - Content (right): a StackLayout of pages, each opening with its own
//     `PageHeader`; the Migrations page swaps between `SimpleView` and
//     `ExpertView` driven by `AppController.viewMode` (persisted in QSettings).
//   - StatusBar along the bottom of the content area.
//
// Responsiveness: the window resizes to match the active view. Each view
// exposes `preferredViewWidth / Height` + `minimumViewWidth / Height` as
// size hints for its *content*; the shell adds the rail, header and status
// bar around them. On view-mode switch we:
//   1. Remember the user's current (potentially hand-resized) size under
//      the *outgoing* mode.
//   2. Swap the window's `minimumWidth / Height` to the incoming view's
//      hard floor so `setWidth/Height` below can shrink past the old one.
//   3. Resize the window to the remembered size for the incoming mode, or
//      fall back to that view's preferred hint on first switch.
// Hand-resizes are tracked live via `onWidthChanged / onHeightChanged` so
// the next toggle restores whatever the user last dragged to, not the
// original hint. All of this is QSettings-persisted so the window reopens
// at the same size + same mode it was closed at.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore
import Lightweight.Migrations

ApplicationWindow {
    id: root

    readonly property bool expert: AppController.viewMode === "expert"

    // Rail state is remembered per view mode: Simple starts collapsed so its
    // narrow window keeps its content width, Expert starts expanded.
    readonly property bool railCollapsed: expert ? settings.railCollapsedExpert : settings.railCollapsedSimple

    // Chrome the shell adds around a view's content hints.
    readonly property int chromeHeight: Theme.topH + Theme.statusH

    function preferredWidthFor(view, collapsed) {
        return view.preferredViewWidth + (collapsed ? Theme.railNarrowW : Theme.railW)
    }
    function minimumWidthFor(view, collapsed) {
        return view.minimumViewWidth + (collapsed ? Theme.railNarrowW : Theme.railW)
    }

    // Initial geometry uses the persisted value for whichever view the
    // user last left us in; falling back to the matching view's preferred
    // hint. These bindings are one-shot at construction time because the
    // `width` / `height` properties become user-settable once the window
    // is visible (Qt breaks the binding as soon as `setWidth` is called
    // by either the user or our own swap logic below).
    width: expert
           ? (settings.expertWidth  > 0 ? settings.expertWidth  : preferredWidthFor(expertView, settings.railCollapsedExpert))
           : (settings.simpleWidth  > 0 ? settings.simpleWidth  : preferredWidthFor(simpleView, settings.railCollapsedSimple))
    height: expert
           ? (settings.expertHeight > 0 ? settings.expertHeight : expertView.preferredViewHeight + chromeHeight)
           : (settings.simpleHeight > 0 ? settings.simpleHeight : simpleView.preferredViewHeight + chromeHeight)
    minimumWidth:  minimumWidthFor(expert ? expertView : simpleView, railCollapsed)
    minimumHeight: (expert ? expertView.minimumViewHeight : simpleView.minimumViewHeight) + chromeHeight
    visible: true
    title: qsTr("Lightweight dbtool — ")
         + (AppController.currentProfile || qsTr("(no profile)"))
         + (AppController.connected ? qsTr("  ● connected") : "")

    color: Theme.clrBase

    // QSettings-backed persistence of the per-mode window size and rail
    // state. Sizes are written on `onWidthChanged` / `onHeightChanged`
    // while the mode is stable; swapped out explicitly by the mode-switch
    // handler below.
    Settings {
        id: settings
        category: "ui"
        property int simpleWidth: 0
        property int simpleHeight: 0
        property int expertWidth: 0
        property int expertHeight: 0
        property bool railCollapsedSimple: true
        property bool railCollapsedExpert: false
    }

    // Track live user resizes so the next toggle restores the last
    // dragged-to size, not the original preferred hint. We only persist
    // *after* the window is visible and reasonably tall — transient values
    // during the initial geometry computation would otherwise clobber the
    // persisted size with zero / partial values.
    property bool _geometryReady: false
    Component.onCompleted: _geometryReady = true

    onWidthChanged: saveCurrentGeometry()
    onHeightChanged: saveCurrentGeometry()

    function saveCurrentGeometry() {
        if (!_geometryReady) return
        if (expert) {
            settings.expertWidth = width
            settings.expertHeight = height
        } else {
            settings.simpleWidth = width
            settings.simpleHeight = height
        }
    }

    // Swap window geometry on view-mode change. Qt has no native "re-adopt
    // my current declarative size binding" slot, so we drive the resize
    // imperatively. The sequence matters: raise both min and current size
    // *before* lowering the opposing mins so we never go below the active
    // minimum mid-resize (which Qt silently clamps against).
    Connections {
        target: AppController
        function onViewModeChanged() {
            const goingExpert = AppController.viewMode === "expert"
            const incomingView = goingExpert ? expertView : simpleView
            const collapsed = goingExpert ? settings.railCollapsedExpert : settings.railCollapsedSimple
            const incomingW = goingExpert
                ? (settings.expertWidth  > 0 ? settings.expertWidth  : preferredWidthFor(incomingView, collapsed))
                : (settings.simpleWidth  > 0 ? settings.simpleWidth  : preferredWidthFor(incomingView, collapsed))
            const incomingH = goingExpert
                ? (settings.expertHeight > 0 ? settings.expertHeight : incomingView.preferredViewHeight + chromeHeight)
                : (settings.simpleHeight > 0 ? settings.simpleHeight : incomingView.preferredViewHeight + chromeHeight)

            // Relax minimums to the smaller of the two views before
            // resizing, so `setWidth/Height` can move freely in either
            // direction without the window manager clamping against the
            // outgoing mode's (possibly larger) minimum.
            root.minimumWidth  = Math.min(minimumWidthFor(simpleView, true), minimumWidthFor(expertView, true))
            root.minimumHeight = Math.min(simpleView.minimumViewHeight, expertView.minimumViewHeight) + chromeHeight

            // Suppress the save handler during the swap — we already saved
            // the outgoing size by tracking it live, and the intermediate
            // values during the resize are noise we don't want persisted.
            _geometryReady = false
            root.width = incomingW
            root.height = incomingH
            root.minimumWidth  = minimumWidthFor(incomingView, collapsed)
            root.minimumHeight = incomingView.minimumViewHeight + chromeHeight
            Qt.callLater(function() { _geometryReady = true })

            // Settings is an Expert-only destination; leaving Expert while
            // on it returns to the migrations page.
            if (!goingExpert && root.currentPage === "settings")
                root.currentPage = "migrations"
        }
    }

    Shortcut {
        sequence: "Ctrl+Q"
        context: Qt.ApplicationShortcut
        autoRepeat: false
        onActivated: {
            console.log("[Shortcut] Ctrl+Q activated, quitting");
            Qt.quit();
        }
    }

    // Transient — not persisted. The app always opens on Migrations.
    property string currentPage: "migrations"

    /// Switches the content area to `page` ("migrations" | "backups" |
    /// "settings"); unknown pages are ignored.
    function navigate(page) {
        if (page === "backups")
            AppController.managedBackups.refreshStatus()
        if (page === "migrations" || page === "backups" || page === "settings")
            currentPage = page
    }

    // Rail destinations. Data-driven so adding a page is one entry here plus
    // one child of the StackLayout below.
    readonly property var railItems: [
        { page: "migrations", label: qsTr("Migrations"), glyph: "layers",
          badge: AppController.pendingCount, badgeKind: "warn" },
        { page: "backups", label: qsTr("Backups"), glyph: "archive-outline",
          badge: AppController.managedBackups.phase !== ManagedBackupController.Idle ? 1 : 0 },
    ]
    readonly property var railPinnedItems: [
        // Settings host the profile-file path, plugins directory and backup
        // folder. Simple-view users should not be tweaking those knobs, so
        // the destination only exists in Expert view.
        { page: "settings", label: qsTr("Settings"), glyph: "sliders", visible: root.expert },
    ]

    RowLayout {
        anchors.fill: parent
        spacing: 0

        NavRail {
            id: rail
            Layout.fillHeight: true
            Layout.preferredWidth: implicitWidth
            items: root.railItems
            pinnedItems: root.railPinnedItems
            currentPage: root.currentPage
            collapsed: root.railCollapsed
            versionText: qsTr("Lightweight migrations")
            onNavigate: (page) => root.navigate(page)
            onToggleCollapsed: {
                if (root.expert)
                    settings.railCollapsedExpert = !settings.railCollapsedExpert
                else
                    settings.railCollapsedSimple = !settings.railCollapsedSimple
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            StackLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                // Index 0 = Migrations, 1 = Settings, 2 = Backups.
                currentIndex: root.currentPage === "settings" ? 1
                            : root.currentPage === "backups" ? 2 : 0

                // --- Migrations: header + Simple/Expert body ---
                ColumnLayout {
                    spacing: 0

                    PageHeader {
                        Layout.fillWidth: true
                        crumbs: [ AppController.currentProfile || qsTr("No profile"), qsTr("Migrations") ]
                        title: root.expert ? qsTr("Migrations") : qsTr("Update database")
                        contextItems: [
                            ConnectionChip { visible: root.expert }
                        ]
                        actions: [
                            SegmentedControl {
                                size: root.expert ? "md" : "sm"
                                model: [ { value: "simple", label: qsTr("Simple") },
                                         { value: "expert", label: qsTr("Expert") } ]
                                current: AppController.viewMode
                                onActivated: (value) => AppController.setViewMode(value)
                            },
                            LsButton {
                                text: qsTr("Refresh")
                                glyph: "refresh"
                                iconOnly: !root.expert
                                ToolTip.visible: hovered
                                ToolTip.delay: 500
                                ToolTip.text: qsTr("Re-read applied migrations and re-scan the plugin directory.")
                                onClicked: AppController.connectToProfile()
                            }
                        ]
                    }

                    StackLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        currentIndex: root.expert ? 1 : 0

                        SimpleView {
                            id: simpleView
                            onOpenBackups: root.navigate("backups")
                        }
                        ExpertView { id: expertView }
                    }
                }

                SettingsPage {
                    id: settingsPage
                    onDone: root.currentPage = "migrations"
                }
                BackupsPage {
                    id: backupsPage
                    onDone: root.currentPage = "migrations"
                    onOpenSettings: root.currentPage = "settings"
                }
            }

            StatusBar {
                Layout.fillWidth: true
                showPluginsDir: root.expert
            }
        }
    }
}
