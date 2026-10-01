// SPDX-License-Identifier: Apache-2.0
//
// Plugins-directory input in the kit's label-above layout (`k-field`): a
// "Plugins directory" label with an "optional" tag, a path field bound to
// `AppController.pluginsDir` beside a Browse… button that opens a native folder
// picker, then a help line saying what the value does.
//
// `errorText` switches the field to the kit's error styling (error border plus
// an error help line with an alert glyph). Nothing in the app validates the
// directory yet, so it is empty by default; it exists so a future check only
// has to supply the message.

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Lightweight.Migrations

Column {
    id: root
    spacing: 5

    /// Help text under the field (rich text). Empty hides it.
    property string helpText: ""
    /// Validation message; non-empty shows the field in its error state.
    property string errorText: ""

    readonly property string _pluginExt:
        Qt.platform.os === "windows" ? ".dll"
        : Qt.platform.os === "osx" ? ".dylib"
        : ".so"

    // ---- Label row: name + right-aligned "optional" tag ----
    RowLayout {
        width: parent.width
        spacing: 6

        Label {
            Layout.fillWidth: true
            text: qsTr("Plugins directory")
            color: Theme.clrOnSurfaceMed
            font.pixelSize: Theme.sizeLabel
            font.weight: Font.DemiBold
        }
        Label {
            text: qsTr("optional")
            color: Theme.clrOnSurfaceFaint
            font.pixelSize: Theme.sizeLabel
        }
    }

    // ---- Field + Browse… ----
    RowLayout {
        width: parent.width
        spacing: 6

        TextField {
            id: pathField
            Layout.fillWidth: true
            Layout.minimumWidth: 80
            implicitHeight: Theme.ctlMd
            placeholderText: qsTr("/path/to/migration/plugins")
            font: Theme.monoFont(12)
            leftPadding: 30
            rightPadding: 10
            topPadding: 0
            bottomPadding: 0
            verticalAlignment: TextInput.AlignVCenter
            color: Theme.clrOnSurface
            placeholderTextColor: Theme.clrOnSurfaceFaint
            selectionColor: Theme.clrPrimarySoftBorder
            selectedTextColor: Theme.clrOnSurface
            selectByMouse: true
            text: AppController.pluginsDir
            onEditingFinished: AppController.setPluginsDir(text)

            // Kit control: hairline field border, brand border + tint on
            // focus, error border when the value is invalid.
            background: Rectangle {
                radius: Theme.r1
                color: pathField.activeFocus ? Theme.clrFieldFocusBg : Theme.clrCard
                border.color: root.errorText !== "" ? Theme.clrError
                            : (pathField.activeFocus ? Theme.clrPrimary : Theme.clrFieldBorder)

                Glyph {
                    anchors.left: parent.left
                    anchors.leftMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    name: "folder-outline"
                    size: 14
                    color: Theme.clrOnSurfaceSubtle
                    knockout: parent.color
                }
            }
        }
        LsButton {
            id: browseButton
            variant: "secondary"
            size: "md"
            text: qsTr("Browse…")
            ToolTip.visible: hovered
            ToolTip.delay: 500
            ToolTip.timeout: 10000
            ToolTip.text: qsTr("Pick the directory containing migration plugin %1 files.").arg(root._pluginExt)
            onClicked: folderDialog.open()
        }
    }

    // ---- Error help line ----
    RowLayout {
        width: parent.width
        visible: root.errorText !== ""
        spacing: 5

        Glyph {
            Layout.alignment: Qt.AlignTop
            Layout.topMargin: 1
            name: "alert"
            size: 13
            color: Theme.clrError
        }
        Label {
            Layout.fillWidth: true
            text: root.errorText
            color: Theme.clrError
            font.pixelSize: Theme.sizeLabel
            wrapMode: Text.WordWrap
        }
    }

    // ---- Help line ----
    Label {
        width: parent.width
        visible: root.helpText !== ""
        text: root.helpText
        color: Theme.clrOnSurfaceSubtle
        font.pixelSize: Theme.sizeLabel
        wrapMode: Text.WordWrap
        textFormat: Text.RichText
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
