// SPDX-License-Identifier: Apache-2.0
//
// Simple view: the stripped, end-user flavour of the migrations GUI, laid
// out as the Lastrada guided flow of three numbered steps:
//
//   1. Connection — the ConnectionPanel; collapses to a one-line summary
//      ("profile · connected") once connected, with a "Change" action.
//   2. Review what will change — the current and resulting release plus the
//      exact list of pending migrations, so nobody presses Run blind.
//   3. Run — an optional "Back up first" checkbox and a single red Run
//      button. During a run the step turns into one progress bar plus the
//      per-migration state (done / running / waiting); afterwards it shows
//      the outcome first, then what was applied, then the way back (the
//      pre-run backup, one click away in Backups).
//
// On failure the step expands into a diagnostic bundle (Copy / Save /
// Show full log) assembled by `AppController.buildFailureReport()` — so
// even the simple-view user can hand their support team everything needed
// to diagnose without re-running anything.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import Lightweight.Migrations

Rectangle {
    id: root
    color: Theme.clrBase

    // --- Size hints (consumed by `Main.qml` on view-mode switch) ---
    // The simple view has far less to show than the expert three-pane
    // layout, so the window shrinks to match when the user flips over.
    // `Main.qml` adds the rail, header and status bar around these content
    // sizes. Declared readonly so the window never writes back here and
    // triggers a binding loop.
    readonly property int preferredViewWidth: 580
    readonly property int preferredViewHeight: 640
    readonly property int minimumViewWidth: 440
    readonly property int minimumViewHeight: 420

    /// Emitted when the user asks to see the pre-run backup in Backups.
    signal openBackups()

    // Retained outside the run step so switching views (or toggling
    // backup-first off) does not wipe the user's last answer mid-decision.
    property bool backupFirst: false

    // --- Run-result feedback ---
    // "none" before the first run, "success" / "failure" after `finished`.
    // Cleared when a new run starts (see MigrationRunner.Connections below).
    property string lastResult: "none"
    property string lastSummary: ""

    // Run-chain state: when `backupFirst` is ticked we run the backup first,
    // wait for its `finished(ok=true)`, then kick off the apply. `pendingApply`
    // guards the transition so a failed backup does NOT silently migrate on.
    property bool pendingApply: false
    // Target file the user picked for the pre-migration backup. Empty while
    // no backup is queued.
    property string pendingBackupFile: ""
    // Archive written by the last successful pre-run backup; shown in the
    // done state as the way back.
    property string lastBackupFile: ""

    // --- Live progress capture ---
    // `MigrationRunner.progress` fires per-migration; we capture the last
    // tuple so QML bindings can reflect it without storing state in QML
    // JavaScript closures (which break when the binding re-evaluates).
    property int  progressIndex: 0
    property int  progressTotal: 0
    property string progressTitle: ""

    // Per-migration state of the current (or last) run, keyed by timestamp:
    // "running" | "done" | "failed". Lets the plan list keep showing a row
    // after the model has flipped it to "applied", so the done state can
    // list exactly what this run changed. Changed in place, which costs the
    // same for every migration of a long run; bindings depend on
    // `runStatesRevision` to see a change.
    property var runStates: ({})
    property int runStatesRevision: 0
    property int runStateCount: 0
    function setRunState(timestamp, state) {
        const key = String(timestamp)
        if (runStates[key] === undefined)
            runStateCount += 1
        runStates[key] = state
        runStatesRevision += 1
        root.runPlan.touch(key)
    }
    function clearRunStates() {
        runStates = ({})
        runStateCount = 0
        runStatesRevision += 1
        root.runPlan.clearTouched()
    }

    // The rows the plan lists show: a view over every row would create a delegate for each hidden one.
    readonly property alias pendingPlan: pendingPlanModel
    readonly property alias runPlan: runPlanModel
    MigrationPlanModel {
        id: pendingPlanModel
        sourceModel: AppController.migrations
    }
    MigrationPlanModel {
        id: runPlanModel
        sourceModel: AppController.migrations
        scope: MigrationPlanModel.Run
    }

    // When connected, step 1 collapses unless the user asked to change it.
    property bool connectionExpanded: false

    readonly property bool migrating: AppController.runner.phase !== MigrationRunner.Idle
    readonly property bool backingUp: AppController.backupRunner.phase !== BackupRunner.Idle
    readonly property bool busy: migrating || backingUp
    // Pre-migrate steps run with every apply, so a run is worth starting even with nothing pending.
    readonly property bool hasWork: AppController.pendingCount > 0 || AppController.preMigrateStepCount > 0
    readonly property bool allIdle: AppController.runner.phase === MigrationRunner.Idle
                                    && AppController.backupRunner.phase === BackupRunner.Idle
                                    && AppController.managedBackups.phase === ManagedBackupController.Idle
    // A required backup is missing and the user did not waive it. "Back up first" makes the backup
    // the run needs; the runner re-checks under the lock, so a backup it cannot record still refuses.
    readonly property bool backupBlocked: AppController.backupRequired && !AppController.migrateWithoutBackup

    Connections {
        target: AppController.runner
        function onProgress(timestamp, title, index, total) {
            root.progressIndex = index
            root.progressTotal = total
            root.progressTitle = title
        }
        function onPhaseChanged() {
            if (AppController.runner.phase === MigrationRunner.Running) {
                // Fresh run starting — reset the result and the progress
                // capture so stale numbers from the previous run do not
                // flash up briefly before the first progress signal.
                root.lastResult = "none"
                root.progressIndex = 0
                root.progressTotal = 0
                root.progressTitle = ""
            }
        }
        function onMigrationStarted(timestamp) { root.setRunState(timestamp, "running") }
        function onMigrationCompleted(timestamp, newStatus) { root.setRunState(timestamp, "done") }
        function onMigrationFailed(timestamp) { root.setRunState(timestamp, "failed") }
        function onFinished(ok, summary) {
            root.lastResult = ok ? "success" : "failure"
            root.lastSummary = summary
        }
    }

    Connections {
        target: AppController.backupRunner
        function onFinished(ok, summary) {
            if (!root.pendingApply)
                return
            root.pendingApply = false
            if (ok) {
                root.lastBackupFile = root.pendingBackupFile
                root.pendingBackupFile = ""
                AppController.runner.applyUpTo("")
            } else {
                root.pendingBackupFile = ""
                // Surface the backup failure in the same place the migration
                // run would; the user's choice to back up first was not an
                // invitation to ignore a failed backup and migrate anyway.
                root.lastResult = "failure"
                root.lastSummary = qsTr("Backup failed before migration could start: ") + summary
            }
        }
    }

    /// Starts a run, optionally behind a backup. Resets the per-run state so
    /// the plan list shows only this run's migrations.
    function startRun() {
        root.lastResult = "none"
        root.clearRunStates()
        root.lastBackupFile = ""
        // The backup comes first, so a failed backup must not show the previous run's rows.
        AppController.runner.tasks.clear()
        if (root.backupFirst)
            backupFileDialog.open()
        else
            AppController.runner.applyUpTo("")
    }

    /// Clears the outcome and returns the flow to the review state.
    function dismissResult() {
        root.lastResult = "none"
        root.clearRunStates()
    }

    FileDialog {
        id: backupFileDialog
        objectName: "backupFileDialog"
        title: qsTr("Choose backup file")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("Backup archive (*.zip)"), qsTr("All files (*)")]
        defaultSuffix: "zip"
        onAccepted: {
            root.pendingBackupFile = selectedFile.toString().replace(/^file:\/\//, "")
            root.pendingApply = true
            AppController.backupRunner.runBackup(root.pendingBackupFile)
        }
    }

    FileDialog {
        id: saveReportDialog
        objectName: "saveReportDialog"
        title: qsTr("Save failure report")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("Text file (*.txt)"), qsTr("All files (*)")]
        defaultSuffix: "txt"
        onAccepted: {
            const path = selectedFile.toString().replace(/^file:\/\//, "")
            reportWriter.path = path
            reportWriter.writeNow()
        }
    }

    // QML cannot write files directly; a QML-side XHR `PUT` to a `file://`
    // URL works cross-platform and avoids a C++ round-trip for what is a
    // one-off text dump of `AppController.buildFailureReport()`.
    QtObject {
        id: reportWriter
        property string path: ""
        function writeNow() {
            if (!path) return
            const xhr = new XMLHttpRequest()
            xhr.open("PUT", "file://" + path, true)
            xhr.onreadystatechange = function() {
                if (xhr.readyState === XMLHttpRequest.DONE) {
                    toast.show(xhr.status === 0 || xhr.status === 200 || xhr.status === 201
                        ? qsTr("Saved report to ") + path
                        : qsTr("Could not save report (status ") + xhr.status + ")")
                }
            }
            xhr.send(AppController.buildFailureReport())
        }
    }

    // --- Building blocks ---

    // Kit readout tile (`k-readout sm`): label, value, sub-line.
    component Readout: Rectangle {
        id: readout
        property string label: ""
        property string value: ""
        /// Small subtle text after the value ("+1 unreleased").
        property string suffix: ""

        implicitHeight: readoutColumn.implicitHeight + 18
        radius: Theme.r3
        color: Theme.clrCard
        border.color: Theme.clrContainerHighest

        Column {
            id: readoutColumn
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: 11
            anchors.rightMargin: 11
            spacing: 2

            Label {
                text: readout.label
                color: Theme.clrOnSurfaceSubtle
                font.pixelSize: Theme.sizeLabel
                font.weight: Font.DemiBold
            }
            // Value and suffix in one rich-text label so they share a
            // baseline and elide together in a narrow window.
            Label {
                width: parent.width
                textFormat: Text.StyledText
                text: readout.value
                      + (readout.suffix !== ""
                         ? "&nbsp;&nbsp;<span style=\"font-size:11px; font-weight:400; color:"
                           + Theme.clrOnSurfaceSubtle + "\">" + readout.suffix + "</span>"
                         : "")
                color: Theme.clrOnSurface
                font.pixelSize: 17
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
        }
    }

    // The migrations this run covers: pending rows before the run, and the
    // rows this run touched (from `runStates`) during and after it. Each row
    // shows its run state — tick (done), spinner (running), clock (waiting),
    // cross (failed) — or, before a run, the release it belongs to.
    component PlanList: Rectangle {
        id: plan
        /// Show run-state icons and hide not-yet-touched applied rows.
        property bool showRunState: false
        /// Show the leading pre-run backup row.
        property bool showBackupRow: false
        readonly property int maxVisibleRows: 6

        radius: Theme.r2
        color: Theme.clrCard
        border.color: Theme.clrContainerHighest
        clip: true
        implicitHeight: (showBackupRow ? 32 : 0)
                        + Math.min(list.contentHeight, maxVisibleRows * 32) + 2

        Column {
            anchors.fill: parent
            anchors.margins: 1

            // Pre-run backup row.
            Rectangle {
                visible: plan.showBackupRow
                width: parent.width
                height: visible ? 32 : 0
                color: root.backingUp ? Theme.clrPrimarySoft : "transparent"

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 10
                    spacing: 10
                    Item {
                        Layout.preferredWidth: 15
                        Layout.preferredHeight: 15
                        Spinner { anchors.centerIn: parent; visible: root.backingUp; running: visible; diameter: 11 }
                        Glyph {
                            anchors.fill: parent
                            visible: !root.backingUp
                            name: root.lastBackupFile !== "" ? "check-circle" : "clock"
                            size: 15
                            color: root.lastBackupFile !== "" ? Theme.clrSuccessDot : Theme.clrOnSurfaceFaint
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Backup · ") + (root.pendingBackupFile || root.lastBackupFile).replace(/^.*[\\/]/, "")
                        color: Theme.clrOnSurfaceMed
                        font.pixelSize: Theme.sizeBodySm
                        font.weight: root.backingUp ? Font.DemiBold : Font.Normal
                        elide: Text.ElideMiddle
                    }
                    Label {
                        text: root.backingUp ? qsTr("writing…") : qsTr("saved")
                        color: Theme.clrOnSurfaceSubtle
                        font.pixelSize: Theme.sizeLabel
                    }
                }
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.clrDivider }
            }

            ListView {
                id: list
                width: parent.width
                height: Math.min(contentHeight, plan.maxVisibleRows * 32)
                interactive: contentHeight > height
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                model: plan.showRunState ? root.runPlan : root.pendingPlan
                ScrollBar.vertical: ScrollBar { policy: list.interactive ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff }

                delegate: Item {
                    id: planRow
                    required property var timestamp
                    required property string title
                    required property string status
                    required property string releaseVersion

                    readonly property string runState: root.runStatesRevision >= 0
                                                       ? root.runStates[String(timestamp)] || "" : ""

                    width: ListView.view.width
                    height: 32

                    Rectangle {
                        anchors.fill: parent
                        color: planRow.runState === "running" ? Theme.clrPrimarySoft
                             : planRow.runState === "failed" ? Theme.clrErrorRowBg : "transparent"
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        spacing: 10

                        Item {
                            visible: plan.showRunState
                            Layout.preferredWidth: 15
                            Layout.preferredHeight: 15
                            Spinner {
                                anchors.centerIn: parent
                                visible: planRow.runState === "running"
                                running: visible
                                diameter: 11
                            }
                            Glyph {
                                anchors.fill: parent
                                visible: planRow.runState !== "running"
                                name: planRow.runState === "done" ? "check-circle"
                                    : planRow.runState === "failed" ? "alert" : "clock"
                                size: 15
                                color: planRow.runState === "done" ? Theme.clrSuccessDot
                                     : planRow.runState === "failed" ? Theme.clrErrorDot : Theme.clrOnSurfaceFaint
                            }
                        }
                        Label {
                            Layout.preferredWidth: 112
                            text: planRow.timestamp
                            color: Theme.clrOnSurfaceSubtle
                            font: Theme.monoFont(11)
                        }
                        Label {
                            Layout.fillWidth: true
                            text: planRow.title
                            color: planRow.runState === "done" ? Theme.clrOnSurfaceMed
                                 : planRow.runState === "" && plan.showRunState ? Theme.clrOnSurfaceSubtle
                                 : Theme.clrOnSurface
                            font.pixelSize: Theme.sizeBodySm
                            font.weight: planRow.runState === "running" ? Font.DemiBold : Font.Normal
                            elide: Text.ElideRight
                        }
                        Label {
                            visible: plan.showRunState
                            text: planRow.runState === "running" ? qsTr("running…")
                                : planRow.runState === "done" ? qsTr("applied")
                                : planRow.runState === "failed" ? qsTr("failed") : qsTr("waiting")
                            color: planRow.runState === "failed" ? Theme.clrError : Theme.clrOnSurfaceSubtle
                            font.pixelSize: Theme.sizeLabel
                        }
                        StatusPill {
                            visible: !plan.showRunState
                            status: planRow.releaseVersion !== "" ? "empty" : "pending"
                            label: planRow.releaseVersion !== "" ? planRow.releaseVersion : qsTr("unreleased")
                            tooltipText: " "
                        }
                    }

                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: 1
                        color: Theme.clrDivider
                    }
                }
            }
        }
    }

    // --- Layout ---
    // A ScrollView gives us overflow scrolling when the user shrinks the
    // window below the content's natural height; the ColumnLayout is
    // anchored with margins so steps track the live window width.
    ScrollView {
        id: scroll
        anchors.fill: parent
        clip: true
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        contentWidth: availableWidth

        Item {
            width: scroll.availableWidth
            implicitHeight: simpleContent.implicitHeight + 2 * 18

            ColumnLayout {
                id: simpleContent
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.leftMargin: Theme.sp5
                anchors.rightMargin: Theme.sp5
                anchors.topMargin: 18
                spacing: Theme.sp3

                // 1. Connection — always present. Once connected it folds to
                //    a summary; "Change" re-opens it so users can switch
                //    profiles without leaving the Simple view.
                StepSection {
                    id: connectionStep
                    Layout.fillWidth: true
                    number: 1
                    title: qsTr("Connection")
                    stepState: AppController.connected ? "done" : "active"
                    collapsed: AppController.connected && !root.connectionExpanded
                    meta: AppController.connected
                          ? (AppController.connectedTarget || qsTr("custom connection")) + qsTr(" · connected")
                          : ""
                    headerActions: [
                        LsButton {
                            visible: AppController.connected && !root.busy
                            variant: "ghost"
                            text: root.connectionExpanded ? qsTr("Done") : qsTr("Change")
                            onClicked: root.connectionExpanded = !root.connectionExpanded
                        }
                    ]

                    ConnectionPanel {
                        width: parent.width
                    }
                }

                // 2. Review — what the run will change. Collapses to a
                //    summary line while the run is in flight or done.
                StepSection {
                    id: reviewStep
                    Layout.fillWidth: true
                    visible: AppController.connected
                    number: 2
                    title: root.hasWork || root.busy || root.lastResult !== "none"
                           ? qsTr("Review what will change") : qsTr("Review")
                    readonly property bool past: root.busy || root.lastResult === "success"
                    stepState: past ? "done" : (root.hasWork ? "active" : "done")
                    collapsed: past
                    meta: past
                          ? qsTr("%1 → %2").arg(reviewStep.fromLabel).arg(AppController.targetReleaseLabel)
                          : (AppController.pendingCount > 0
                             ? qsTr("%n migration(s)", "", AppController.pendingCount)
                             : qsTr("up to date"))

                    // Release before the run started; frozen while running
                    // so the summary line keeps reading "from → to".
                    property string fromLabel: ""
                    Connections {
                        target: AppController.runner
                        function onPhaseChanged() {
                            if (AppController.runner.phase === MigrationRunner.Running)
                                reviewStep.fromLabel = AppController.currentReleaseLabel
                        }
                    }

                    GridLayout {
                        width: parent.width
                        columns: 2
                        columnSpacing: 8
                        Readout {
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1
                            label: qsTr("Now")
                            value: AppController.currentReleaseLabel
                        }
                        Readout {
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1
                            label: AppController.pendingCount > 0 ? qsTr("After this run") : qsTr("Latest release")
                            value: AppController.targetReleaseLabel
                            suffix: AppController.pendingUnreleasedCount > 0
                                    ? qsTr("+%1 unreleased").arg(AppController.pendingUnreleasedCount) : ""
                        }
                    }

                    Banner {
                        width: parent.width
                        visible: AppController.pendingCount === 0
                        kind: "ok"
                        title: qsTr("Database is up to date")
                        text: qsTr("There is nothing to apply.")
                    }

                    PlanList {
                        width: parent.width
                        visible: AppController.pendingCount > 0
                    }

                    Label {
                        width: parent.width
                        visible: AppController.pendingUnreleasedCount > 0
                        text: AppController.pendingUnreleasedCount === AppController.pendingCount
                              ? qsTr("All pending migrations are unreleased (no release tag yet).")
                              : qsTr("%n migration(s) are newer than the latest release.", "",
                                     AppController.pendingUnreleasedCount)
                        color: Theme.clrWarning
                        font.pixelSize: Theme.sizeLabel
                        wrapMode: Text.Wrap
                    }
                }

                // 3. Run — idle, running, success and failure states.
                StepSection {
                    id: runStep
                    objectName: "runStep"
                    Layout.fillWidth: true
                    visible: AppController.connected
                             && (root.hasWork || root.busy || root.lastResult !== "none")
                    number: 3
                    title: root.busy ? qsTr("Running")
                         : root.lastResult === "success" ? qsTr("Done")
                         : root.lastResult === "failure" ? qsTr("Run failed") : qsTr("Run")
                    stepState: root.lastResult === "success" ? "done" : "active"

                    // --- Idle ---
                    Column {
                        width: parent.width
                        spacing: 10
                        visible: !root.busy && root.lastResult === "none"

                        CheckBox {
                            id: backupFirstCheckBox
                            objectName: "backupFirstCheckBox"
                            text: qsTr("Back up the database first")
                            checked: root.backupFirst
                            onCheckedChanged: root.backupFirst = checked
                            ToolTip.visible: hovered
                            ToolTip.delay: 500
                            ToolTip.text: qsTr("Write a backup archive before applying, so you can restore it from Backups.")
                        }

                        BackupRequiredAlert {
                            width: parent.width
                            visible: AppController.backupRequired || AppController.backupRequirementPending
                            pending: AppController.backupRequirementPending
                            reasons: AppController.backupRequirementReasons
                            note: AppController.backupRequirementNote
                            busy: !root.allIdle
                            waived: AppController.migrateWithoutBackup
                            onBackupRequested: AppController.backUpNow()
                            onWaivedToggled: (checked) => AppController.setMigrateWithoutBackup(checked)
                        }

                        LsButton {
                            objectName: "runButton"
                            width: parent.width
                            variant: "primary"
                            size: "lg"
                            glyph: "play"
                            text: AppController.pendingCount > 0
                                  ? qsTr("Run %n migration(s)", "", AppController.pendingCount)
                                  : qsTr("Run pre-migrate steps")
                            // Mirrors the mutual busy guard in C++: a migration
                            // started during a managed backup (or an ad-hoc
                            // backup/restore) would rewrite the schema the
                            // archive is being read from.
                            enabled: root.allIdle && root.hasWork && (!root.backupBlocked || root.backupFirst)
                                     && !AppController.backupRequirementPending && !AppController.busy
                            onClicked: root.startRun()
                        }

                        Label {
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            text: qsTr("Need a dry run or a specific release? Switch to <a href=\"expert\">Expert</a>.")
                            textFormat: Text.StyledText
                            linkColor: Theme.clrPrimary
                            color: Theme.clrOnSurfaceSubtle
                            font.pixelSize: Theme.sizeLabel
                            wrapMode: Text.Wrap
                            onLinkActivated: AppController.setViewMode("expert")
                            HoverHandler { cursorShape: parent.hoveredLink ? Qt.PointingHandCursor : Qt.ArrowCursor }
                        }
                    }

                    // --- Running ---
                    Column {
                        width: parent.width
                        spacing: 10
                        visible: root.busy

                        TaskProgressList {
                            id: runTasks
                            width: parent.width
                            model: AppController.runner.tasks
                            visible: !root.backingUp && count > 0
                        }

                        RowLayout {
                            width: parent.width
                            visible: !runTasks.visible
                            Label {
                                Layout.fillWidth: true
                                text: root.backingUp
                                      ? qsTr("Writing backup archive…")
                                      : (root.progressTotal > 0
                                         ? qsTr("Applying %1 of %2").arg(root.progressIndex).arg(root.progressTotal)
                                         : qsTr("Preparing…"))
                                color: Theme.clrOnSurface
                                font.pixelSize: Theme.sizeBodySm + 1
                                font.weight: Font.DemiBold
                                elide: Text.ElideRight
                            }
                            Label {
                                visible: !root.backingUp && root.progressTitle !== ""
                                Layout.maximumWidth: parent.width * 0.5
                                text: root.progressTitle
                                color: Theme.clrOnSurfaceSubtle
                                font.pixelSize: Theme.sizeLabel
                                elide: Text.ElideRight
                            }
                        }

                        ProgressTrack {
                            width: parent.width
                            visible: !runTasks.visible
                            implicitHeight: 8
                            from: 0
                            to: Math.max(1, root.progressTotal)
                            value: root.progressIndex
                            indeterminate: root.backingUp || root.progressTotal === 0
                        }

                        PlanList {
                            width: parent.width
                            showRunState: true
                            showBackupRow: root.pendingBackupFile !== "" || root.lastBackupFile !== ""
                        }

                        LsButton {
                            objectName: "cancelButton"
                            width: parent.width
                            size: "md"
                            text: AppController.runner.phase === MigrationRunner.Cancelling
                                  ? qsTr("Cancelling…") : qsTr("Cancel")
                            busy: AppController.runner.phase === MigrationRunner.Cancelling
                            enabled: AppController.runner.phase === MigrationRunner.Running
                            onClicked: AppController.runner.cancel()
                        }
                    }

                    // --- Success ---
                    Column {
                        width: parent.width
                        spacing: 10
                        visible: !root.busy && root.lastResult === "success"

                        Rectangle {
                            width: parent.width
                            implicitHeight: heroRow.implicitHeight + 28
                            radius: Theme.r3
                            color: Theme.clrSuccessBg
                            border.color: Theme.clrSuccessBorder

                            RowLayout {
                                id: heroRow
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.margins: 14
                                spacing: 14

                                Rectangle {
                                    Layout.preferredWidth: 40
                                    Layout.preferredHeight: 40
                                    radius: 20
                                    color: Theme.clrSuccessDot
                                    Glyph {
                                        anchors.centerIn: parent
                                        name: "check"
                                        size: 20
                                        strokeWidth: 2
                                        color: "#ffffff"
                                    }
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2
                                    Label {
                                        objectName: "successTitle"
                                        text: AppController.pendingCount === 0
                                              ? qsTr("Database is up to date") : qsTr("Migrations applied")
                                        color: Theme.clrSuccess
                                        font.pixelSize: Theme.sizeTitle
                                        font.weight: Font.DemiBold
                                    }
                                    Label {
                                        Layout.fillWidth: true
                                        text: root.lastSummary !== ""
                                              ? root.lastSummary
                                              : qsTr("Now at %1.").arg(AppController.currentReleaseLabel)
                                        color: Theme.clrOnSurfaceMed
                                        font.pixelSize: Theme.sizeBodySm
                                        wrapMode: Text.Wrap
                                    }
                                }
                            }
                        }

                        TaskProgressList {
                            width: parent.width
                            model: AppController.runner.tasks
                            visible: count > 0
                        }

                        PlanList {
                            width: parent.width
                            showRunState: true
                            showBackupRow: root.lastBackupFile !== ""
                        }

                        Card {
                            width: parent.width
                            visible: root.lastBackupFile !== ""
                            title: qsTr("Backup taken before the run")
                            glyph: "archive-outline"

                            RowLayout {
                                width: parent.width
                                spacing: 10
                                Label {
                                    Layout.fillWidth: true
                                    text: qsTr("If something looks wrong, restore %1 to return to %2.")
                                              .arg(root.lastBackupFile.replace(/^.*[\\/]/, ""))
                                              .arg(reviewStep.fromLabel || qsTr("the previous state"))
                                    color: Theme.clrOnSurfaceSubtle
                                    font.pixelSize: Theme.sizeLabel
                                    wrapMode: Text.Wrap
                                }
                                LsButton {
                                    text: qsTr("Open Backups")
                                    onClicked: root.openBackups()
                                }
                            }
                        }

                        RowLayout {
                            width: parent.width
                            spacing: 8
                            Item { Layout.fillWidth: true }
                            LsButton {
                                objectName: "viewLogButton"
                                variant: "ghost"
                                glyph: "terminal"
                                text: qsTr("View log")
                                onClicked: AppController.setLogVisible(true)
                            }
                            LsButton {
                                objectName: "doneButton"
                                size: "md"
                                text: qsTr("Done")
                                onClicked: root.dismissResult()
                            }
                        }
                    }

                    // --- Failure: the escalation path to support ---
                    // Always includes the diagnostic bundle; the user should
                    // not have to click "expand" to see the summary.
                    Column {
                        id: failureBlock
                        width: parent.width
                        spacing: 10
                        visible: !root.busy && root.lastResult === "failure"

                        // Collapsible bundle: headline by default, expanded
                        // only when the user decides to file a ticket. The
                        // bundle is always computed (cheap) so Copy / Save
                        // never race the toggle.
                        property bool bundleVisible: false

                        Banner {
                            objectName: "failureBanner"
                            width: parent.width
                            kind: "err"
                            title: qsTr("Migration run failed")
                            text: [root.lastSummary, AppController.lastError]
                                      .filter(function(s) { return s && s.length > 0 }).join("\n")
                        }

                        TaskProgressList {
                            width: parent.width
                            model: AppController.runner.tasks
                            visible: count > 0
                        }

                        PlanList {
                            width: parent.width
                            visible: root.runStateCount > 0
                            showRunState: true
                        }

                        Flow {
                            width: parent.width
                            spacing: 8

                            LsButton {
                                variant: "ghost"
                                text: failureBlock.bundleVisible ? qsTr("Hide details") : qsTr("Show details")
                                onClicked: failureBlock.bundleVisible = !failureBlock.bundleVisible
                            }
                            LsButton {
                                objectName: "copyReportButton"
                                glyph: "copy"
                                text: qsTr("Copy report")
                                onClicked: {
                                    bundleTextArea.text = AppController.buildFailureReport()
                                    bundleTextArea.selectAll()
                                    bundleTextArea.copy()
                                    toast.show(qsTr("Report copied — paste it into your support ticket."))
                                }
                            }
                            LsButton {
                                objectName: "saveReportButton"
                                text: qsTr("Save report…")
                                onClicked: saveReportDialog.open()
                            }
                            LsButton {
                                objectName: "showFullLogButton"
                                glyph: "terminal"
                                text: qsTr("Show full log")
                                onClicked: AppController.setLogVisible(true)
                            }
                            LsButton {
                                objectName: "retryButton"
                                variant: "primary"
                                glyph: "refresh"
                                text: qsTr("Retry")
                                enabled: root.hasWork && root.allIdle && !root.backupBlocked
                                         && !AppController.backupRequirementPending && !AppController.busy
                                onClicked: {
                                    root.dismissResult()
                                    AppController.runner.applyUpTo("")
                                }
                            }
                        }

                        Rectangle {
                            width: parent.width
                            height: bundleTextArea.implicitHeight + 16
                            visible: failureBlock.bundleVisible
                            radius: Theme.r2
                            color: Theme.clrSidebarBg

                            TextArea {
                                id: bundleTextArea
                                anchors.fill: parent
                                anchors.margins: 8
                                readOnly: true
                                wrapMode: TextEdit.NoWrap
                                font: Theme.monoFont(Theme.sizeMono)
                                color: Theme.clrCodeText
                                selectByMouse: true
                                text: failureBlock.bundleVisible ? AppController.buildFailureReport() : ""
                                background: null
                            }
                        }
                    }
                }

                // Bottom padding so the last step doesn't kiss the window edge.
                Item { Layout.fillWidth: true; Layout.preferredHeight: 6 }
            }
        }
    }

    // --- Toast ---
    // Non-blocking confirmation for Copy/Save, in the kit's dark toast style.
    // One line in the bottom right; auto-dismisses after ~3 seconds.
    Rectangle {
        id: toast
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: Theme.sp6
        radius: Theme.r2
        color: Theme.clrOnSurface
        opacity: toastAnim.running || visible ? 1 : 0
        visible: false
        width: toastLabel.implicitWidth + 32
        height: toastLabel.implicitHeight + 20

        property string message: ""
        function show(m) {
            message = m
            visible = true
            toastHide.restart()
        }

        Label {
            id: toastLabel
            anchors.centerIn: parent
            text: toast.message
            color: "#ffffff"
            font.pixelSize: Theme.sizeBodySm
        }

        Timer {
            id: toastHide
            interval: 3000
            repeat: false
            onTriggered: toast.visible = false
        }
        Behavior on opacity { NumberAnimation { id: toastAnim; duration: 160 } }
    }
}
