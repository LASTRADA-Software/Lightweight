// SPDX-License-Identifier: Apache-2.0
//
// Full-page settings view. Opened from the toolbar's Settings button (Expert
// view only) and dismissed via the Done button which flips Main.qml's
// `showSettings` flag back to false. Three sections, each rendered as a
// `Card`:
//
//   1. Profiles file — TextField for the `dbtool.yml` path with Browse / Reset.
//      Empty path means "use the platform default" — surfaced as placeholder.
//   2. Plugins directory — reuses the existing `PluginsDirField` component.
//   3. Theme — radio group bound to `ThemeController.mode`.
//
// All three settings persist through QSettings (handled in C++): no save /
// cancel here — every change is immediate, matching the rest of the GUI.

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Lightweight.Migrations

Rectangle {
    id: root
    color: Theme.bgPage

    /// Emitted when the user dismisses the Settings page via the Done button.
    /// Main.qml binds this to `showSettings = false`.
    signal done()

    readonly property string _pluginExt:
        Qt.platform.os === "windows" ? ".dll"
        : Qt.platform.os === "osx" ? ".dylib"
        : ".so"

    ScrollView {
        anchors.fill: parent
        clip: true
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        contentWidth: availableWidth

        ColumnLayout {
            width: Math.min(720, parent ? parent.width : 720)
            anchors.horizontalCenter: parent ? parent.horizontalCenter : undefined
            spacing: 14

            Item { Layout.preferredHeight: 8 }

            // Page header
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                spacing: 8

                Label {
                    text: qsTr("Settings")
                    color: Theme.text
                    font.pixelSize: 22
                    font.weight: Font.DemiBold
                    Layout.fillWidth: true
                }
                Button {
                    text: qsTr("Done")
                    onClicked: root.done()
                }
            }

            // --- 1. Profile store (dbtool.yml) ---
            Card {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16

                Label {
                    width: parent.width
                    text: qsTr("Profiles file")
                    color: Theme.text
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                }
                Label {
                    width: parent.width
                    text: qsTr("Location of the <b>dbtool.yml</b> file the GUI loads its connection profiles from. Leave empty to use the platform default.")
                    color: Theme.textMuted
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                    textFormat: Text.RichText
                }
                Row {
                    width: parent.width
                    spacing: 6
                    TextField {
                        id: storePathField
                        width: parent.width - storeBrowseButton.width - storeResetButton.width - 12
                        placeholderText: AppController.defaultProfileStorePath
                        text: AppController.profileStorePath
                        font: Theme.monoFont(12)
                        onEditingFinished: AppController.setProfileStorePath(text)
                    }
                    Button {
                        id: storeBrowseButton
                        text: qsTr("Browse…")
                        onClicked: storeFileDialog.open()
                    }
                    Button {
                        id: storeResetButton
                        text: qsTr("Reset")
                        onClicked: {
                            storePathField.text = "";
                            AppController.setProfileStorePath("");
                        }
                    }
                }
                Label {
                    width: parent.width
                    text: qsTr("Default: %1").arg(AppController.defaultProfileStorePath)
                    color: Theme.textFaint
                    font.pixelSize: 10
                    elide: Text.ElideMiddle
                }
            }

            // --- 2. Plugins directory ---
            Card {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16

                Label {
                    width: parent.width
                    text: qsTr("Plugins directory")
                    color: Theme.text
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                }
                Label {
                    width: parent.width
                    text: qsTr("Directory containing the compiled migration plugin <b>%1</b> files. Ignored when a profile (which carries its own plugins-dir) is selected.").arg(root._pluginExt)
                    color: Theme.textMuted
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                    textFormat: Text.RichText
                }
                PluginsDirField { width: parent.width }
            }

            // --- 3. Theme ---
            Card {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16

                Label {
                    width: parent.width
                    text: qsTr("Theme")
                    color: Theme.text
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                }
                Label {
                    width: parent.width
                    text: qsTr("System follows your OS colour scheme; Light and Dark override it.")
                    color: Theme.textMuted
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                }
                ButtonGroup { id: themeGroup }
                Row {
                    width: parent.width
                    spacing: 16
                    RadioButton {
                        text: qsTr("System")
                        ButtonGroup.group: themeGroup
                        checked: ThemeController.mode === ThemeController.System
                        onToggled: if (checked) ThemeController.mode = ThemeController.System
                    }
                    RadioButton {
                        text: qsTr("Light")
                        ButtonGroup.group: themeGroup
                        checked: ThemeController.mode === ThemeController.Light
                        onToggled: if (checked) ThemeController.mode = ThemeController.Light
                    }
                    RadioButton {
                        text: qsTr("Dark")
                        ButtonGroup.group: themeGroup
                        checked: ThemeController.mode === ThemeController.Dark
                        onToggled: if (checked) ThemeController.mode = ThemeController.Dark
                    }
                }
            }

            Item { Layout.preferredHeight: 16 }
        }
    }

    FileDialog {
        id: storeFileDialog
        title: qsTr("Select dbtool.yml")
        nameFilters: [ qsTr("YAML files (*.yml *.yaml)"), qsTr("All files (*)") ]
        currentFolder: {
            const cur = AppController.profileStorePath.length > 0
                ? AppController.profileStorePath
                : AppController.defaultProfileStorePath;
            // Strip the trailing filename so the dialog opens in the parent
            // folder. Works for both forward- and back-slash paths.
            const sep = Math.max(cur.lastIndexOf('/'), cur.lastIndexOf('\\'));
            const folder = sep > 0 ? cur.substring(0, sep) : cur;
            return folder.length > 0 ? Qt.resolvedUrl("file:///" + folder) : "";
        }
        onAccepted: {
            const url = selectedFile.toString();
            const localPath = Qt.platform.os === "windows"
                ? url.replace(/^file:\/{2,3}/, "")
                : url.replace(/^file:\/{2}/, "");
            const decoded = decodeURIComponent(localPath);
            storePathField.text = decoded;
            AppController.setProfileStorePath(decoded);
        }
    }
}
