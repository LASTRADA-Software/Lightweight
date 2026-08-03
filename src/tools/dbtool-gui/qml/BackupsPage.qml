// SPDX-License-Identifier: Apache-2.0
//
// Full-page managed-backups view. Opened from the toolbar's "Backup /
// Restore" button (which first calls `refreshStatus()`) and dismissed via the
// Done button, which flips Main.qml's `showBackups` flag back to false.
//
// Layout is a two-region master-detail (a real GridLayout at the page level,
// so the regions size independently and never overlap):
//
//   LEFT RAIL (configuration + master list)
//     1. Backup folder — where each profile's `<profile>.zip` archive lives,
//        with a link back to Settings and a warnSoft banner when the folder is
//        unusable (`folderProblem`).
//     2. Profiles — one clickable row per profile (name + status badge +
//        Backup / Restore… actions). Clicking a row pins the details region to
//        it; clicking it again unpins (auto-follow the running profile).
//     3. Custom archive — back up / restore the *currently connected* database
//        to an arbitrary archive path via `AppController.backupRunner`.
//
//   RIGHT REGION (detail)
//     BackupDetailPanel — the live per-table view for the selected profile:
//     each table with a progress bar, a state badge, and its latest message.
//     It has its OWN column and gets real width; on a narrow page the whole
//     body collapses to a single stacked column.
//
// The destructive per-profile restore is confirmed through an in-page
// `Dialog` (see `restoreDialog`): it lets the user pick a restore target (any
// profile, with the archive's own profile preselected, or a custom connection
// string) and warns on cross-target restores.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Lightweight.Migrations

Rectangle {
    id: root
    color: Theme.bgPage

    /// Emitted when the user dismisses the Backups page via the Done button.
    /// Main.qml binds this to `showBackups = false`.
    signal done()

    /// Emitted by the folder card's "Change in Settings…" link. Main.qml
    /// routes it to the Settings page (backup-folder card).
    signal openSettings()

    readonly property var _managed: AppController.managedBackups
    // Mirrors the mutual busy guard enforced in C++ (AppController wires each
    // runner's busy probe to the other two): a migration or an ad-hoc
    // backup/restore running against the same databases would tear the archive
    // a managed run is writing, so the managed actions stay disabled until
    // every runner is idle.
    readonly property bool _canRun: _managed.phase === ManagedBackupController.Idle
                                    && _managed.folderProblem === ""
                                    && AppController.runner.phase === MigrationRunner.Idle
                                    && AppController.backupRunner.phase === BackupRunner.Idle

    // Case-insensitive name filter over the profile master-list, driven by the
    // search field above the list. Filtering happens in C++ (see
    // BackupProfileFilterModel) so a large profile set (hundreds of entries)
    // narrows to the wanted profile without scrolling. Selection/auto-follow
    // still resolve against the unfiltered status model, so filtering never
    // changes which profile's details are shown.
    BackupProfileFilterModel {
        id: profileFilter
        sourceModel: root._managed.status
        filterText: profileSearch.text
    }

    // Latest backup-all/backup-profile summary, shown as a success/summary
    // banner under the profile list.
    property string _lastRunSummary: ""
    property bool _lastRunOk: true

    // Master-detail selection. `_pinnedProfile` is set when the user clicks a
    // row; while empty the detail region auto-follows the currently-running
    // profile (`_runningProfile`).
    property string _pinnedProfile: ""
    property string _runningProfile: ""

    // The profile whose details are shown: the pin if set, else the running
    // profile, else the first profile (so the region is never blank when rows
    // exist).
    readonly property string _detailProfile:
        _pinnedProfile !== "" ? _pinnedProfile
        : (_runningProfile !== "" ? _runningProfile
        : (profileList.count > 0 ? _firstProfileName : ""))
    property string _firstProfileName: ""

    // Worker count shown in the detail header. Mirrors BackupConcurrency():
    // min(hardwareThreads, 8), floored at 1. 8 is the ceiling the C++ side
    // uses, shown as the nominal parallelism.
    readonly property int _workerCount: 8

    // The table model + run-state + archive meta for `_detailProfile`, looked
    // up through the status model's index/data API (row found by name role).
    property var _detailTablesModel: null
    property string _detailRunState: "idle"
    property bool _detailArchiveExists: false
    property var _detailArchiveSize: 0
    property var _detailArchiveMtime: undefined

    // Role numbers: Qt.UserRole is 256, so the BackupStatusListModel::Role enum
    // is NameRole=257, ArchiveExistsRole=258, ArchiveSizeRole=259,
    // ArchiveMtimeRole=260, RunStateRole=261, ErrorTextRole=262, TablesRole=263.
    readonly property int _nameRole: 257
    readonly property int _archiveExistsRole: 258
    readonly property int _archiveSizeRole: 259
    readonly property int _archiveMtimeRole: 260
    readonly property int _runStateRole: 261
    readonly property int _tablesRole: 263

    /// Re-derives the detail bindings for the currently selected
    /// `_detailProfile` by scanning the status model (a QAbstractListModel, so
    /// reached via index()/data() rather than []).
    function _refreshDetailBindings() {
        const m = root._managed.status
        // Clear the detail bindings first so a profile that is not found (or a
        // null status model) leaves the panel in its empty state rather than
        // showing stale rows from the previously-selected profile.
        root._detailTablesModel = null
        root._detailRunState = "idle"
        root._detailArchiveExists = false
        root._detailArchiveSize = 0
        root._detailArchiveMtime = undefined
        if (!m || root._detailProfile === "")
            return
        for (let i = 0; i < m.rowCount(); ++i) {
            const idx = m.index(i, 0)
            if (m.data(idx, root._nameRole) === root._detailProfile) {
                root._detailTablesModel = m.data(idx, root._tablesRole)
                root._detailRunState = m.data(idx, root._runStateRole)
                root._detailArchiveExists = m.data(idx, root._archiveExistsRole)
                root._detailArchiveSize = m.data(idx, root._archiveSizeRole)
                root._detailArchiveMtime = m.data(idx, root._archiveMtimeRole)
                return
            }
        }
    }

    // Non-underscore alias so QML generates a valid `onDetailProfileChanged`
    // change handler: the engine rejects the auto-generated handler name for a
    // leading-underscore property (`on_detailProfileChanged`) at load time.
    readonly property string detailProfile: root._detailProfile
    onDetailProfileChanged: _refreshDetailBindings()

    /// Human-readable byte size (e.g. "47.7 MB"). Empty for non-positive
    /// inputs so the caller can omit the size segment entirely.
    /// @param bytes Size in bytes.
    /// @return Formatted string or "".
    function formatSize(bytes) {
        if (!bytes || bytes <= 0)
            return ""
        const units = ["B", "KB", "MB", "GB", "TB"]
        let value = bytes
        let i = 0
        while (value >= 1024 && i < units.length - 1) {
            value /= 1024
            i++
        }
        return (i === 0 ? value.toFixed(0) : value.toFixed(1)) + " " + units[i]
    }

    /// Palette key handed to StatusPill for a row's run/archive state.
    function pillStatus(runState, exists) {
        if (runState === "running" || runState === "queued")
            return "running"
        if (runState === "failed")
            return "unknown"
        if (runState === "ok" || exists)
            return "applied"
        return "empty"
    }

    /// Friendly caption shown inside the pill (decoupled from the palette key).
    function pillLabel(runState, exists) {
        if (runState === "running")
            return qsTr("running")
        if (runState === "queued")
            return qsTr("queued")
        if (runState === "failed")
            return qsTr("failed")
        if (runState === "ok" || exists)
            return qsTr("backed up")
        return qsTr("no archive")
    }

    /// Meta text for the selected profile (error on failure, archive mtime +
    /// size when present, em-dash otherwise).
    function rowMeta(runState, exists, mtime, size, errorText) {
        if (runState === "failed")
            return errorText
        if (runState === "running")
            return qsTr("running…")
        if (runState === "queued")
            return qsTr("queued")
        if (exists) {
            const parts = []
            const when = mtime ? Qt.formatDateTime(mtime, "yyyy-MM-dd HH:mm") : ""
            if (when)
                parts.push(when)
            const s = formatSize(size)
            if (s)
                parts.push(s)
            return parts.join(" · ")
        }
        return "—"
    }

    Connections {
        target: root._managed
        function onFinished(ok, summary) {
            root._lastRunOk = ok
            root._lastRunSummary = summary
            root._runningProfile = ""
            root._refreshDetailBindings()
        }
        function onTableProgress(profile, table, current, total, state, message) {
            // Auto-follow the profile currently reporting progress; the detail
            // region tracks it through `_detailProfile` while no pin is set.
            root._runningProfile = profile
            root._refreshDetailBindings()
        }
    }

    // Page frame: a fixed header row + a body that fills the remaining height.
    // The body itself does NOT scroll as one block — instead the profile
    // master-list (left) and the detail panel (right) each own an internal
    // scrollbar, so scrolling 300 profiles never scrolls the details away and
    // vice-versa (the user's report: "I need to scroll all the way to the top
    // to see the details").
    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 16
        anchors.rightMargin: 16
        anchors.topMargin: 8
        anchors.bottomMargin: 12
        spacing: 14

        // Centre + cap the whole page so it doesn't sprawl on wide monitors,
        // while still filling height for the independent scroll regions.
        readonly property int _pageWidth: Math.min(1180, width)

        // Page header (fixed — never scrolls). The backup folder used to own a
        // whole Card below the fold; it now lives in the header's context row,
        // which keeps the path permanently visible (the user needs to know what
        // "Backup all" will overwrite) while returning that vertical space to
        // the profile list.
        PageHeader {
            Layout.fillWidth: true
            Layout.maximumWidth: parent._pageWidth
            Layout.alignment: Qt.AlignHCenter

            eyebrow: qsTr("Managed archives")
            title: qsTr("Backups")

            contextItems: [
                Glyph {
                    name: "folder"
                    size: 12
                    color: root._managed.folderProblem !== "" ? Theme.warn : Theme.textFaint
                    knockout: Theme.bgWindow
                },
                Label {
                    text: root._managed.effectiveBackupFolder
                    color: Theme.textMuted
                    font: Theme.monoFont(11)
                    elide: Text.ElideMiddle
                    // Bounded so a deep path cannot push the counts and the
                    // Settings link off the header.
                    width: Math.min(implicitWidth, 340)
                    ToolTip.visible: folderHover.hovered && truncated
                    ToolTip.text: root._managed.effectiveBackupFolder
                    HoverHandler { id: folderHover }
                },
                Label {
                    text: "·"
                    color: Theme.textFaint
                    font.pixelSize: 11
                },
                Label {
                    text: root._managed.folderProblem !== ""
                          ? qsTr("not writable")
                          : qsTr("%n profile(s)", "", profileList.count)
                    color: root._managed.folderProblem !== "" ? Theme.warnText : Theme.textMuted
                    font.pixelSize: 11
                },
                Label {
                    text: "·"
                    color: Theme.textFaint
                    font.pixelSize: 11
                },
                Label {
                    text: qsTr("Change in Settings")
                    color: Theme.accent
                    font.pixelSize: 11
                    TapHandler { onTapped: root.openSettings() }
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                }
            ]

            actions: [
                Button {
                    text: qsTr("Backup all")
                    highlighted: true
                    enabled: root._canRun
                    ToolTip.visible: hovered
                    ToolTip.delay: 500
                    ToolTip.timeout: 10000
                    ToolTip.text: qsTr("Write one archive per profile into the managed folder.")
                    onClicked: root._managed.backupAll()
                },
                Button {
                    text: qsTr("Done")
                    onClicked: root.done()
                }
            ]
        }

        // ===== Master-detail body =====
        // A REAL layout (GridLayout) filling the remaining height, so the left
        // rail and the right detail region size independently — the details
        // view gets its own column and never overlaps the profile list.
        // Collapses to a single stacked column when the page is narrow.
        GridLayout {
            id: body
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.maximumWidth: parent._pageWidth
            Layout.alignment: Qt.AlignHCenter
            columns: width > 860 ? 2 : 1
            columnSpacing: 16
            rowSpacing: 14

            // ---------- Left rail: config + master list ----------
            // Fills the body height so the profile ListView in the middle can
            // take the flexible space and scroll internally.
            ColumnLayout {
                id: leftRail
                Layout.alignment: Qt.AlignTop
                Layout.fillHeight: true
                Layout.preferredWidth: body.columns === 2 ? 400 : body.width
                Layout.maximumWidth: body.columns === 2 ? 400 : body.width
                Layout.fillWidth: body.columns === 1
                spacing: 14

                    // --- 1. Unusable-folder banner ---
                    // The folder path itself moved into the page header; only
                    // the *problem* still needs page real-estate, because it
                    // blocks every action below it. Stated once, at the top of
                    // the rail, with the consequence and the fix — rather than
                    // as a footnote inside a card the user has to read past
                    // when nothing is wrong.
                    Rectangle {
                        Layout.fillWidth: true
                        visible: root._managed.folderProblem !== ""
                        implicitHeight: folderProblemRow.implicitHeight + 18
                        color: Theme.warnSoft
                        radius: Theme.radiusSmall

                        RowLayout {
                            id: folderProblemRow
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.leftMargin: 11
                            anchors.rightMargin: 11
                            spacing: 8

                            Glyph {
                                name: "warning"
                                size: 13
                                color: Theme.warn
                                knockout: Theme.warnSoft
                                Layout.alignment: Qt.AlignTop
                                Layout.topMargin: 1
                            }
                            Label {
                                Layout.fillWidth: true
                                text: qsTr("<b>Backups are paused.</b> %1 No archive can be written until this is fixed.")
                                      .arg(root._managed.folderProblem)
                                color: Theme.warnText
                                font.pixelSize: 12
                                wrapMode: Text.WordWrap
                                textFormat: Text.RichText
                            }
                        }
                    }

                    // --- 2. Profiles (master list) ---
                    // NOT a `Card`: a Card is a content-sized Column, but the
                    // profile list must own the left rail's FLEXIBLE height and
                    // scroll internally (300 profiles). So this is a fill-height
                    // bordered panel whose middle ListView takes the slack and
                    // carries its own scrollbar.
                    Rectangle {
                        id: profilesPanel
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.minimumHeight: 160
                        color: Theme.bgPanel
                        border.color: Theme.border
                        radius: Theme.radiusLarge

                        ColumnLayout {
                            anchors.fill: parent
                            spacing: 0

                            // Panel header strip: title, a count chip, and the
                            // search field on ONE row. Previously the title,
                            // a wrapping help sentence, and the search box
                            // stacked into three rows — ~64px of chrome above
                            // a list that is the panel's actual content.
                            RowLayout {
                                Layout.fillWidth: true
                                Layout.margins: 12
                                Layout.bottomMargin: 10
                                spacing: 8

                                Label {
                                    text: qsTr("Profiles")
                                    color: Theme.text
                                    font.pixelSize: 13
                                    font.weight: Font.DemiBold
                                }
                                // Count chip. Reads "3 / 12" while a filter is
                                // active so the search's effect is visible
                                // without a sentence explaining it.
                                Rectangle {
                                    implicitWidth: countLabel.implicitWidth + 14
                                    implicitHeight: countLabel.implicitHeight + 4
                                    radius: Theme.radiusPill
                                    color: Theme.bgSubtle

                                    Label {
                                        id: countLabel
                                        anchors.centerIn: parent
                                        text: profileSearch.text.length > 0
                                              ? profileList.count + " / " + root._managed.status.rowCount()
                                              : "" + profileList.count
                                        color: Theme.textFaint
                                        font: Theme.monoFont(11)
                                    }
                                }
                                Item { Layout.fillWidth: true }

                                // Search field: filters the master-list by
                                // profile name (case-insensitive substring) so
                                // the wanted profile is found without scrolling
                                // a long list.
                                TextField {
                                    id: profileSearch
                                    Layout.preferredWidth: 160
                                    implicitHeight: 26
                                    placeholderText: qsTr("Search profiles…")
                                    font.pixelSize: 12
                                    selectByMouse: true
                                    leftPadding: searchIcon.width + 16
                                    rightPadding: 26

                                    Glyph {
                                        id: searchIcon
                                        name: "search"
                                        size: 12
                                        color: Theme.textFaint
                                        anchors.left: parent.left
                                        anchors.leftMargin: 8
                                        anchors.verticalCenter: parent.verticalCenter
                                    }
                                    // Clear affordance, shown only while filtering.
                                    Glyph {
                                        name: "cross"
                                        size: 11
                                        color: clearHover.hovered ? Theme.text : Theme.textFaint
                                        visible: profileSearch.text.length > 0
                                        anchors.right: parent.right
                                        anchors.rightMargin: 9
                                        anchors.verticalCenter: parent.verticalCenter
                                        TapHandler { onTapped: profileSearch.clear() }
                                        HoverHandler {
                                            id: clearHover
                                            cursorShape: Qt.PointingHandCursor
                                        }
                                    }
                                }
                            }

                            // Hairline under the panel header.
                            Rectangle {
                                Layout.fillWidth: true
                                implicitHeight: 1
                                color: Theme.divider
                            }

                            // Profile list (master). Clicking a row pins the
                            // detail region to it (click again to unpin →
                            // auto-follow). Its own vertical scrollbar keeps the
                            // list scrollable without moving the header above or
                            // the details region to the right.
                            ListView {
                                id: profileList
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                Layout.minimumHeight: 0
                                clip: true
                                model: profileFilter
                                boundsBehavior: Flickable.StopAtBounds

                                ScrollBar.vertical: ScrollBar {
                                    policy: profileList.contentHeight > profileList.height
                                            ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
                                }

                                // Each row now carries its OWN archive metadata
                                // (mtime · size, or the error text) instead of a
                                // single shared line under the list showing only
                                // the selected profile's values — which made
                                // every row look identical and defeated the point
                                // of a comparison list. See ProfileRow.qml.
                                // Each row now carries its OWN archive metadata
                                // (mtime · size, or the error text) rather than
                                // relying on a single shared line under the list
                                // showing only the selected profile's values —
                                // which made every row look identical and
                                // defeated the point of a comparison list.
                                // See ProfileRow.qml.
                                delegate: ProfileRow {
                                    id: profileRow

                                    // Roles are read through `model.<role>`
                                    // rather than as required properties: the
                                    // model's `name` role collides with
                                    // ProfileRow's own `name` property, and a
                                    // required property of that name would
                                    // shadow it — turning `name: name` into a
                                    // self-binding instead of a role read. The
                                    // `model.` form has no such ambiguity and is
                                    // the same pattern MigrationView uses.
                                    readonly property string rowRunState: model.runState
                                    readonly property bool rowArchiveExists: model.archiveExists

                                    width: profileList.width
                                    // Explicit: a ListView positions delegates
                                    // by `height`, and does not adopt an
                                    // `implicitHeight`. Rows are no longer a
                                    // uniform height (the profile name wraps to
                                    // 1-3 lines), so without this every row
                                    // would be drawn at the same default height
                                    // and wrapped names would overlap the row
                                    // below.
                                    height: implicitHeight

                                    name: model.name
                                    pillStatus: root.pillStatus(rowRunState, rowArchiveExists)
                                    pillLabel: root.pillLabel(rowRunState, rowArchiveExists)
                                    meta: root.rowMeta(rowRunState, rowArchiveExists,
                                                       model.archiveMtime, model.archiveSize,
                                                       model.errorText)
                                    metaIsError: rowRunState === "failed"
                                    selected: root._detailProfile === model.name
                                    running: rowRunState === "running"
                                    canBackup: root._canRun
                                    canRestore: rowArchiveExists && root._canRun
                                    // A failed run's primary action is to try
                                    // again, so the button says so.
                                    backupLabel: rowRunState === "failed"
                                                 ? qsTr("Retry") : qsTr("Backup")

                                    onActivated: {
                                        // Toggle: clicking the pinned row unpins (auto-follow).
                                        root._pinnedProfile =
                                            (root._pinnedProfile === model.name) ? "" : model.name
                                    }
                                    onBackupRequested: root._managed.backupProfile(model.name)
                                    onRestoreRequested: restoreDialog.openFor(model.name,
                                                                              model.archiveSize,
                                                                              model.archiveMtime)
                                }

                                // "No matches" placeholder — shown only when a
                                // search string filters every row out, so the
                                // list area is not a confusing blank.
                                Label {
                                    anchors.centerIn: parent
                                    width: parent.width - 24
                                    visible: profileList.count === 0 && profileSearch.text.length > 0
                                    horizontalAlignment: Text.AlignHCenter
                                    wrapMode: Text.WordWrap
                                    text: qsTr("No profiles match “%1”.").arg(profileSearch.text)
                                    color: Theme.textMuted
                                    font.pixelSize: 12
                                }
                            }

                            // NOTE: the shared "archive meta for the selected
                            // profile" line that used to sit here is gone — each
                            // ProfileRow now shows its own mtime · size, so a
                            // single line repeating the selected row's values
                            // was both redundant and the reason the list could
                            // not be scanned.

                            // Last-run summary banner (green on success, red on failure).
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.margins: 12
                                Layout.topMargin: 0
                                visible: root._lastRunSummary !== ""
                                implicitHeight: summaryRow.implicitHeight + 16
                                color: root._lastRunOk ? Theme.okSoft : Theme.errSoft
                                radius: Theme.radiusSmall

                                RowLayout {
                                    id: summaryRow
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.leftMargin: 11
                                    anchors.rightMargin: 11
                                    spacing: 8

                                    Glyph {
                                        name: root._lastRunOk ? "check" : "cross"
                                        size: 12
                                        color: root._lastRunOk ? Theme.okText : Theme.errText
                                        Layout.alignment: Qt.AlignTop
                                        Layout.topMargin: 2
                                    }
                                    Label {
                                        Layout.fillWidth: true
                                        text: root._lastRunSummary
                                        color: root._lastRunOk ? Theme.okText : Theme.errText
                                        font.pixelSize: 12
                                        wrapMode: Text.WordWrap
                                    }
                                }
                            }
                        }
                    }

                    // --- 3. Custom archive (ported from BackupRestoreDialog) ---
                    Card {
                        Layout.fillWidth: true

                        Label {
                            width: parent.width
                            text: qsTr("Custom archive")
                            color: Theme.text
                            font.pixelSize: 14
                            font.weight: Font.DemiBold
                        }
                        Label {
                            width: parent.width
                            text: qsTr("Back up or restore the <b>currently connected</b> database using any archive path.")
                            color: Theme.textMuted
                            font.pixelSize: 11
                            wrapMode: Text.WordWrap
                            textFormat: Text.RichText
                        }

                        // Safe action sits inline with the path field.
                        RowLayout {
                            width: parent.width
                            spacing: 6

                            TextField {
                                id: customPathField
                                Layout.fillWidth: true
                                placeholderText: qsTr("/path/to/backup.zip")
                                font: Theme.monoFont(12)
                            }
                            Button {
                                text: qsTr("Backup")
                                // Gated on the managed controller too: an ad-hoc
                                // backup started during a managed backup-all run
                                // competes for the same database.
                                enabled: customPathField.text.length > 0
                                         && AppController.backupRunner.phase === BackupRunner.Idle
                                         && root._managed.phase === ManagedBackupController.Idle
                                         && AppController.runner.phase === MigrationRunner.Idle
                                ToolTip.visible: hovered
                                ToolTip.delay: 500
                                ToolTip.timeout: 10000
                                ToolTip.text: qsTr("Write a schema + data snapshot to the .zip above.")
                                onClicked: AppController.backupRunner.runBackup(customPathField.text)
                            }
                        }

                        // Destructive action gets its own band, separated from
                        // "Backup" and captioned with what it does.
                        //
                        // These two buttons previously sat 6px apart in the same
                        // row: one writes a file, the other drops and recreates
                        // every table of the connected database. A mis-aimed
                        // click was one pixel-column away from a wiped schema.
                        // The confirmation dialog is still the real guard (see
                        // customRestoreDialog) — this makes the two actions stop
                        // *looking* interchangeable, and states the consequence
                        // before the click rather than after it.
                        Rectangle {
                            width: parent.width
                            implicitHeight: dangerRow.implicitHeight + 16
                            color: Theme.bgSubtle
                            border.color: Theme.border
                            radius: Theme.radiusSmall

                            RowLayout {
                                id: dangerRow
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.leftMargin: 11
                                anchors.rightMargin: 11
                                spacing: 9

                                Glyph {
                                    name: "warning"
                                    size: 13
                                    color: Theme.warn
                                    knockout: Theme.bgSubtle
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: AppController.connected
                                          ? qsTr("<b>Destructive.</b> Restoring drops and recreates every table of %1.")
                                                .arg(AppController.currentProfile)
                                          : qsTr("<b>Destructive.</b> Restoring drops and recreates every table of the connected database.")
                                    color: Theme.textMuted
                                    font.pixelSize: 11
                                    wrapMode: Text.WordWrap
                                    textFormat: Text.RichText
                                }
                                Button {
                                    // Named so the QML test can assert the click
                                    // is routed through the confirmation dialog.
                                    objectName: "customRestoreButton"
                                    text: qsTr("Restore…")
                                    enabled: customPathField.text.length > 0
                                             && AppController.backupRunner.phase === BackupRunner.Idle
                                             && root._managed.phase === ManagedBackupController.Idle
                                             && AppController.runner.phase === MigrationRunner.Idle
                                    // The ellipsis is a promise: this must never
                                    // fire on one click — same rule as the
                                    // per-profile restore.
                                    onClicked: customRestoreDialog.openFor(customPathField.text)
                                }
                            }
                        }
                    }
                }

                // ---------- Right region: live per-table details ----------
                // Fill-height bordered panel (not a content-sized Card) so the
                // detail panel's internal active-table ListView owns the slack
                // and scrolls independently of the profile list on the left.
                Rectangle {
                    id: detailCard
                    Layout.alignment: Qt.AlignTop
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 360
                    color: Theme.bgPanel
                    border.color: Theme.border
                    radius: 10

                    BackupDetailPanel {
                        id: detailPanel
                        anchors.fill: parent
                        anchors.margins: 12
                        profileName: root._detailProfile
                        tablesModel: root._detailTablesModel
                        overallState: root._detailRunState
                        workerCount: root._workerCount
                    }
                }
            }
        }

    // Reactive list of profile names for the restore-target dropdown, kept in
    // store order. `Instantiator` maintains it across profile-file reloads
    // without us hand-rolling a model iteration (QAbstractListModel exposes no
    // count/data to QML directly).
    property var _profileNames: []
    Instantiator {
        model: AppController.profiles
        delegate: QtObject {
            required property string name
        }
        onObjectAdded: function(index, object) {
            const names = root._profileNames.slice()
            names.splice(index, 0, object.name)
            root._profileNames = names
            root._firstProfileName = names.length > 0 ? names[0] : ""
        }
        onObjectRemoved: function(index, object) {
            const names = root._profileNames.slice()
            names.splice(index, 1)
            root._profileNames = names
            root._firstProfileName = names.length > 0 ? names[0] : ""
        }
    }

    // --- Destructive per-profile restore confirmation ---
    Dialog {
        id: restoreDialog
        modal: true
        anchors.centerIn: parent
        width: 460
        padding: 18
        // The profile name is NOT interpolated into the title: names run to
        // ~50 characters, and a fixed-width dialog's title bar has no eliding
        // of its own — a long name simply overflowed the frame. The archive
        // (name included) is spelled out in the "Archive" row of the body,
        // which does elide.
        title: qsTr("Restore this backup?")

        property string sourceProfile: ""
        property var archiveSizeBytes: 0
        property var archiveMtimeValue: undefined

        readonly property string _customEntry: qsTr("Custom connection string…")
        readonly property var _targets: root._profileNames.concat([_customEntry])
        readonly property bool _isCustom: targetCombo.currentIndex === root._profileNames.length
        readonly property string _target: _isCustom ? "" : root._profileNames[targetCombo.currentIndex]

        /// Populates and opens the dialog for a given archive profile.
        /// @param profile Archive/source profile name.
        /// @param sizeBytes Archive size in bytes.
        /// @param mtime Archive modification time.
        function openFor(profile, sizeBytes, mtime) {
            sourceProfile = profile
            archiveSizeBytes = sizeBytes
            archiveMtimeValue = mtime
            customConnField.text = ""
            customSchemaField.text = ""
            const idx = root._profileNames.indexOf(profile)
            targetCombo.currentIndex = idx >= 0 ? idx : 0
            open()
        }

        contentItem: ColumnLayout {
            spacing: 12

            // Destructive warning banner.
            DestructiveWarningBanner {
                Layout.fillWidth: true
            }

            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: 12
                rowSpacing: 7

                Label {
                    text: qsTr("Archive")
                    color: Theme.textMuted
                    font.pixelSize: 12
                }
                // Archive filename, plus its size on a second line.
                //
                // These are two Labels rather than one "<name>.zip · <size>"
                // string: with a ~50-character profile name the single line
                // elided from the right, which threw away the SIZE — the part
                // the user is checking — while keeping the name they already
                // know. Splitting them means the name elides on its own line
                // (mid-string, so both ends stay readable) and the size is
                // always shown in full.
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 1

                    Label {
                        Layout.fillWidth: true
                        text: restoreDialog.sourceProfile + ".zip"
                        color: Theme.text
                        font: Theme.monoFont(12)
                        elide: Text.ElideMiddle
                        ToolTip.visible: archiveHover.hovered && truncated
                        ToolTip.delay: 400
                        ToolTip.text: restoreDialog.sourceProfile + ".zip"
                        HoverHandler { id: archiveHover }
                    }
                    Label {
                        Layout.fillWidth: true
                        visible: text !== ""
                        text: root.formatSize(restoreDialog.archiveSizeBytes)
                        color: Theme.textFaint
                        font: Theme.monoFont(11)
                    }
                }

                Label {
                    text: qsTr("Backed up")
                    color: Theme.textMuted
                    font.pixelSize: 12
                }
                Label {
                    Layout.fillWidth: true
                    text: restoreDialog.archiveMtimeValue
                          ? Qt.formatDateTime(restoreDialog.archiveMtimeValue, "yyyy-MM-dd HH:mm")
                          : "—"
                    color: Theme.text
                    font.pixelSize: 12
                }

                Label {
                    text: qsTr("Restore to")
                    color: Theme.textMuted
                    font.pixelSize: 12
                }
                ComboBox {
                    id: targetCombo
                    Layout.fillWidth: true
                    model: restoreDialog._targets
                    font.pixelSize: 12
                }

                // Custom connection string + schema (only when the custom
                // dropdown entry is selected).
                Label {
                    visible: restoreDialog._isCustom
                    text: qsTr("Connection")
                    color: Theme.textMuted
                    font.pixelSize: 12
                }
                TextField {
                    id: customConnField
                    visible: restoreDialog._isCustom
                    Layout.fillWidth: true
                    placeholderText: qsTr("Driver={…};Server=…;Database=…")
                    font: Theme.monoFont(12)
                }
                Label {
                    visible: restoreDialog._isCustom
                    text: qsTr("Schema")
                    color: Theme.textMuted
                    font.pixelSize: 12
                }
                TextField {
                    id: customSchemaField
                    visible: restoreDialog._isCustom
                    Layout.fillWidth: true
                    placeholderText: qsTr("(server default)")
                    font: Theme.monoFont(12)
                }
                Item { visible: restoreDialog._isCustom; implicitWidth: 1; implicitHeight: 1 }
                Label {
                    visible: restoreDialog._isCustom
                    Layout.fillWidth: true
                    text: qsTr("Optional. Qualifies restored tables, like dbtool's --schema.")
                    color: Theme.textFaint
                    font.pixelSize: 10
                    wrapMode: Text.WordWrap
                }
            }

            // Cross-target warning: restoring into a profile other than the
            // archive's own source.
            Rectangle {
                Layout.fillWidth: true
                visible: !restoreDialog._isCustom && restoreDialog._target !== restoreDialog.sourceProfile
                implicitHeight: xtargetRow.implicitHeight + 18
                color: Theme.warnSoft
                radius: Theme.radiusSmall

                RowLayout {
                    id: xtargetRow
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.leftMargin: 11
                    anchors.rightMargin: 11
                    spacing: 8

                    Glyph {
                        name: "warning"
                        size: 13
                        color: Theme.warn
                        knockout: Theme.warnSoft
                        Layout.alignment: Qt.AlignTop
                        Layout.topMargin: 1
                    }
                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Target <b>%1</b> differs from the archive's source profile <b>%2</b> — its current data will be replaced with %2's snapshot.")
                              .arg(restoreDialog._target).arg(restoreDialog.sourceProfile)
                        color: Theme.warnText
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                        textFormat: Text.RichText
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 2
                spacing: 8

                Item { Layout.fillWidth: true }

                Button {
                    text: qsTr("Cancel")
                    onClicked: restoreDialog.close()
                }
                Button {
                    highlighted: true
                    enabled: !restoreDialog._isCustom || customConnField.text.length > 0
                    // Deliberately does NOT interpolate the target name. It used
                    // to read "Restore into <profile>", which with a ~50-char
                    // name produced a button wider than the 460px dialog. The
                    // target is already stated — and selected — in the "Restore
                    // to" dropdown two rows above, so the button need not
                    // restate it.
                    text: qsTr("Restore")
                    onClicked: {
                        if (restoreDialog._isCustom) {
                            root._managed.restoreArchiveToConnectionString(
                                restoreDialog.sourceProfile,
                                customConnField.text,
                                customSchemaField.text)
                        } else {
                            root._managed.restoreArchive(restoreDialog.sourceProfile,
                                                         restoreDialog._target)
                        }
                        restoreDialog.close()
                    }
                }
            }
        }
    }

    // --- Destructive custom-archive restore confirmation ---
    //
    // The "Restore…" button next to the custom archive path used to call
    // `BackupRunner.runRestore` directly: one click, no confirmation, on a
    // surface that drops and recreates every table of the *connected*
    // database. It sits a few pixels from "Backup" and its ellipsis promises a
    // dialog, so it gets one — with the same warning banner as the per-profile
    // restore above.
    Dialog {
        id: customRestoreDialog
        objectName: "customRestoreDialog"
        modal: true
        anchors.centerIn: parent
        width: 460
        padding: 18
        title: qsTr("Restore into the connected database?")

        property string archivePath: ""

        /// Populates and opens the dialog for a custom archive path.
        /// @param path Archive file the user typed into the path field.
        function openFor(path) {
            archivePath = path
            open()
        }

        contentItem: ColumnLayout {
            spacing: 12

            DestructiveWarningBanner {
                Layout.fillWidth: true
            }

            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: 12
                rowSpacing: 7

                Label {
                    text: qsTr("Archive")
                    color: Theme.textMuted
                    font.pixelSize: 12
                }
                Label {
                    Layout.fillWidth: true
                    text: customRestoreDialog.archivePath
                    color: Theme.text
                    font: Theme.monoFont(12)
                    elide: Text.ElideMiddle
                }

                Label {
                    text: qsTr("Restore to")
                    color: Theme.textMuted
                    font.pixelSize: 12
                }
                Label {
                    Layout.fillWidth: true
                    text: AppController.connected
                          ? qsTr("the currently connected database (%1)").arg(AppController.currentProfile)
                          : qsTr("the currently connected database")
                    color: Theme.text
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 2
                spacing: 8

                Item { Layout.fillWidth: true }

                Button {
                    text: qsTr("Cancel")
                    onClicked: customRestoreDialog.close()
                }
                Button {
                    highlighted: true
                    text: qsTr("Restore")
                    onClicked: {
                        AppController.backupRunner.runRestore(customRestoreDialog.archivePath)
                        customRestoreDialog.close()
                    }
                }
            }
        }
    }
}
