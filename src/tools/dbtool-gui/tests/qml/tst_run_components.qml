// SPDX-License-Identifier: Apache-2.0
//
// Behaviour tests for the pieces Back up and Restore share on the Backups
// page: the progress block, the outcome banner and the formatting helpers
// behind both. They are fed `lastResult`-shaped maps directly, so what a
// finished run looks like is pinned without running one.

import QtQuick
import QtTest
import Lightweight.Migrations
import "../../qml/RunFormat.js" as RunFormat

TestCase {
    id: root
    name: "RunComponents"
    when: windowShown
    visible: true
    width: 700
    height: 700

    Component { id: progressComponent; RunProgress { width: 600 } }
    Component { id: outcomeComponent; RunOutcome { width: 600 } }
    SignalSpy { id: dismissSpy; signalName: "dismissed" }
    SignalSpy { id: folderSpy; signalName: "showInFolder" }

    function okRestore() {
        return { operation: "restore", ok: true, archive: "D:\\exports\\staging-2026-10-01.zip",
                 error: "", tables: 38, failedCount: 0, failedTables: [], rows: 4600000,
                 durationMs: 41000, finishedAt: new Date(2026, 9, 6, 14, 31) }
    }

    function failedRestore(count, listed) {
        const failures = []
        for (let i = 0; i < listed; ++i)
            failures.push({ table: "T" + i, reason: "FOREIGN KEY constraint failed (547)" })
        return { operation: "restore", ok: false, archive: "DK_PROD_LATEST.zip", error: "",
                 tables: 681, failedCount: count, failedTables: failures, rows: 1000, durationMs: 90000 }
    }

    // --- RunFormat ---------------------------------------------------------

    function test_format_count_is_compact() {
        compare(RunFormat.formatCount(0), "0")
        compare(RunFormat.formatCount(950), "950")
        compare(RunFormat.formatCount(12500), "13 k")
        compare(RunFormat.formatCount(4600000), "4.6 M")
    }

    function test_format_duration_reads_as_prose() {
        compare(RunFormat.formatDuration(800), "0.8 s")
        compare(RunFormat.formatDuration(41000), "41 s")
        compare(RunFormat.formatDuration(125000), "2 min 05 s")
    }

    function test_clock_pads_both_fields() {
        compare(RunFormat.clock(0), "00:00")
        compare(RunFormat.clock(23000), "00:23")
        compare(RunFormat.clock(125000), "02:05")
        compare(RunFormat.clock(-5), "00:00")
    }

    function test_base_name_handles_both_separators() {
        compare(RunFormat.baseName("D:\\exports\\a.zip"), "a.zip")
        compare(RunFormat.baseName("/tmp/dir/b.zip"), "b.zip")
        compare(RunFormat.baseName("c.zip"), "c.zip")
    }

    // --- RunProgress -------------------------------------------------------

    function test_progress_is_determinate_once_the_total_is_known() {
        const block = createTemporaryObject(progressComponent, root,
            { title: "Table 18 of 38 · orders", detail: "00:23 · 2.1 M rows", value: 18, to: 38 })
        verify(block !== null)
        const track = findChild(block, "runProgressTrack")
        verify(track !== null)
        verify(!track.indeterminate)
        verify(Math.abs(track._fraction - 18 / 38) < 1e-9)
    }

    function test_progress_sweeps_while_the_total_is_unknown() {
        const block = createTemporaryObject(progressComponent, root, { title: "Preparing…", value: 0, to: 0 })
        verify(block !== null)
        verify(findChild(block, "runProgressTrack").indeterminate)
    }

    // --- RunOutcome --------------------------------------------------------

    function test_successful_restore_names_the_database_the_archive_and_the_facts() {
        const outcome = createTemporaryObject(outcomeComponent, root,
            { result: okRestore(), target: "staging-postgres" })
        verify(outcome !== null)
        const banner = findChild(outcome, "runOutcomeBanner")
        compare(banner.kind, "ok")
        compare(banner.title, "Restored — staging-postgres from staging-2026-10-01.zip")
        verify(banner.text.indexOf("38 tables") >= 0)
        verify(banner.text.indexOf("4.6 M rows") >= 0)
        verify(banner.text.indexOf("41 s") >= 0)
        verify(banner.text.indexOf("14:31") >= 0)
        verify(!findChild(outcome, "runOutcomeFailures").visible)
        verify(!findChild(outcome, "runOutcomeCopy").visible, "nothing to copy on success")
    }

    function test_backup_and_restore_share_banner_and_facts_and_differ_only_in_verbs() {
        const restore = okRestore()
        const backup = okRestore()
        backup.operation = "backup"
        const a = createTemporaryObject(outcomeComponent, root, { result: restore, target: "db" })
        const b = createTemporaryObject(outcomeComponent, root, { result: backup, target: "db" })
        compare(findChild(a, "runOutcomeBanner").text, findChild(b, "runOutcomeBanner").text)
        compare(findChild(b, "runOutcomeBanner").title, "Backup written — db to staging-2026-10-01.zip")
        compare(findChild(a, "runOutcomeBanner").kind, findChild(b, "runOutcomeBanner").kind)
    }

    // The outcome names the database the run was against, as recorded when it started,
    // not whatever the page is connected to by the time the banner is read.
    function test_the_outcome_names_the_database_the_run_was_against() {
        const result = okRestore()
        result.target = "staging-postgres"
        const outcome = createTemporaryObject(outcomeComponent, root, { result: result, target: "some-other-db" })
        compare(findChild(outcome, "runOutcomeBanner").title, "Restored — staging-postgres from staging-2026-10-01.zip")
    }

    function test_a_restore_that_lost_tables_is_a_failure_with_the_tables_listed() {
        const outcome = createTemporaryObject(outcomeComponent, root,
            { result: failedRestore(3, 3), target: "rasch_flat01" })
        const banner = findChild(outcome, "runOutcomeBanner")
        compare(banner.kind, "err")
        compare(banner.title, "Restore incomplete — 3 of 681 tables failed")
        verify(banner.text.indexOf("678") >= 0, "says how many tables did make it")
        verify(banner.text.indexOf("rasch_flat01") >= 0)
        verify(banner.text.indexOf("partial state") >= 0)
        verify(findChild(outcome, "runOutcomeFailures").visible)
        verify(findChild(outcome, "runOutcomeCopy").visible)
    }

    function test_a_partial_backup_says_the_archive_is_missing_tables() {
        const result = failedRestore(1, 1)
        result.operation = "backup"
        const outcome = createTemporaryObject(outcomeComponent, root, { result: result, target: "db" })
        const banner = findChild(outcome, "runOutcomeBanner")
        compare(banner.title, "Backup incomplete — 1 of 681 tables failed")
        verify(banner.text.indexOf("missing these tables") >= 0)
    }

    function test_an_aborted_run_shows_the_error_text() {
        const result = { operation: "restore", ok: false, archive: "missing.zip", error: "cannot open archive",
                         tables: 0, failedCount: 0, failedTables: [], rows: 0, durationMs: 5 }
        const outcome = createTemporaryObject(outcomeComponent, root, { result: result, target: "db" })
        const banner = findChild(outcome, "runOutcomeBanner")
        compare(banner.kind, "err")
        compare(banner.title, "Restore failed")
        compare(banner.text, "cannot open archive")
    }

    function test_an_error_that_names_no_table_is_still_reported() {
        const result = { operation: "restore", ok: false, archive: "a.zip", error: "", tables: 0, failedCount: 0,
                         otherErrors: 1, rows: 0, durationMs: 5,
                         failedTables: [ { table: "", reason: "Restore aborted: No tables could be created" } ] }
        const outcome = createTemporaryObject(outcomeComponent, root, { result: result, target: "db" })
        const banner = findChild(outcome, "runOutcomeBanner")
        compare(banner.kind, "err")
        compare(banner.title, "Restore finished with errors")
        verify(banner.text.indexOf("partial state") >= 0)
        verify(findChild(outcome, "runOutcomeFailures").visible)
        verify(outcome.details().indexOf("No tables could be created") >= 0)
    }

    function test_long_failure_lists_are_bounded_and_say_how_many_more() {
        const outcome = createTemporaryObject(outcomeComponent, root, { result: failedRestore(120, 50) })
        verify(outcome !== null)
        const details = outcome.details()
        verify(details.indexOf("T0:") >= 0)
        verify(details.indexOf("… and 70 more") >= 0, "the clipboard report counts what the list left out")
        const list = findChild(outcome, "runOutcomeFailures")
        verify(list.visible)
        // Only `maxListedFailures` rows are drawn; the rest are summarised.
        verify(list.implicitHeight < 50 * 20)
    }

    function test_dismiss_and_show_in_folder_are_forwarded() {
        const outcome = createTemporaryObject(outcomeComponent, root, { result: okRestore() })
        dismissSpy.target = outcome
        folderSpy.target = outcome
        dismissSpy.clear()
        folderSpy.clear()
        findChild(outcome, "runOutcomeDismiss").clicked()
        compare(dismissSpy.count, 1)
        // "Show in folder" has no object name; it is the only other action.
        outcome.showInFolder()
        compare(folderSpy.count, 1)
    }

    function test_an_empty_result_renders_nothing_to_say() {
        const outcome = createTemporaryObject(outcomeComponent, root, { result: ({}) })
        compare(outcome.headline(), "")
        compare(outcome.body(), "")
    }
}
