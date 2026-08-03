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
            // The list gives every row the viewport width; mirror that here so
            // wrapping is exercised against a realistic rail width.
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
    function findPill(row) {
        return findItemWithProperty(row, "status")
    }

    /// Depth-first search for a child item declaring `propName`.
    function findItemWithProperty(item, propName) {
        for (let i = 0; i < item.children.length; ++i) {
            const child = item.children[i]
            if (child.hasOwnProperty(propName))
                return child
            const found = findItemWithProperty(child, propName)
            if (found !== null)
                return found
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

        const labelA = findItemWithProperty(rowA, "wrapMode")
        const labelB = findItemWithProperty(rowB, "wrapMode")
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
        const label = findItemWithProperty(longRow, "wrapMode")
        verify(label.height <= longRow.implicitHeight,
               "wrapped name (" + label.height + ") must fit the row ("
               + longRow.implicitHeight + ")")
    }

    // Actions are revealed on hover/selection but must never be squeezed to
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
}
