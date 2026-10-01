// SPDX-License-Identifier: Apache-2.0
//
// Managed-backups page — a destination of Main.qml's navigation rail (no
// longer an overlay with a Done button). Built on the Lastrada kit:
//
//   PageHeader — "dbtool › Backups", a running pill while a managed run is in
//   flight, and the page's one primary action, "Back up all profiles".
//
//   LEFT COLUMN (centred, max ~960 px)
//     1. Backup folder — the read-only `dt-path` box naming where each
//        profile's `<profile>.zip` lives, with a ghost "Change in Settings"
//        button (`openSettings`) and a warning banner when the folder is
//        unusable (`folderProblem`), because that blocks every action below.
//     2. Profiles — the kit table (`dt-br`): PROFILE / STATUS / LAST BACKUP /
//        SIZE / actions, one ProfileRow per profile, a name filter in the
//        panel header, and the last-run summary in the table footer. The table
//        owns the column's flexible height and scrolls internally (hundreds of
//        profiles). Clicking a row pins the detail region to it; clicking it
//        again unpins (auto-follow the running profile).
//     3. Custom archive — back up / restore the *currently connected* database
//        to an arbitrary archive path via `AppController.backupRunner`.
//
//   DETAIL REGION
//     BackupDetailPanel — the live per-table view for the selected profile.
//     Beside the table when the page is wide enough, below it otherwise; it
//     scrolls independently of the profile table either way.
//
// Both restores are confirmed through a kit dialog (`dt-dlg`): what is lost,
// the archive's provenance, and — for the per-profile restore — a typed
// confirmation: the red Restore button stays disabled until the name of the
// database being overwritten is typed exactly.

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Lightweight.Migrations

Rectangle {
    id: root
    color: Theme.clrBase

    /// Kept for Main.qml's existing connection. The page is a rail
    /// destination now and has no Done button of its own, so nothing on the
    /// page emits it.
    signal done()

    /// Emitted by the folder card's "Change in Settings" button. Main.qml
    /// routes it to the Settings page (backup-folder card).
    signal openSettings()

    readonly property var _managed: AppController.managedBackups
    readonly property bool _managedRunning: _managed.phase !== ManagedBackupController.Idle
    // Mirrors the mutual busy guard enforced in C++ (AppController wires each
    // runner's busy probe to the other two): a migration or an ad-hoc
    // backup/restore running against the same databases would tear the archive
    // a managed run is writing, so the managed actions stay disabled until
    // every runner is idle.
    readonly property bool _canRun: _managed.phase === ManagedBackupController.Idle
                                    && _managed.folderProblem === ""
                                    && AppController.runner.phase === MigrationRunner.Idle
                                    && AppController.backupRunner.phase === BackupRunner.Idle
    // Same guard for the custom-archive actions, which do not need the
    // managed folder.
    readonly property bool _canRunCustom: AppController.backupRunner.phase === BackupRunner.Idle
                                          && _managed.phase === ManagedBackupController.Idle
                                          && AppController.runner.phase === MigrationRunner.Idle

    // Case-insensitive name filter over the profile table, driven by the
    // search field in the panel header. Filtering happens in C++ (see
    // BackupProfileFilterModel) so a large profile set (hundreds of entries)
    // narrows to the wanted profile without scrolling. Selection/auto-follow
    // still resolve against the unfiltered status model, so filtering never
    // changes which profile's details are shown.
    BackupProfileFilterModel {
        id: profileFilter
        sourceModel: root._managed.status
        filterText: profileSearch.text
    }

    // Latest backup-all/backup-profile summary, shown in the table footer.
    property string _lastRunSummary: ""
    property bool _lastRunOk: true
    property var _lastRunAt: undefined

    // Master-detail selection. `_pinnedProfile` is set when the user clicks a
    // row; while empty the detail region auto-follows the currently-running
    // profile (`_runningProfile`).
    property string _pinnedProfile: ""
    property string _runningProfile: ""
    // Most recent table the running profile reported, for the row's
    // "Table x of y · name" caption. Tables run in parallel, so this is "a
    // table in flight", not "the" table.
    property string _runningTable: ""

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

    // The table model + run-state for `_detailProfile`, looked up through the
    // status model's index/data API (row found by name role).
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

    // Fixed column widths of the profile table (STATUS, LAST BACKUP, SIZE,
    // actions) — must match ProfileRow.columnWidths so the header labels sit
    // over their cells.
    readonly property var _columnWidths: [124, 150, 70, 176]

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

    /// Archive time as the table shows it: "Today 14:02", "Yesterday 09:15",
    /// else the full date. Empty when unknown.
    /// @param when Archive modification time (Date) or undefined.
    /// @return Formatted string or "".
    function formatWhen(when) {
        if (!when)
            return ""
        const d = new Date(when)
        if (isNaN(d.getTime()))
            return ""
        const today = new Date()
        const yesterday = new Date(today.getFullYear(), today.getMonth(), today.getDate() - 1)
        const time = Qt.formatDateTime(d, "HH:mm")
        if (d.toDateString() === today.toDateString())
            return qsTr("Today %1").arg(time)
        if (d.toDateString() === yesterday.toDateString())
            return qsTr("Yesterday %1").arg(time)
        return Qt.formatDateTime(d, "yyyy-MM-dd HH:mm")
    }

    /// Masks password values in a connection string before it is shown on
    /// screen (the table shows every profile's string at once).
    /// @param cs ODBC connection string.
    /// @return The string with PWD/Password values replaced by bullets.
    function redact(cs) {
        return (cs || "").replace(/((?:^|;)\s*(?:pwd|password)\s*=)[^;]*/gi, "$1•••")
    }

    /// Shortens `s` to `n` characters with a trailing ellipsis. Used where a
    /// ~50-character profile name would otherwise widen a button or a pill.
    function shorten(s, n) {
        return s.length > n ? s.substring(0, n - 1) + "…" : s
    }

    /// Palette key handed to StatusPill for a row's run/archive state.
    function pillStatus(runState, exists) {
        if (runState === "running")
            return "running"
        if (runState === "queued")
            return "empty"
        if (runState === "failed")
            return "unknown"
        if (runState === "ok" || exists)
            return "applied"
        return "empty"
    }

    /// Friendly caption shown inside the pill (decoupled from the palette key).
    function pillLabel(runState, exists) {
        if (runState === "running")
            return qsTr("Running")
        if (runState === "queued")
            return qsTr("Queued")
        if (runState === "failed")
            return qsTr("Failed")
        if (runState === "ok" || exists)
            return qsTr("Backed up")
        return qsTr("No archive")
    }

    /// LAST BACKUP cell text: the failure reason on a failed row, the progress
    /// caption while running, the archive time when one exists, else "Never".
    function rowMeta(runState, exists, mtime, errorText, tables) {
        if (runState === "failed")
            return errorText !== "" ? errorText : qsTr("Backup failed.")
        if (runState === "running") {
            const total = tables ? tables.totalCount : 0
            if (total <= 0)
                return qsTr("Preparing…")
            const caption = qsTr("Table %1 of %2").arg(Math.min(tables.doneCount + 1, total)).arg(total)
            return root._runningTable !== "" ? caption + " · " + root._runningTable : caption
        }
        if (runState === "queued")
            return qsTr("Waiting…")
        if (exists)
            return root.formatWhen(mtime) || qsTr("On disk")
        return qsTr("Never")
    }

    Connections {
        target: root._managed
        function onFinished(ok, summary) {
            root._lastRunOk = ok
            root._lastRunSummary = summary
            root._lastRunAt = new Date()
            root._runningProfile = ""
            root._runningTable = ""
            root._refreshDetailBindings()
        }
        function onTableProgress(profile, table, current, total, state, message) {
            // Auto-follow the profile currently reporting progress; the detail
            // region tracks it through `_detailProfile` while no pin is set.
            root._runningProfile = profile
            if (state === "running")
                root._runningTable = table
            root._refreshDetailBindings()
        }
    }

    // Kit text field (`k-ctl`): 32 px, hairline field border, brand border and
    // tint on focus, the read-only palette when `readOnly`, error border when
    // `invalid`. An optional leading glyph sits inside the field.
    component KitField: TextField {
        id: field
        property bool mono: false
        property bool invalid: false
        property string leadingGlyph: ""

        implicitHeight: Theme.ctlMd
        font: mono ? Theme.monoFont(12) : Qt.font({ family: Theme.fontFamily, pixelSize: Theme.sizeBody })
        leftPadding: leadingGlyph !== "" ? 30 : 10
        rightPadding: 10
        topPadding: 0
        bottomPadding: 0
        verticalAlignment: TextInput.AlignVCenter
        color: readOnly ? Theme.clrFieldRoText : Theme.clrOnSurface
        placeholderTextColor: Theme.clrOnSurfaceFaint
        selectionColor: Theme.clrPrimarySoftBorder
        selectedTextColor: Theme.clrOnSurface
        selectByMouse: true

        background: Rectangle {
            radius: Theme.r1
            color: field.readOnly ? Theme.clrFieldRoBg
                 : (field.activeFocus ? Theme.clrFieldFocusBg : Theme.clrCard)
            border.color: field.invalid ? Theme.clrError
                        : field.activeFocus ? Theme.clrPrimary
                        : (field.readOnly ? Theme.clrFieldRoBorder : Theme.clrFieldBorder)

            Glyph {
                visible: field.leadingGlyph !== ""
                anchors.left: parent.left
                anchors.leftMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                name: field.leadingGlyph
                size: 14
                color: Theme.clrOnSurfaceSubtle
                knockout: parent.color
            }
        }
    }

    // Kit table header cell label (10 px bold uppercase).
    component ThLabel: Label {
        color: Theme.clrOnSurfaceSubtle
        font.pixelSize: Theme.sizeGroup
        font.weight: Font.Bold
        font.letterSpacing: 0.4
        font.capitalization: Font.AllUppercase
        elide: Text.ElideRight
    }

    // Kit "dl" row label for the confirmation dialogs' provenance block.
    component DlTerm: Label {
        Layout.alignment: Qt.AlignTop
        color: Theme.clrOnSurfaceSubtle
        font.pixelSize: Theme.sizeBodySm
    }

    // Kit field label (`k-field label`): 11.5 px semibold, above the control.
    component FieldLabel: Label {
        color: Theme.clrOnSurfaceMed
        font.pixelSize: Theme.sizeLabel
        font.weight: Font.DemiBold
        wrapMode: Text.WordWrap
        textFormat: Text.RichText
    }

    // Kit restore-confirmation dialog (`dt-dlg`): white, r3, the shared
    // DestructiveWarningBanner as its header, the caller's body indented under
    // the warning text, and a tinted footer strip with Cancel + the red
    // confirming button. Both restore flows use it so their frame, wording and
    // button placement cannot drift apart.
    component KitConfirmDialog: Dialog {
        id: dlg

        /// The dialog's question (wraps; may contain a long profile name).
        property string heading: ""
        /// Database being overwritten, emphasised in the warning text.
        property string subject: ""
        /// Label of the red confirming button.
        property string confirmText: qsTr("Restore")
        /// Enables the confirming button.
        property bool confirmEnabled: true
        /// The confirming button, exposed for tests.
        readonly property alias confirmButton: confirmBtn
        /// Body items, laid out in a ColumnLayout under the warning text.
        default property alias body: bodyColumn.data

        /// Emitted by the confirming button; the dialog closes itself after.
        signal confirmed()

        modal: true
        anchors.centerIn: parent
        width: Math.max(360, Math.min(520, (parent ? parent.width : 520) - 48))
        padding: 0
        header: null

        background: Rectangle {
            color: Theme.clrCard
            radius: Theme.r3
            border.color: Theme.clrContainerHighest
        }

        contentItem: ColumnLayout {
            spacing: 0

            DestructiveWarningBanner {
                Layout.fillWidth: true
                Layout.leftMargin: 20
                Layout.rightMargin: 20
                Layout.topMargin: 20
                Layout.bottomMargin: 6
                title: dlg.heading
                subject: dlg.subject
            }
            ColumnLayout {
                id: bodyColumn
                Layout.fillWidth: true
                // 70 = 20 margin + 36 icon + 14 gap: the body lines up under
                // the warning text, not under the icon.
                Layout.leftMargin: 70
                Layout.rightMargin: 20
                Layout.topMargin: 10
                Layout.bottomMargin: 18
                spacing: 14
            }
        }

        footer: Rectangle {
            implicitHeight: footerRow.implicitHeight + 24
            color: Theme.clrContainerLow
            radius: Theme.r3

            // Squares off the strip's top corners (only the bottom two belong
            // to the dialog's rounded outline) and draws the hairline.
            Rectangle {
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                height: Theme.r3
                color: Theme.clrContainerLow
            }
            Rectangle {
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                height: 1
                color: Theme.clrContainerHighest
            }

            RowLayout {
                id: footerRow
                anchors.right: parent.right
                anchors.rightMargin: 20
                anchors.verticalCenter: parent.verticalCenter
                spacing: 8

                LsButton {
                    variant: "secondary"
                    size: "md"
                    text: qsTr("Cancel")
                    onClicked: dlg.close()
                }
                LsButton {
                    id: confirmBtn
                    variant: "dangerFill"
                    size: "md"
                    text: dlg.confirmText
                    enabled: dlg.confirmEnabled
                    onClicked: {
                        dlg.confirmed()
                        dlg.close()
                    }
                }
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        PageHeader {
            Layout.fillWidth: true

            crumbs: [ "dbtool", qsTr("Backups") ]
            title: qsTr("Backups")

            contextItems: [
                StatusPill {
                    visible: root._managedRunning
                    status: "running"
                    label: root._runningProfile !== ""
                           ? qsTr("Backing up %1").arg(root.shorten(root._runningProfile, 28))
                           : qsTr("Running")
                    tooltipText: " "
                }
            ]

            actions: [
                LsButton {
                    variant: "primary"
                    size: "md"
                    glyph: "download"
                    text: qsTr("Back up all profiles")
                    enabled: root._canRun
                    ToolTip.visible: hovered
                    ToolTip.delay: 500
                    ToolTip.timeout: 10000
                    ToolTip.text: qsTr("Write one archive per profile into the backup folder.")
                    onClicked: root._managed.backupAll()
                }
            ]
        }

        // Page body. It does NOT scroll as one block: the profile table and
        // the detail panel each own an internal scrollbar, so scrolling 300
        // profiles never scrolls the details away and vice-versa (the user's
        // report: "I need to scroll all the way to the top to see the
        // details").
        Item {
            id: bodyArea
            Layout.fillWidth: true
            Layout.fillHeight: true

            // Side-by-side once the table can keep its full wide layout next
            // to a 360 px detail panel; stacked below that.
            readonly property bool twoColumns: width - 48 >= 1180

            GridLayout {
                id: body
                x: Math.round((parent.width - width) / 2)
                y: 20
                width: Math.max(0, Math.min(parent.width - 48, bodyArea.twoColumns ? 1360 : 960))
                height: Math.max(0, parent.height - 40)
                columns: bodyArea.twoColumns ? 2 : 1
                columnSpacing: 14
                rowSpacing: 14

                // ---------- Main column: folder, profile table, custom archive ----------
                ColumnLayout {
                    id: leftRail
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumWidth: 0
                    spacing: 14

                    // --- 1. Backup folder ---
                    Card {
                        Layout.fillWidth: true
                        title: qsTr("Backup folder")
                        glyph: "folder-outline"
                        // Guillemets, not angle brackets: the Card meta is an AutoText Label,
                        // which would read "<profile>" as an HTML tag and drop it.
                        meta: qsTr("one ‹profile›.zip per profile, replaced on each run")

                        // Read-only path box (`dt-path`) — the user needs to
                        // know what "Back up all" will overwrite; changing it
                        // is a Settings concern.
                        Rectangle {
                            width: parent.width
                            implicitHeight: 34
                            radius: Theme.r1
                            color: Theme.clrFieldRoBg
                            border.color: root._managed.folderProblem !== "" ? Theme.clrWarningBorder
                                                                              : Theme.clrFieldRoBorder

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 10
                                anchors.rightMargin: 4
                                spacing: 8

                                Glyph {
                                    name: "folder-outline"
                                    size: 14
                                    color: root._managed.folderProblem !== "" ? Theme.clrWarningDot
                                                                               : Theme.clrOnSurfaceSubtle
                                    knockout: Theme.clrFieldRoBg
                                }
                                Label {
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 0
                                    text: root._managed.effectiveBackupFolder
                                    color: Theme.clrOnSurface
                                    font: Theme.monoFont(12)
                                    elide: Text.ElideMiddle
                                    ToolTip.visible: folderHover.hovered && truncated
                                    ToolTip.text: root._managed.effectiveBackupFolder
                                    HoverHandler { id: folderHover }
                                }
                                LsButton {
                                    variant: "ghost"
                                    size: "sm"
                                    glyph: "external"
                                    text: qsTr("Change in Settings")
                                    onClicked: root.openSettings()
                                }
                            }
                        }

                        // The problem blocks every action on the page, so it is
                        // stated once, with the consequence and the fix — not
                        // as a footnote the user has to read past.
                        Banner {
                            width: parent.width
                            visible: root._managed.folderProblem !== ""
                            kind: "warn"
                            title: qsTr("Backups are paused.")
                            text: qsTr("%1 No archive can be written until this is fixed.")
                                  .arg(root._managed.folderProblem)
                        }
                    }

                    // --- 2. Profiles (kit table) ---
                    // NOT a `Card`: a Card is a content-sized Column, but the
                    // table must own the column's FLEXIBLE height and scroll
                    // internally (300 profiles). So this is a fill-height kit
                    // panel whose ListView takes the slack.
                    Rectangle {
                        id: profilesPanel
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.minimumHeight: 180
                        color: Theme.clrCard
                        border.color: Theme.clrContainerHighest
                        radius: Theme.r3
                        clip: true

                        // Card-mode rows drop the column headings.
                        readonly property bool compactRows: profileList.width < 760

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 1
                            spacing: 0

                            // Panel header (k-panel .hd): title, source count,
                            // and the name filter on ONE 40 px row.
                            Item {
                                Layout.fillWidth: true
                                implicitHeight: 40

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 14
                                    anchors.rightMargin: 8
                                    spacing: 8

                                    Glyph {
                                        name: "archive-outline"
                                        size: 15
                                        color: Theme.clrOnSurfaceSubtle
                                    }
                                    Label {
                                        text: qsTr("Profiles")
                                        color: Theme.clrOnSurface
                                        font.pixelSize: Theme.sizeBody
                                        font.weight: Font.DemiBold
                                    }
                                    // Reads "3 of 12 shown" while a filter is
                                    // active so the search's effect is visible.
                                    Label {
                                        Layout.fillWidth: true
                                        Layout.minimumWidth: 0
                                        text: profileSearch.text.length > 0
                                              ? qsTr("%1 of %n shown", "", root._profileNames.length).arg(profileList.count)
                                              : qsTr("%n profile(s) from dbtool.yml", "", root._profileNames.length)
                                        color: Theme.clrOnSurfaceFaint
                                        font.pixelSize: Theme.sizeLabel
                                        elide: Text.ElideRight
                                    }

                                    // Name filter (case-insensitive substring),
                                    // so the wanted profile is found without
                                    // scrolling a long table.
                                    KitField {
                                        id: profileSearch
                                        Layout.preferredWidth: 190
                                        implicitHeight: Theme.ctlSm
                                        leadingGlyph: "search"
                                        placeholderText: qsTr("Search profiles…")
                                        font: Qt.font({ family: Theme.fontFamily, pixelSize: Theme.sizeBodySm })
                                        rightPadding: 26

                                        // Clear affordance, shown only while filtering.
                                        Glyph {
                                            name: "cross"
                                            size: 11
                                            color: clearHover.hovered ? Theme.clrOnSurface : Theme.clrOnSurfaceFaint
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
                                Rectangle {
                                    anchors.bottom: parent.bottom
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    height: 1
                                    color: Theme.clrDivider
                                }
                            }

                            // Table header row (`dt-br.th`). Column widths
                            // mirror ProfileRow's.
                            Rectangle {
                                visible: !profilesPanel.compactRows
                                Layout.fillWidth: true
                                implicitHeight: 30
                                color: Theme.clrContainerLow

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 14
                                    anchors.rightMargin: 14
                                    spacing: 12

                                    ThLabel { Layout.fillWidth: true; text: qsTr("Profile") }
                                    ThLabel { Layout.preferredWidth: root._columnWidths[0]; text: qsTr("Status") }
                                    ThLabel { Layout.preferredWidth: root._columnWidths[1]; text: qsTr("Last backup") }
                                    ThLabel { Layout.preferredWidth: root._columnWidths[2]; text: qsTr("Size") }
                                    Item { Layout.preferredWidth: root._columnWidths[3] }
                                }
                                Rectangle {
                                    anchors.bottom: parent.bottom
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    height: 1
                                    color: Theme.clrContainerHighest
                                }
                            }

                            // Profile rows. Clicking a row pins the detail
                            // region to it (click again to unpin → auto-follow).
                            // Its own vertical scrollbar keeps the table
                            // scrollable without moving the header above or the
                            // detail panel beside it.
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
                                    readonly property var rowTables: model.tables
                                    readonly property int rowTablesTotal: rowTables ? rowTables.totalCount : 0
                                    readonly property int rowTablesDone: rowTables ? rowTables.doneCount : 0

                                    width: profileList.width
                                    // Explicit: a ListView positions delegates
                                    // by `height`, and does not adopt an
                                    // `implicitHeight`. Rows are not a uniform
                                    // height (the profile name wraps to 1-3
                                    // lines), so without this wrapped names
                                    // would overlap the row below.
                                    height: implicitHeight

                                    name: model.name
                                    connectionString: root.redact(root._profileConnectionStrings[model.name] || "")
                                    current: AppController.connected
                                             && AppController.connectionMode === "profile"
                                             && AppController.currentProfile === model.name
                                    pillStatus: root.pillStatus(rowRunState, rowArchiveExists)
                                    pillLabel: root.pillLabel(rowRunState, rowArchiveExists)
                                    meta: root.rowMeta(rowRunState, rowArchiveExists, model.archiveMtime,
                                                       model.errorText, rowTables)
                                    metaSub: rowRunState === "ok" && rowTablesTotal > 0
                                             ? qsTr("%n table(s)", "", rowTablesTotal) : ""
                                    metaIsError: rowRunState === "failed"
                                    metaMuted: rowRunState === "queued" || (!rowArchiveExists && rowRunState !== "ok")
                                    sizeText: rowArchiveExists ? root.formatSize(model.archiveSize) : ""
                                    selected: root._detailProfile === model.name
                                    running: rowRunState === "running"
                                    progress: rowTablesTotal > 0 ? rowTablesDone / rowTablesTotal : 0
                                    progressIndeterminate: rowTablesTotal <= 0
                                    canBackup: root._canRun
                                    canRestore: rowArchiveExists && root._canRun
                                    // A failed run's primary action is to try
                                    // again, so the button says so.
                                    backupLabel: rowRunState === "failed"
                                                 ? qsTr("Retry") : qsTr("Back up")

                                    onActivated: {
                                        // Toggle: clicking the pinned row unpins (auto-follow).
                                        root._pinnedProfile =
                                            (root._pinnedProfile === model.name) ? "" : model.name
                                    }
                                    onBackupRequested: root._managed.backupProfile(model.name)
                                    onRestoreRequested: restoreDialog.openFor(model.name,
                                                                              model.archiveSize,
                                                                              model.archiveMtime,
                                                                              rowTablesTotal)
                                }

                                // Placeholder when nothing is listed: either the
                                // search filtered every row out, or no profiles
                                // are loaded at all.
                                Label {
                                    anchors.centerIn: parent
                                    width: parent.width - 24
                                    visible: profileList.count === 0
                                    horizontalAlignment: Text.AlignHCenter
                                    wrapMode: Text.WordWrap
                                    text: profileSearch.text.length > 0
                                          ? qsTr("No profiles match “%1”.").arg(profileSearch.text)
                                          : qsTr("No profiles loaded. Set the dbtool.yml location in Settings.")
                                    color: Theme.clrOnSurfaceSubtle
                                    font.pixelSize: Theme.sizeBodySm
                                }
                            }

                            // Table footer (`dt-foot`): the last run's summary.
                            Rectangle {
                                visible: root._lastRunSummary !== ""
                                Layout.fillWidth: true
                                implicitHeight: footRow.implicitHeight + 20
                                color: Theme.clrContainerLow
                                radius: Theme.r3

                                // Square off the top corners; only the bottom
                                // ones follow the panel outline.
                                Rectangle {
                                    anchors.top: parent.top
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    height: Theme.r3
                                    color: Theme.clrContainerLow
                                }
                                Rectangle {
                                    anchors.top: parent.top
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    height: 1
                                    color: Theme.clrDivider
                                }

                                RowLayout {
                                    id: footRow
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.leftMargin: 14
                                    anchors.rightMargin: 14
                                    spacing: 8

                                    Glyph {
                                        Layout.alignment: Qt.AlignTop
                                        Layout.topMargin: 1
                                        name: "history"
                                        size: 14
                                        color: root._lastRunOk ? Theme.clrSuccess : Theme.clrError
                                        knockout: Theme.clrContainerLow
                                    }
                                    Label {
                                        Layout.fillWidth: true
                                        text: root._lastRunAt
                                              ? qsTr("Last run %1 — %2")
                                                    .arg(Qt.formatDateTime(root._lastRunAt, "HH:mm"))
                                                    .arg(root._lastRunSummary)
                                              : root._lastRunSummary
                                        color: root._lastRunOk ? Theme.clrOnSurfaceMed : Theme.clrError
                                        font.pixelSize: Theme.sizeBodySm
                                        wrapMode: Text.WordWrap
                                    }
                                }
                            }
                        }
                    }

                    // --- 3. Custom archive ---
                    Card {
                        Layout.fillWidth: true
                        title: qsTr("Custom archive")
                        glyph: "upload"
                        meta: qsTr("uses the current connection")

                        RowLayout {
                            width: parent.width
                            spacing: 6

                            KitField {
                                id: customPathField
                                Layout.fillWidth: true
                                Layout.minimumWidth: 120
                                mono: true
                                leadingGlyph: "folder-outline"
                                placeholderText: qsTr("/path/to/backup.zip")
                            }
                            LsButton {
                                variant: "secondary"
                                size: "md"
                                text: qsTr("Browse…")
                                onClicked: customArchiveDialog.open()
                            }
                            LsButton {
                                variant: "secondary"
                                size: "md"
                                text: qsTr("Back up")
                                // Gated on the managed controller too: an ad-hoc
                                // backup started during a managed backup-all run
                                // competes for the same database.
                                enabled: customPathField.text.length > 0 && root._canRunCustom
                                ToolTip.visible: hovered
                                ToolTip.delay: 500
                                ToolTip.timeout: 10000
                                ToolTip.text: qsTr("Write a schema + data snapshot to the .zip above.")
                                onClicked: AppController.backupRunner.runBackup(customPathField.text)
                            }
                            LsButton {
                                // Named so the QML test can assert the click
                                // is routed through the confirmation dialog.
                                objectName: "customRestoreButton"
                                variant: "danger"
                                size: "md"
                                text: qsTr("Restore…")
                                enabled: customPathField.text.length > 0 && root._canRunCustom
                                // The ellipsis is a promise: this must never
                                // fire on one click — same rule as the
                                // per-profile restore.
                                onClicked: customRestoreDialog.openFor(customPathField.text)
                            }
                        }

                        // States the consequence before the click rather than
                        // after it: "Back up" writes a file, "Restore…" drops
                        // and recreates every table of the connected database.
                        Label {
                            width: parent.width
                            text: AppController.connected
                                  ? qsTr("Restore… replaces every table of <b>%1</b>.").arg(AppController.currentProfile)
                                  : qsTr("Restore… replaces every table of the connected database.")
                            color: Theme.clrOnSurfaceSubtle
                            font.pixelSize: Theme.sizeLabel
                            wrapMode: Text.WordWrap
                            textFormat: Text.RichText
                        }
                    }
                }

                // ---------- Detail region: live per-table details ----------
                BackupDetailPanel {
                    id: detailPanel
                    Layout.alignment: Qt.AlignTop
                    Layout.fillHeight: bodyArea.twoColumns
                    Layout.fillWidth: !bodyArea.twoColumns
                    Layout.preferredWidth: bodyArea.twoColumns ? 360 : -1
                    Layout.preferredHeight: bodyArea.twoColumns ? -1 : 280
                    Layout.minimumHeight: bodyArea.twoColumns ? 0 : 220
                    profileName: root._detailProfile
                    tablesModel: root._detailTablesModel
                    overallState: root._detailRunState
                    workerCount: root._workerCount
                }
            }
        }
    }

    // Reactive list of profile names (for the restore-target dropdown) and a
    // name → connection-string map (for the table and the dialog's Target
    // row), kept in store order. `Instantiator` maintains both across
    // profile-file reloads without us hand-rolling a model iteration
    // (QAbstractListModel exposes no count/data to QML directly).
    property var _profileNames: []
    property var _profileConnectionStrings: ({})
    Instantiator {
        model: AppController.profiles
        delegate: QtObject {
            required property string name
            required property string connectionString
        }
        onObjectAdded: function(index, object) {
            const names = root._profileNames.slice()
            names.splice(index, 0, object.name)
            root._profileNames = names
            root._firstProfileName = names.length > 0 ? names[0] : ""
            const map = Object.assign({}, root._profileConnectionStrings)
            map[object.name] = object.connectionString
            root._profileConnectionStrings = map
        }
        onObjectRemoved: function(index, object) {
            const names = root._profileNames.slice()
            names.splice(index, 1)
            root._profileNames = names
            root._firstProfileName = names.length > 0 ? names[0] : ""
            const map = Object.assign({}, root._profileConnectionStrings)
            delete map[object.name]
            root._profileConnectionStrings = map
        }
    }

    // Picks the custom archive path. A save dialog, because "Back up" needs a
    // file that may not exist yet; overwrite confirmation is off because the
    // same path also feeds "Restore…", where the file is expected to exist.
    FileDialog {
        id: customArchiveDialog
        title: qsTr("Select archive")
        fileMode: FileDialog.SaveFile
        options: FileDialog.DontConfirmOverwrite
        defaultSuffix: "zip"
        nameFilters: [ qsTr("Backup archives (*.zip)"), qsTr("All files (*)") ]
        onAccepted: {
            const url = selectedFile.toString()
            const localPath = Qt.platform.os === "windows"
                ? url.replace(/^file:\/{2,3}/, "")
                : url.replace(/^file:\/{2}/, "")
            customPathField.text = decodeURIComponent(localPath)
        }
    }

    // --- Destructive per-profile restore confirmation ---
    KitConfirmDialog {
        id: restoreDialog
        objectName: "restoreDialog"

        property string sourceProfile: ""
        property var archiveSizeBytes: 0
        property var archiveMtimeValue: undefined
        property int archiveTables: 0

        readonly property string _customEntry: qsTr("Custom connection string…")
        readonly property var _targets: root._profileNames.concat([_customEntry])
        readonly property bool _isCustom: targetCombo.currentIndex === root._profileNames.length
        readonly property string _target: _isCustom ? "" : root._profileNames[targetCombo.currentIndex]
        // The name the user must type: the database being overwritten. A
        // custom connection string has no name, so the archive's own profile
        // stands in for it.
        readonly property string confirmName: _isCustom ? sourceProfile : _target
        /// The typed-confirmation field, exposed for tests.
        readonly property alias confirmField: restoreConfirmField
        /// The custom connection-string field, exposed for tests.
        readonly property alias customConnectionField: customConnField

        heading: qsTr("Restore %1 from its backup?").arg(restoreDialog.sourceProfile)
        subject: _isCustom ? "" : _target
        // Long names are shortened on the button only — the heading above
        // wraps and carries the full name.
        confirmText: confirmName !== "" ? qsTr("Restore %1").arg(root.shorten(confirmName, 24))
                                        : qsTr("Restore")
        confirmEnabled: (!_isCustom || customConnField.text.length > 0)
                        && confirmName !== ""
                        && restoreConfirmField.text === confirmName

        /// Populates and opens the dialog for a given archive profile.
        /// @param profile Archive/source profile name.
        /// @param sizeBytes Archive size in bytes.
        /// @param mtime Archive modification time.
        /// @param tables Table count of the archive when this session wrote
        ///        it, else 0 (unknown).
        function openFor(profile, sizeBytes, mtime, tables) {
            sourceProfile = profile
            archiveSizeBytes = sizeBytes
            archiveMtimeValue = mtime
            archiveTables = tables || 0
            customConnField.text = ""
            customSchemaField.text = ""
            restoreConfirmField.text = ""
            const idx = root._profileNames.indexOf(profile)
            targetCombo.currentIndex = idx >= 0 ? idx : 0
            open()
        }

        onConfirmed: {
            if (restoreDialog._isCustom) {
                root._managed.restoreArchiveToConnectionString(
                    restoreDialog.sourceProfile,
                    customConnField.text,
                    customSchemaField.text)
            } else {
                root._managed.restoreArchive(restoreDialog.sourceProfile,
                                             restoreDialog._target)
            }
        }

        // Provenance (`dt-dlg dl`): what is about to be restored, and where.
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: provenance.implicitHeight + 20
            color: Theme.clrContainerLow
            border.color: Theme.clrContainerHighest
            radius: Theme.r2

            GridLayout {
                id: provenance
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                columns: 2
                columnSpacing: 12
                rowSpacing: 6

                DlTerm { text: qsTr("Archive"); Layout.preferredWidth: 72 }
                // Mid-elided so both ends of a ~50-character name stay
                // readable; the full name is on hover.
                Label {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    text: restoreDialog.sourceProfile + ".zip"
                    color: Theme.clrOnSurface
                    font: Theme.monoFont(12)
                    elide: Text.ElideMiddle
                    ToolTip.visible: archiveHover.hovered && truncated
                    ToolTip.delay: 400
                    ToolTip.text: restoreDialog.sourceProfile + ".zip"
                    HoverHandler { id: archiveHover }
                }

                DlTerm { text: qsTr("Taken") }
                Label {
                    Layout.fillWidth: true
                    text: root.formatWhen(restoreDialog.archiveMtimeValue) || "—"
                    color: Theme.clrOnSurface
                    font.pixelSize: Theme.sizeBodySm
                }

                DlTerm { text: qsTr("Contents") }
                Label {
                    Layout.fillWidth: true
                    text: {
                        const parts = []
                        if (restoreDialog.archiveTables > 0)
                            parts.push(qsTr("%n table(s)", "", restoreDialog.archiveTables))
                        const size = root.formatSize(restoreDialog.archiveSizeBytes)
                        if (size !== "")
                            parts.push(size)
                        return parts.length > 0 ? parts.join(" · ") : "—"
                    }
                    color: Theme.clrOnSurface
                    font.pixelSize: Theme.sizeBodySm
                }

                DlTerm { text: qsTr("Target") }
                Label {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    text: {
                        if (restoreDialog._isCustom)
                            return customConnField.text !== "" ? root.redact(customConnField.text) : "—"
                        const cs = root._profileConnectionStrings[restoreDialog._target] || ""
                        return cs !== "" ? root.redact(cs) : restoreDialog._target
                    }
                    color: Theme.clrOnSurface
                    font: Theme.monoFont(11)
                    elide: Text.ElideRight
                }
            }
        }

        // Restore target: any profile (the archive's own preselected) or a
        // custom connection string.
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 5

            FieldLabel { Layout.fillWidth: true; text: qsTr("Restore into") }
            ComboBox {
                id: targetCombo
                Layout.fillWidth: true
                implicitHeight: Theme.ctlMd
                model: restoreDialog._targets
                font.pixelSize: Theme.sizeBody
            }
        }

        // Custom connection string + schema (only when the custom dropdown
        // entry is selected).
        ColumnLayout {
            visible: restoreDialog._isCustom
            Layout.fillWidth: true
            spacing: 5

            FieldLabel { Layout.fillWidth: true; text: qsTr("Connection string") }
            KitField {
                id: customConnField
                Layout.fillWidth: true
                mono: true
                placeholderText: qsTr("Driver={…};Server=…;Database=…")
            }
            FieldLabel { Layout.fillWidth: true; Layout.topMargin: 4; text: qsTr("Schema") }
            KitField {
                id: customSchemaField
                Layout.fillWidth: true
                mono: true
                placeholderText: qsTr("(server default)")
            }
            Label {
                Layout.fillWidth: true
                text: qsTr("Optional. Qualifies restored tables, like dbtool's --schema.")
                color: Theme.clrOnSurfaceSubtle
                font.pixelSize: Theme.sizeLabel
                wrapMode: Text.WordWrap
            }
        }

        // Cross-target warning: restoring into a profile other than the
        // archive's own source.
        Banner {
            Layout.fillWidth: true
            visible: !restoreDialog._isCustom && restoreDialog._target !== restoreDialog.sourceProfile
            kind: "warn"
            text: qsTr("Target %1 differs from the archive's source profile %2 — its current data will be replaced with %2's snapshot.")
                  .arg(restoreDialog._target).arg(restoreDialog.sourceProfile)
        }

        // Typed confirmation (`k-field`): the red button stays disabled until
        // the overwritten database's name is typed exactly, so a restore is
        // never one mis-aimed click away.
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 5

            FieldLabel {
                Layout.fillWidth: true
                text: qsTr("Type <span style=\"font-family:'%1'; color:%2\">%3</span> to confirm")
                      .arg(Theme.monoFamilies[0]).arg(Theme.clrOnSurface).arg(restoreDialog.confirmName)
            }
            KitField {
                id: restoreConfirmField
                objectName: "restoreConfirmField"
                Layout.fillWidth: true
                placeholderText: restoreDialog.confirmName
                inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                onAccepted: if (restoreDialog.confirmEnabled) restoreDialog.confirmButton.clicked()
            }
        }
    }

    // --- Destructive custom-archive restore confirmation ---
    //
    // The "Restore…" button next to the custom archive path used to call
    // `BackupRunner.runRestore` directly: one click, no confirmation, on a
    // surface that drops and recreates every table of the *connected*
    // database. Its ellipsis promises a dialog, so it gets one — the same kit
    // frame and warning as the per-profile restore above.
    KitConfirmDialog {
        id: customRestoreDialog
        objectName: "customRestoreDialog"

        property string archivePath: ""

        heading: qsTr("Restore into the connected database?")
        subject: AppController.connected ? AppController.currentProfile : ""
        confirmText: qsTr("Restore")

        /// Populates and opens the dialog for a custom archive path.
        /// @param path Archive file the user typed into the path field.
        function openFor(path) {
            archivePath = path
            open()
        }

        onConfirmed: AppController.backupRunner.runRestore(customRestoreDialog.archivePath)

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: customProvenance.implicitHeight + 20
            color: Theme.clrContainerLow
            border.color: Theme.clrContainerHighest
            radius: Theme.r2

            GridLayout {
                id: customProvenance
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                columns: 2
                columnSpacing: 12
                rowSpacing: 6

                DlTerm { text: qsTr("Archive"); Layout.preferredWidth: 72 }
                Label {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    text: customRestoreDialog.archivePath
                    color: Theme.clrOnSurface
                    font: Theme.monoFont(12)
                    elide: Text.ElideMiddle
                    ToolTip.visible: customArchiveHover.hovered && truncated
                    ToolTip.delay: 400
                    ToolTip.text: customRestoreDialog.archivePath
                    HoverHandler { id: customArchiveHover }
                }

                DlTerm { text: qsTr("Target") }
                Label {
                    Layout.fillWidth: true
                    text: AppController.connected
                          ? qsTr("the currently connected database (%1)").arg(AppController.currentProfile)
                          : qsTr("the currently connected database")
                    color: Theme.clrOnSurface
                    font.pixelSize: Theme.sizeBodySm
                    wrapMode: Text.WordWrap
                }
            }
        }
    }
}
