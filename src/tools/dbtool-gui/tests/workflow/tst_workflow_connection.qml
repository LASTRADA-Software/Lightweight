// SPDX-License-Identifier: Apache-2.0
//
// The connection panel in its three modes (profile, DSN, custom connection string): what each mode
// connects to and says it will connect to, how the ODBC list is refreshed, how a failure shows, how
// an open connection is kept as a profile, and how a change of the profile file is picked up.

import QtQuick
import QtTest
import Lightweight.Migrations
import DbtoolGuiWorkflow

WorkflowTestCase {
    name: "WorkflowConnection"

    /// The profile file `prepare()` wrote.
    property string profileFile: ""
    /// The connection string of profile `q`'s database.
    property string databaseQ: ""

    SignalSpy {
        id: fileChangeSpy
        signalName: "profileFileChangeHandled"
    }

    /// Writes profiles `p` (the default) and `q` on two new SQLite databases with the workflow plugin,
    /// and opens the Expert view, whose connection panel the tests drive.
    function prepare() {
        WorkflowFixture.setSetting("config/pluginsDir", WorkflowFixture.newPluginsDir(["workflow"]))
        databaseQ = WorkflowFixture.newSqliteDatabase()
        profileFile = WorkflowFixture.writeProfileFile([
            { name: "p", connectionString: WorkflowFixture.newSqliteDatabase() },
            { name: "q", connectionString: databaseQ },
        ], "p")
        WorkflowFixture.setSetting("config/profileStorePath", profileFile)
        WorkflowFixture.setSetting("ui/viewMode", "expert")
        openMain()
        waitForIdle()
    }

    function waitForIdle() {
        tryCompare(AppController, "busy", false, 15000)
    }

    /// Picks the connection mode @p mode and waits for the plugin reload the switch requests: the Connect
    /// button stays disabled while the application is busy, so a click before the reload ends misses it.
    function chooseMode(mode) {
        clickControl(`connectionModeControl_${mode}`)
        waitForIdle()
    }

    function connectAndWait() {
        clickControl("connectButton")
        tryCompare(AppController, "connected", true, 15000)
        waitForIdle()
    }

    /// @return The row of the model @p model whose name role (257) is @p name; fails when there is none.
    function rowNamed(model, name) {
        for (let row = 0; row < model.rowCount(); ++row) {
            if (model.data(model.index(row, 0), 257) === name)
                return row
        }
        fail(`no row named ${name}`)
        return -1
    }

    /// Waits for the Save-as-profile or Add-profile dialog's name field to show.
    function waitForProfileDialog() {
        tryVerify(() => findRequired("profileNameField").visible, 5000, "the profile dialog must open")
    }

    function test_profile_mode_connects_via_the_selected_profile() {
        prepare()
        compare(findRequired("connectionSummary").text, "via profile ‘p’")
        pickWithKeys("profileCombo", rowNamed(AppController.profiles, "q"))
        compare(AppController.currentProfile, "q")
        waitForIdle()
        compare(findRequired("connectionSummary").text, "via profile ‘q’")
        compare(findRequired("connectionStatusLabel").text, "Not connected")

        connectAndWait()
        compare(findRequired("connectionStatusLabel").text, "Connected")
        compare(AppController.connectedTarget, "q")
        compare(AppController.connectedMode, "profile")
        compare(AppController.migrationCount, 3)
    }

    function test_dsn_mode_uses_dsn_user_password_and_schema_fields() {
        prepare()
        verify(WorkflowFixture.addUserDsn("dbtool-gui-workflow", databaseQ), "the user DSN must register")
        chooseMode("dsn")
        compare(AppController.connectionMode, "dsn")
        clickControl("refreshDsnButton")
        tryVerify(() => AppController.odbcDataSources.rowCount() > 0
                        && !AppController.odbcRefreshing, 15000, "the ODBC list must refresh")
        const row = rowNamed(AppController.odbcDataSources, "dbtool-gui-workflow")
        pickWithKeys("dsnCombo", row)
        compare(AppController.selectedDsn, "dbtool-gui-workflow")

        typeInto("dsnUserField", "tester")
        typeInto("dsnPasswordField", "secret")
        typeInto("dsnSchemaField", "main")
        compare(AppController.dsnUser, "tester")
        compare(AppController.dsnPassword, "secret")
        compare(AppController.dsnSchema, "main")
        compare(findRequired("connectionSummary").text, "via ODBC DSN ‘dbtool-gui-workflow’ (schema ‘main’)")

        connectAndWait()
        compare(findRequired("connectionStatusLabel").text, "Connected")
        compare(AppController.connectedMode, "dsn")
        compare(AppController.connectedTarget, "dbtool-gui-workflow")
        verify(AppController.logLines.text().indexOf("Schema search path: main") >= 0)
        // The user and password reach the driver but no log line or label shows them; the draft is the state.
        compare(AppController.connectionDraft().user, "tester")
    }

    function test_custom_mode_connects_with_the_typed_string_and_schema() {
        prepare()
        const connectionString = WorkflowFixture.newSqliteDatabase()
        chooseMode("custom")
        compare(AppController.connectionMode, "custom")
        compare(findRequired("connectButton").enabled, false)
        typeInto("customConnectionStringField", connectionString)
        typeInto("customSchemaField", "main")
        compare(AppController.connectionStringOverride, connectionString)
        compare(findRequired("connectionSummary").text, "via custom connection string (schema ‘main’)")
        compare(findRequired("connectButton").enabled, true)

        connectAndWait()
        compare(findRequired("connectionStatusLabel").text, "Connected")
        compare(AppController.connectedMode, "custom")
        verify(AppController.logLines.text().indexOf("Schema search path: main") >= 0)
        compare(AppController.migrationCount, 3)
    }

    function test_odbc_list_refresh_lists_a_new_dsn() {
        prepare()
        chooseMode("dsn")
        clickControl("refreshDsnButton")
        tryCompare(AppController, "odbcRefreshing", false, 15000)
        const before = AppController.odbcDataSources.rowCount()

        verify(WorkflowFixture.addUserDsn("dbtool-gui-workflow-new", databaseQ), "the user DSN must register")
        clickControl("refreshDsnButton")
        tryCompare(AppController, "odbcRefreshing", false, 15000)
        tryVerify(() => AppController.odbcDataSources.rowCount() === before + 1, 15000)
        verify(rowNamed(AppController.odbcDataSources, "dbtool-gui-workflow-new") >= 0)
        compare(findRequired("dsnCombo").count, before + 1)
        compare(AppController.odbcRefreshing, false)
    }

    function test_a_connect_failure_is_shown() {
        prepare()
        const unknownDataSource = "DSN=dbtool-gui-workflow-no-such-dsn"
        chooseMode("custom")
        typeInto("customConnectionStringField", unknownDataSource)
        clickControl("connectButton")
        tryVerify(() => findRequired("connectionError").visible, 15000, "the failure must show")
        verify(findRequired("connectionError").message.startsWith("Could not open the database connection:"),
               findRequired("connectionError").message)
        waitForIdle()
        compare(AppController.connected, false)
        compare(findRequired("connectionStatusLabel").text, "Not connected")
        compare(findRequired("connectButton").enabled, true)
        compare(findRequired("connectButton").text, "Connect")
    }

    function test_save_as_profile_adds_the_open_connection() {
        prepare()
        const connectionString = WorkflowFixture.newSqliteDatabase()
        chooseMode("custom")
        typeInto("customConnectionStringField", connectionString)
        connectAndWait()

        clickControl("saveAsProfileButton")
        waitForProfileDialog()
        compare(findRequired("profileConnectionSummary").visible, true)
        compare(findRequired("profileNameField").text, AppController.connectionDraft().suggestedName)
        verify(findRequired("profileNameField").text !== "", "the name is prefilled")
        typeText("profileNameField", "saved-custom")
        compare(findRequired("profileSave").enabled, true)
        clickControl("profileSave")
        tryVerify(() => !findRequired("profileNameField").visible, 5000, "the dialog must close")

        compare(WorkflowFixture.queryProfileField(AppController.profilePath, "saved-custom", "connectionString"),
                connectionString)
        compare(AppController.profiles.rowCount(), 3)
        verify(rowNamed(AppController.profiles, "saved-custom") >= 0)
        chooseMode("profile")
        compare(findRequired("profileCombo").count, 3)
    }

    function test_add_profile_from_backups_page() {
        prepare()
        const connectionString = WorkflowFixture.newSqliteDatabase()
        clickControl("navItem_backups")
        clickControl("addProfileButton")
        waitForProfileDialog()
        typeText("profileNameField", "added-from-backups")
        compare(findRequired("profileSave").enabled, false)
        clickControl("profileKindControl_connectionString")
        clickControl("profileConnectionStringField")
        for (const character of connectionString)
            keyClick(character)
        compare(findRequired("profileSave").enabled, true)
        clickControl("profileSave")
        tryVerify(() => !findRequired("profileNameField").visible, 5000, "the dialog must close")

        compare(WorkflowFixture.queryProfileField(AppController.profilePath, "added-from-backups", "connectionString"),
                connectionString)
        compare(AppController.profiles.rowCount(), 3)
        tryVerify(() => findRequired("profileRow_added-from-backups").visible, 5000, "the added profile shows in the table")
    }

    function test_external_profile_file_change_is_picked_up() {
        prepare()
        compare(findRequired("profileCombo").count, 2)
        fileChangeSpy.target = AppController
        fileChangeSpy.clear()

        // Another program writes the profile file: a third profile appears.
        outsideWatchdog("write the profile file", () => WorkflowFixture.writeProfileFile([
            { name: "p", connectionString: WorkflowFixture.newSqliteDatabase() },
            { name: "q", connectionString: databaseQ },
            { name: "external", connectionString: WorkflowFixture.newSqliteDatabase() },
        ], "p"))
        tryVerify(() => fileChangeSpy.count > 0, 15000, "the change must be noticed")
        compare(fileChangeSpy.signalArguments[0][0], AppController.ProfileFileReaction.Reloaded)
        compare(findRequired("profileCombo").count, 3)
        verify(rowNamed(AppController.profiles, "external") >= 0)
    }

    function test_clicking_the_profile_file_path_opens_the_file() {
        prepare()
        compare(WorkflowFixture.openedUrls(), [])
        clickControl("profilePathLabel")
        compare(WorkflowFixture.openedUrls(), [WorkflowFixture.localFileUrl(profileFile).toString()])
    }
}
