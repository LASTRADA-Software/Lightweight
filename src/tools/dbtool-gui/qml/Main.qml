// SPDX-License-Identifier: Apache-2.0
//
// Top-level window. Composes:
//   - Toolbar (logo, title, Simple/Expert tabs, quick actions)
//   - Body: a StackLayout that swaps between `SimpleView` and `ExpertView`
//     driven by `AppController.viewMode` (persisted in QSettings).
//
// Responsiveness: the window resizes to match the active view. Each view
// exposes `preferredViewWidth / Height` + `minimumViewWidth / Height` as
// size hints; on view-mode switch we:
//   1. Remember the user's current (potentially hand-resized) size under
//      the *outgoing* mode.
//   2. Swap the window's `minimumWidth / Height` to the incoming view's
//      hard floor so `setWidth/Height` below can shrink past the old one.
//   3. Resize the window to the remembered size for the incoming mode, or
//      fall back to that view's `preferredView*` hint on first switch.
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

    // Initial geometry uses the persisted value for whichever view the
    // user last left us in; falling back to the matching view's preferred
    // hint. These bindings are one-shot at construction time because the
    // `width` / `height` properties become user-settable once the window
    // is visible (Qt breaks the binding as soon as `setWidth` is called
    // by either the user or our own swap logic below).
    width: AppController.viewMode === "expert"
           ? (settings.expertWidth  > 0 ? settings.expertWidth  : expertView.preferredViewWidth)
           : (settings.simpleWidth  > 0 ? settings.simpleWidth  : simpleView.preferredViewWidth)
    height: AppController.viewMode === "expert"
           ? (settings.expertHeight > 0 ? settings.expertHeight : expertView.preferredViewHeight)
           : (settings.simpleHeight > 0 ? settings.simpleHeight : simpleView.preferredViewHeight)
    minimumWidth:  AppController.viewMode === "expert"
                   ? expertView.minimumViewWidth  : simpleView.minimumViewWidth
    minimumHeight: AppController.viewMode === "expert"
                   ? expertView.minimumViewHeight : simpleView.minimumViewHeight
    visible: true
    title: qsTr("Lightweight Migrations — ")
         + (AppController.currentProfile || qsTr("(no profile)"))
         + (AppController.connected ? qsTr("  ● connected") : "")

    color: Theme.bgPage

    // QSettings-backed persistence of the per-mode window size. Written on
    // `onWidthChanged` / `onHeightChanged` while the mode is stable;
    // swapped out explicitly by the mode-switch handler below.
    Settings {
        id: settings
        category: "ui"
        property int simpleWidth: 0
        property int simpleHeight: 0
        property int expertWidth: 0
        property int expertHeight: 0
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
        if (AppController.viewMode === "expert") {
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
            const incomingW = goingExpert
                ? (settings.expertWidth  > 0 ? settings.expertWidth  : incomingView.preferredViewWidth)
                : (settings.simpleWidth  > 0 ? settings.simpleWidth  : incomingView.preferredViewWidth)
            const incomingH = goingExpert
                ? (settings.expertHeight > 0 ? settings.expertHeight : incomingView.preferredViewHeight)
                : (settings.simpleHeight > 0 ? settings.simpleHeight : incomingView.preferredViewHeight)

            // Relax minimums to the smaller of the two views before
            // resizing, so `setWidth/Height` can move freely in either
            // direction without the window manager clamping against the
            // outgoing mode's (possibly larger) minimum.
            root.minimumWidth  = Math.min(simpleView.minimumViewWidth,  expertView.minimumViewWidth)
            root.minimumHeight = Math.min(simpleView.minimumViewHeight, expertView.minimumViewHeight)

            // Suppress the save handler during the swap — we already saved
            // the outgoing size by tracking it live, and the intermediate
            // values during the resize are noise we don't want persisted.
            _geometryReady = false
            root.width = incomingW
            root.height = incomingH
            root.minimumWidth  = incomingView.minimumViewWidth
            root.minimumHeight = incomingView.minimumViewHeight
            Qt.callLater(function() { _geometryReady = true })
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

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        ToolBar {
            Layout.fillWidth: true
            onRefreshClicked: AppController.connectToProfile()
            onBackupRestoreClicked: backupDialog.open()
            onSettingsClicked: { /* no-op placeholder */ }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            // Index 0 = Simple, 1 = Expert. Keeps the mapping trivial
            // so both the ToolBar TabBar and the QSettings string
            // agree on which index is which.
            currentIndex: AppController.viewMode === "expert" ? 1 : 0

            SimpleView { id: simpleView }
            ExpertView { id: expertView }
        }
    }

    BackupRestoreDialog {
        id: backupDialog
        anchors.centerIn: parent
    }
}
