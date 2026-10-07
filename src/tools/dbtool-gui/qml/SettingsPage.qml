// SPDX-License-Identifier: Apache-2.0
//
// Settings page — a destination of Main.qml's navigation rail (reachable from
// both the Simple and the Expert view), no longer an overlay with a Done button. Changes save immediately,
// which the header says ("Saved automatically") in place of that button.
//
// A centred kit column (max ~760 px) of three panels, every field in the
// kit's label-above layout (`k-field`) with an "optional" tag, help text that
// says what an empty value means, and a validation line where the app has
// something to report:
//
//   1. Profiles file — the `dbtool.yml` path with Browse… / Use default, the
//      platform default it falls back to, and how many profiles were loaded.
//   2. Migration plugins — the `PluginsDirField` component, with an info
//      banner when the current profile carries its own plugins directory
//      (which then takes precedence over this value).
//   3. Backups — the managed-backups output folder with Browse… / Use
//      default, in the error state while the controller reports the folder
//      unusable (`folderProblem`).
//
// All settings persist through QSettings (handled in C++): no save / cancel
// here — every change is immediate, matching the rest of the GUI.

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Lightweight.Migrations

Rectangle {
    id: root
    color: Theme.clrBase

    /// Kept for Main.qml's existing connection. The page is a rail
    /// destination now and has no Done button of its own, so nothing on the
    /// page emits it.
    signal done()

    readonly property string _pluginExt:
        Qt.platform.os === "windows" ? ".dll"
        : Qt.platform.os === "osx" ? ".dylib"
        : ".so"

    // Loaded profiles, mirrored so the page can count them and find the
    // current profile's own plugins directory (QAbstractListModel exposes no
    // count/data to QML directly).
    Instantiator {
        id: profileMirror
        model: AppController.profiles
        delegate: QtObject {
            required property string name
            required property string pluginsDir
        }
    }

    // The current profile's own plugins directory, or "". Mirrors the first
    // step of AppController::EffectivePluginsDir(): when set, it wins over the
    // Settings value below.
    readonly property string _profilePluginsDir: {
        const count = profileMirror.count
        const current = AppController.currentProfile
        for (let i = 0; i < count; ++i) {
            const p = profileMirror.objectAt(i)
            if (p && p.name === current)
                return p.pluginsDir
        }
        return ""
    }

    /// Converts a file dialog URL to a local path for display and storage.
    function _localPath(url) {
        const s = url.toString()
        const localPath = Qt.platform.os === "windows"
            ? s.replace(/^file:\/{2,3}/, "")
            : s.replace(/^file:\/{2}/, "")
        return decodeURIComponent(localPath)
    }

    // Kit text field (`k-ctl`): 32 px, mono for paths, brand border + tint on
    // focus, error border when `invalid`, optional leading glyph.
    component KitField: TextField {
        id: field
        property bool invalid: false
        property string leadingGlyph: ""

        implicitHeight: Theme.ctlMd
        font: Theme.monoFont(12)
        leftPadding: leadingGlyph !== "" ? 30 : 10
        rightPadding: 10
        topPadding: 0
        bottomPadding: 0
        verticalAlignment: TextInput.AlignVCenter
        color: Theme.clrOnSurface
        placeholderTextColor: Theme.clrOnSurfaceFaint
        selectionColor: Theme.clrPrimarySoftBorder
        selectedTextColor: Theme.clrOnSurface
        selectByMouse: true

        background: Rectangle {
            radius: Theme.r1
            color: field.activeFocus ? Theme.clrFieldFocusBg : Theme.clrCard
            border.color: field.invalid ? Theme.clrError
                        : (field.activeFocus ? Theme.clrPrimary : Theme.clrFieldBorder)

            Glyph {
                visible: field.leadingGlyph !== ""
                anchors.left: parent.left
                anchors.leftMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                name: field.leadingGlyph
                size: 14
                color: Theme.clrOnSurfaceSubtle
                knockout: parent.color
            }
        }
    }

    // Kit field label row: name on the left, faint "optional" tag on the right.
    component FieldHeader: RowLayout {
        property string text: ""
        property bool optional: true
        spacing: 6

        Label {
            Layout.fillWidth: true
            text: parent.text
            color: Theme.clrOnSurfaceMed
            font.pixelSize: Theme.sizeLabel
            font.weight: Font.DemiBold
        }
        Label {
            visible: parent.optional
            text: qsTr("optional")
            color: Theme.clrOnSurfaceFaint
            font.pixelSize: Theme.sizeLabel
        }
    }

    // Kit help line under a field; `kind` "ok"/"err" adds the glyph and colour
    // of a validation result.
    component HelpLine: RowLayout {
        property string text: ""
        property string kind: ""
        spacing: 5

        Glyph {
            visible: parent.kind !== ""
            Layout.alignment: Qt.AlignTop
            Layout.topMargin: 1
            name: parent.kind === "ok" ? "check-circle" : "alert"
            size: 13
            color: parent.kind === "ok" ? Theme.clrSuccess : Theme.clrError
            knockout: Theme.clrCard
        }
        Label {
            Layout.fillWidth: true
            text: parent.text
            color: parent.kind === "ok" ? Theme.clrSuccess
                 : (parent.kind === "err" ? Theme.clrError : Theme.clrOnSurfaceSubtle)
            font.pixelSize: Theme.sizeLabel
            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
            textFormat: Text.RichText
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        PageHeader {
            Layout.fillWidth: true

            crumbs: [ "dbtool", qsTr("Settings") ]
            title: qsTr("Settings")

            actions: [
                // Stands in for the old Done button: says why there is none.
                Row {
                    spacing: 6
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 8
                        height: 8
                        radius: 4
                        color: Theme.clrSuccessDot
                    }
                    Label {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Saved automatically")
                        color: Theme.clrOnSurfaceSubtle
                        font.pixelSize: Theme.sizeBodySm
                    }
                }
            ]
        }

        ScrollView {
            id: scroller
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            contentWidth: availableWidth

            ColumnLayout {
                x: Math.round((scroller.availableWidth - width) / 2)
                width: Math.max(0, Math.min(760, scroller.availableWidth - 48))
                spacing: 14

                Item { Layout.preferredHeight: 6 }

                // --- 1. Profile store (dbtool.yml) ---
                Card {
                    Layout.fillWidth: true
                    title: qsTr("Profiles file")
                    glyph: "document"
                    spacing: 5

                    FieldHeader {
                        width: parent.width
                        text: qsTr("dbtool.yml location")
                    }
                    RowLayout {
                        width: parent.width
                        spacing: 6

                        KitField {
                            id: storePathField
                            Layout.fillWidth: true
                            Layout.minimumWidth: 80
                            placeholderText: AppController.defaultProfileStorePath
                            text: AppController.profileStorePath
                            onEditingFinished: AppController.setProfileStorePath(text)
                        }
                        LsButton {
                            id: storeBrowseButton
                            variant: "secondary"
                            size: "md"
                            text: qsTr("Browse…")
                            onClicked: storeFileDialog.open()
                        }
                        LsButton {
                            id: storeResetButton
                            variant: "ghost"
                            size: "md"
                            text: qsTr("Use default")
                            onClicked: {
                                storePathField.text = "";
                                AppController.setProfileStorePath("");
                            }
                        }
                    }
                    HelpLine {
                        width: parent.width
                        visible: profileMirror.count > 0
                        kind: "ok"
                        text: qsTr("%n profile(s) loaded", "", profileMirror.count)
                    }
                    HelpLine {
                        width: parent.width
                        text: qsTr("Empty uses the platform default, <span style=\"font-family:'%1'\">%2</span>.")
                              .arg(Theme.monoFamilies[0]).arg(AppController.defaultProfileStorePath)
                    }
                }

                // --- 2. Migration plugins ---
                Card {
                    Layout.fillWidth: true
                    title: qsTr("Migration plugins")
                    glyph: "plug"

                    Banner {
                        width: parent.width
                        visible: root._profilePluginsDir !== ""
                        kind: "info"
                        text: qsTr("Profile <b>%1</b> sets its own plugins directory, so it takes precedence over this value while that profile is selected.")
                              .arg(AppController.currentProfile)
                    }
                    PluginsDirField {
                        width: parent.width
                        helpText: qsTr("Directory containing the compiled migration plugin <b>%1</b> files. Used when neither the selected profile nor dbtool.yml names one.")
                                  .arg(root._pluginExt)
                    }
                }

                // --- 3. Backups folder ---
                Card {
                    Layout.fillWidth: true
                    title: qsTr("Backups")
                    glyph: "archive-outline"
                    spacing: 5

                    FieldHeader {
                        width: parent.width
                        text: qsTr("Backup folder")
                    }
                    RowLayout {
                        width: parent.width
                        spacing: 6

                        KitField {
                            id: backupFolderField
                            Layout.fillWidth: true
                            Layout.minimumWidth: 80
                            leadingGlyph: "folder-outline"
                            invalid: AppController.managedBackups.folderProblem !== ""
                            placeholderText: AppController.managedBackups.defaultBackupFolder
                            text: AppController.managedBackups.backupFolder
                            onEditingFinished: AppController.managedBackups.setBackupFolder(text)
                        }
                        LsButton {
                            id: backupBrowseButton
                            variant: "secondary"
                            size: "md"
                            text: qsTr("Browse…")
                            onClicked: backupFolderDialog.open()
                        }
                        LsButton {
                            id: backupResetButton
                            variant: "ghost"
                            size: "md"
                            text: qsTr("Use default")
                            onClicked: {
                                backupFolderField.text = "";
                                AppController.managedBackups.setBackupFolder("");
                            }
                        }
                    }
                    HelpLine {
                        width: parent.width
                        visible: AppController.managedBackups.folderProblem !== ""
                        kind: "err"
                        text: AppController.managedBackups.folderProblem
                    }
                    HelpLine {
                        width: parent.width
                        text: qsTr("Each profile is written as <span style=\"font-family:'%1'\">&lt;profile&gt;.zip</span> and replaced on the next run. Empty uses <span style=\"font-family:'%1'\">%2</span>.")
                              .arg(Theme.monoFamilies[0]).arg(AppController.managedBackups.defaultBackupFolder)
                    }
                }

                Item { Layout.preferredHeight: 16 }
            }
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
            const decoded = root._localPath(selectedFile);
            storePathField.text = decoded;
            AppController.setProfileStorePath(decoded);
        }
    }

    FolderDialog {
        id: backupFolderDialog
        title: qsTr("Select backups folder")
        currentFolder: {
            const cur = AppController.managedBackups.backupFolder.length > 0
                ? AppController.managedBackups.backupFolder
                : AppController.managedBackups.defaultBackupFolder;
            return cur.length > 0 ? Qt.resolvedUrl("file:///" + cur) : "";
        }
        onAccepted: {
            const decoded = root._localPath(selectedFolder);
            backupFolderField.text = decoded;
            AppController.managedBackups.setBackupFolder(decoded);
        }
    }
}
