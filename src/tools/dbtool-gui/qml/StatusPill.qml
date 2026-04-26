// SPDX-License-Identifier: Apache-2.0
//
// Small rounded badge for status strings ("applied", "pending", …). Centralises
// the status→colour mapping so every row, card, and summary uses the same
// palette. A built-in ToolTip surfaces a per-status explanation when the user
// hovers — useful for the rarer states ("checksum-mismatch", "unknown") whose
// label alone doesn't convey the actionable context.

import QtQuick
import QtQuick.Controls
import Lightweight.Migrations

Rectangle {
    id: root

    property string status: ""

    /// Optional override for the hover tooltip. When empty the per-status
    /// default from `_tooltips` is used; set this to suppress the tooltip for
    /// a specific instance (e.g. inside a chip already accompanied by helper
    /// text) by passing a single space.
    property string tooltipText: ""

    readonly property var _palette: ({
        "applied":           [Theme.okSoft,   Theme.okText,   Theme.ok],
        "pending":           [Theme.warnSoft, Theme.warnText, Theme.warn],
        "partial":           [Theme.warnSoft, Theme.warnText, Theme.warn],
        "running":           [Theme.accentSoft, Theme.accent, Theme.accent],
        "unknown":           [Theme.errSoft,  Theme.errText,  Theme.err],
        "checksum-mismatch": [Theme.errSoft,  Theme.errText,  Theme.err],
        "empty":             [Theme.bgSubtle, Theme.textMuted, Theme.textFaint]
    })
    readonly property var _colours: _palette[status] || [Theme.bgSubtle, Theme.textMuted, Theme.textFaint]

    // Per-status tooltip copy. The "checksum-mismatch" entry is intentionally
    // long: when the GUI flags a row, the user is staring at a red badge with
    // no other explanation, so the tooltip needs to spell out *why* a hash can
    // diverge from the one stored in `SchemaMigration.checksum` — the schema
    // itself usually hasn't drifted; only the rendered SQL text has.
    readonly property var _tooltips: ({
        "applied":
            qsTr("Migration is recorded in the schema_migrations history table " +
                 "and its current checksum matches what was stored when applied."),
        "pending":
            qsTr("Migration is registered with the plugin but has not yet been " +
                 "applied to this database. Use Apply to run it."),
        "partial":
            qsTr("This release contains both applied and pending migrations. " +
                 "Apply the rest to bring the release fully up to date."),
        "running":
            qsTr("Migration is currently being applied or reverted."),
        "unknown":
            qsTr("Migration is recorded in the schema_migrations history table, " +
                 "but no plugin currently registers a migration with this " +
                 "timestamp. Either the plugin that defined it is missing, or it " +
                 "was renamed/removed in source. Re-load the plugin or revert the " +
                 "row to clear the warning."),
        "checksum-mismatch":
            qsTr("The SHA-256 stored when this migration was applied does not " +
                 "match the SHA-256 the migration would produce today.\n\n" +
                 "ComputeChecksum() runs the migration's Up() body through the " +
                 "current SQL formatter, concatenates every emitted statement, " +
                 "and hashes the result. Anything that changes the *text* the " +
                 "plan emits will change the hash, even if the migration's " +
                 "effect on the schema is identical. The database has not " +
                 "necessarily drifted — only the rendering has.\n\n" +
                 "Common causes:\n" +
                 "• The migration plugin was regenerated (e.g. lup2dbtool re-run " +
                 "  with new options — uppercased identifiers, IfNotExists guards, " +
                 "  index renaming, --varchar-scale, FK constraint quoting, etc.).\n" +
                 "• The Lightweight library was upgraded and now formats some DDL " +
                 "  differently (e.g. wrapping AddForeignKey in DO $$ on Postgres, " +
                 "  or IF NOT EXISTS guards on SQL Server).\n" +
                 "• The migration's source was hand-edited after deployment.\n" +
                 "• The connected backend changed dialect (each backend's " +
                 "  formatter renders the same plan to different SQL).\n\n" +
                 "If the schema is what you expect, this is informational. To " +
                 "clear the warning, re-bootstrap the database (drop + re-apply) " +
                 "or update the stored checksum via dbtool."),
        "empty":
            qsTr("No migrations match the current filter.")
    })
    readonly property string _resolvedTooltip:
        tooltipText !== "" ? tooltipText : (_tooltips[status] || "")

    color: _colours[0]
    radius: 999
    implicitWidth: row.implicitWidth + 16
    implicitHeight: row.implicitHeight + 4

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 5

        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: 6
            height: 6
            radius: 3
            color: root._colours[2]
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: root.status
            color: root._colours[1]
            font.pixelSize: 11
            font.weight: Font.Medium
        }
    }

    // Hover area + attached ToolTip. Anchored over the whole pill so users can
    // hover the dot, the text, or the padding and still get the explanation.
    // Stays interaction-transparent for clicks (the parent row's MouseArea
    // still receives presses / double-clicks for opening the SQL preview).
    MouseArea {
        id: tooltipHover
        anchors.fill: parent
        hoverEnabled: root._resolvedTooltip !== ""
        acceptedButtons: Qt.NoButton
        ToolTip.visible: containsMouse && root._resolvedTooltip !== ""
        ToolTip.delay: 350
        ToolTip.timeout: 12000
        ToolTip.text: root._resolvedTooltip
    }
}
