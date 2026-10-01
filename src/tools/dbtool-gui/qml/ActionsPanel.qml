// SPDX-License-Identifier: Apache-2.0
//
// Right-pane actions (`dt-act` in the Lastrada design): Target picker (three
// radio-style option cards) plus a plan-summary banner, the Options switch
// rows, and — pinned to the bottom of the pane when there is room — the
// primary action button, the destructive rollback and the CLI equivalent.
// Each option has a trailing input where applicable (release picker for "Up
// to a release", timestamp autocomplete for "Up to a timestamp"). When the
// user ticks individual rows in the migration list, the primary button
// switches to acting on just that selection and the target picker is
// bypassed — shown via an info banner in place of the plan.
//
// The host gives this layout an explicit height of at least its implicit
// height (see `ExpertView.qml`); the spacer above the buttons absorbs the
// difference.

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

    /// Kit group label (`dt-lbl`): 10 px bold uppercase, letter-spaced.
    component GroupLabel: Label {
        Layout.fillWidth: true
        color: Theme.clrOnSurfaceSubtle
        font.pixelSize: Theme.sizeGroup
        font.weight: Font.Bold
        font.letterSpacing: 0.7
    }

    GroupLabel { text: qsTr("TARGET") }

    // Target option cards (`dt-opt`): radio, title and one-line help; the
    // active card takes the brand border, the field-focus tint and a focus
    // halo, and grows to host its input (release picker / timestamp field).
    Repeater {
        model: [
            { key: "latest",    label: qsTr("Latest — everything pending"),
              help: AppController.pendingCount === 1
                    ? qsTr("Apply the 1 pending migration.")
                    : qsTr("Apply all %1 pending migrations.").arg(AppController.pendingCount),
              tip: qsTr("Apply every pending migration.") },
            { key: "release",   label: qsTr("Up to a release"),
              help: qsTr("Stop after the last migration of a declared release."),
              tip: qsTr("Apply up to the highestTimestamp of the selected release.") },
            { key: "timestamp", label: qsTr("Up to a timestamp"),
              help: qsTr("Apply pending migrations with timestamp ≤ a value."),
              tip: qsTr("Apply up to a specific timestamp or migration title.") },
        ]
        Rectangle {
            id: optionRect
            required property var modelData
            readonly property bool active: root.target === modelData.key
            readonly property bool expanded: active && (modelData.key === "release" || modelData.key === "timestamp")

            Layout.fillWidth: true
            Layout.preferredHeight: optionBody.implicitHeight + 20
            color: active ? Theme.clrFieldFocusBg : Theme.clrCard
            border.color: active ? Theme.clrPrimary
                        : (optionHover.hovered && !root.hasSelection ? Theme.clrBorderStrong
                                                                     : Theme.clrContainerHighest)
            radius: Theme.r2
            opacity: root.hasSelection ? 0.5 : 1.0

            Accessible.role: Accessible.RadioButton
            Accessible.name: modelData.label
            Accessible.checked: active

            // Focus halo around the active card.
            Rectangle {
                anchors.fill: parent
                anchors.margins: -3
                radius: parent.radius + 3
                color: "transparent"
                border.width: 3
                border.color: Theme.clrFocusRing
                visible: optionRect.active
            }

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
                anchors.leftMargin: 12
                anchors.topMargin: 11
                width: 16; height: 16; radius: 8
                border.width: 1.5
                border.color: optionRect.active ? Theme.clrPrimary : Theme.clrBorderStrong
                color: Theme.clrCard
                Rectangle {
                    anchors.centerIn: parent
                    width: 8; height: 8; radius: 4
                    color: Theme.clrPrimary
                    visible: optionRect.active
                }
            }

            Column {
                id: optionBody
                anchors.left: radio.right
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.leftMargin: 10
                anchors.rightMargin: 12
                anchors.topMargin: 10
                spacing: 2

                Label {
                    width: parent.width
                    text: optionRect.modelData.label
                    color: Theme.clrOnSurface
                    font.pixelSize: Theme.sizeBodySm + 1
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
                Label {
                    width: parent.width
                    text: optionRect.modelData.help
                    color: Theme.clrOnSurfaceSubtle
                    font.pixelSize: Theme.sizeLabel
                    wrapMode: Text.WordWrap
                }

                Item {
                    width: parent.width
                    height: 6
                    visible: optionRect.expanded
                }

                ComboBox {
                    width: parent.width
                    visible: optionRect.expanded && optionRect.modelData.key === "release"
                    model: AppController.releases
                    textRole: "version"
                    onActivated: index => {
                        const idx = AppController.releases.index(index, 0);
                        root.selectedRelease = AppController.releases.data(idx, 257);
                    }
                }

                TimestampAutocomplete {
                    width: parent.width
                    visible: optionRect.expanded && optionRect.modelData.key === "timestamp"
                    value: root.specificTimestamp
                    onValueChanged: root.specificTimestamp = value
                    placeholderText: qsTr("Type a timestamp or title — e.g. 'Initial'")
                }
            }
        }
    }

    // Selection-override banner — shown only when the user has ticked rows
    // in the migration list. The button and plan summary switch to acting
    // on the selection when this is visible.
    Banner {
        Layout.fillWidth: true
        Layout.topMargin: 2
        visible: root.hasSelection
        kind: "info"
        title: AppController.selectionCount === 1
               ? qsTr("1 migration selected")
               : qsTr("%1 migrations selected").arg(AppController.selectionCount)
        text: qsTr("The target picker is bypassed.")
        actions: [
            LsButton {
                text: qsTr("Clear")
                variant: "ghost"
                onClicked: AppController.selectAllPending(false)
            }
        ]
    }

    // Plan summary banner
    Banner {
        Layout.fillWidth: true
        Layout.topMargin: 2
        visible: !root.hasSelection
        kind: "info"
        title: qsTr("Plan")
        text: {
            if (root.target === "release" && root.selectedRelease.length > 0)
                return qsTr("Apply every pending migration up to release %1.")
                        .arg(root.selectedRelease);
            if (root.target === "timestamp" && root.specificTimestamp.length > 0)
                return qsTr("Apply every pending migration with timestamp ≤ %1.")
                        .arg(root.specificTimestamp);
            return qsTr("Apply every pending migration.");
        }
    }

    GroupLabel {
        text: qsTr("OPTIONS")
        Layout.topMargin: 6
    }

    // Switch rows (`dt-swrow`): label + help on the left, kit switch on the
    // right, hairline dividers between rows.
    // A ColumnLayout rather than a Column: rows sized `width: parent.width`
    // inside a Column feed the Column's own implicit width back into itself,
    // which overflowed the switches past the pane edge.
    ColumnLayout {
        Layout.fillWidth: true
        spacing: 0

        Repeater {
            id: optionRows
            model: [
                { label: qsTr("Dry run"),
                  desc: qsTr("Run in a transaction, then roll back."),
                  prop: "dryRun",
                  tip: qsTr("Run inside a transaction and roll back — no changes persist.") },
                { label: qsTr("Acquire lock"),
                  desc: qsTr("Block concurrent migrators (recommended)."),
                  prop: "acquireLock",
                  tip: qsTr("Advisory lock on schema_migrations — blocks concurrent migrators.") },
                { label: qsTr("Back up first"),
                  desc: qsTr("Snapshot schema + data to .zip."),
                  prop: "backupBeforeApply",
                  tip: qsTr("Write a snapshot before the first migration runs.") },
            ]
            Item {
                id: switchRow
                required property var modelData
                required property int index
                Layout.fillWidth: true
                Layout.preferredHeight: Math.max(rowText.implicitHeight, toggleSwitch.implicitHeight) + 16

                // Hover anywhere on the row to surface the long-form
                // explanation — the inline `desc` label is deliberately
                // one line so the tooltip carries the nuance.
                ToolTip.visible: rowHover.hovered
                ToolTip.text: modelData.tip
                ToolTip.delay: 500
                ToolTip.timeout: 10000
                HoverHandler { id: rowHover }

                Column {
                    id: rowText
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - toggleSwitch.width - 12
                    spacing: 1
                    Label {
                        text: switchRow.modelData.label
                        color: Theme.clrOnSurface
                        font.pixelSize: Theme.sizeBodySm + 1
                        font.weight: Font.DemiBold
                        width: parent.width
                        elide: Text.ElideRight
                    }
                    Label {
                        text: switchRow.modelData.desc
                        color: Theme.clrOnSurfaceSubtle
                        font.pixelSize: Theme.sizeLabel
                        width: parent.width
                        elide: Text.ElideRight
                    }
                }

                Switch {
                    id: toggleSwitch
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    padding: 0
                    // Fusion sizes a Switch from its (empty) content, not from
                    // a custom indicator; without these the right-anchored
                    // control is narrower than the 32 px pill, which then
                    // hangs past the pane edge.
                    implicitWidth: 32
                    implicitHeight: 18
                    checked: root[switchRow.modelData.prop]
                    onToggled: root[switchRow.modelData.prop] = checked
                    Accessible.name: switchRow.modelData.label

                    // Kit switch (`dt-sw`): 32×18 pill, brand red when on,
                    // white knob sliding between the ends.
                    indicator: Rectangle {
                        implicitWidth: 32
                        implicitHeight: 18
                        x: toggleSwitch.leftPadding
                        y: (toggleSwitch.height - height) / 2
                        radius: height / 2
                        color: toggleSwitch.checked
                               ? (toggleSwitch.hovered ? Theme.clrPrimaryHover : Theme.clrPrimary)
                               : (toggleSwitch.hovered ? Theme.clrBorderStrong : Theme.clrContainerHighest)
                        Behavior on color { ColorAnimation { duration: 120 } }

                        Rectangle {
                            width: 14
                            height: 14
                            radius: 7
                            y: 2
                            x: toggleSwitch.checked ? parent.width - width - 2 : 2
                            color: "#ffffff"
                            border.color: Qt.rgba(0, 0, 0, 0.08)
                            Behavior on x { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
                        }

                        Rectangle {
                            anchors.fill: parent
                            anchors.margins: -3
                            radius: height / 2
                            color: "transparent"
                            border.width: 3
                            border.color: Theme.clrFocusRing
                            visible: toggleSwitch.visualFocus
                        }
                    }
                    contentItem: Item {}
                }

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 1
                    color: Theme.clrDivider
                    visible: switchRow.index < optionRows.count - 1
                }
            }
        }
    }

    // Takes the spare height when the hosting pane is taller than the
    // content, pinning the action buttons to the bottom edge.
    Item {
        Layout.fillHeight: true
        Layout.minimumHeight: 6
    }

    // Primary action button — the one brand-red action on the screen.
    LsButton {
        Layout.fillWidth: true
        variant: "primary"
        size: "lg"
        glyph: "play"
        busy: AppController.runner.phase !== MigrationRunner.Idle
        // The backup/managed-backup terms mirror the mutual busy guard wired in
        // AppController: applying migrations while a backup is writing an
        // archive tears that archive, and the C++ guard would refuse the click
        // anyway. Dry-runs are read-only and stay available, exactly like the
        // C++ side, which only gates the mutating entry points.
        enabled: AppController.connected
                 && AppController.runner.phase === MigrationRunner.Idle
                 && (root.dryRun
                     || (AppController.backupRunner.phase === BackupRunner.Idle
                         && AppController.managedBackups.phase === ManagedBackupController.Idle))
                 && (root.target !== "release" || root.hasSelection || root.selectedRelease.length > 0)
                 && (root.target !== "timestamp" || root.hasSelection || root.specificTimestamp.length > 0)
        ToolTip.visible: hovered
        ToolTip.delay: 500
        ToolTip.timeout: 10000
        ToolTip.text: root.dryRun
            ? qsTr("Run inside a transaction and roll back.")
            : qsTr("<b>Destructive.</b> Apply the chosen target and commit.")
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
    // the destructive action requires an explicit click, and outlined in
    // error red (`danger`) so it never reads as the main action.
    LsButton {
        Layout.fillWidth: true
        variant: "danger"
        size: "md"
        glyph: "history"
        visible: root.target === "release" && root.selectedRelease.length > 0
        enabled: AppController.connected
                 && AppController.runner.phase === MigrationRunner.Idle
                 && AppController.backupRunner.phase === BackupRunner.Idle
                 && AppController.managedBackups.phase === ManagedBackupController.Idle
        text: qsTr("Rollback to release %1…").arg(root.selectedRelease)
        ToolTip.visible: hovered
        ToolTip.delay: 500
        ToolTip.timeout: 10000
        ToolTip.text: qsTr("<b>Destructive.</b> Revert every migration above the selected release.")
        onClicked: AppController.runner.rollbackToRelease(root.selectedRelease)
    }

    Label {
        Layout.fillWidth: true
        horizontalAlignment: Text.AlignHCenter
        text: qsTr("≈ dbtool migrate %1").arg(root.dryRun ? "--dry-run" : "")
        color: Theme.clrOnSurfaceFaint
        font: Theme.monoFont(Theme.sizeLabel)
        elide: Text.ElideRight
    }
}
