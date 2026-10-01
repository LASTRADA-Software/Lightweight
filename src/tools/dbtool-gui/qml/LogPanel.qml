// SPDX-License-Identifier: Apache-2.0
//
// Log body of the bottom panel — one of the two dark surfaces in the
// Lastrada design (the SQL editor is the other). It uses the rail's warm
// black (`clrSidebarBg`) rather than slate, so the log reads as a terminal
// and stays distinct from the light panels around it in every palette.
//
// Each line is rendered as `HH:MM:SS  LEVEL  message` (`dt-code`): the
// receive time in the muted code colour, a fixed-width level tag in the
// level's colour, and the message. Warnings and errors colour the message
// too, so they can be spotted while scrolling.
//
// The body is a read-only `TextArea` (not a ListView) so the user can
// rubber-band across lines and copy chunks of the log to the clipboard.
// Per-level colour is preserved via rich-text `<span>` tags — we build the
// HTML incrementally instead of rebinding `text` on every new line, which
// keeps long-running migrations from re-layouting the whole document on
// each append.
//
// `LogPanel` is body-only: the tab strip, copy / clear tools and the
// collapse toggle live in `BottomPanel.qml`, which calls `copyLog()` /
// `clearLog()` and reads `lineCount`.

import QtQuick
import QtQuick.Controls
import Lightweight.Migrations

Rectangle {
    id: root
    color: Theme.clrSidebarBg
    // Follow the host panel's rounded bottom corners so the dark body does
    // not poke out past its border.
    bottomLeftRadius: Theme.r3 - 1
    bottomRightRadius: Theme.r3 - 1

    // Accumulating HTML document. Kept out of `TextArea.text` directly so
    // the viewport does not re-highlight existing text while the migration
    // is still streaming — TextArea's `append()` is the targeted-insert
    // API that preserves cursor/selection state.
    property int lineCount: 0

    // `level` is `DbtoolGui::LogLevel` from C++ — the QML side receives the
    // underlying integer (0=Info, 1=Warning, 2=Error). Keep this table in
    // sync with `LogLevel.hpp` if the enum is ever extended. Each entry is
    // [tag, tag colour, message colour]; tags are padded to one width so the
    // messages line up.
    readonly property var _levels: [
        ["INFO ", Theme.clrCodeOk,   Theme.clrCodeText],
        ["WARN ", Theme.clrCodeWarn, Theme.clrCodeWarn],
        ["ERROR", Theme.clrCodeErr,  Theme.clrCodeErr],
    ]

    function levelFor(level) {
        return _levels[level] || _levels[0];
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

    // Drain any startup banner / plugin-discovery / connect lines that
    // `AppController` buffered before this panel existed. Idempotent on the
    // C++ side, so a panel re-instantiation does not replay the banner.
    Component.onCompleted: AppController.attachLogSink()

    Connections {
        target: AppController
        function onLogLine(line, level) {
            const lv = root.levelFor(level);
            const time = Qt.formatTime(new Date(), "hh:mm:ss");
            // Each append is its own paragraph; the line-height opens the
            // log up to the design's ~1.6 rhythm (Qt's rich-text subset
            // honours it as a proportional block line height).
            logText.append("<p style=\"margin:0; line-height:160%;\">"
                + "<span style=\"color:" + Theme.clrCodeMuted + ";\">" + time + "</span>"
                + "&nbsp;&nbsp;<span style=\"color:" + lv[1] + ";\">" + root.escapeHtml(lv[0]) + "</span>"
                + "&nbsp;&nbsp;<span style=\"color:" + lv[2] + ";\">" + root.escapeHtml(line) + "</span></p>");
            root.lineCount += 1;
        }
    }

    ScrollView {
        id: logScroll
        anchors.top: parent.top
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
            font: Theme.monoFont(Theme.sizeMono)
            color: Theme.clrCodeText
            selectionColor: Qt.rgba(Theme.clrPrimary.r, Theme.clrPrimary.g, Theme.clrPrimary.b, 0.55)
            selectedTextColor: "#ffffff"
            background: null
            leftPadding: 14
            rightPadding: 14
            topPadding: 8
            bottomPadding: 8
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
