// SPDX-License-Identifier: Apache-2.0
//
// Adds a profile to dbtool.yml. Two ways in:
//
//   * `openForConnection()` — "Save as profile…": keeps the DSN or connection string
//     that is open right now. The connection is shown read-only (it is what was proven
//     to work), the user only names it and decides whether its password is stored.
//   * `openBlank()` — "Add profile…": a form for a data source or connection string
//     that has not been connected yet.
//
// Without a profile a database cannot be backed up or restored from the per-profile
// rows, so both entry points exist: from the connection panel (after connecting by
// DSN or connection string) and from the Backups page's profile table.
//
// Passwords go through `AppController.addProfile`, which stores them encrypted;
// the password of an open connection never reaches QML at all (only `hasPassword`).

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Lightweight.Migrations

Dialog {
    id: root

    /// True for "Save as profile…" (open connection), false for "Add profile…".
    property bool fromConnection: false

    /// Emitted after the profile was written and the list reloaded.
    /// @param name The new profile's name.
    signal saved(string name)

    // The open connection as `AppController.connectionDraft()` describes it.
    property var _draft: ({})
    // "dsn" | "connectionString" — only editable in the blank form.
    property string _kind: "dsn"
    property string _error: ""

    readonly property string _nameProblem: nameField.text.trim() === "" ? ""
                                                                        : AppController.profileNameProblem(nameField.text)
    readonly property bool _hasSource: fromConnection
                                       || (_kind === "dsn" ? dsnField.text.trim() !== ""
                                                           : connectionStringField.text.trim() !== "")
    readonly property bool _canSave: nameField.text.trim() !== "" && _nameProblem === "" && _hasSource

    /// "Save as profile…": prefill from the open connection.
    function openForConnection() {
        _draft = AppController.connectionDraft()
        if (!_draft.available)
            return
        fromConnection = true
        _kind = _draft.kind
        reset(_draft.suggestedName)
        storePassword.checked = _draft.hasPassword
        open()
    }

    /// "Add profile…": an empty form.
    function openBlank() {
        _draft = ({})
        fromConnection = false
        _kind = "dsn"
        reset("")
        open()
    }

    function reset(suggestedName) {
        nameField.text = suggestedName
        dsnField.text = ""
        connectionStringField.text = ""
        userField.text = ""
        passwordField.text = ""
        schemaField.text = ""
        _error = ""
    }

    function save() {
        const fields = {
            name: nameField.text,
            kind: fromConnection ? _draft.kind : _kind,
            dsn: fromConnection ? _draft.dsn : dsnField.text,
            connectionString: fromConnection ? _draft.connectionString : connectionStringField.text,
            user: fromConnection ? _draft.user : userField.text,
            schema: fromConnection ? _draft.schema : schemaField.text,
            password: fromConnection ? "" : passwordField.text,
            useOpenConnectionPassword: fromConnection && storePassword.checked
        }
        const name = nameField.text.trim()
        const error = AppController.addProfile(fields)
        if (error !== "") {
            _error = error
            return
        }
        close()
        saved(name)
    }

    modal: true
    anchors.centerIn: parent
    width: Math.max(380, Math.min(540, (parent ? parent.width : 540) - 48))
    padding: 0
    header: null

    background: Rectangle {
        color: Theme.clrCard
        radius: Theme.r3
        border.color: Theme.clrContainerHighest
    }

    contentItem: ColumnLayout {
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 20
            Layout.bottomMargin: 12
            spacing: 14

            Rectangle {
                Layout.alignment: Qt.AlignTop
                Layout.preferredWidth: 36
                Layout.preferredHeight: 36
                radius: 18
                color: Theme.clrPrimarySoft

                Glyph {
                    anchors.centerIn: parent
                    name: "plus"
                    size: 18
                    color: Theme.clrPrimary
                    knockout: parent.color
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4

                Label {
                    Layout.fillWidth: true
                    text: root.fromConnection ? qsTr("Save as profile") : qsTr("Add profile")
                    color: Theme.clrOnSurface
                    font.pixelSize: 16
                    font.weight: Font.DemiBold
                }
                Label {
                    Layout.fillWidth: true
                    text: root.fromConnection
                          ? qsTr("Keep this connection in dbtool.yml, so you can back it up, restore it and migrate it from the profile list.")
                          : qsTr("Profiles live in dbtool.yml. Once added, a profile can be backed up and restored from its row.")
                    color: Theme.clrOnSurfaceMed
                    font.pixelSize: Theme.sizeBodySm + 1
                    wrapMode: Text.Wrap
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 70
            Layout.rightMargin: 20
            Layout.bottomMargin: 16
            spacing: 12

            // --- Name ---
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 5
                Label {
                    text: qsTr("Name")
                    color: Theme.clrOnSurfaceMed
                    font.pixelSize: Theme.sizeLabel
                    font.weight: Font.DemiBold
                }
                KitField {
                    id: nameField
                    objectName: "profileNameField"
                    Layout.fillWidth: true
                    placeholderText: qsTr("e.g. warehouse-prod")
                    invalid: root._nameProblem !== ""
                    onAccepted: if (root._canSave) root.save()
                }
                Label {
                    objectName: "profileNameProblem"
                    Layout.fillWidth: true
                    visible: root._nameProblem !== ""
                    text: root._nameProblem
                    color: Theme.clrError
                    font.pixelSize: Theme.sizeLabel
                    wrapMode: Text.Wrap
                }
            }

            // --- The open connection (read-only) ---
            Rectangle {
                objectName: "profileConnectionSummary"
                Layout.fillWidth: true
                visible: root.fromConnection
                implicitHeight: summaryColumn.implicitHeight + 20
                color: Theme.clrContainerLow
                border.color: Theme.clrContainerHighest
                radius: Theme.r2

                ColumnLayout {
                    id: summaryColumn
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    spacing: 4

                    Label {
                        Layout.fillWidth: true
                        text: root._draft.kind === "dsn" ? qsTr("ODBC data source") : qsTr("Connection string")
                        color: Theme.clrOnSurfaceSubtle
                        font.pixelSize: Theme.sizeLabel
                    }
                    Label {
                        Layout.fillWidth: true
                        text: root._draft.kind === "dsn" ? (root._draft.dsn || "") : (root._draft.connectionString || "")
                        color: Theme.clrOnSurface
                        font: Theme.monoFont(12)
                        wrapMode: Text.WrapAnywhere
                    }
                    Label {
                        Layout.fillWidth: true
                        visible: (root._draft.user || "") !== "" || (root._draft.schema || "") !== ""
                        text: [ (root._draft.user || "") !== "" ? qsTr("user %1").arg(root._draft.user) : "",
                                (root._draft.schema || "") !== "" ? qsTr("schema %1").arg(root._draft.schema) : "" ]
                              .filter(function(part) { return part !== "" }).join(" · ")
                        color: Theme.clrOnSurfaceMed
                        font.pixelSize: Theme.sizeBodySm
                    }
                }
            }

            CheckBox {
                id: storePassword
                objectName: "storePasswordCheck"
                Layout.fillWidth: true
                visible: root.fromConnection && !!root._draft.hasPassword
                text: qsTr("Store the password in dbtool.yml (encrypted)")
                ToolTip.visible: hovered
                ToolTip.delay: 500
                ToolTip.text: qsTr("Without it the profile has no password, and connecting with it asks the driver for one.")
            }

            // --- The blank form: what to connect to ---
            ColumnLayout {
                Layout.fillWidth: true
                visible: !root.fromConnection
                spacing: 12

                SegmentedControl {
                    Layout.fillWidth: true
                    fill: true
                    model: [ { value: "dsn", label: qsTr("ODBC data source") },
                             { value: "connectionString", label: qsTr("Connection string") } ]
                    current: root._kind
                    onActivated: value => root._kind = value
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    visible: root._kind === "dsn"
                    spacing: 5
                    Label {
                        text: qsTr("Data source")
                        color: Theme.clrOnSurfaceMed
                        font.pixelSize: Theme.sizeLabel
                        font.weight: Font.DemiBold
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        KitField {
                            id: dsnField
                            objectName: "profileDsnField"
                            Layout.fillWidth: true
                            mono: true
                            placeholderText: qsTr("DSN name")
                        }
                        ComboBox {
                            id: dsnPicker
                            Layout.preferredWidth: 150
                            model: AppController.odbcDataSources
                            textRole: "name"
                            displayText: qsTr("Pick…")
                            onActivated: index => {
                                const idx = AppController.odbcDataSources.index(index, 0)
                                dsnField.text = AppController.odbcDataSources.data(idx, 257)
                            }
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    visible: root._kind === "connectionString"
                    spacing: 5
                    Label {
                        text: qsTr("Connection string")
                        color: Theme.clrOnSurfaceMed
                        font.pixelSize: Theme.sizeLabel
                        font.weight: Font.DemiBold
                    }
                    KitField {
                        id: connectionStringField
                        objectName: "profileConnectionStringField"
                        Layout.fillWidth: true
                        mono: true
                        placeholderText: qsTr("Driver={…};Server=…;Database=…")
                    }
                    Label {
                        Layout.fillWidth: true
                        text: qsTr("A PWD= in it is moved out and stored encrypted.")
                        color: Theme.clrOnSurfaceSubtle
                        font.pixelSize: Theme.sizeLabel
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        spacing: 5
                        Label {
                            text: qsTr("User (optional)")
                            color: Theme.clrOnSurfaceMed
                            font.pixelSize: Theme.sizeLabel
                            font.weight: Font.DemiBold
                        }
                        KitField { id: userField; Layout.fillWidth: true }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        spacing: 5
                        Label {
                            text: qsTr("Password (optional)")
                            color: Theme.clrOnSurfaceMed
                            font.pixelSize: Theme.sizeLabel
                            font.weight: Font.DemiBold
                        }
                        KitField {
                            id: passwordField
                            Layout.fillWidth: true
                            echoMode: TextInput.Password
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 5
                    Label {
                        text: qsTr("Schema (optional)")
                        color: Theme.clrOnSurfaceMed
                        font.pixelSize: Theme.sizeLabel
                        font.weight: Font.DemiBold
                    }
                    KitField { id: schemaField; Layout.fillWidth: true; mono: true }
                }
            }

            Banner {
                objectName: "profileError"
                Layout.fillWidth: true
                visible: root._error !== ""
                kind: "err"
                text: root._error
            }

            Label {
                Layout.fillWidth: true
                text: qsTr("Saved to %1").arg(AppController.profilePath !== "" ? AppController.profilePath
                                                                               : AppController.defaultProfileStorePath)
                color: Theme.clrOnSurfaceSubtle
                font: Theme.monoFont(11)
                elide: Text.ElideMiddle
            }
        }
    }

    footer: Rectangle {
        implicitHeight: footerRow.implicitHeight + 24
        color: Theme.clrContainerLow
        radius: Theme.r3

        // Square off the top corners; only the bottom two follow the dialog outline.
        Rectangle {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: Theme.r3
            color: Theme.clrContainerLow
        }
        Rectangle {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: 1
            color: Theme.clrContainerHighest
        }

        RowLayout {
            id: footerRow
            anchors.right: parent.right
            anchors.rightMargin: 20
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8

            LsButton {
                objectName: "profileCancel"
                variant: "secondary"
                size: "md"
                text: qsTr("Cancel")
                onClicked: root.close()
            }
            LsButton {
                objectName: "profileSave"
                variant: "primary"
                size: "md"
                text: qsTr("Save profile")
                enabled: root._canSave
                onClicked: root.save()
            }
        }
    }
}
