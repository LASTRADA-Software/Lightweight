// SPDX-License-Identifier: Apache-2.0
//
// Plugins-directory input: a label, a TextField bound to
// `AppController.pluginsDir`, and a Browse… button that opens a native
// folder picker. Used in both the DSN and Custom connection modes.

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import Lightweight.Migrations

Column {
    id: root
    spacing: 6

    readonly property string _pluginExt:
        Qt.platform.os === "windows" ? ".dll"
        : Qt.platform.os === "osx" ? ".dylib"
        : ".so"

    Label {
        width: parent.width
        text: qsTr("Plugins directory")
        color: Theme.textMuted
        font.pixelSize: 11
        font.weight: Font.Medium
    }
    Row {
        width: parent.width
        spacing: 6
        TextField {
            id: pathField
            width: parent.width - browseButton.width - 6
            placeholderText: qsTr("/path/to/migration/plugins")
            font.pixelSize: 12
            text: AppController.pluginsDir
            onEditingFinished: AppController.setPluginsDir(text)
        }
        Button {
            id: browseButton
            text: qsTr("Browse…")
            ToolTip.visible: hovered
            ToolTip.delay: 500
            ToolTip.timeout: 10000
            ToolTip.text: qsTr("Pick the directory containing migration plugin %1 files.").arg(root._pluginExt)
            onClicked: folderDialog.open()
        }
    }

    FolderDialog {
        id: folderDialog
        title: qsTr("Select migration plugins directory")
        // Start in the current selection if we have one, otherwise the
        // user's home. Qt's FolderDialog wants a URL for `currentFolder`.
        currentFolder: AppController.pluginsDir.length > 0
            ? Qt.resolvedUrl("file:///" + AppController.pluginsDir)
            : ""
        onAccepted: {
            // selectedFolder is a QUrl like `file:///C:/path/to/plugins`.
            // Strip the scheme for user-friendly display and storage.
            const url = selectedFolder.toString();
            const localPath = Qt.platform.os === "windows"
                ? url.replace(/^file:\/{2,3}/, "")
                : url.replace(/^file:\/{2}/, "");
            AppController.setPluginsDir(decodeURIComponent(localPath));
        }
    }
}
