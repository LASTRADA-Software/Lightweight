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

    /// Optional display label. When empty the pill shows `status` verbatim
    /// (the migrations views rely on this). Callers that need a friendlier
    /// caption than the palette key — e.g. the backups view rendering
    /// "backed up" while colouring it with the "applied" palette — set this
    /// to decouple the visible text from the colour lookup.
    property string label: ""

    /// Optional override for the hover tooltip. When empty the per-status
    /// default from `_tooltips` is used; set this to suppress the tooltip for
    /// a specific instance (e.g. inside a chip already accompanied by helper
    /// text) by passing a single space.
    property string tooltipText: ""

    // [background, foreground, border] per status.
    readonly property var _palette: ({
        "applied":           [Theme.clrSuccessBg, Theme.clrSuccess, Theme.clrSuccessBorder],
        "pending":           [Theme.clrWarningBg, Theme.clrWarning, Theme.clrWarningBorder],
        "partial":           [Theme.clrWarningBg, Theme.clrWarning, Theme.clrWarningBorder],
        "running":           [Theme.clrInfoBg,    Theme.clrInfo,    Theme.clrInfoBorder],
        "unknown":           [Theme.clrErrorBg,   Theme.clrError,   Theme.clrErrorBorder],
        "checksum-mismatch": [Theme.clrErrorBg,   Theme.clrError,   Theme.clrErrorBorder],
        "empty":             [Theme.clrContainer, Theme.clrOnSurfaceMed, Theme.clrContainerHighest]
    })
    readonly property var _colours: _palette[status] || _palette["empty"]

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
                 "• The migration plugin was regenerated (e.g. its code generator " +
                 "  re-run with new options — uppercased identifiers, IfNotExists guards, " +
                 "  index renaming, FK constraint quoting, etc.).\n" +
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

    // Kit pills (`k-pill`) pair every colour with a glyph so a status never
    // relies on hue alone; "running" animates instead.
    readonly property var _glyphs: ({
        "applied":           "check",
        "pending":           "clock",
        "partial":           "clock",
        "unknown":           "alert",
        "checksum-mismatch": "alert",
    })
    readonly property string _glyph: _glyphs[status] || ""

    color: _colours[0]
    border.color: _colours[2]
    radius: Theme.rPill
    implicitWidth: row.implicitWidth + 18
    implicitHeight: 22

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 5

        Glyph {
            anchors.verticalCenter: parent.verticalCenter
            visible: root._glyph !== ""
            name: root._glyph
            size: 11
            strokeWidth: 2
            color: root._colours[1]
        }
        Spinner {
            anchors.verticalCenter: parent.verticalCenter
            visible: root.status === "running"
            running: visible
            diameter: 10
            color: root._colours[1]
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: root.label !== "" ? root.label : root.status
            color: root._colours[1]
            font.pixelSize: Theme.sizeLabel
            font.weight: Font.DemiBold
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
