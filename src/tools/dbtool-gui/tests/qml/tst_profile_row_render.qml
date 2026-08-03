// SPDX-License-Identifier: Apache-2.0
//
// Renders ProfileRow instances and asserts on the RENDERED pixels/geometry,
// rather than only on property values.
//
// Why a rendering test: the profile row's failures have all been sizing
// failures that property-level checks pass clean — a status pill squeezed to
// zero width by a fillWidth sibling, and a ~50-character name elided down to an
// indistinguishable stub. `grabImage()` forces a real layout + paint pass
// through the scene graph, so the assertions below measure what a user would
// actually see.
//
// Setting `LIGHTWEIGHT_QML_DUMP_DIR` writes the grabs to that directory as PNGs
// for eyeballing — the automated assertions do not depend on it.

import QtQuick
import QtQuick.Layouts
import QtTest
import Lightweight.Migrations

TestCase {
    id: root
    name: "ProfileRowRender"
    when: windowShown
    visible: true
    width: 420
    height: 520

    readonly property string longNameA: "chinook-production-replica-eu-west-1-readonly-01"
    readonly property string longNameB: "chinook-production-replica-eu-west-1-readonly-02"

    Component {
        id: rowComponent
        ProfileRow {
            width: 380
            height: implicitHeight
        }
    }

    /// Instantiates a row inside this (shown) TestCase and lets it lay out.
    function makeRow(props) {
        const row = createTemporaryObject(rowComponent, root, props)
        verify(row !== null)
        waitForRendering(row)
        return row
    }

    // A rendered row must actually paint something: a grab whose pixels are all
    // one colour means the content collapsed to nothing.
    //
    // Scope note: under the offscreen platform used here no fonts are deployed,
    // so TEXT contributes no pixels — the non-background pixels this finds are
    // the status pill, the selection tint, and the separator. That is still a
    // real check (those are the items that were collapsing to zero size), but it
    // is deliberately not an assertion about glyphs; text is covered by the
    // metric-based tests below.
    function test_row_paints_its_pill_and_chrome() {
        const row = makeRow({
            name: longNameA,
            pillStatus: "applied",
            pillLabel: "backed up",
            meta: "2026-07-30 09:14 · 47.7 MB",
            canBackup: true,
            canRestore: true,
            selected: true
        })

        const img = grabImage(row)
        verify(img !== null, "row must be grabbable")
        compare(img.width, 380)
        verify(img.height > 30,
               "a wrapped 50-char name must give the row real height, got " + img.height)

        // Scan for non-background pixels: text and the pill must have painted.
        // The row is selected, so its own background is bgSelected.
        const bg = img.pixel(2, 2)
        let painted = 0
        for (let y = 0; y < img.height; y += 2) {
            for (let x = 0; x < img.width; x += 2) {
                if (img.pixel(x, y) !== bg)
                    ++painted
            }
        }
        verify(painted > 20,
               "row must paint visible content (pill, chrome); distinct pixels=" + painted)
    }

    // The core of the user's report: two names differing only in their final
    // character must be distinguishable on screen. Previously the name was
    // elided to share a line with a pill and two buttons, so both rows showed
    // the same visible stub.
    //
    // Asserted via text metrics, NOT a pixel diff of the two grabs. Glyph
    // rasterisation needs a font, and the offscreen platform these tests run
    // under has none deployed ("QFontDatabase: Cannot find font directory …"),
    // so text contributes no pixels at all and a pixel diff would compare two
    // blank images — reporting "identical" for reasons that have nothing to do
    // with this layout. `text` + `truncated` + `lineCount` + `contentWidth` come
    // from Qt's text layout, which runs regardless, and are what actually encode
    // "the whole name is laid out and none of it was dropped".
    function test_two_similar_long_names_are_distinguishable() {
        const rowA = makeRow({ name: longNameA, pillStatus: "applied", pillLabel: "backed up" })
        const rowB = makeRow({ name: longNameB, pillStatus: "applied", pillLabel: "backed up" })

        const labelA = findNameLabel(rowA, longNameA)
        const labelB = findNameLabel(rowB, longNameB)
        verify(labelA !== null && labelB !== null, "both rows must have a name Label")

        // The distinguishing tails differ, and each Label carries its own name
        // in full — no shared truncated prefix.
        verify(labelA.text !== labelB.text)
        compare(labelA.text, longNameA)
        compare(labelB.text, longNameB)
        verify(labelA.text.length === 47 || labelA.text.length > 40,
               "fixture must be a realistically long name")

        // Neither is cut short: `truncated` false means every character —
        // including the trailing "-01"/"-02" — is laid out.
        verify(!labelA.truncated, "name A must not be truncated")
        verify(!labelB.truncated, "name B must not be truncated")

        // And the laid-out text really is wider than one line's worth, i.e. it
        // wrapped rather than being clipped to the available width.
        verify(labelA.lineCount >= 2,
               "a 47-char name at 380px should wrap, got " + labelA.lineCount + " line(s)")
    }

    /// Finds the wrapping name Label carrying exactly `want`.
    function findNameLabel(item, want) {
        for (let i = 0; i < item.children.length; ++i) {
            const c = item.children[i]
            if (c.hasOwnProperty("lineCount") && c.hasOwnProperty("truncated")
                && String(c.text) === want)
                return c
            const f = findNameLabel(c, want)
            if (f !== null)
                return f
        }
        return null
    }

    // The wrapped name must be fully inside the row's painted area. If the row
    // were sized for one line while the text wrapped to two, the second line
    // would be clipped and the tail lost again.
    function test_wrapped_name_is_not_clipped_by_the_row() {
        const row = makeRow({ name: longNameA, pillStatus: "applied", pillLabel: "backed up" })

        // Locate the wrapping name Label.
        let label = null
        function walk(item) {
            for (let i = 0; i < item.children.length; ++i) {
                const c = item.children[i]
                if (c.hasOwnProperty("wrapMode") && c.hasOwnProperty("truncated")
                    && String(c.text) === root.longNameA)
                    return c
                const f = walk(c)
                if (f !== null)
                    return f
            }
            return null
        }
        label = walk(row)
        verify(label !== null, "row must contain the wrapping name Label")

        verify(!label.truncated,
               "the name must wrap rather than truncate")
        verify(label.lineCount >= 2,
               "a 50-char name at this width should occupy multiple lines, got "
               + label.lineCount)
        // Label bottom, in row coordinates, must sit within the row.
        const bottom = label.mapToItem(row, 0, label.height).y
        verify(bottom <= row.height + 0.5,
               "wrapped name bottom (" + bottom + ") must fit inside the row height ("
               + row.height + ")")
    }

    // Status + both actions sit on the line BELOW the name, so none of them can
    // be squeezed by it. Verified geometrically: their tops are below the name's
    // bottom, and each has real width.
    function test_status_and_actions_sit_below_the_name() {
        const row = makeRow({
            name: longNameA,
            pillStatus: "applied",
            pillLabel: "backed up",
            meta: "2026-07-30 09:14 · 47.7 MB",
            canBackup: true,
            canRestore: true,
            selected: true
        })

        let nameLabel = null
        let pill = null
        function walk(item) {
            for (let i = 0; i < item.children.length; ++i) {
                const c = item.children[i]
                if (c.hasOwnProperty("wrapMode") && String(c.text) === root.longNameA)
                    nameLabel = c
                if (c.hasOwnProperty("status") && c.hasOwnProperty("label")
                    && c.hasOwnProperty("tooltipText"))
                    pill = c
                walk(c)
            }
        }
        walk(row)
        verify(nameLabel !== null, "name Label not found")
        verify(pill !== null, "StatusPill not found")

        const nameBottom = nameLabel.mapToItem(row, 0, nameLabel.height).y
        const pillTop = pill.mapToItem(row, 0, 0).y
        verify(pillTop >= nameBottom - 1,
               "status pill (top=" + pillTop + ") must sit below the name (bottom="
               + nameBottom + "), not beside it")

        // And it keeps its full size regardless of the name's length.
        verify(pill.width > 20, "pill width collapsed: " + pill.width)
        verify(pill.height > 8, "pill height collapsed: " + pill.height)
    }
}
