import QtQuick
import QtTest
import Lightweight.Migrations

TestCase {
    id: root
    name: "BackupsPage"
    when: windowShown

    Component { id: pageComponent; BackupsPage {} }
    Component { id: bannerComponent; DestructiveWarningBanner {} }

    function test_instantiates() {
        const page = createTemporaryObject(pageComponent, root)
        verify(page !== null)
        verify(page.hasOwnProperty("done"))
    }

    // The custom-archive "Restore…" button used to call
    // `BackupRunner.runRestore` on a single click — a destructive, unconfirmed
    // action sitting a few pixels from "Backup", on a surface that is now on by
    // default. It must route through a confirmation dialog instead.
    function test_custom_restore_requires_confirmation() {
        const page = createTemporaryObject(pageComponent, root)
        verify(page !== null)

        const button = findChild(page, "customRestoreButton")
        verify(button !== null)
        const dialog = findChild(page, "customRestoreDialog")
        verify(dialog !== null)
        verify(!dialog.visible)

        button.clicked()

        tryVerify(function() { return dialog.visible })
        // Confirmation pending: nothing has been restored yet.
        compare(AppController.backupRunner.phase, BackupRunner.Idle)
        dialog.close()
    }

    // Both restore confirmations share one banner component so their wording
    // and prominence cannot drift apart.
    function test_destructive_banner_defaults_to_the_restore_warning() {
        const banner = createTemporaryObject(bannerComponent, root)
        verify(banner !== null)
        verify(banner.text.indexOf("Destructive") >= 0)
        verify(banner.text.indexOf("dropped and recreated") >= 0)
    }

    // The per-profile restore drops and recreates every table of the target,
    // so its red Restore button stays disabled until the name of the database
    // being overwritten is typed exactly — not a prefix, not another case.
    function test_profile_restore_requires_the_typed_name() {
        const page = createTemporaryObject(pageComponent, root)
        verify(page !== null)

        const dialog = findChild(page, "restoreDialog")
        verify(dialog !== null)
        dialog.openFor("prod", 47700000, new Date(), 38)
        tryVerify(function() { return dialog.visible })

        const button = dialog.confirmButton
        const field = dialog.confirmField
        verify(button !== null && field !== null)
        compare(field.text, "", "the confirmation must start empty on every open")

        // The test harness loads no dbtool.yml, so the only restore target is
        // the custom connection string — fill it so the typed name is the
        // only remaining gate.
        if (dialog._isCustom)
            dialog.customConnectionField.text = "DRIVER=SQLite3;Database=restore-target.db"
        compare(dialog.confirmName, "prod")

        verify(!button.enabled, "Restore must be disabled before anything is typed")
        field.text = "pro"
        verify(!button.enabled, "a prefix of the name must not enable Restore")
        field.text = "Prod"
        verify(!button.enabled, "the match is case-sensitive")
        field.text = "prod "
        verify(!button.enabled, "the match is exact (no trailing space)")
        field.text = "prod"
        verify(button.enabled, "typing the exact name enables Restore")

        // Re-opening resets the confirmation, so a previous dialog's typing
        // never pre-arms the next restore.
        dialog.close()
        tryVerify(function() { return !dialog.visible })
        dialog.openFor("prod", 47700000, new Date(), 38)
        tryVerify(function() { return dialog.visible })
        compare(field.text, "")
        verify(!button.enabled, "re-opened dialog must be disarmed again")
        dialog.close()
    }
}
