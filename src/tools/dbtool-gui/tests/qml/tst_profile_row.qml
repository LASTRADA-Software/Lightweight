// SPDX-License-Identifier: Apache-2.0
//
// Geometry tests for ProfileRow — the Backups page profile master-list row.
//
// These assert measured layout, not just instantiation, because every bug this
// row has had was a sizing bug that a "does it load" test passes clean:
//
//   * The status pill vanished from the list entirely. StatusPill is a plain
//     Rectangle carrying only `implicitWidth`, and a RowLayout will shrink any
//     child below its implicit size unless told not to — so the fillWidth
//     profile-name Label beside it took the whole row and squeezed the pill to
//     zero width. Nothing errored; the status was simply not there.
//   * Long names were elided down to indistinguishable stubs. Profile names run
//     to ~50 characters and differ in their tails, so truncation removed exactly
//     the part that tells two rows apart.
//
// Both are invisible to a compile check and to `verify(row !== null)`, so the
// checks here are on width/height/visibility of the actual items.

import QtQuick
import QtTest
import Lightweight.Migrations

TestCase {
    id: root
    name: "ProfileRow"
    when: windowShown
    width: 400
    height: 400

    // A realistic long name: ~50 chars, no spaces (so word-boundary wrapping
    // alone cannot break it), and distinguished only by its tail.
    readonly property string longNameA: "chinook-production-replica-eu-west-1-readonly-01"
    readonly property string longNameB: "chinook-production-replica-eu-west-1-readonly-02"
    readonly property string shortName: "sqlite-dev"

    Component {
        id: rowComponent
        ProfileRow {
            // 380 px is a narrow single-column page: the row folds into its
            // card layout (name on its own line), which is where wrapping of a
            // long name is exercised. The wide table layout is covered by
            // `wideRowComponent` below.
            width: 380
        }
    }

    /// Builds a row with the given name and status, laid out and ready to measure.
    function makeRow(profileName, status, label, meta) {
        const row = createTemporaryObject(rowComponent, root, {
            name: profileName,
            pillStatus: status,
            pillLabel: label,
            meta: meta !== undefined ? meta : "",
            canBackup: true,
            canRestore: true
        })
        verify(row !== null)
        // Force a layout pass before measuring.
        wait(0)
        return row
    }

    /// Finds the row's StatusPill by walking children (it has no objectName).
    /// Keyed on `status` + `tooltipText` together: the kit Glyphs are Shapes,
    /// which carry a `status` property of their own.
    function findPill(row) {
        return findItemWithProperties(row, ["status", "tooltipText"])
    }

    /// Depth-first search for a child item declaring `propName`.
    function findItemWithProperty(item, propName) {
        return findItemWithProperties(item, [propName])
    }

    /// Depth-first search for a child item declaring every name in `propNames`.
    function findItemWithProperties(item, propNames) {
        for (let i = 0; i < item.children.length; ++i) {
            const child = item.children[i]
            if (propNames.every(p => child.hasOwnProperty(p)))
                return child
            const found = findItemWithProperties(child, propNames)
            if (found !== null)
                return found
        }
        return null
    }

    /// Finds the wrapping name Label carrying exactly `want`. By text, not by
    /// "first item with a wrapMode": the DBMS badge, the current tag and the
    /// LAST BACKUP cell are text items too.
    function findNameLabel(item, want) {
        for (let i = 0; i < item.children.length; ++i) {
            const c = item.children[i]
            if (c.hasOwnProperty("wrapMode") && c.hasOwnProperty("truncated")
                && String(c.text) === want)
                return c
            const f = findNameLabel(c, want)
            if (f !== null)
                return f
        }
        return null
    }

    // NOTE on `visible`: these tests assert measured GEOMETRY, never
    // `item.visible`. `visible` is inherited, and createTemporaryObject() parents
    // the row to this TestCase, which is not itself shown — so every child
    // reports `visible === false` regardless of the layout. The bug being guarded
    // against was a pill collapsed to *zero width* by its fillWidth sibling, so
    // width/height are the properties that actually capture it.
    function test_status_pill_is_visible_and_has_width_with_a_short_name() {
        const row = makeRow(shortName, "applied", "backed up", "2026-07-30 09:14 · 47.7 MB")
        const pill = findPill(row)
        verify(pill !== null, "row must contain a StatusPill")
        // The regression: the pill was collapsed to 0 width.
        verify(pill.width > 20, "status pill must have real width, got " + pill.width)
        verify(pill.height > 8, "status pill must have real height, got " + pill.height)
        // Not hidden by its own property (as opposed to inherited invisibility).
        compare(pill.opacity, 1.0)
    }

    // The bug the user reported: with a ~50-character name the pill was squeezed
    // out. The name now occupies its own line, so the pill keeps its width no
    // matter how long the name is.
    function test_status_pill_keeps_its_width_with_a_50_char_name() {
        const shortRow = makeRow(shortName, "applied", "backed up")
        const longRow = makeRow(longNameA, "applied", "backed up")

        const shortPill = findPill(shortRow)
        const longPill = findPill(longRow)
        verify(shortPill !== null && longPill !== null)

        verify(longPill.width > 20,
               "pill must keep real width with a long name, got " + longPill.width)
        verify(longPill.height > 8,
               "pill must keep real height with a long name, got " + longPill.height)
        // Same label ⇒ same pill width, regardless of the name beside it.
        fuzzyCompare(longPill.width, shortPill.width, 1.0)
    }

    // Truncation to an indistinguishable stub was the second complaint: two
    // names differing only in their last character must not render identically.
    function test_long_names_are_shown_in_full_not_elided_to_a_stub() {
        const rowA = makeRow(longNameA, "applied", "backed up")
        const rowB = makeRow(longNameB, "applied", "backed up")

        const labelA = findNameLabel(rowA, longNameA)
        const labelB = findNameLabel(rowB, longNameB)
        verify(labelA !== null, "row must contain a wrapping name Label")

        // The full name is the Label's text (not a truncated copy) ...
        compare(labelA.text, longNameA)
        compare(labelB.text, longNameB)
        // ... and it is actually laid out rather than elided away: `truncated`
        // is false because the name wraps instead of being cut.
        verify(!labelA.truncated,
               "a 50-char name must wrap, not truncate — the tail is what "
               + "distinguishes profiles")
    }

    // Wrapping only helps if the row grows to contain the extra lines; otherwise
    // the name is clipped and we are back to losing the tail.
    function test_row_grows_taller_for_a_wrapped_name() {
        const shortRow = makeRow(shortName, "applied", "backed up")
        const longRow = makeRow(longNameA, "applied", "backed up")

        verify(longRow.implicitHeight > shortRow.implicitHeight,
               "a wrapped name must make the row taller (short="
               + shortRow.implicitHeight + " long=" + longRow.implicitHeight + ")")

        // And the name Label must fit inside the row, not overflow it.
        const label = findNameLabel(longRow, longNameA)
        verify(label.height <= longRow.implicitHeight,
               "wrapped name (" + label.height + ") must fit the row ("
               + longRow.implicitHeight + ")")
    }

    // The actions are always shown but must never be squeezed to
    // nothing — a zero-width Restore button is unclickable.
    function test_action_buttons_keep_their_width_when_selected() {
        const row = makeRow(longNameA, "applied", "backed up")
        row.selected = true
        wait(0)

        // Both buttons live in the actions Row; find them by their text.
        const restore = findButtonWithText(row, "Restore")
        verify(restore !== null, "row must contain a Restore button")
        verify(restore.width > 20,
               "Restore button must keep real width, got " + restore.width)
    }

    /// Finds a Button-like child whose `text` starts with `prefix`.
    function findButtonWithText(item, prefix) {
        for (let i = 0; i < item.children.length; ++i) {
            const child = item.children[i]
            if (child.hasOwnProperty("text") && child.hasOwnProperty("checkable")
                && String(child.text).indexOf(prefix) === 0)
                return child
            const found = findButtonWithText(child, prefix)
            if (found !== null)
                return found
        }
        return null
    }

    // A failed row shows its error text where the archive meta would go, in the
    // error colour, and still keeps a usable pill.
    function test_failed_row_shows_error_meta_and_a_pill() {
        const row = makeRow(longNameA, "unknown", "failed",
                            "Login failed for user 'sa'.")
        row.metaIsError = true
        wait(0)

        const pill = findPill(row)
        verify(pill !== null)
        verify(pill.width > 20, "failed row must still show its status pill")
        verify(pill.height > 8)
    }

    // ---- Wide (table) layout ----

    Component {
        id: wideRowComponent
        ProfileRow {
            // A two-column Backups page gives the table ~900 px.
            width: 900
        }
    }

    function makeWideRow(props) {
        const row = createTemporaryObject(wideRowComponent, root, props)
        verify(row !== null)
        wait(0)
        return row
    }

    // At table width the row is one line of fixed columns: the pill sits in
    // the STATUS column to the RIGHT of the name, not under it, and keeps its
    // natural width even beside a 50-character name.
    function test_wide_row_puts_status_in_its_own_column() {
        const row = makeWideRow({ name: longNameA, pillStatus: "applied", pillLabel: "Backed up" })
        verify(!row.compact, "a 900 px row must use the table layout")

        const nameLabel = findNameLabel(row, longNameA)
        const pill = findPill(row)
        verify(nameLabel !== null && pill !== null)

        const nameRight = nameLabel.mapToItem(row, nameLabel.width, 0).x
        const pillLeft = pill.mapToItem(row, 0, 0).x
        verify(pillLeft >= nameRight,
               "pill (x=" + pillLeft + ") must sit right of the name (right=" + nameRight + ")")
        verify(pill.width > 20, "pill width collapsed: " + pill.width)
        verify(!nameLabel.truncated, "the name must wrap, not truncate")
        // The kit row is at least 52 px tall.
        verify(row.implicitHeight >= 52, "row height " + row.implicitHeight)
    }

    // The STATUS column starts at the same x on every row, whatever the name,
    // so the column reads as a column (and lines up with the page's header).
    function test_wide_rows_align_their_status_column() {
        const a = makeWideRow({ name: shortName, pillStatus: "applied", pillLabel: "Backed up" })
        const b = makeWideRow({ name: longNameA, pillStatus: "unknown", pillLabel: "Failed",
                                meta: "Login failed for user 'sa'.", metaIsError: true })
        const pa = findPill(a)
        const pb = findPill(b)
        fuzzyCompare(pa.mapToItem(a, 0, 0).x, pb.mapToItem(b, 0, 0).x, 0.5)
    }

    // A failed row gets the kit error row: warm tint, and the reason inline.
    function test_failed_row_uses_the_error_row_treatment() {
        const row = makeWideRow({ name: shortName, pillStatus: "unknown", pillLabel: "Failed",
                                  meta: "Unable to open database file dev.db", metaIsError: true })
        verify(Qt.colorEqual(row.color, Theme.clrErrorRowBg), "failed row must use clrErrorRowBg, got " + row.color)
        const reason = findNameLabel(row, "Unable to open database file dev.db")
        verify(reason !== null, "the failure reason must be shown on the row")
        verify(Qt.colorEqual(reason.color, Theme.clrError), "the reason must be in clrError")
    }

    // The DBMS badge is derived from the connection string, and omitted when
    // it cannot be (a DSN profile) rather than guessed.
    function test_dbms_badge_is_derived_from_the_connection_string() {
        const ms = makeWideRow({ name: "prod",
            connectionString: "Driver={ODBC Driver 18 for SQL Server};Server=db1;Database=prod" })
        compare(ms.dbmsBadge, "MS")
        const pg = makeWideRow({ name: "staging",
            connectionString: "Driver={PostgreSQL Unicode};Server=db2;Database=staging" })
        compare(pg.dbmsBadge, "PG")
        const sq = makeWideRow({ name: "dev", connectionString: "DRIVER=SQLite3;Database=dev.db" })
        compare(sq.dbmsBadge, "SQ")
        const dsn = makeWideRow({ name: "dsn-only", connectionString: "" })
        compare(dsn.dbmsBadge, "")
    }

    // The connected profile is tagged "current".
    function test_current_profile_shows_the_current_tag() {
        const row = makeWideRow({ name: "staging", current: true })
        const tag = findNameLabel(row, "current")
        verify(tag !== null, "the current row must carry a 'current' tag")
        verify(tag.width > 10)
    }
}
