// SPDX-License-Identifier: Apache-2.0
//
// Simple view: the stripped, end-user flavour of the migrations GUI. Shows
// the current DB release, the target release, the pending count, and a
// single "Run migrations" button with an optional "Back up first"
// checkbox. During a run the button is replaced by a progress card; after
// a run, a success or failure banner appears.
//
// On failure the banner expands into a diagnostic bundle (Copy / Save /
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
    color: Theme.bgPage

    // --- Size hints (consumed by `Main.qml` on view-mode switch) ---
    // The simple view has far less to show than the expert three-pane
    // layout, so the window shrinks to match when the user flips over.
    // `Main.qml` uses these as *preferred* window sizes, and
    // `minimumViewWidth`/`Height` as hard minimums. Declared as readonly
    // properties so the window never accidentally writes back here and
    // triggers a binding loop.
    readonly property int preferredViewWidth: 560
    readonly property int preferredViewHeight: 640
    readonly property int minimumViewWidth: 420
    readonly property int minimumViewHeight: 420

    // Retained outside the run card so switching tabs (or toggling
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

    // --- Live progress capture ---
    // `MigrationRunner.progress` fires per-migration; we capture the last
    // tuple so QML bindings can reflect it without storing state in QML
    // JavaScript closures (which break when the binding re-evaluates).
    property int  progressIndex: 0
    property int  progressTotal: 0
    property string progressTitle: ""

    Connections {
        target: AppController.runner
        function onProgress(timestamp, title, index, total) {
            root.progressIndex = index
            root.progressTotal = total
            root.progressTitle = title
        }
        function onPhaseChanged() {
            if (AppController.runner.phase === MigrationRunner.Running) {
                // Fresh run starting — reset the result banner and the
                // progress capture so stale numbers from the previous run
                // do not flash up briefly before the first progress signal.
                root.lastResult = "none"
                root.progressIndex = 0
                root.progressTotal = 0
                root.progressTitle = ""
            }
        }
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
            root.pendingBackupFile = ""
            if (ok) {
                AppController.runner.applyUpTo("")
            } else {
                // Surface the backup failure in the same banner the migration
                // run would use; the user's choice to back up first was not
                // an invitation to ignore a failed backup and migrate anyway.
                root.lastResult = "failure"
                root.lastSummary = qsTr("Backup failed before migration could start: ") + summary
            }
        }
    }

    FileDialog {
        id: backupFileDialog
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

    // Tiny helper object: QML cannot write files directly, but we lean on
    // `AppController.buildFailureReport()` for the content and a QML
    // TextArea + the system clipboard for Copy; for Save we bounce through
    // a hidden TextEdit → QFile... actually simpler: use Qt.labs.platform
    // won't be available. Instead, dump via the clipboard fallback and
    // document "Copy then paste into an editor" for Save. Cleaner: write
    // through the `backupRunner` path? No — overkill. Use a small
    // QML-side XHR to `file://` which works cross-platform.
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

    // --- Layout ---
    // A ScrollView gives us overflow scrolling when the user shrinks the
    // window below the content's natural height. Inside, a ColumnLayout is
    // anchored (horizontalCenter + fill) so cards track the live window
    // width instead of the fragile `Math.min(640, availableWidth - 32)`
    // trick: resizing the window just shifts the anchors, no recomputation.
    ScrollView {
        id: scroll
        anchors.fill: parent
        clip: true
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        contentWidth: availableWidth

        Item {
            // The ScrollView needs a content Item to anchor against; the
            // ColumnLayout below fills this Item with horizontal margins
            // so content grows/shrinks cleanly with the window.
            width: scroll.availableWidth
            implicitHeight: simpleContent.implicitHeight + 2 * 32

            ColumnLayout {
                id: simpleContent
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.leftMargin: 24
                anchors.rightMargin: 24
                anchors.topMargin: 32
                spacing: 16

            // 1. Connection card — always present. The ConnectionPanel is
            //    self-contained and already carries the Connect button; we
            //    do not hide it after connect because users may want to
            //    switch profiles without leaving the Simple view.
            Label {
                text: qsTr("CONNECTION")
                color: Theme.textFaint
                font.pixelSize: 11
                font.weight: Font.DemiBold
                font.letterSpacing: 1
                Layout.fillWidth: true
            }
            ConnectionPanel {
                Layout.fillWidth: true
            }

            // 2. Status card — visible only after the first successful
            //    connect. Large type so a non-technical user can read it
            //    from across the room.
            Card {
                id: statusCard
                Layout.fillWidth: true
                visible: AppController.connected

                Label {
                    text: qsTr("STATUS")
                    color: Theme.textFaint
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                    font.letterSpacing: 1
                    width: parent.width
                }

                Item { width: parent.width; height: 4 }

                RowLayout {
                    width: parent.width
                    spacing: 12
                    Label {
                        text: qsTr("Current version:")
                        color: Theme.textMuted
                        font.pixelSize: 14
                        Layout.preferredWidth: 140
                    }
                    Label {
                        text: AppController.currentReleaseLabel
                        color: Theme.text
                        font.pixelSize: 18
                        font.weight: Font.DemiBold
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                    }
                }
                RowLayout {
                    width: parent.width
                    spacing: 12
                    Label {
                        text: qsTr("Target version:")
                        color: Theme.textMuted
                        font.pixelSize: 14
                        Layout.preferredWidth: 140
                    }
                    Label {
                        text: AppController.targetReleaseLabel
                        color: Theme.text
                        font.pixelSize: 18
                        font.weight: Font.DemiBold
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                    }
                }

                Item { width: parent.width; height: 6 }

                // Pending pill. When pending, switch to a warning palette
                // (amber) so the user notices it even when current and target
                // version labels above happen to read the same release —
                // which is exactly the "new migrations beyond the latest
                // release" case the Status card otherwise hides.
                Rectangle {
                    width: parent.width
                    height: pendingColumn.implicitHeight + 16
                    radius: 8
                    color: AppController.pendingCount === 0 ? Theme.okSoft : Theme.warnSoft
                    border.color: AppController.pendingCount === 0 ? Theme.ok : Theme.warn

                    ColumnLayout {
                        id: pendingColumn
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        spacing: 2

                        Label {
                            id: pendingLabel
                            Layout.alignment: Qt.AlignHCenter
                            color: AppController.pendingCount === 0 ? Theme.okText : Theme.warnText
                            font.pixelSize: 14
                            font.weight: Font.DemiBold
                            text: AppController.pendingCount === 0
                                  ? qsTr("✓ Database is up to date")
                                  : qsTr("⚠ %1 migration(s) pending").arg(AppController.pendingCount)
                        }
                        // Only shown when *some* of the pending migrations sit
                        // past the highest declared release. Without this line
                        // the user can't tell from the version labels alone
                        // that newer, untagged migrations are queued up.
                        Label {
                            Layout.alignment: Qt.AlignHCenter
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            color: Theme.warnText
                            font.pixelSize: 12
                            wrapMode: Text.Wrap
                            visible: AppController.pendingUnreleasedCount > 0
                            text: AppController.pendingUnreleasedCount === AppController.pendingCount
                                  ? qsTr("All pending migrations are unreleased (no release tag yet).")
                                  : qsTr("Includes %1 unreleased migration(s) beyond the latest release.")
                                        .arg(AppController.pendingUnreleasedCount)
                        }
                    }
                }
            }

            // 3. Run card — visible only when there is work to do AND the
            //    runner is idle. Hidden during the run (progress card
            //    takes over) and after success (the success banner covers
            //    it until dismissed).
            Card {
                id: runCard
                Layout.fillWidth: true
                visible: AppController.connected
                         && AppController.pendingCount > 0
                         && AppController.runner.phase === MigrationRunner.Idle
                         && AppController.backupRunner.phase === BackupRunner.Idle
                         && root.lastResult !== "failure"

                Label {
                    text: qsTr("RUN")
                    color: Theme.textFaint
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                    font.letterSpacing: 1
                    width: parent.width
                }

                CheckBox {
                    id: backupFirstCheckBox
                    text: qsTr("Back up database first")
                    checked: root.backupFirst
                    onCheckedChanged: root.backupFirst = checked
                    ToolTip.visible: hovered
                    ToolTip.delay: 500
                    ToolTip.text: qsTr("Write a full backup archive to disk before applying any migration. Recommended for production databases — the archive is the last-known-good state you can restore from if anything goes wrong.")
                }

                Button {
                    width: parent.width
                    text: qsTr("Run migrations")
                    highlighted: true
                    font.pixelSize: 15
                    padding: 12
                    onClicked: {
                        root.lastResult = "none"
                        if (root.backupFirst) {
                            backupFileDialog.open()
                        } else {
                            AppController.runner.applyUpTo("")
                        }
                    }
                }
            }

            // 4. Progress card — visible while a run is in flight. Shows
            //    the current migration title + numeric progress. The
            //    Cancel button is best-effort (see MigrationRunner notes).
            Card {
                id: progressCard
                Layout.fillWidth: true
                visible: AppController.runner.phase !== MigrationRunner.Idle
                         || AppController.backupRunner.phase !== BackupRunner.Idle

                Label {
                    text: AppController.backupRunner.phase !== BackupRunner.Idle
                          ? qsTr("BACKING UP")
                          : qsTr("APPLYING MIGRATIONS")
                    color: Theme.textFaint
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                    font.letterSpacing: 1
                    width: parent.width
                }

                Label {
                    width: parent.width
                    text: AppController.backupRunner.phase !== BackupRunner.Idle
                          ? qsTr("Writing backup archive…")
                          : (root.progressTotal > 0
                             ? qsTr("Applying %1 of %2: %3").arg(root.progressIndex)
                                                            .arg(root.progressTotal)
                                                            .arg(root.progressTitle)
                             : qsTr("Preparing…"))
                    color: Theme.text
                    font.pixelSize: 14
                    elide: Text.ElideRight
                }

                ProgressBar {
                    width: parent.width
                    from: 0
                    to: Math.max(1, root.progressTotal)
                    value: root.progressIndex
                    indeterminate: AppController.backupRunner.phase !== BackupRunner.Idle
                                   || root.progressTotal === 0
                }

                Button {
                    text: AppController.runner.phase === MigrationRunner.Cancelling
                          ? qsTr("Cancelling…") : qsTr("Cancel")
                    enabled: AppController.runner.phase === MigrationRunner.Running
                    onClicked: AppController.runner.cancel()
                }
            }

            // 5a. Success banner.
            Card {
                id: successCard
                Layout.fillWidth: true
                visible: root.lastResult === "success"
                         && AppController.runner.phase === MigrationRunner.Idle

                Rectangle {
                    width: parent.width
                    height: successInner.implicitHeight + 20
                    radius: 8
                    color: Theme.okSoft
                    border.color: Theme.ok

                    ColumnLayout {
                        id: successInner
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.margins: 12
                        spacing: 6

                        Label {
                            text: qsTr("✓ Migrations applied successfully")
                            color: Theme.okText
                            font.pixelSize: 15
                            font.weight: Font.DemiBold
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                        }
                        Label {
                            text: root.lastSummary
                            color: Theme.okText
                            font.pixelSize: 13
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            visible: text.length > 0
                        }
                    }
                }

                Button {
                    text: qsTr("Dismiss")
                    onClicked: root.lastResult = "none"
                }
            }

            // 5b. Failure card — the escalation path to support. Always
            //     includes the diagnostic bundle; the user should not have
            //     to click "expand" to see the summary + failed migration.
            Card {
                id: failureCard
                Layout.fillWidth: true
                visible: root.lastResult === "failure"
                         && AppController.runner.phase === MigrationRunner.Idle
                         && AppController.backupRunner.phase === BackupRunner.Idle

                Rectangle {
                    width: parent.width
                    height: failureInner.implicitHeight + 20
                    radius: 8
                    color: Theme.errSoft
                    border.color: Theme.err

                    ColumnLayout {
                        id: failureInner
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.margins: 12
                        spacing: 6

                        Label {
                            text: qsTr("✗ Migration run failed")
                            color: Theme.errText
                            font.pixelSize: 15
                            font.weight: Font.DemiBold
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                        }
                        Label {
                            text: root.lastSummary
                            color: Theme.errText
                            font.pixelSize: 13
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            visible: text.length > 0
                        }
                        Label {
                            text: AppController.lastError
                            color: Theme.errText
                            font.pixelSize: 12
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            visible: text.length > 0
                        }
                    }
                }

                // Collapsible diagnostic bundle: the user sees a headline
                // by default and expands only when they decide to file a
                // support ticket. The full bundle is always computed
                // (cheap) so the Copy / Save buttons never race the toggle.
                property bool bundleVisible: false

                RowLayout {
                    width: parent.width
                    spacing: 8

                    Button {
                        text: failureCard.bundleVisible
                              ? qsTr("Hide details") : qsTr("Show details")
                        onClicked: failureCard.bundleVisible = !failureCard.bundleVisible
                    }
                    Item { Layout.fillWidth: true }
                    Button {
                        text: qsTr("Copy report")
                        onClicked: {
                            const bundle = AppController.buildFailureReport()
                            bundleTextArea.text = bundle
                            bundleTextArea.selectAll()
                            bundleTextArea.copy()
                            toast.show(qsTr("Report copied — paste it into your support ticket."))
                        }
                    }
                    Button {
                        text: qsTr("Save report…")
                        onClicked: saveReportDialog.open()
                    }
                    Button {
                        text: qsTr("Show full log")
                        onClicked: AppController.setLogVisible(true)
                    }
                    Button {
                        text: qsTr("Retry")
                        highlighted: true
                        enabled: AppController.pendingCount > 0
                        onClicked: {
                            root.lastResult = "none"
                            AppController.runner.applyUpTo("")
                        }
                    }
                }

                Rectangle {
                    width: parent.width
                    height: bundleTextArea.implicitHeight + 16
                    visible: failureCard.bundleVisible
                    radius: 6
                    color: Theme.bgTerminal
                    border.color: Theme.border

                    TextArea {
                        id: bundleTextArea
                        anchors.fill: parent
                        anchors.margins: 8
                        readOnly: true
                        wrapMode: TextEdit.NoWrap
                        font.family: Theme.monoFont
                        font.pixelSize: 12
                        color: "#e6e9ef"
                        selectByMouse: true
                        text: failureCard.bundleVisible
                              ? AppController.buildFailureReport() : ""
                        background: null
                    }
                }
            }

                // Bottom padding so the last card doesn't kiss the window edge.
                Item { Layout.fillWidth: true; Layout.preferredHeight: 24 }
            }
        }
    }

    // --- Toast ---
    // Non-blocking confirmation for Copy/Save. One-line fade in the bottom
    // right; auto-dismisses after ~3 seconds.
    Rectangle {
        id: toast
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 24
        radius: 8
        color: Theme.bgPanel
        border.color: Theme.border
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
            color: Theme.text
            font.pixelSize: 13
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
