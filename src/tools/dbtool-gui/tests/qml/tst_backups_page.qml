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

    // --- Backup & restore panel: connection, gating, confirmation --------------------

    // The panel works on the connection AppController holds, so without one
    // the actions must be off even when a path has been typed.
    function test_custom_archive_actions_need_a_connection() {
        verify(!AppController.connected)
        const page = createTemporaryObject(pageComponent, root)
        verify(page !== null)

        findChild(page, "customPathField").text = "D:/exports/staging.zip"
        const backup = findChild(page, "customBackupButton")
        const restore = findChild(page, "customRestoreButton")
        verify(backup !== null && restore !== null)
        verify(!backup.enabled, "Back up needs a connection")
        verify(!restore.enabled, "Restore… needs a connection")
        verify(page._customDisabledReason.indexOf("Connect to a database first") >= 0,
               "the tooltip says why the actions are off")
        verify(findChild(page, "customConnectButton").enabled, "Connect… is always available while idle")
    }

    function test_connect_button_opens_the_connection_dialog() {
        const page = createTemporaryObject(pageComponent, root)
        const dialog = findChild(page, "connectDialog")
        verify(dialog !== null)
        verify(!dialog.visible)

        findChild(page, "customConnectButton").clicked()

        tryVerify(function() { return dialog.visible })
        findChild(dialog, "connectDialogClose").clicked()
        tryVerify(function() { return !dialog.visible })
    }

    // The custom restore asks for the same typed name as the per-profile one.
    // Not connected there is no database name to type, so it stays disarmed.
    function test_custom_restore_stays_disarmed_without_a_connection() {
        const page = createTemporaryObject(pageComponent, root)
        const dialog = findChild(page, "customRestoreDialog")
        dialog.openFor("D:/exports/staging.zip")
        tryVerify(function() { return dialog.visible })

        compare(dialog.confirmField.text, "")
        compare(dialog.confirmName, "")
        verify(!dialog.confirmButton.enabled)
        dialog.confirmField.text = "anything"
        verify(!dialog.confirmButton.enabled, "typing cannot arm a restore that has no target")
        dialog.close()
    }

    function test_outcome_and_progress_are_hidden_before_any_run() {
        const page = createTemporaryObject(pageComponent, root)
        verify(!findChild(page, "customProgress").visible)
        verify(!findChild(page, "customOutcome").visible)
    }

    // Without a profile a database has no row on this page, so adding one must be possible from it.
    function test_profiles_panel_offers_add_profile() {
        const page = createTemporaryObject(pageComponent, root)
        const button = findChild(page, "addProfileButton")
        verify(button !== null)
        verify(button.enabled)
        const dialog = findChild(page, "saveProfileDialog")
        verify(dialog !== null)
        verify(!dialog.visible)

        button.clicked()

        tryVerify(function() { return dialog.visible })
        verify(!dialog.fromConnection, "a blank form, not one prefilled from a connection")
        findChild(dialog, "profileCancel").clicked()
        tryVerify(function() { return !dialog.visible })
    }

    // Saving the open connection only makes sense for a DSN / connection-string connection.
    function test_save_as_profile_is_offered_only_for_an_open_unsaved_connection() {
        verify(!AppController.connected)
        const page = createTemporaryObject(pageComponent, root)
        verify(!findChild(page, "customSaveProfileButton").visible)
    }
}
