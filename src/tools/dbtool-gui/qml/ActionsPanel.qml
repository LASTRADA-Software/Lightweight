// SPDX-License-Identifier: Apache-2.0
//
// Right-pane actions: Target picker (three radio-style option cards) plus
// a plan-summary banner, an Options card with toggles, and the primary
// action button. Each option has a trailing input where applicable (release
// picker for "Release version", numeric text field for "Specific timestamp").
// When the user ticks individual rows in the migration list, the primary
// button switches to acting on just that selection and the target picker is
// bypassed — shown via a blue banner above.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Lightweight.Migrations

ColumnLayout {
    id: root
    spacing: 10

    property string target: "latest" // latest | release | timestamp
    property string selectedRelease: ""
    property string specificTimestamp: ""
    property bool dryRun: true
    property bool acquireLock: true
    property bool backupBeforeApply: false

    readonly property bool hasSelection: AppController.selectionCount > 0

    /// Resolves the effective `targetTimestamp` for `applyUpTo` / `dryRunUpTo`.
    /// Empty string means "apply everything pending".
    function resolvedTargetTimestamp() {
        if (root.target === "latest") return "";
        if (root.target === "release")
            return root.selectedRelease.length > 0
                ? AppController.releaseHighestTimestamp(root.selectedRelease)
                : "";
        if (root.target === "timestamp") return root.specificTimestamp;
        return "";
    }

    Label {
        text: qsTr("TARGET")
        color: Theme.textFaint
        font.pixelSize: 11
        font.weight: Font.DemiBold
        font.letterSpacing: 1
        Layout.fillWidth: true
    }

    // Target options
    Repeater {
        model: [
            { key: "latest",    label: qsTr("Latest (all pending)"),
              help: qsTr("Apply every pending migration."),
              tip: qsTr("Runs every registered migration that has not yet been applied to the connected database, in chronological order. Equivalent to <b>dbtool migrate</b> with no target. Safe default for normal deployments — leaves nothing pending when finished.") },
            { key: "release",   label: qsTr("Release version"),
              help: qsTr("Apply up to the highestTimestamp of a declared release."),
              tip: qsTr("Apply every pending migration whose timestamp is ≤ the newest migration declared in the selected release bundle. Use this to bring the database up to a specific shipped product version (e.g. 2024.Q1) without accidentally pulling in work for a later release. Also unlocks the <b>Rollback to release</b> button below.") },
            { key: "timestamp", label: qsTr("Specific timestamp"),
              help: qsTr("Apply every pending migration with timestamp ≤ this value."),
              tip: qsTr("Apply every pending migration whose timestamp is ≤ the value you enter. Useful for pinning the schema to a precise point in history — e.g. reproducing a bug on the exact state a tester saw, or staging a partial rollout that stops short of a risky later migration. Accepts either the raw timestamp or a migration title; autocomplete suggests valid values.") },
        ]
        Rectangle {
            id: optionRect
            required property var modelData
            readonly property bool active: root.target === modelData.key
            readonly property bool expanded: active && (modelData.key === "release" || modelData.key === "timestamp")

            Layout.fillWidth: true
            Layout.preferredHeight: expanded ? 100 : 60
            color: Theme.bgPanel
            border.color: active ? Theme.accent : Theme.border
            border.width: active ? 2 : 1
            radius: 6
            opacity: root.hasSelection ? 0.5 : 1.0

            // Explanatory tooltip describing the full scope of the target
            // option. Richer than the inline one-liner, intended to remove
            // any ambiguity about what the button will actually do.
            ToolTip.visible: optionHover.hovered && !root.hasSelection
            ToolTip.text: modelData.tip
            ToolTip.delay: 500
            ToolTip.timeout: 10000

            HoverHandler { id: optionHover }

            MouseArea {
                anchors.fill: parent
                cursorShape: root.hasSelection ? Qt.ArrowCursor : Qt.PointingHandCursor
                enabled: !root.hasSelection
                onClicked: root.target = modelData.key
                z: -1
            }

            Rectangle {
                id: radio
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.leftMargin: 10
                anchors.topMargin: 12
                width: 16; height: 16; radius: 8
                border.width: 1.5
                border.color: optionRect.active ? Theme.accent : Theme.borderStrong
                color: "transparent"
                Rectangle {
                    anchors.centerIn: parent
                    width: 8; height: 8; radius: 4
                    color: Theme.accent
                    visible: optionRect.active
                }
            }

            Label {
                id: optionLabel
                anchors.left: radio.right
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.topMargin: 10
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                text: optionRect.modelData.label
                color: Theme.text
                font.pixelSize: 13
                font.weight: Font.Medium
            }

            Label {
                anchors.left: radio.right
                anchors.right: parent.right
                anchors.top: optionLabel.bottom
                anchors.topMargin: 2
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                text: optionRect.modelData.help
                color: Theme.textMuted
                font.pixelSize: 11
                wrapMode: Text.WordWrap
            }

            ComboBox {
                anchors.left: radio.right
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                anchors.bottomMargin: 8
                visible: optionRect.expanded && optionRect.modelData.key === "release"
                model: AppController.releases
                textRole: "version"
                onActivated: index => {
                    const idx = AppController.releases.index(index, 0);
                    root.selectedRelease = AppController.releases.data(idx, 257);
                }
            }

            TimestampAutocomplete {
                anchors.left: radio.right
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                anchors.bottomMargin: 8
                visible: optionRect.expanded && optionRect.modelData.key === "timestamp"
                value: root.specificTimestamp
                onValueChanged: root.specificTimestamp = value
                placeholderText: qsTr("Type a timestamp or title — e.g. 'Initial'")
            }
        }
    }

    // Selection-override banner — shown only when the user has ticked rows
    // in the migration list. The button and plan summary switch to acting
    // on the selection when this is visible.
    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: 50
        color: Theme.accentSoft
        radius: 6
        visible: root.hasSelection
        RowLayout {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 8
            Label {
                Layout.fillWidth: true
                text: qsTr("%1 migration(s) selected — target picker is bypassed.")
                        .arg(AppController.selectionCount)
                color: Theme.accent
                font.pixelSize: 12
                font.weight: Font.Medium
                wrapMode: Text.WordWrap
            }
            Button {
                text: qsTr("Clear")
                flat: true
                ToolTip.visible: hovered
                ToolTip.delay: 500
                ToolTip.timeout: 8000
                ToolTip.text: qsTr("Deselect every ticked migration in the list. The target picker above then drives the next action again.")
                onClicked: AppController.selectAllPending(false)
            }
        }
    }

    // Plan summary banner
    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: 50
        color: Theme.infoSoft
        radius: 6
        visible: !root.hasSelection
        Label {
            anchors.fill: parent
            anchors.margins: 10
            wrapMode: Text.WordWrap
            text: {
                if (root.target === "release" && root.selectedRelease.length > 0)
                    return qsTr("<b>Plan:</b> apply every pending migration up to release <b>%1</b>.")
                            .arg(root.selectedRelease);
                if (root.target === "timestamp" && root.specificTimestamp.length > 0)
                    return qsTr("<b>Plan:</b> apply every pending migration with timestamp ≤ <b>%1</b>.")
                            .arg(root.specificTimestamp);
                return qsTr("<b>Plan:</b> apply every pending migration.");
            }
            color: Theme.infoText
            font.pixelSize: 12
            textFormat: Text.RichText
        }
    }

    Label {
        text: qsTr("OPTIONS")
        color: Theme.textFaint
        font.pixelSize: 11
        font.weight: Font.DemiBold
        font.letterSpacing: 1
        Layout.fillWidth: true
        Layout.topMargin: 4
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: optionsColumn.implicitHeight + 16
        color: Theme.bgPanel
        border.color: Theme.border
        radius: 10

        Column {
            id: optionsColumn
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: 8
            spacing: 2

            Repeater {
                model: [
                    { label: qsTr("Dry-run"),
                      desc: qsTr("Show SQL & progress; roll back at end."),
                      prop: "dryRun",
                      tip: qsTr("Executes every step inside a transaction and rolls it back at the end instead of committing. Produces the exact SQL that a real apply would emit and streams it to the log pane so you can review it. <b>No schema or data changes persist.</b> Recommended before every production rollout — turn this OFF only once you have reviewed the dry-run output and want to commit the changes for real.") },
                    { label: qsTr("Acquire lock"),
                      desc: qsTr("Prevent concurrent migrators (recommended)."),
                      prop: "acquireLock",
                      tip: qsTr("Takes an advisory lock in the <b>schema_migrations</b> table so no other migrator — another GUI instance, a CI job, or <b>dbtool migrate</b> — can run simultaneously against the same database. Prevents two concurrent runs from racing and corrupting schema_migrations bookkeeping. Disable only for offline, single-user test databases where you know nothing else is touching the schema.") },
                    { label: qsTr("Backup before apply"),
                      desc: qsTr("Snapshot schema + data to .zip."),
                      prop: "backupBeforeApply",
                      tip: qsTr("Before the first migration is applied, writes a full schema + data snapshot to a timestamped .zip file next to the profile. Gives you a recovery point you can restore via <b>Backup / Restore</b> if a migration fails or produces unexpected results. Skipped automatically on a dry-run. Can take minutes and noticeable disk space on large databases.") },
                ]
                Rectangle {
                    required property var modelData
                    width: parent.width
                    height: 46
                    color: "transparent"

                    // Hover anywhere on the row to surface the long-form
                    // explanation — the inline `desc` label is deliberately
                    // one line so the tooltip carries the nuance.
                    ToolTip.visible: rowHover.hovered
                    ToolTip.text: modelData.tip
                    ToolTip.delay: 500
                    ToolTip.timeout: 10000
                    HoverHandler { id: rowHover }

                    Column {
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - toggleSwitch.width - 12
                        spacing: 2
                        Label {
                            text: modelData.label
                            color: Theme.text
                            font.pixelSize: 12
                            width: parent.width
                        }
                        Label {
                            text: modelData.desc
                            color: Theme.textMuted
                            font.pixelSize: 11
                            width: parent.width
                            elide: Text.ElideRight
                        }
                    }
                    Switch {
                        id: toggleSwitch
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        checked: root[modelData.prop]
                        onToggled: root[modelData.prop] = checked
                        ToolTip.visible: hovered
                        ToolTip.text: modelData.tip
                        ToolTip.delay: 500
                        ToolTip.timeout: 10000
                    }
                }
            }
        }
    }

    // Primary action button
    Button {
        Layout.fillWidth: true
        Layout.topMargin: 4
        Layout.preferredHeight: 38
        highlighted: true
        enabled: AppController.connected
                 && AppController.runner.phase === MigrationRunner.Idle
                 && (root.target !== "release" || root.hasSelection || root.selectedRelease.length > 0)
                 && (root.target !== "timestamp" || root.hasSelection || root.specificTimestamp.length > 0)
        ToolTip.visible: hovered
        ToolTip.delay: 500
        ToolTip.timeout: 10000
        ToolTip.text: root.dryRun
            ? qsTr("Simulate the chosen target without committing. The migrator opens a transaction, runs every matching migration, streams the SQL to the log pane, then rolls back. No changes persist — safe to run repeatedly.")
            : qsTr("Apply the chosen target to the connected database. Changes are committed transactionally and the <b>schema_migrations</b> table is updated. Irreversible except by restoring a backup or rolling back to an earlier release.")
        text: {
            const verb = root.dryRun ? qsTr("Dry-run") : qsTr("Apply");
            if (root.hasSelection)
                return `${verb} ${AppController.selectionCount} selected migration(s)`;
            if (root.target === "release" && root.selectedRelease.length > 0)
                return `${verb} → release ${root.selectedRelease}`;
            if (root.target === "timestamp" && root.specificTimestamp.length > 0)
                return `${verb} → timestamp ${root.specificTimestamp}`;
            return qsTr("%1 pending migrations").arg(verb);
        }
        onClicked: {
            if (root.hasSelection) {
                const ids = AppController.selectedMigrationTimestamps();
                if (root.dryRun)
                    AppController.runner.dryRunSelected(ids);
                else
                    AppController.runner.applySelected(ids);
                return;
            }
            const targetTs = root.resolvedTargetTimestamp();
            if (root.dryRun)
                AppController.runner.dryRunUpTo(targetTs);
            else
                AppController.runner.applyUpTo(targetTs);
        }
    }

    // Secondary "Rollback to release" button — only meaningful with the
    // release target. Kept separate from the primary apply/dry-run button so
    // the destructive action requires an explicit click.
    Button {
        Layout.fillWidth: true
        visible: root.target === "release" && root.selectedRelease.length > 0
        enabled: AppController.connected
                 && AppController.runner.phase === MigrationRunner.Idle
        text: qsTr("Rollback to release %1…").arg(root.selectedRelease)
        ToolTip.visible: hovered
        ToolTip.delay: 500
        ToolTip.timeout: 10000
        ToolTip.text: qsTr("<b>Destructive.</b> Reverts every migration currently applied above the selected release by running its <b>down()</b> step, in reverse order. The database ends up at exactly the schema the chosen release shipped with. Data written by the rolled-back migrations is lost unless an <b>up()</b> script preserves it — always take a backup first.")
        onClicked: AppController.runner.rollbackToRelease(root.selectedRelease)
    }

    Label {
        Layout.fillWidth: true
        horizontalAlignment: Text.AlignHCenter
        text: qsTr("≈ dbtool migrate %1").arg(root.dryRun ? "--dry-run" : "")
        color: Theme.textFaint
        font.family: Theme.monoFont
        font.pixelSize: 11
    }

    Item { Layout.fillHeight: true }
}
