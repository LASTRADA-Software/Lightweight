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
//
// The suggestion popup follows the kit menu look: white, hairline border,
// 6 px radius, 34 px rows, the keyboard/hover row tinted brand-soft with a
// brand bar on its left edge.

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
        placeholderTextColor: Theme.clrOnSurfaceFaint
        font: Theme.monoFont(Theme.sizeMono)
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
        y: field.height + 4
        width: field.width
        padding: 0
        height: Math.min(260, suggestionList.contentHeight + 10)
        visible: false

        background: Rectangle {
            color: Theme.clrCard
            border.color: Theme.clrContainerHighest
            radius: Theme.r2

            // Single elevation step (`Theme.shadow`) lifting the list off
            // the option card it overlaps.
            Rectangle {
                z: -1
                anchors.fill: parent
                anchors.margins: -1
                anchors.topMargin: 1
                anchors.bottomMargin: -4
                radius: parent.radius + 1
                color: Theme.shadow
            }
        }

        ListView {
            id: suggestionList
            anchors.fill: parent
            anchors.margins: 5
            model: root._matches
            clip: true
            currentIndex: _matches.length > 0 ? 0 : -1
            highlightMoveDuration: 0
            highlight: Rectangle {
                color: Theme.clrPrimarySoft
                radius: Theme.r1
                Rectangle {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    anchors.topMargin: 6
                    anchors.bottomMargin: 6
                    width: 3
                    radius: 1.5
                    color: Theme.clrPrimary
                }
            }

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
                        color: Theme.clrOnSurface
                        font.pixelSize: Theme.sizeBodySm
                        elide: Text.ElideRight
                        width: parent.width
                    }
                    Label {
                        text: modelData.timestamp
                        color: Theme.clrOnSurfaceSubtle
                        font: Theme.monoFont(Theme.sizeLabel)
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
