// SPDX-License-Identifier: Apache-2.0
//
// Dark terminal-style log panel. The background is terminal-dark regardless
// of the window palette — the mockup intentionally keeps the log looking
// like a shell, and mixing it with the light theme loses the at-a-glance
// distinction from the rest of the UI.
//
// The body is a read-only `TextArea` (not a ListView) so the user can
// rubber-band across lines and copy chunks of the log to the clipboard.
// Per-level colour is preserved via rich-text `<span>` tags — we build the
// HTML incrementally instead of rebinding `text` on every new line, which
// keeps long-running migrations from re-layouting the whole document on
// each append.
//
// As of the SQL Query tab work, `LogPanel` is body-only: the collapse /
// expand toggle moved to `BottomPanel.qml` so it can be shared with the
// sibling tabs. `LogPanel` exposes `lineCount` so the parent can render
// "Log — N line(s)" in the shared footer.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Lightweight.Migrations

Rectangle {
    id: root
    color: Theme.bgTerminal
    radius: 0

    // Accumulating HTML document. Kept out of `TextArea.text` directly so
    // the viewport does not re-highlight existing text while the migration
    // is still streaming — TextArea's `append()` is the targeted-insert
    // API that preserves cursor/selection state.
    property int lineCount: 0

    function colorFor(level) {
        if (level === 2) return "#f87171";
        if (level === 1) return "#fbbf24";
        return "#cbd5e1";
    }

    function escapeHtml(s) {
        return s.replace(/&/g, "&amp;")
                .replace(/</g, "&lt;")
                .replace(/>/g, "&gt;")
                .replace(/ /g, "&nbsp;");
    }

    function clearLog() {
        logText.clear();
        root.lineCount = 0;
    }

    function copyLog() {
        logText.selectAll();
        logText.copy();
        logText.deselect();
    }

    // Thin coloured header matching the mockup's "Dry-run · 7 operations · …"
    // strip. Kept compact so the log body dominates the panel.
    Rectangle {
        id: header
        width: parent.width
        height: 28
        color: Theme.bgChrome
        anchors.top: parent.top

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: Theme.border
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 14
            anchors.rightMargin: 14
            spacing: 8
            Rectangle {
                width: 7
                height: 7
                radius: 3.5
                color: Theme.accent
                Layout.alignment: Qt.AlignVCenter
            }
            Label {
                text: AppController.runner.phase === MigrationRunner.Idle
                    ? qsTr("Idle")
                    : qsTr("Running…")
                color: Theme.text
                font.pixelSize: 12
                font.weight: Font.Medium
            }
            Label {
                text: qsTr("%1 log lines").arg(root.lineCount)
                color: Theme.textFaint
                font.pixelSize: 11
            }
            Item { Layout.fillWidth: true }
            Button {
                text: qsTr("Copy")
                flat: true
                enabled: root.lineCount > 0
                ToolTip.visible: hovered
                ToolTip.delay: 500
                ToolTip.timeout: 8000
                ToolTip.text: qsTr("Copy the full log buffer to the clipboard as plain text. Use this to attach the run output to a bug report or paste it into a review comment. Colour highlighting is dropped; line order is preserved.")
                onClicked: root.copyLog()
            }
            Button {
                text: qsTr("Clear")
                flat: true
                enabled: root.lineCount > 0
                ToolTip.visible: hovered
                ToolTip.delay: 500
                ToolTip.timeout: 8000
                ToolTip.text: qsTr("Discard every line in the log buffer. Only affects the on-screen buffer — nothing is written to or deleted from disk. Useful before starting a new migration run so the next log is self-contained.")
                onClicked: root.clearLog()
            }
        }
    }

    Connections {
        target: AppController.runner
        function onLogLine(line, level) {
            logText.append("<span style=\"color:" + root.colorFor(level) + ";\">"
                + root.escapeHtml(line) + "</span>");
            root.lineCount += 1;
        }
    }

    ScrollView {
        id: logScroll
        anchors.top: header.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        clip: true

        WheelScrollAmplifier { target: logScroll.contentItem }

        TextArea {
            id: logText
            readOnly: true
            selectByMouse: true
            persistentSelection: true
            wrapMode: TextEdit.NoWrap
            textFormat: TextEdit.RichText
            font.family: Theme.monoFont
            font.pixelSize: 12
            color: "#cbd5e1"
            background: null
            leftPadding: 14
            rightPadding: 14
            topPadding: 4
            bottomPadding: 4
            // Keep the view pinned to the tail as new lines arrive, but only
            // when the user is already at the bottom — otherwise they are
            // mid-copy or mid-scroll and auto-scrolling would yank the
            // viewport out from under them.
            onTextChanged: {
                if (cursorPosition >= length - 1)
                    cursorPosition = length;
            }
        }
    }
}
