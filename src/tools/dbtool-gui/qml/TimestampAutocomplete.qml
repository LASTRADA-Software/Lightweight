// SPDX-License-Identifier: Apache-2.0
//
// Editable text field that shows a popup of migration matches as the user
// types. Matching is prefix-on-timestamp **or** substring-on-title, so the
// user can hit Enter on "Init" to pick the "Initial Migration" row just as
// easily as typing a long numeric timestamp. Selecting a match populates
// the `value` property (the timestamp) and updates the editable text to the
// raw timestamp for clarity.
//
// Public surface:
//   property string value         — the resolved timestamp (empty if unset)
//   property string placeholderText
//   signal valueChanged()         — emitted when `value` is set from a pick

import QtQuick
import QtQuick.Controls
import Lightweight.Migrations

Item {
    id: root

    property string value: ""
    property string placeholderText: qsTr("e.g. 20230201000000 or 'Initial'")

    implicitHeight: field.implicitHeight
    implicitWidth: 200

    // --- Match computation --------------------------------------------------
    // Roles on MigrationListModel — keep in sync with the enum in the header.
    readonly property int _roleTimestamp: 257
    readonly property int _roleTitle:     258
    readonly property int _roleStatus:    259

    readonly property var _matches: {
        const query = field.text.trim();
        const out = [];
        // Empty query shows every pending migration so the user can browse.
        const m = AppController.migrations;
        const n = m.rowCount();
        const qLower = query.toLowerCase();
        for (let i = 0; i < n; ++i) {
            const idx = m.index(i, 0);
            const status = m.data(idx, _roleStatus);
            if (status === "applied" || status === "unknown") continue;
            const ts = m.data(idx, _roleTimestamp);
            const title = m.data(idx, _roleTitle);
            if (query.length === 0
                || ts.indexOf(query) === 0
                || title.toLowerCase().indexOf(qLower) >= 0) {
                out.push({ timestamp: ts, title: title, status: status });
                if (out.length >= 20) break;
            }
        }
        return out;
    }

    // --- Field + popup ------------------------------------------------------

    TextField {
        id: field
        width: parent.width
        placeholderText: root.placeholderText
        font.family: Theme.monoFont
        font.pixelSize: 12
        // Two-way sync: external changes to `value` update the visible text;
        // user keystrokes are tracked separately and only promoted to
        // `value` when a popup match is selected (or when the raw entry is
        // already a valid 14-digit timestamp).
        text: root.value

        onTextChanged: {
            if (text.length === 14 && /^\d{14}$/.test(text))
                root.value = text;
            else if (text.length === 0)
                root.value = "";

            if (activeFocus && root._matches.length > 0)
                popup.open();
            else if (text.length === 0)
                popup.close();
        }
        onActiveFocusChanged: {
            if (activeFocus && root._matches.length > 0) popup.open();
            else popup.close();
        }
        Keys.onDownPressed: if (popup.visible) suggestionList.incrementCurrentIndex()
        Keys.onUpPressed:   if (popup.visible) suggestionList.decrementCurrentIndex()
        Keys.onReturnPressed: root._pickCurrent()
        Keys.onEnterPressed:  root._pickCurrent()
        Keys.onEscapePressed: popup.close()
    }

    function _pickCurrent() {
        if (!popup.visible) return;
        const row = suggestionList.currentIndex;
        if (row < 0 || row >= _matches.length) return;
        const pick = _matches[row];
        root.value = pick.timestamp;
        field.text = pick.timestamp;
        popup.close();
    }

    Popup {
        id: popup
        y: field.height + 2
        width: field.width
        padding: 0
        height: Math.min(260, suggestionList.contentHeight + 2)
        visible: false

        background: Rectangle {
            color: Theme.bgPanel
            border.color: Theme.border
            radius: 6
        }

        ListView {
            id: suggestionList
            anchors.fill: parent
            anchors.margins: 1
            model: root._matches
            clip: true
            currentIndex: _matches.length > 0 ? 0 : -1
            highlightMoveDuration: 0
            highlight: Rectangle { color: Theme.bgSelected; radius: 4 }

            delegate: Item {
                required property var modelData
                required property int index
                width: ListView.view.width
                height: 34

                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    onEntered: suggestionList.currentIndex = index
                    onClicked: {
                        suggestionList.currentIndex = index;
                        root._pickCurrent();
                    }
                }

                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.leftMargin: 10
                    anchors.right: pillHolder.left
                    anchors.rightMargin: 8
                    spacing: 2
                    Label {
                        text: modelData.title
                        color: Theme.text
                        font.pixelSize: 12
                        elide: Text.ElideRight
                        width: parent.width
                    }
                    Label {
                        text: modelData.timestamp
                        color: Theme.textFaint
                        font.family: Theme.monoFont
                        font.pixelSize: 11
                        width: parent.width
                    }
                }

                Item {
                    id: pillHolder
                    anchors.right: parent.right
                    anchors.rightMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    width: pill.width
                    height: pill.height
                    StatusPill { id: pill; status: modelData.status }
                }
            }
        }
    }
}
