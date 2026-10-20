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
// The body is a ListView over `AppController.logLines`, which keeps the
// newest lines only, so a run of any length stays cheap: only the visible
// lines have delegates. The whole log is copied through the model.
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

    /// Number of lines the log holds.
    readonly property int lineCount: AppController.logLines.count

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

    function clearLog() {
        AppController.logLines.clear();
    }

    function copyLog() {
        AppController.logLines.copyToClipboard();
    }

    // Lines logged before this panel existed are in the model already; the
    // sink also releases the buffered `logLine` signals for other listeners.
    Component.onCompleted: AppController.attachLogSink()

    ListView {
        id: logView
        objectName: "logView"
        anchors.fill: parent
        clip: true
        topMargin: 8
        bottomMargin: 8
        boundsBehavior: Flickable.StopAtBounds
        reuseItems: true
        model: AppController.logLines

        /// Follows new lines while the user is at the tail; scrolling up stops that.
        property bool pinnedToTail: true
        property bool _following: false

        // Lines added below leave contentY alone, so only the user's scrolling moves it.
        onContentYChanged: {
            if (!_following)
                pinnedToTail = contentY + height >= originY + contentHeight - 24;
        }
        onCountChanged: {
            if (!pinnedToTail)
                return;
            _following = true;
            positionViewAtEnd();
            _following = false;
        }

        WheelScrollAmplifier { target: logView }
        ScrollBar.vertical: ScrollBar {}

        // Lines the log no longer holds, so the top of the list does not pass for the start.
        header: Label {
            objectName: "logDroppedLines"
            x: 14
            width: logView.width - 28
            height: visible ? implicitHeight + 6 : 0
            visible: AppController.logLines.droppedCount > 0
            text: qsTr("… %1 earlier lines dropped").arg(AppController.logLines.droppedCount)
            textFormat: Text.PlainText
            color: Theme.clrCodeMuted
            font: Theme.monoFont(Theme.sizeMono)
        }

        // Long lines wrap, so nothing is cut off at the right edge.
        delegate: Item {
            id: lineRow
            required property string time
            required property int level
            required property string text
            readonly property var levelStyle: root.levelFor(level)

            width: ListView.view.width
            // The design's ~1.6 line rhythm: the spacing of one line goes below the (wrapped) message.
            height: message.implicitHeight + Math.ceil(timeLabel.implicitHeight * 0.6)

            Label {
                id: timeLabel
                x: 14
                text: lineRow.time + "  "
                textFormat: Text.PlainText
                color: Theme.clrCodeMuted
                font: Theme.monoFont(Theme.sizeMono)
            }
            Label {
                id: levelLabel
                anchors.left: timeLabel.right
                text: lineRow.levelStyle[0] + "  "
                textFormat: Text.PlainText
                color: lineRow.levelStyle[1]
                font: Theme.monoFont(Theme.sizeMono)
            }
            Label {
                id: message
                anchors.left: levelLabel.right
                anchors.right: parent.right
                anchors.rightMargin: 14
                text: lineRow.text
                textFormat: Text.PlainText
                wrapMode: Text.WrapAnywhere
                color: lineRow.levelStyle[2]
                font: Theme.monoFont(Theme.sizeMono)
            }
        }
    }
}
