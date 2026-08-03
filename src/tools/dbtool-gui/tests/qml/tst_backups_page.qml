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
}
