// SPDX-License-Identifier: Apache-2.0
//
// Behaviour tests for the "Add profile…" / "Save as profile…" dialog: what the form
// requires before it can save, and that it only offers to keep a connection that is
// actually open and not saved yet. Writing the profile itself is covered at the
// AppController level (AppControllerTests.cpp), where a temporary dbtool.yml is
// available.

import QtQuick
import QtTest
import Lightweight.Migrations

TestCase {
    id: root
    name: "SaveProfileDialog"
    when: windowShown
    visible: true
    width: 800
    height: 700

    Component { id: dialogComponent; SaveProfileDialog { parent: root } }

    function saveButton(dialog) { return findChild(dialog, "profileSave") }

    function test_blank_form_needs_a_name_and_a_source() {
        const dialog = createTemporaryObject(dialogComponent, root)
        dialog.openBlank()
        tryVerify(function() { return dialog.visible })

        verify(!saveButton(dialog).enabled, "nothing typed yet")

        findChild(dialog, "profileNameField").text = "zz-new-profile-xyz"
        verify(!saveButton(dialog).enabled, "a name alone is not a connection")

        findChild(dialog, "profileDsnField").text = "WAREHOUSE"
        verify(saveButton(dialog).enabled, "name and data source are enough")

        findChild(dialog, "profileNameField").text = "   "
        verify(!saveButton(dialog).enabled, "a blank name does not count")
        dialog.close()
    }

    function test_connection_string_form_has_its_own_requirement() {
        const dialog = createTemporaryObject(dialogComponent, root)
        dialog.openBlank()
        tryVerify(function() { return dialog.visible })
        findChild(dialog, "profileNameField").text = "zz-new-profile-xyz"
        findChild(dialog, "profileDsnField").text = "WAREHOUSE"
        verify(saveButton(dialog).enabled)

        dialog._kind = "connectionString"
        verify(!saveButton(dialog).enabled, "the data source no longer counts once the kind is a connection string")
        findChild(dialog, "profileConnectionStringField").text = "Driver={SQLite3};Database=x.db"
        verify(saveButton(dialog).enabled)
        dialog.close()
    }

    function test_reopening_starts_from_an_empty_form() {
        const dialog = createTemporaryObject(dialogComponent, root)
        dialog.openBlank()
        tryVerify(function() { return dialog.visible })
        findChild(dialog, "profileNameField").text = "leftover"
        findChild(dialog, "profileDsnField").text = "LEFTOVER"
        dialog.close()
        tryVerify(function() { return !dialog.visible })

        dialog.openBlank()
        tryVerify(function() { return dialog.visible })
        compare(findChild(dialog, "profileNameField").text, "")
        compare(findChild(dialog, "profileDsnField").text, "")
        verify(!saveButton(dialog).enabled)
        dialog.close()
    }

    // A profile connection is already in dbtool.yml and nothing is open here, so there
    // is nothing for "Save as profile…" to keep: the dialog must not open empty.
    function test_save_as_profile_does_not_open_without_an_open_connection() {
        verify(!AppController.connected)
        const dialog = createTemporaryObject(dialogComponent, root)
        dialog.openForConnection()
        wait(50)
        verify(!dialog.visible)
    }

    function test_cancel_closes_without_saving() {
        const dialog = createTemporaryObject(dialogComponent, root)
        dialog.openBlank()
        tryVerify(function() { return dialog.visible })
        findChild(dialog, "profileCancel").clicked()
        tryVerify(function() { return !dialog.visible })
    }
}
