import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Dialogs

ApplicationWindow {
    id: window
    width: 1100
    height: 760
    minimumWidth: 900
    minimumHeight: 560
    visible: true
    title: qsTr("dbtool-gui")

    Material.theme: Material.Light
    Material.primary: "#4F46E5"   // Indigo 600
    Material.accent: "#F97316"    // Orange 500
    Material.foreground: "#111827"
    Material.background: "#F9FAFB"

    readonly property color surfaceCard: "#FFFFFF"
    readonly property color surfaceSubtle: "#F3F4F6"
    readonly property color textMuted: "#6B7280"
    readonly property color borderSoft: "#E5E7EB"
    readonly property color successColor: "#16A34A"
    readonly property color successSoft: "#DCFCE7"
    readonly property color warningColor: "#B45309"
    readonly property color warningSoft: "#FEF3C7"
    readonly property color dangerColor: "#DC2626"

    readonly property string fontSans: "Inter, Segoe UI, sans-serif"
    readonly property string fontMono: "JetBrains Mono, SF Mono, Consolas, monospace"

    // YYYYMMDDHHMMSS  →  "2026-04-22 00:01"  (seconds appended only if non-zero)
    function formatTimestamp(ts) {
        if (ts === undefined || ts === null)
            return ""
        const s = String(ts).padStart(14, "0")
        const date = s.substring(0, 4) + "-" + s.substring(4, 6) + "-" + s.substring(6, 8)
        const time = s.substring(8, 10) + ":" + s.substring(10, 12)
        const sec  = s.substring(12, 14)
        return sec === "00" ? (date + " " + time) : (date + " " + time + ":" + sec)
    }

    FileDialog {
        id: pluginDialog
        title: qsTr("Select migration plugin")
        nameFilters: ["Shared libraries (*.so *.dll *.dylib)", "All files (*)"]
        onAccepted: backend.setPathFromUrl(selectedFile)
    }

    FileDialog {
        id: dbDialog
        title: qsTr("Select or create SQLite database")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "db"
        nameFilters: ["SQLite database (*.db *.sqlite *.sqlite3)", "All files (*)"]
        onAccepted: backend.setDatabaseFromUrl(selectedFile)
    }

    Dialog {
        id: sqlDialog
        modal: true
        anchors.centerIn: parent
        width: Math.min(window.width * 0.82, 960)
        height: Math.min(window.height * 0.78, 640)
        padding: 0
        standardButtons: Dialog.Close
        Material.background: "#FFFFFF"

        property string migrationTitle: ""
        property string migrationTs: ""
        property string sqlText: ""

        function openFor(index) {
            if (index < 0) return
            const idx = backend.migrations.index(index, 0)
            const ts = backend.migrations.data(idx, Qt.UserRole + 1)
            const t  = backend.migrations.data(idx, Qt.UserRole + 2)
            sqlDialog.migrationTs    = formatTimestamp(ts)
            sqlDialog.migrationTitle = t
            sqlDialog.sqlText        = backend.migrationSql(ts)
            sqlDialog.open()
        }

        header: Rectangle {
            implicitHeight: 72
            color: Material.primary
            radius: 4
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 24
                anchors.rightMargin: 24
                spacing: 14

                Rectangle {
                    implicitWidth: 36
                    implicitHeight: 36
                    radius: 8
                    color: Qt.rgba(1, 1, 1, 0.18)
                    Label {
                        anchors.centerIn: parent
                        text: "⟩_"
                        color: "white"
                        font.pixelSize: 16
                        font.family: fontMono
                        font.weight: Font.DemiBold
                    }
                }

                ColumnLayout {
                    spacing: 2
                    Label {
                        text: sqlDialog.migrationTitle
                        color: "white"
                        font.pixelSize: 15
                        font.weight: Font.DemiBold
                        font.family: fontSans
                        elide: Label.ElideRight
                    }
                    Label {
                        text: sqlDialog.migrationTs
                        color: Qt.rgba(1, 1, 1, 0.75)
                        font.pixelSize: 12
                        font.family: fontMono
                    }
                }

                Item { Layout.fillWidth: true }

                Button {
                    text: qsTr("Copy")
                    flat: true
                    Material.foreground: "white"
                    onClicked: {
                        sqlArea.selectAll()
                        sqlArea.copy()
                        sqlArea.deselect()
                    }
                }
            }
        }

        contentItem: Rectangle {
            color: "#0F172A"   // slate-900 – "editor" backdrop

            ScrollView {
                anchors.fill: parent
                anchors.margins: 4
                clip: true

                TextArea {
                    id: sqlArea
                    text: sqlDialog.sqlText
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextArea.NoWrap
                    font.family: fontMono
                    font.pixelSize: 13
                    color: "#E5E7EB"
                    selectionColor: "#4338CA"
                    background: null
                    leftPadding: 16
                    rightPadding: 16
                    topPadding: 14
                    bottomPadding: 14
                }
            }
        }
    }

    // ── Reusable pieces ──────────────────────────────────────────────────

    component Chip: Rectangle {
        property string label: ""
        property color bg: surfaceSubtle
        property color fg: textMuted
        implicitHeight: 24
        implicitWidth: chipText.implicitWidth + 20
        radius: height / 2
        color: bg
        Text {
            id: chipText
            anchors.centerIn: parent
            text: parent.label
            color: parent.fg
            font.pixelSize: 11
            font.weight: Font.Medium
            font.family: fontSans
            font.letterSpacing: 0.2
        }
    }

    component SectionTitle: RowLayout {
        property string title: ""
        property string subtitle: ""
        spacing: 12
        Label {
            text: title
            font.family: fontSans
            font.pixelSize: 15
            font.weight: Font.DemiBold
            color: Material.foreground
        }
        Label {
            text: subtitle
            visible: subtitle.length > 0
            font.family: fontSans
            font.pixelSize: 13
            color: textMuted
            Layout.fillWidth: true
        }
    }

    // ── Header ───────────────────────────────────────────────────────────

    header: ToolBar {
        Material.elevation: 2
        Material.background: "#FFFFFF"
        Material.foreground: Material.foreground
        height: 64

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 20
            anchors.rightMargin: 20
            spacing: 14

            Rectangle {
                implicitWidth: 36
                implicitHeight: 36
                radius: 10
                gradient: Gradient {
                    orientation: Gradient.Vertical
                    GradientStop { position: 0.0; color: "#6366F1" }
                    GradientStop { position: 1.0; color: "#4F46E5" }
                }
                Label {
                    anchors.centerIn: parent
                    text: "◆"
                    color: "white"
                    font.pixelSize: 18
                    font.weight: Font.Medium
                }
            }

            ColumnLayout {
                spacing: 0
                Label {
                    text: qsTr("Lightweight Migrations")
                    font.family: fontSans
                    font.pixelSize: 16
                    font.weight: Font.DemiBold
                    color: Material.foreground
                }
                Label {
                    text: qsTr("SQL migration runner")
                    font.family: fontSans
                    font.pixelSize: 11
                    color: textMuted
                }
            }

            Item { Layout.fillWidth: true }

            Chip {
                label: backend.pluginLoaded ? qsTr("connected") : qsTr("disconnected")
                bg: backend.pluginLoaded ? successSoft : "#FEE2E2"
                fg: backend.pluginLoaded ? successColor : dangerColor
            }

            Button {
                text: backend.pluginLoaded ? qsTr("Reload") : qsTr("Connect")
                highlighted: true
                Material.background: Material.primary
                onClicked: backend.loadPlugin()
                Layout.preferredHeight: 38
            }
        }
    }

    // ── Body ─────────────────────────────────────────────────────────────

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 16

        // Connection pane
        Pane {
            Layout.fillWidth: true
            Material.elevation: 1
            Material.background: surfaceCard
            padding: 20

            ColumnLayout {
                anchors.fill: parent
                spacing: 16

                SectionTitle {
                    title: qsTr("Connection")
                    subtitle: qsTr("SQLite file via ODBC — edit paths or browse")
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 16

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        TextField {
                            id: dbField
                            Layout.fillWidth: true
                            text: backend.databasePath
                            placeholderText: qsTr("Database path")
                            font.family: fontMono
                            font.pixelSize: 13
                            onEditingFinished: backend.databasePath = text
                            Material.accent: Material.primary
                        }
                        Button {
                            text: qsTr("Browse")
                            flat: true
                            onClicked: dbDialog.open()
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        TextField {
                            id: pluginField
                            Layout.fillWidth: true
                            text: backend.pluginPath
                            placeholderText: qsTr("Plugin path")
                            font.family: fontMono
                            font.pixelSize: 13
                            onEditingFinished: backend.pluginPath = text
                            Material.accent: Material.primary
                        }
                        Button {
                            text: qsTr("Browse")
                            flat: true
                            onClicked: pluginDialog.open()
                        }
                    }
                }

                RowLayout {
                    Layout.topMargin: 4
                    Label {
                        text: qsTr("Database")
                        font.pixelSize: 11
                        color: textMuted
                        font.weight: Font.Medium
                        font.capitalization: Font.AllUppercase
                        font.letterSpacing: 0.6
                        Layout.preferredWidth: dbField.width + 80
                    }
                    Label {
                        text: qsTr("Plugin")
                        font.pixelSize: 11
                        color: textMuted
                        font.weight: Font.Medium
                        font.capitalization: Font.AllUppercase
                        font.letterSpacing: 0.6
                        Layout.fillWidth: true
                    }
                }
            }
        }

        // Migrations pane
        Pane {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Material.elevation: 1
            Material.background: surfaceCard
            padding: 0

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                // Section header with actions
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 60
                    color: surfaceCard
                    radius: 4

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 20
                        anchors.rightMargin: 12
                        spacing: 12

                        ColumnLayout {
                            spacing: 0
                            Label {
                                text: qsTr("Migrations")
                                font.family: fontSans
                                font.pixelSize: 15
                                font.weight: Font.DemiBold
                                color: Material.foreground
                            }
                            Label {
                                text: backend.pluginLoaded
                                      ? qsTr("%1 of %2 applied").arg(backend.appliedCount).arg(backend.totalCount)
                                      : qsTr("Connect a plugin to see migrations")
                                font.family: fontSans
                                font.pixelSize: 12
                                color: textMuted
                            }
                        }

                        Item { Layout.fillWidth: true }

                        ToolButton {
                            text: "⟳"
                            font.pixelSize: 18
                            enabled: backend.pluginLoaded
                            onClicked: backend.refresh()
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("Refresh")
                        }

                        Button {
                            text: qsTr("View SQL")
                            flat: true
                            enabled: backend.pluginLoaded && list.currentIndex >= 0
                            onClicked: sqlDialog.openFor(list.currentIndex)
                        }

                        Button {
                            text: qsTr("Apply Up To Selected")
                            flat: true
                            enabled: backend.pluginLoaded && list.currentIndex >= 0
                            onClicked: {
                                const idx = backend.migrations.index(list.currentIndex, 0)
                                const ts = backend.migrations.data(idx, Qt.UserRole + 1)
                                backend.applyUpTo(ts)
                            }
                        }

                        Button {
                            text: qsTr("Apply All Pending")
                            highlighted: true
                            Material.background: Material.primary
                            enabled: backend.pluginLoaded
                            onClicked: backend.applyAll()
                        }
                    }

                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: 1
                        color: borderSoft
                    }
                }

                // Column headers
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 38
                    color: surfaceSubtle

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 20
                        anchors.rightMargin: 20
                        spacing: 16

                        Label {
                            text: qsTr("Timestamp")
                            color: textMuted
                            font.pixelSize: 10
                            font.weight: Font.DemiBold
                            font.capitalization: Font.AllUppercase
                            font.letterSpacing: 0.8
                            Layout.preferredWidth: 170
                        }
                        Label {
                            text: qsTr("Status")
                            color: textMuted
                            font.pixelSize: 10
                            font.weight: Font.DemiBold
                            font.capitalization: Font.AllUppercase
                            font.letterSpacing: 0.8
                            Layout.preferredWidth: 110
                        }
                        Label {
                            text: qsTr("Description")
                            color: textMuted
                            font.pixelSize: 10
                            font.weight: Font.DemiBold
                            font.capitalization: Font.AllUppercase
                            font.letterSpacing: 0.8
                            Layout.fillWidth: true
                        }
                    }
                }

                ListView {
                    id: list
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    currentIndex: -1
                    model: backend.migrations
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                    delegate: ItemDelegate {
                        id: row
                        required property int index
                        required property var timestamp
                        required property string title
                        required property bool applied

                        width: ListView.view.width
                        height: 54
                        highlighted: ListView.isCurrentItem
                        onClicked: list.currentIndex = index
                        onDoubleClicked: {
                            list.currentIndex = index
                            sqlDialog.openFor(index)
                        }
                        ToolTip.visible: hovered
                        ToolTip.delay: 900
                        ToolTip.text: qsTr("Double-click to view SQL")

                        background: Rectangle {
                            color: row.ListView.isCurrentItem
                                   ? "#EEF2FF"
                                   : (row.hovered ? surfaceSubtle : surfaceCard)
                            Rectangle {
                                visible: row.ListView.isCurrentItem
                                width: 3
                                anchors {
                                    left: parent.left
                                    top: parent.top
                                    bottom: parent.bottom
                                }
                                color: Material.primary
                            }
                            Rectangle {
                                anchors.bottom: parent.bottom
                                width: parent.width
                                height: 1
                                color: borderSoft
                                visible: row.index < list.count - 1
                            }
                        }

                        contentItem: RowLayout {
                            spacing: 16

                            Label {
                                text: formatTimestamp(row.timestamp)
                                color: Material.foreground
                                font.pixelSize: 13
                                font.family: fontMono
                                Layout.leftMargin: 8
                                Layout.preferredWidth: 170
                            }

                            Chip {
                                Layout.preferredWidth: 110
                                label: row.applied ? "✓  applied" : "•  pending"
                                bg: row.applied ? successSoft : warningSoft
                                fg: row.applied ? successColor : warningColor
                            }

                            Label {
                                text: row.title
                                color: Material.foreground
                                font.pixelSize: 14
                                font.family: fontSans
                                Layout.fillWidth: true
                                elide: Label.ElideRight
                            }
                        }
                    }

                    // Empty state
                    ColumnLayout {
                        anchors.centerIn: parent
                        visible: list.count === 0
                        spacing: 8
                        Label {
                            Layout.alignment: Qt.AlignHCenter
                            text: backend.pluginLoaded ? "✓" : "◌"
                            color: textMuted
                            font.pixelSize: 36
                        }
                        Label {
                            Layout.alignment: Qt.AlignHCenter
                            text: backend.pluginLoaded
                                  ? qsTr("No migrations registered.")
                                  : qsTr("Click Connect to load a plugin.")
                            color: textMuted
                            font.pixelSize: 13
                            font.family: fontSans
                        }
                    }
                }
            }
        }
    }

    // ── Status bar ───────────────────────────────────────────────────────

    footer: Pane {
        Material.elevation: 2
        Material.background: "#FFFFFF"
        padding: 0
        implicitHeight: 38

        Rectangle {
            anchors.top: parent.top
            width: parent.width
            height: 1
            color: borderSoft
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            spacing: 14

            // Connection indicator
            RowLayout {
                spacing: 6
                Rectangle {
                    implicitWidth: 8
                    implicitHeight: 8
                    radius: 4
                    color: backend.pluginLoaded ? successColor : "#9CA3AF"

                    SequentialAnimation on opacity {
                        running: backend.pluginLoaded
                        loops: Animation.Infinite
                        NumberAnimation { from: 1.0; to: 0.4; duration: 1200; easing.type: Easing.InOutQuad }
                        NumberAnimation { from: 0.4; to: 1.0; duration: 1200; easing.type: Easing.InOutQuad }
                    }
                }
                Label {
                    text: backend.pluginLoaded ? qsTr("Connected") : qsTr("Idle")
                    color: Material.foreground
                    font.pixelSize: 12
                    font.weight: Font.Medium
                    font.family: fontSans
                }
            }

            Rectangle { implicitWidth: 1; implicitHeight: 20; color: borderSoft }

            // Database name
            RowLayout {
                spacing: 6
                visible: backend.databasePath.length > 0
                Label {
                    text: "DB"
                    color: textMuted
                    font.pixelSize: 10
                    font.weight: Font.DemiBold
                    font.family: fontSans
                    font.capitalization: Font.AllUppercase
                    font.letterSpacing: 0.6
                }
                Label {
                    text: backend.databasePath.split("/").slice(-1)[0] || backend.databasePath
                    color: Material.foreground
                    font.pixelSize: 12
                    font.family: fontMono
                    elide: Label.ElideMiddle
                    Layout.maximumWidth: 200
                }
            }

            Rectangle {
                implicitWidth: 1; implicitHeight: 20; color: borderSoft
                visible: backend.pluginLoaded
            }

            // Plugin name
            RowLayout {
                spacing: 6
                visible: backend.pluginLoaded
                Label {
                    text: "Plugin"
                    color: textMuted
                    font.pixelSize: 10
                    font.weight: Font.DemiBold
                    font.family: fontSans
                    font.capitalization: Font.AllUppercase
                    font.letterSpacing: 0.6
                }
                Label {
                    text: backend.pluginPath.split("/").slice(-1)[0] || ""
                    color: Material.foreground
                    font.pixelSize: 12
                    font.family: fontMono
                    elide: Label.ElideMiddle
                    Layout.maximumWidth: 220
                }
            }

            Item { Layout.fillWidth: true }

            // Status message
            Label {
                text: backend.status
                color: textMuted
                font.pixelSize: 12
                font.family: fontSans
                elide: Label.ElideRight
                Layout.maximumWidth: 260
            }

            Rectangle {
                implicitWidth: 1; implicitHeight: 20; color: borderSoft
                visible: backend.pluginLoaded
            }

            // Progress
            RowLayout {
                spacing: 8
                visible: backend.pluginLoaded

                ProgressBar {
                    implicitWidth: 120
                    from: 0
                    to: Math.max(1, backend.totalCount)
                    value: backend.appliedCount
                    Material.accent: Material.primary
                }
                Label {
                    text: qsTr("%1 / %2").arg(backend.appliedCount).arg(backend.totalCount)
                    color: Material.foreground
                    font.pixelSize: 12
                    font.family: fontMono
                    font.weight: Font.Medium
                }
            }
        }
    }
}
