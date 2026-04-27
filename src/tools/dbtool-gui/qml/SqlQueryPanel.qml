// SPDX-License-Identifier: Apache-2.0
//
// SQL Query panel — sibling tab of `LogPanel` inside `BottomPanel.qml`.
// The user types a query in the editor on top, hits Execute (or
// `Ctrl+Enter`) and sees results / errors below. Intended for the Expert
// view only; not embedded by `SimpleView.qml`.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Lightweight.Migrations

Rectangle {
    id: root
    color: Theme.bgPanel

    /// Number of rows in the most recent successful execution. Bound by
    /// `BottomPanel` so the shared footer can show "SQL — N row(s)".
    property int rowCount: 0

    // True after `errorOccurred` until the next successful run; flips the
    // results pane to the error view.
    property bool hasError: false
    property string errorMessage: ""
    property string errorSqlState: ""
    property int errorNative: 0

    property string statusText: qsTr("Ready")

    /// Currently focused cell in the result grid, or (-1, -1) when nothing
    /// is selected. Drives the cell-highlight delegate and the Ctrl+C
    /// shortcut target.
    property int selectedRow: -1
    property int selectedColumn: -1

    /// Hidden TextEdit used as a clipboard shim — QML has no direct
    /// clipboard API, so we route every Copy action through `copy()` on
    /// this off-screen, read-only `TextEdit`. Same trick `LogPanel` uses
    /// for its Copy button.
    TextEdit {
        id: clipboardShim
        visible: false
        readOnly: true
    }

    function copyToClipboard(text) {
        clipboardShim.text = text
        clipboardShim.selectAll()
        clipboardShim.copy()
        clipboardShim.deselect()
    }

    function copySelectedCell() {
        if (root.selectedRow < 0 || root.selectedColumn < 0)
            return
        copyToClipboard(AppController.sqlQueryRunner.model.cellText(
            root.selectedRow, root.selectedColumn))
    }

    function runQuery() {
        if (!AppController.connected || AppController.sqlQueryRunner.busy)
            return
        if (editor.text.trim().length === 0)
            return
        AppController.sqlQueryRunner.execute(editor.text)
    }

    Connections {
        target: AppController.sqlQueryRunner
        function onStarted() {
            root.hasError = false
            root.statusText = qsTr("Running…")
            root.selectedRow = -1
            root.selectedColumn = -1
        }
        function onFinished(rows, elapsedMs, status) {
            root.hasError = false
            root.rowCount = rows
            root.statusText = status
        }
        function onErrorOccurred(message, sqlState, nativeError) {
            root.hasError = true
            root.errorMessage = message
            root.errorSqlState = sqlState
            root.errorNative = nativeError
            root.rowCount = 0
            root.statusText = qsTr("Error")
        }
    }

    // `Ctrl+Enter` / `Ctrl+Return` runs the query. Numpad enter and main
    // return key map to different `Qt.Key` values across platforms, so we
    // accept both sequences.
    Shortcut {
        sequences: ["Ctrl+Return", "Ctrl+Enter"]
        context: Qt.WindowShortcut
        // Only fire when the SQL panel is the visible tab AND something
        // inside the panel has keyboard focus. Avoids hijacking the
        // shortcut while the user is reading the log on the other tab.
        enabled: root.visible && (editor.activeFocus || root.activeFocus)
        onActivated: root.runQuery()
    }

    // `Ctrl+C` copies the currently-selected result cell. Disabled while the
    // editor has focus so the user's normal text-edit copy still wins.
    Shortcut {
        sequences: [StandardKey.Copy]
        context: Qt.WindowShortcut
        enabled: root.visible && !editor.activeFocus
                 && root.selectedRow >= 0 && root.selectedColumn >= 0
        onActivated: root.copySelectedCell()
    }

    // Right-click context menu over a result cell. Populated with the
    // selected cell's coordinates by the delegate's `onPressed` handler
    // before `popup()`.
    Menu {
        id: cellMenu
        MenuItem {
            text: qsTr("Copy cell")
            enabled: root.selectedRow >= 0 && root.selectedColumn >= 0
            onTriggered: root.copySelectedCell()
        }
        MenuItem {
            text: qsTr("Copy row")
            enabled: root.selectedRow >= 0
            onTriggered: root.copyToClipboard(
                AppController.sqlQueryRunner.model.rowAsTsv(root.selectedRow))
        }
        MenuItem {
            text: qsTr("Copy row as JSON")
            enabled: root.selectedRow >= 0
            onTriggered: root.copyToClipboard(
                AppController.sqlQueryRunner.model.rowAsJson(root.selectedRow))
        }
        MenuItem {
            text: qsTr("Copy column header")
            enabled: root.selectedColumn >= 0
            onTriggered: root.copyToClipboard(
                AppController.sqlQueryRunner.model.columnHeader(root.selectedColumn))
        }
        MenuSeparator {}
        MenuItem {
            text: qsTr("Copy all (TSV)")
            enabled: AppController.sqlQueryRunner.model.rowCount > 0
            onTriggered: root.copyToClipboard(
                AppController.sqlQueryRunner.model.allAsTsv())
        }
    }

    SplitView {
        id: split
        anchors.fill: parent
        orientation: Qt.Vertical
        handle: Rectangle {
            implicitHeight: 4
            color: SplitHandle.pressed ? Theme.accent
                 : SplitHandle.hovered ? Theme.borderStrong
                 : Theme.border
        }

        // Editor + toolbar
        Rectangle {
            SplitView.preferredHeight: 140
            SplitView.minimumHeight: 80
            color: Theme.bgPanel

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                // Toolbar row
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 32
                    color: Theme.bgChrome
                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 1
                        color: Theme.border
                    }
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        spacing: 6
                        Button {
                            text: qsTr("Execute")
                            highlighted: true
                            enabled: AppController.connected
                                     && !AppController.sqlQueryRunner.busy
                                     && editor.text.trim().length > 0
                            ToolTip.visible: hovered
                            ToolTip.delay: 500
                            ToolTip.text: qsTr("Run the SQL above against the connected database. Shortcut: Ctrl+Enter.")
                            onClicked: root.runQuery()
                        }
                        Button {
                            text: qsTr("Clear")
                            flat: true
                            enabled: editor.text.length > 0 || root.hasError || root.rowCount > 0
                            onClicked: {
                                editor.clear()
                                AppController.sqlQueryRunner.model.clearRows()
                                root.rowCount = 0
                                root.hasError = false
                                root.selectedRow = -1
                                root.selectedColumn = -1
                                root.statusText = qsTr("Ready")
                            }
                        }
                        Item { Layout.fillWidth: true }
                        Label {
                            text: AppController.connected
                                ? root.statusText
                                : qsTr("Not connected")
                            color: root.hasError ? Theme.errText : Theme.textFaint
                            font.pixelSize: 11
                        }
                    }
                }

                ScrollView {
                    id: editorScroll
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true

                    WheelScrollAmplifier { target: editorScroll.contentItem }

                    TextArea {
                        id: editor
                        placeholderText: qsTr("Type SQL here. Ctrl+Enter to run.")
                        placeholderTextColor: Theme.textFaint
                        font.family: Theme.monoFont
                        font.pixelSize: 13
                        color: Theme.text
                        selectByMouse: true
                        wrapMode: TextEdit.NoWrap
                        background: Rectangle { color: Theme.bgPanel }
                        leftPadding: 14
                        rightPadding: 14
                        topPadding: 6
                        bottomPadding: 6
                        SqlSyntaxHighlighter {
                            textDocument: editor.textDocument
                        }
                    }
                }
            }
        }

        // Results / error pane
        Rectangle {
            SplitView.fillHeight: true
            SplitView.minimumHeight: 80
            color: Theme.bgPanel

            // Error view: replaces the table when the last execution failed.
            Rectangle {
                anchors.fill: parent
                anchors.margins: 8
                visible: root.hasError
                color: Theme.errSoft
                border.color: Theme.err
                border.width: 1
                radius: 4

                ScrollView {
                    id: errorScroll
                    anchors.fill: parent
                    anchors.margins: 10
                    clip: true

                    WheelScrollAmplifier { target: errorScroll.contentItem }

                    TextArea {
                        readOnly: true
                        selectByMouse: true
                        wrapMode: TextEdit.Wrap
                        font.family: Theme.monoFont
                        font.pixelSize: 12
                        color: Theme.errText
                        background: null
                        text: {
                            var msg = root.errorMessage
                            if (root.errorSqlState.length > 0)
                                msg += "\nSQL state: " + root.errorSqlState
                                     + " (native error: " + root.errorNative + ")"
                            return msg
                        }
                    }
                }
            }

            // Result view: header row + table view bound to the runner's model.
            ColumnLayout {
                anchors.fill: parent
                spacing: 0
                visible: !root.hasError

                HorizontalHeaderView {
                    id: headerView
                    Layout.fillWidth: true
                    syncView: tableView
                    clip: true
                    model: AppController.sqlQueryRunner.model
                    delegate: Rectangle {
                        implicitHeight: 26
                        implicitWidth: 100
                        color: Theme.bgChrome
                        border.color: Theme.border
                        border.width: 1
                        Label {
                            anchors.fill: parent
                            anchors.leftMargin: 6
                            anchors.rightMargin: 6
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                            text: display !== undefined ? display : ""
                            color: Theme.text
                            font.pixelSize: 11
                            font.weight: Font.DemiBold
                        }
                    }
                }

                TableView {
                    id: tableView
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    columnSpacing: 0
                    rowSpacing: 0
                    boundsBehavior: Flickable.StopAtBounds
                    model: AppController.sqlQueryRunner.model

                    // The default Basic-style ScrollBar fades its handle to
                    // opacity 0 whenever `active` is false — and Flickable
                    // writes to that property from C++ at the end of every
                    // gesture, defeating any QML binding. Pin handle opacity
                    // to `size < 1.0` (i.e. "content overflows the view")
                    // instead so both bars stay visible while there's
                    // something to scroll. Same fix as `KineticListView`.
                    ScrollBar.vertical: ScrollBar {
                        id: vbar
                        policy: ScrollBar.AsNeeded
                        interactive: true
                        contentItem: Rectangle {
                            implicitWidth: 8
                            radius: width / 2
                            color: vbar.pressed ? Theme.textMuted : Theme.textFaint
                            opacity: vbar.size < 1.0
                                ? (vbar.pressed || vbar.hovered ? 0.85 : 0.55)
                                : 0.0
                            Behavior on opacity { NumberAnimation { duration: 150 } }
                        }
                    }
                    ScrollBar.horizontal: ScrollBar {
                        id: hbar
                        policy: ScrollBar.AsNeeded
                        interactive: true
                        contentItem: Rectangle {
                            implicitHeight: 8
                            radius: height / 2
                            color: hbar.pressed ? Theme.textMuted : Theme.textFaint
                            opacity: hbar.size < 1.0
                                ? (hbar.pressed || hbar.hovered ? 0.85 : 0.55)
                                : 0.0
                            Behavior on opacity { NumberAnimation { duration: 150 } }
                        }
                    }

                    WheelScrollAmplifier { target: tableView }

                    delegate: Rectangle {
                        id: cellDelegate
                        implicitHeight: 24
                        implicitWidth: 140
                        readonly property bool selected: row === root.selectedRow
                                                         && column === root.selectedColumn
                        readonly property bool inSelectedRow: row === root.selectedRow
                        color: selected
                            ? Theme.bgSelected
                            : (inSelectedRow
                                ? Theme.bgHover
                                : (row % 2 === 0 ? Theme.bgPanel : Theme.bgSubtle))
                        border.color: Theme.divider
                        border.width: 1
                        Label {
                            anchors.fill: parent
                            anchors.leftMargin: 6
                            anchors.rightMargin: 6
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                            text: model.display !== undefined ? model.display : ""
                            color: model.isNull ? Theme.textFaint : Theme.text
                            font.italic: model.isNull === true
                            font.family: Theme.monoFont
                            font.pixelSize: 12
                        }
                        MouseArea {
                            anchors.fill: parent
                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                            // Single-click selects, double-click also copies
                            // the cell — the latter matches what most SQL
                            // clients do and saves a context-menu trip.
                            onPressed: (mouse) => {
                                root.selectedRow = row
                                root.selectedColumn = column
                                if (mouse.button === Qt.RightButton)
                                    cellMenu.popup()
                            }
                            onDoubleClicked: root.copySelectedCell()
                        }
                    }
                }

                // Empty-state hint when no result has been produced yet.
                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: AppController.sqlQueryRunner.model.rowCount === 0
                             && !root.hasError
                             && root.rowCount === 0
                    Label {
                        anchors.centerIn: parent
                        text: AppController.connected
                            ? qsTr("Type a query above and press Ctrl+Enter to run it.")
                            : qsTr("Connect to a database to run queries.")
                        color: Theme.textFaint
                        font.pixelSize: 12
                    }
                }
            }
        }
    }
}
