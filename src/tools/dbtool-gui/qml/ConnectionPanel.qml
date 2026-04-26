// SPDX-License-Identifier: Apache-2.0
//
// Left-pane connection card. Three mutually-exclusive modes — Profile, ODBC
// DSN, and Custom connection string — picked via a segmented button at the
// top. Only the input for the active mode is shown, and a summary banner
// tells the user exactly what Connect will do next. This removes the old
// ambiguity where all three inputs were visible simultaneously and the user
// could not tell which one actually drove the connection.

import QtQuick
import QtQuick.Controls
import Lightweight.Migrations

Card {
    id: root

    // --- Mode selector (segmented control) ---
    Rectangle {
        width: parent.width
        height: 32
        radius: 6
        color: Theme.bgSubtle
        border.color: Theme.border

        Row {
            anchors.fill: parent
            anchors.margins: 2
            spacing: 2

            Repeater {
                model: [
                    { key: "profile", label: qsTr("Profile"),
                      tip: qsTr("Connect via a named entry in the Lightweight profile YAML. The profile stores the driver, server, database, and credentials (via the system keychain) so the same named target can be reached from dbtool, the GUI, and CI. Recommended for day-to-day use.") },
                    { key: "dsn",     label: qsTr("DSN"),
                      tip: qsTr("Connect via an ODBC Data Source Name registered on this machine (<i>odbcinst</i> on Linux, the ODBC Administrator on Windows). The DSN defines driver and server; username / password fields below override the DSN's stored credentials when needed. Use this to reach databases that are already configured at the system level.") },
                    { key: "custom",  label: qsTr("Custom"),
                      tip: qsTr("Connect with a raw ODBC connection string you type in full, e.g. <b>DRIVER=...;SERVER=...;DATABASE=...;UID=...;PWD=...</b>. Use this for ad-hoc targets that are not in the profile file and not registered as a system DSN — prototyping, test containers, or one-off investigations.") },
                ]
                Rectangle {
                    required property var modelData
                    readonly property bool active: AppController.connectionMode === modelData.key
                    width: (parent.width - 4) / 3
                    height: parent.height - 4
                    anchors.verticalCenter: parent.verticalCenter
                    color: active ? Theme.bgPanel : "transparent"
                    border.color: active ? Theme.border : "transparent"
                    radius: 4

                    ToolTip.visible: modeHover.hovered
                    ToolTip.text: modelData.tip
                    ToolTip.delay: 500
                    ToolTip.timeout: 10000
                    HoverHandler { id: modeHover }

                    Text {
                        anchors.centerIn: parent
                        text: modelData.label
                        color: parent.active ? Theme.text : Theme.textMuted
                        font.pixelSize: 12
                        font.weight: Font.Medium
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: AppController.setConnectionMode(modelData.key)
                    }
                }
            }
        }
    }

    // --- Profile mode ---
    Item {
        width: parent.width
        height: AppController.connectionMode === "profile" ? profileColumn.implicitHeight : 0
        visible: AppController.connectionMode === "profile"

        Column {
            id: profileColumn
            width: parent.width
            spacing: 6

            Label {
                width: parent.width
                text: qsTr("Profile")
                color: Theme.textMuted
                font.pixelSize: 11
                font.weight: Font.Medium
            }
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
            // Clickable file-path label. Opens the YAML with the system's
            // default handler so the user can edit it without leaving the
            // app. Hover underlines; cursor becomes a pointing hand.
            Label {
                id: profilePathLabel
                width: parent.width
                text: AppController.profilePath.length > 0
                    ? qsTr("Loaded from: %1").arg(AppController.profilePathDisplay)
                    : qsTr("No profile file loaded.")
                color: AppController.profilePath.length > 0 ? Theme.accent : Theme.textFaint
                font.pixelSize: 11
                font.underline: mouseArea.containsMouse
                wrapMode: Text.WrapAnywhere
                MouseArea {
                    id: mouseArea
                    anchors.fill: parent
                    hoverEnabled: AppController.profilePath.length > 0
                    cursorShape: AppController.profilePath.length > 0
                        ? Qt.PointingHandCursor : Qt.ArrowCursor
                    enabled: AppController.profilePath.length > 0
                    onClicked: AppController.openProfileFileExternally()
                    ToolTip.visible: containsMouse
                    ToolTip.text: qsTr("Open in default editor")
                    ToolTip.delay: 400
                }
            }
        }
    }

    // --- DSN mode ---
    Item {
        width: parent.width
        height: AppController.connectionMode === "dsn" ? dsnColumn.implicitHeight : 0
        visible: AppController.connectionMode === "dsn"

        Column {
            id: dsnColumn
            width: parent.width
            spacing: 6

            Label {
                width: parent.width
                text: qsTr("ODBC Data Source")
                color: Theme.textMuted
                font.pixelSize: 11
                font.weight: Font.Medium
            }
            Row {
                width: parent.width
                spacing: 6
                ComboBox {
                    id: dsnCombo
                    width: parent.width - refreshDsnButton.width - 6
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
                Button {
                    id: refreshDsnButton
                    text: "⟳"
                    ToolTip.visible: hovered
                    ToolTip.delay: 500
                    ToolTip.timeout: 10000
                    ToolTip.text: qsTr("Re-enumerate the ODBC Data Sources registered on this machine and update the dropdown. Use this after installing a driver, editing <b>odbc.ini</b>, or registering a new DSN via the ODBC Administrator — the list is cached at startup so new entries only appear after a refresh.")
                    onClicked: AppController.refreshOdbcDataSources()
                }
            }

            // Optional UID / PWD overrides. Leave empty for DSNs that use
            // Windows authentication or have credentials saved in the DSN
            // itself; fill in when the driver reports
            // "Login failed for user ''" (the SQL-Server-auth-without-
            // persisted-creds case).
            Label {
                width: parent.width
                text: qsTr("Username (optional)")
                color: Theme.textMuted
                font.pixelSize: 11
                font.weight: Font.Medium
            }
            TextField {
                width: parent.width
                placeholderText: qsTr("leave empty to use DSN / Windows auth")
                text: AppController.dsnUser
                onEditingFinished: AppController.setDsnUser(text)
            }
            Label {
                width: parent.width
                text: qsTr("Password (optional)")
                color: Theme.textMuted
                font.pixelSize: 11
                font.weight: Font.Medium
            }
            TextField {
                width: parent.width
                placeholderText: qsTr("leave empty to use DSN / Windows auth")
                echoMode: TextInput.Password
                text: AppController.dsnPassword
                onEditingFinished: AppController.setDsnPassword(text)
            }

            PluginsDirField { width: parent.width }
        }
    }

    // --- Custom connection-string mode ---
    Item {
        width: parent.width
        height: AppController.connectionMode === "custom" ? customColumn.implicitHeight : 0
        visible: AppController.connectionMode === "custom"

        Column {
            id: customColumn
            width: parent.width
            spacing: 6

            Label {
                width: parent.width
                text: qsTr("Connection string")
                color: Theme.textMuted
                font.pixelSize: 11
                font.weight: Font.Medium
            }
            TextField {
                width: parent.width
                placeholderText: qsTr("DRIVER=...;DATABASE=...")
                font.family: Theme.monoFont
                font.pixelSize: 12
                text: AppController.connectionStringOverride
                onEditingFinished: AppController.setConnectionStringOverride(text)
            }
            PluginsDirField { width: parent.width }
        }
    }

    // --- "Will connect …" summary banner ---
    Item { width: parent.width; height: 2 }
    Rectangle {
        width: parent.width
        height: summaryLabel.contentHeight + 16
        radius: 6
        color: Theme.accentSoft
        Row {
            anchors.left: parent.left
            anchors.leftMargin: 10
            anchors.right: parent.right
            anchors.rightMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6
            Label {
                anchors.verticalCenter: parent.verticalCenter
                text: "→"
                color: Theme.accent
                font.bold: true
                font.pixelSize: 13
            }
            Label {
                id: summaryLabel
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - 24
                text: AppController.connectionSummary
                color: Theme.accent
                font.pixelSize: 12
                font.weight: Font.Medium
                wrapMode: Text.WordWrap
            }
        }
    }

    // --- Connection-status banner ---
    Rectangle {
        width: parent.width
        height: 30
        radius: 6
        color: AppController.connected ? Theme.okSoft : Theme.warnSoft
        Row {
            anchors.left: parent.left
            anchors.leftMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: AppController.connected ? "✓" : "•"
                color: AppController.connected ? Theme.okText : Theme.warnText
                font.pixelSize: 12
                font.bold: true
            }
            Label {
                anchors.verticalCenter: parent.verticalCenter
                text: AppController.connected ? qsTr("Connected") : qsTr("Not connected")
                color: AppController.connected ? Theme.okText : Theme.warnText
                font.pixelSize: 12
                font.weight: Font.Medium
            }
        }
    }

    // --- Last error banner (if any) — red, blocking failures. ---
    // TextEdit (read-only) instead of Label so the user can mouse-select
    // the ODBC diagnostic and paste it into a bug report.
    Rectangle {
        width: parent.width
        height: lastErrorLabel.contentHeight + 16
        radius: 6
        color: Theme.errSoft
        visible: AppController.lastError.length > 0
        TextEdit {
            id: lastErrorLabel
            anchors.fill: parent
            anchors.margins: 8
            text: AppController.lastError
            color: Theme.errText
            font.pixelSize: 11
            wrapMode: TextEdit.WordWrap
            readOnly: true
            selectByMouse: true
            selectByKeyboard: true
            persistentSelection: true
        }
    }

    // --- Last warning banner (yellow, non-blocking) — e.g. "the database
    // is unmanaged". The connection is established; the user can still
    // apply migrations to bootstrap the schema_migrations table.
    Rectangle {
        width: parent.width
        height: lastWarningLabel.contentHeight + 16
        radius: 6
        color: Theme.warnSoft
        visible: AppController.lastWarning.length > 0
        TextEdit {
            id: lastWarningLabel
            anchors.fill: parent
            anchors.margins: 8
            text: qsTr("⚠ %1").arg(AppController.lastWarning)
            color: Theme.warnText
            font.pixelSize: 11
            wrapMode: TextEdit.WordWrap
            readOnly: true
            selectByMouse: true
            selectByKeyboard: true
            persistentSelection: true
        }
    }

    Button {
        width: parent.width
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
            ? qsTr("Close the current session and open a fresh one using the settings above. Use this after editing the profile file, switching connection mode, or when the existing session has gone stale (timeout, server restart). The migration list and release summary are reloaded afterwards.")
            : qsTr("Open an ODBC session to the target described by the <b>→</b> summary above, discover its migration state, and load the plugin directory. Does not modify the database — this is purely a read-only probe; the database will be marked <i>unmanaged</i> (yellow banner) if the <b>schema_migrations</b> table is missing.")
        onClicked: AppController.connectToProfile()
    }
}
