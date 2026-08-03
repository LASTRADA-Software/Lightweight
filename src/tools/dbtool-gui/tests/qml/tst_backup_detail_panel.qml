import QtQuick
import QtTest
import Lightweight.Migrations

TestCase {
    id: root
    name: "BackupDetailPanel"
    when: windowShown

    Component { id: panelComponent; BackupDetailPanel {} }

    function test_instantiates_with_defaults() {
        const panel = createTemporaryObject(panelComponent, root)
        verify(panel !== null)
        compare(panel.profileName, "")
        compare(panel.workerCount, 1)
    }

    function test_shows_empty_state_without_model() {
        const panel = createTemporaryObject(panelComponent, root, { profileName: "prod" })
        verify(panel !== null)
        compare(panel.profileName, "prod")
        // No tablesModel -> the empty-state path is active (no crash, no rows).
        verify(panel.tablesModel === null)
    }
}
