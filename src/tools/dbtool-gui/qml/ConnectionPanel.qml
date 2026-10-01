// SPDX-License-Identifier: Apache-2.0
//
// Connection form: three mutually-exclusive modes — Profile, ODBC DSN, and
// Custom connection string — picked via the kit segmented control at the
// top. Only the input for the active mode is shown, and a summary line tells
// the user exactly what Connect will do next. This removes the old ambiguity
// where all three inputs were visible simultaneously and the user could not
// tell which one actually drove the connection.
//
// The panel draws no frame of its own: the Expert view hosts it inside a
// "Connection" kit panel and the Simple view inside its numbered Connection
// step, and both supply the white surface and the heading. Give it a width
// (`width: parent.width` / `Layout.fillWidth`); its height is implicit.

import QtQuick
import QtQuick.Controls
import Lightweight.Migrations

Column {
    id: root
    spacing: 10

    /// Kit field label (`k-field label`).
    component FieldLabel: Label {
        width: parent ? parent.width : implicitWidth
        color: Theme.clrOnSurfaceMed
        font.pixelSize: Theme.sizeLabel
        font.weight: Font.DemiBold
        elide: Text.ElideRight
    }

    /// Copyable message box styled as a kit banner. A read-only TextEdit
    /// rather than `Banner` so the user can mouse-select an ODBC diagnostic
    /// and paste it into a bug report.
    component MessageBox: Rectangle {
        id: box
        property string message: ""
        property color background: Theme.clrErrorBg
        property color borderColour: Theme.clrErrorBorder
        property color foreground: Theme.clrError
        property string glyph: "alert"

        width: parent ? parent.width : 200
        height: messageText.contentHeight + 20
        radius: Theme.r2
        color: box.background
        border.color: box.borderColour

        Glyph {
            id: boxGlyph
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.leftMargin: 10
            anchors.topMargin: 11
            name: box.glyph
            size: 14
            color: box.foreground
        }
        TextEdit {
            id: messageText
            anchors.left: boxGlyph.right
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.leftMargin: 8
            anchors.rightMargin: 10
            anchors.topMargin: 10
            text: box.message
            color: box.foreground
            font.pixelSize: Theme.sizeLabel
            wrapMode: TextEdit.WordWrap
            readOnly: true
            selectByMouse: true
            selectByKeyboard: true
            persistentSelection: true
            selectionColor: Theme.clrPrimarySoftBorder
            selectedTextColor: Theme.clrOnSurface
        }
    }

    // --- Mode selector ---
    SegmentedControl {
        width: parent.width
        fill: true
        current: AppController.connectionMode
        model: [
            { value: "profile", label: qsTr("Profile") },
            { value: "dsn",     label: qsTr("DSN") },
            { value: "custom",  label: qsTr("Custom") },
        ]
        onActivated: value => AppController.setConnectionMode(value)

        HoverHandler { id: modeHover }
        ToolTip.visible: modeHover.hovered
        ToolTip.delay: 500
        ToolTip.timeout: 10000
        ToolTip.text: qsTr("Profile: a named entry in the Lightweight profile YAML.\n"
                           + "DSN: a system-registered ODBC Data Source Name.\n"
                           + "Custom: a raw ODBC connection string.")
    }

    // --- Profile mode ---
    Column {
        width: parent.width
        spacing: 5
        visible: AppController.connectionMode === "profile"

        FieldLabel { text: qsTr("Profile") }
        ComboBox {
            width: parent.width
            model: AppController.profiles
            textRole: "name"
            displayText: currentText.length > 0 ? currentText : qsTr("(no profiles loaded)")
            currentIndex: {
                let found = -1;
                for (let i = 0; i < AppController.profiles.rowCount(); ++i) {
                    const idx = AppController.profiles.index(i, 0);
                    if (AppController.profiles.data(idx, 257) === AppController.currentProfile)
                        found = i;
                }
                return found;
            }
            onActivated: index => {
                const idx = AppController.profiles.index(index, 0);
                AppController.currentProfile = AppController.profiles.data(idx, 257);
            }
        }
        // Clickable file-path help line. Opens the YAML with the system's
        // default handler so the user can edit it without leaving the app;
        // hover turns it into a brand-coloured link.
        Label {
            id: profilePathLabel
            width: parent.width
            readonly property bool hasPath: AppController.profilePath.length > 0
            text: hasPath ? qsTr("from %1").arg(AppController.profilePathDisplay)
                          : qsTr("No profile file loaded.")
            color: hasPath && mouseArea.containsMouse ? Theme.clrPrimary : Theme.clrOnSurfaceSubtle
            // Whole-value assignment: `font.families` is not settable per property.
            font: Qt.font({ families: Theme.monoFamilies, pixelSize: Theme.sizeLabel,
                            underline: mouseArea.containsMouse })
            wrapMode: Text.WrapAnywhere
            MouseArea {
                id: mouseArea
                anchors.fill: parent
                hoverEnabled: profilePathLabel.hasPath
                cursorShape: profilePathLabel.hasPath ? Qt.PointingHandCursor : Qt.ArrowCursor
                enabled: profilePathLabel.hasPath
                onClicked: AppController.openProfileFileExternally()
                ToolTip.visible: containsMouse
                ToolTip.text: qsTr("Open in default editor")
                ToolTip.delay: 400
            }
        }
    }

    // --- DSN mode ---
    Column {
        width: parent.width
        spacing: 5
        visible: AppController.connectionMode === "dsn"

        FieldLabel { text: qsTr("ODBC Data Source") }
        Row {
            width: parent.width
            spacing: 6
            ComboBox {
                id: dsnCombo
                width: parent.width - refreshDsnButton.width - parent.spacing
                model: AppController.odbcDataSources
                textRole: "name"
                displayText: AppController.selectedDsn.length > 0
                    ? AppController.selectedDsn
                    : qsTr("(pick a DSN)")
                onActivated: index => {
                    const idx = AppController.odbcDataSources.index(index, 0);
                    AppController.setSelectedDsn(AppController.odbcDataSources.data(idx, 257));
                }
            }
            LsButton {
                id: refreshDsnButton
                anchors.verticalCenter: dsnCombo.verticalCenter
                height: dsnCombo.height
                width: height
                iconOnly: true
                glyph: "refresh"
                text: qsTr("Re-enumerate ODBC Data Sources.")
                onClicked: AppController.refreshOdbcDataSources()
            }
        }

        // Optional UID / PWD overrides. Leave empty for DSNs that use
        // Windows authentication or have credentials saved in the DSN
        // itself; fill in when the driver reports
        // "Login failed for user ''" (the SQL-Server-auth-without-
        // persisted-creds case).
        Item { width: 1; height: 3 }
        FieldLabel { text: qsTr("Username (optional)") }
        TextField {
            width: parent.width
            placeholderText: qsTr("leave empty to use DSN / Windows auth")
            placeholderTextColor: Theme.clrOnSurfaceFaint
            text: AppController.dsnUser
            onEditingFinished: AppController.setDsnUser(text)
        }
        Item { width: 1; height: 3 }
        FieldLabel { text: qsTr("Password (optional)") }
        TextField {
            width: parent.width
            placeholderText: qsTr("leave empty to use DSN / Windows auth")
            placeholderTextColor: Theme.clrOnSurfaceFaint
            echoMode: TextInput.Password
            text: AppController.dsnPassword
            onEditingFinished: AppController.setDsnPassword(text)
        }
        Item { width: 1; height: 3 }
        FieldLabel { text: qsTr("Schema (optional)") }
        TextField {
            width: parent.width
            placeholderText: qsTr("leave empty for the server default (e.g. dbo / public)")
            placeholderTextColor: Theme.clrOnSurfaceFaint
            font: Theme.monoFont(Theme.sizeMono)
            text: AppController.dsnSchema
            onEditingFinished: AppController.setDsnSchema(text)
        }
    }

    // --- Custom connection-string mode ---
    Column {
        width: parent.width
        spacing: 5
        visible: AppController.connectionMode === "custom"

        FieldLabel { text: qsTr("Connection string") }
        TextField {
            width: parent.width
            placeholderText: qsTr("DRIVER=...;DATABASE=...")
            placeholderTextColor: Theme.clrOnSurfaceFaint
            font: Theme.monoFont(Theme.sizeMono)
            text: AppController.connectionStringOverride
            onEditingFinished: AppController.setConnectionStringOverride(text)
        }
        Item { width: 1; height: 3 }
        FieldLabel { text: qsTr("Schema (optional)") }
        TextField {
            width: parent.width
            placeholderText: qsTr("leave empty for the server default (e.g. dbo / public)")
            placeholderTextColor: Theme.clrOnSurfaceFaint
            font: Theme.monoFont(Theme.sizeMono)
            text: AppController.customSchema
            onEditingFinished: AppController.setCustomSchema(text)
        }
    }

    // --- "Will connect …" summary (kit help text) ---
    Row {
        width: parent.width
        spacing: 6
        visible: AppController.connectionSummary.length > 0

        Glyph {
            id: summaryGlyph
            y: 1
            name: "plug"
            size: 13
            color: Theme.clrOnSurfaceSubtle
        }
        Label {
            width: parent.width - summaryGlyph.width - parent.spacing
            text: AppController.connectionSummary
            color: Theme.clrOnSurfaceSubtle
            font.pixelSize: Theme.sizeLabel
            lineHeight: 1.15
            wrapMode: Text.WordWrap
        }
    }

    // --- Last error (red, blocking failures) ---
    MessageBox {
        visible: AppController.lastError.length > 0
        message: AppController.lastError
    }

    // --- Last warning (amber, non-blocking) — e.g. "the database is
    // unmanaged". The connection is established; the user can still apply
    // migrations to bootstrap the schema_migrations table.
    MessageBox {
        visible: AppController.lastWarning.length > 0
        message: AppController.lastWarning
        background: Theme.clrWarningBg
        borderColour: Theme.clrWarningBorder
        foreground: Theme.clrWarning
    }

    // --- Connection state (`k-st`) + Connect / Reconnect ---
    Item {
        width: parent.width
        height: connectButton.implicitHeight

        Row {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            spacing: 7

            // Status dot with the kit's soft halo when connected.
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: 14
                height: 14
                radius: 7
                color: AppController.connected ? Qt.rgba(23 / 255, 163 / 255, 74 / 255, 0.2) : "transparent"
                Rectangle {
                    anchors.centerIn: parent
                    width: 8
                    height: 8
                    radius: 4
                    color: AppController.connected ? Theme.clrSuccessDot : Theme.clrWarningDot
                }
            }
            Label {
                anchors.verticalCenter: parent.verticalCenter
                text: AppController.connected ? qsTr("Connected") : qsTr("Not connected")
                color: AppController.connected ? Theme.clrOnSurface : Theme.clrOnSurfaceMed
                font.pixelSize: Theme.sizeBodySm + 1
            }
        }

        LsButton {
            id: connectButton
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            variant: "secondary"
            glyph: AppController.connected ? "refresh" : "plug"
            text: AppController.connected ? qsTr("Reconnect") : qsTr("Connect")
            enabled: {
                if (AppController.connectionMode === "profile") return AppController.currentProfile.length > 0;
                if (AppController.connectionMode === "dsn")     return AppController.selectedDsn.length > 0;
                return AppController.connectionStringOverride.length > 0;
            }
            ToolTip.visible: hovered
            ToolTip.delay: 500
            ToolTip.timeout: 10000
            ToolTip.text: AppController.connected
                ? qsTr("Close and reopen the session.")
                : qsTr("Open an ODBC session to the target above.")
            onClicked: AppController.connectToProfile()
        }
    }
}
