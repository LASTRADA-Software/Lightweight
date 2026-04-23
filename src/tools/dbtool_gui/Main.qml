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
        onAccepted: { backend.setPathFromUrl(selectedFile); backend.loadMigrationPlugin() }
    }

    FileDialog {
        id: dbDialog
        title: qsTr("Select or create SQLite database")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "db"
        nameFilters: ["SQLite database (*.db *.sqlite *.sqlite3)", "All files (*)"]
        onAccepted: backend.setDatabaseFromUrl(selectedFile)
    }

    property url _pendingBackupFile
    property url _pendingRestoreFile

    FileDialog {
        id: backupTargetDialog
        title: qsTr("Backup target file")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "zip"
        nameFilters: ["Backup archives (*.zip)", "All files (*)"]
        onAccepted: { window._pendingBackupFile = selectedFile; backupPathField.text = selectedFile.toString().replace("file://", "") }
    }

    FileDialog {
        id: restoreSourceDialog
        title: qsTr("Backup archive to restore")
        fileMode: FileDialog.OpenFile
        nameFilters: ["Backup archives (*.zip)", "All files (*)"]
        onAccepted: { window._pendingRestoreFile = selectedFile; restorePathField.text = selectedFile.toString().replace("file://", "") }
    }

    // ── Credentials dialog ───────────────────────────────────────────────
    Dialog {
        id: credentialsDialog
        modal: true
        anchors.centerIn: parent
        width: 420
        padding: 24
        closePolicy: Popup.NoAutoClose
        standardButtons: Dialog.Ok | Dialog.Cancel
        title: qsTr("Database Credentials")

        property string errorMessage: ""

        onAccepted: backend.connectWithCredentials(credUserField.text, credPassField.text)
        onRejected: {}

        ColumnLayout {
            width: parent.width
            spacing: 16

            Label {
                visible: credentialsDialog.errorMessage.length > 0
                text: credentialsDialog.errorMessage
                color: dangerColor
                font.pixelSize: 12
                font.family: fontSans
                wrapMode: Label.WordWrap
                Layout.fillWidth: true
            }

            TextField {
                id: credUserField
                Layout.fillWidth: true
                placeholderText: qsTr("Username")
                text: backend.username
                font.family: fontMono
                font.pixelSize: 13
                Material.accent: Material.primary
                Keys.onReturnPressed: credPassField.forceActiveFocus()
            }

            TextField {
                id: credPassField
                Layout.fillWidth: true
                placeholderText: qsTr("Password")
                echoMode: TextInput.Password
                font.family: fontMono
                font.pixelSize: 13
                Material.accent: Material.primary
                Keys.onReturnPressed: credentialsDialog.accept()
            }
        }
    }

    Connections {
        target: backend
        function onCredentialsNeeded(errorMessage) {
            credentialsDialog.errorMessage = errorMessage
            credUserField.text = backend.username
            credPassField.text = ""
            credentialsDialog.open()
        }
        function onMigrationSqlReady(sql) {
            sqlDialog.sqlText = sql
        }
        function onQueryResultReady(result) {
            queryResults.text = result
        }
        function onSchemaReady(schema) {
            schemaView.schemaData = schema
            schemaView.selectedTable = schema.length > 0 ? 0 : -1
        }
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
            sqlDialog.sqlText        = qsTr("Loading…")
            sqlDialog.open()
            backend.migrationSql(ts)
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
                label: backend.dbConnected ? qsTr("connected") : qsTr("disconnected")
                bg: backend.dbConnected ? successSoft : "#FEE2E2"
                fg: backend.dbConnected ? successColor : dangerColor
            }

            Button {
                text: backend.dbConnected ? qsTr("Reload") : qsTr("Connect")
                highlighted: true
                Material.background: Material.primary
                onClicked: backend.loadPlugin()
                Layout.preferredHeight: 38
            }
        }
    }

    // ── Body ─────────────────────────────────────────────────────────────

    property int currentView: 0

    component NavItem: Rectangle {
        id: navRoot
        property string icon: ""
        property string label: ""
        property int viewIndex: 0
        Layout.fillWidth: true
        implicitHeight: 40
        radius: 8
        color: window.currentView === viewIndex
               ? Qt.rgba(79/255, 70/255, 229/255, 0.10)
               : (navMouse.containsMouse ? surfaceSubtle : "transparent")

        Behavior on color { ColorAnimation { duration: 120 } }

        Rectangle {
            visible: window.currentView === navRoot.viewIndex
            width: 3
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.leftMargin: 0
            height: parent.height - 14
            radius: 2
            color: Material.primary
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 14
            anchors.rightMargin: 12
            spacing: 10

            Label {
                text: navRoot.icon
                font.pixelSize: 15
                color: window.currentView === navRoot.viewIndex ? Material.primary : textMuted
                Layout.preferredWidth: 20
            }
            Label {
                text: navRoot.label
                font.family: fontSans
                font.pixelSize: 13
                font.weight: window.currentView === navRoot.viewIndex ? Font.DemiBold : Font.Medium
                color: window.currentView === navRoot.viewIndex ? Material.foreground : textMuted
                Layout.fillWidth: true
            }
        }

        MouseArea {
            id: navMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: window.currentView = navRoot.viewIndex
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // ── Sidebar ──────────────────────────────────────────────────────
        Pane {
            Layout.preferredWidth: 220
            Layout.fillHeight: true
            padding: 16
            Material.background: "#FFFFFF"
            Material.elevation: 0

            ColumnLayout {
                anchors.fill: parent
                spacing: 6

                Label {
                    text: qsTr("Workspace")
                    font.pixelSize: 10
                    font.weight: Font.DemiBold
                    font.capitalization: Font.AllUppercase
                    font.letterSpacing: 0.8
                    color: textMuted
                    Layout.leftMargin: 8
                    Layout.bottomMargin: 4
                }

                NavItem { icon: "▤"; label: qsTr("Migrations"); viewIndex: 0 }
                NavItem { icon: "⌘"; label: qsTr("Schema"); viewIndex: 2 }
                NavItem { icon: "⟩_"; label: qsTr("Execute Query"); viewIndex: 1 }
                NavItem { icon: "⬇"; label: qsTr("Backup / Restore"); viewIndex: 3 }
                NavItem { icon: "≡"; label: qsTr("Log"); viewIndex: 4 }

                Item { Layout.fillHeight: true }

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 1
                    color: borderSoft
                }
                Label {
                    text: qsTr("dbtool-gui · v1.0")
                    color: textMuted
                    font.pixelSize: 11
                    font.family: fontMono
                    Layout.leftMargin: 8
                    Layout.topMargin: 6
                }
            }

            Rectangle {
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: 1
                color: borderSoft
            }
        }

        // ── Main content area ────────────────────────────────────────────
        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: window.currentView

    ColumnLayout {
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.margins: 20
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

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 0

                    SectionTitle {
                        title: qsTr("Connection")
                        subtitle: backend.useDsn ? qsTr("System ODBC DSN") : qsTr("SQLite file via ODBC — edit path or browse")
                        Layout.fillWidth: true
                    }

                    // DSN / File toggle (Windows only)
                    RowLayout {
                        spacing: 4
                        Label {
                            text: qsTr("SQLite file")
                            font.pixelSize: 12
                            color: backend.useDsn ? textMuted : Material.foreground
                            font.family: fontSans
                        }
                        Switch {
                            id: dsnToggle
                            checked: backend.useDsn
                            onCheckedChanged: {
                                backend.useDsn = checked
                                if (checked)
                                    dsnInputArea.loadDsns()
                            }
                        }
                        Label {
                            text: qsTr("System DSN")
                            font.pixelSize: 12
                            color: backend.useDsn ? Material.foreground : textMuted
                            font.family: fontSans
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 16

                    // Database input: file path or DSN picker
                    RowLayout {
                        Layout.fillWidth: !backend.useDsn
                        Layout.preferredWidth: backend.useDsn ? 0 : -1
                        spacing: 8
                        visible: !backend.useDsn

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

                    ComboBox {
                        id: dsnCombo
                        Layout.fillWidth: backend.useDsn
                        Layout.preferredWidth: backend.useDsn ? -1 : 0
                        visible: backend.useDsn
                        editable: true
                        font.family: fontMono
                        font.pixelSize: 13

                        model: backend.availableOdbcDsns()

                        Component.onCompleted: {
                            var dsns = backend.availableOdbcDsns()
                            model = dsns
                            var idx = dsns.indexOf(backend.odbcDsn)
                            if (idx >= 0) currentIndex = idx
                            else { currentIndex = -1; editText = backend.odbcDsn }
                        }

                        onActivated: backend.odbcDsn = currentText
                        onEditTextChanged: backend.odbcDsn = editText

                        Connections {
                            target: backend
                            function onUseDsnChanged() {
                                if (backend.useDsn) {
                                    var dsns = backend.availableOdbcDsns()
                                    dsnCombo.model = dsns
                                    var idx = dsns.indexOf(backend.odbcDsn)
                                    if (idx >= 0) dsnCombo.currentIndex = idx
                                    else { dsnCombo.currentIndex = -1; dsnCombo.editText = backend.odbcDsn }
                                }
                            }
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
                            onEditingFinished: { backend.pluginPath = text; backend.loadMigrationPlugin() }
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
                        text: backend.useDsn ? qsTr("DSN") : qsTr("Database")
                        font.pixelSize: 11
                        color: textMuted
                        font.weight: Font.Medium
                        font.capitalization: Font.AllUppercase
                        font.letterSpacing: 0.6
                        Layout.fillWidth: true
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
    }   // end Migrations view (StackLayout child 0)

    // ── Execute Query view (StackLayout child 1) ────────────────────
    ColumnLayout {
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.margins: 20
        spacing: 16

        // Editor pane
        Pane {
            Layout.fillWidth: true
            Material.elevation: 1
            Material.background: surfaceCard
            padding: 0
            implicitHeight: 260

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 52
                    color: surfaceCard
                    radius: 4

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 20
                        anchors.rightMargin: 12
                        spacing: 10

                        ColumnLayout {
                            spacing: 0
                            Label {
                                text: qsTr("SQL Editor")
                                font.family: fontSans
                                font.pixelSize: 15
                                font.weight: Font.DemiBold
                                color: Material.foreground
                            }
                            Label {
                                text: qsTr("Runs against the configured database")
                                font.family: fontSans
                                font.pixelSize: 12
                                color: textMuted
                            }
                        }

                        Item { Layout.fillWidth: true }

                        Button {
                            text: qsTr("Clear")
                            flat: true
                            onClicked: { queryInput.text = ""; queryResults.text = "" }
                        }

                        Button {
                            text: qsTr("▶  Execute")
                            highlighted: true
                            Material.background: Material.primary
                            onClicked: { queryResults.text = qsTr("Running…"); backend.executeQuery(queryInput.text) }
                        }
                    }

                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: 1
                        color: borderSoft
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: "#0F172A"

                    ScrollView {
                        anchors.fill: parent
                        clip: true

                        TextArea {
                            id: queryInput
                            placeholderText: qsTr("-- Enter SQL here, e.g.\n--   SELECT * FROM Artist LIMIT 10;\n\n")
                            placeholderTextColor: "#64748B"
                            color: "#E5E7EB"
                            font.family: fontMono
                            font.pixelSize: 13
                            wrapMode: TextArea.WordWrap
                            selectByMouse: true
                            selectionColor: "#4338CA"
                            background: null
                            leftPadding: 16
                            rightPadding: 16
                            topPadding: 14
                            bottomPadding: 14
                            Keys.onPressed: function(event) {
                                if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_Return) {
                                    queryResults.text = qsTr("Running…")
                                    backend.executeQuery(queryInput.text)
                                    event.accepted = true
                                }
                            }
                        }
                    }
                }
            }
        }

        // Results pane
        Pane {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Material.elevation: 1
            Material.background: surfaceCard
            padding: 0

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 44
                    color: surfaceSubtle

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 20
                        anchors.rightMargin: 16
                        spacing: 10

                        Label {
                            text: qsTr("Results")
                            font.family: fontSans
                            font.pixelSize: 12
                            font.weight: Font.DemiBold
                            font.capitalization: Font.AllUppercase
                            font.letterSpacing: 0.8
                            color: textMuted
                        }

                        Item { Layout.fillWidth: true }

                        Label {
                            text: qsTr("Ctrl+Enter to run")
                            font.family: fontSans
                            font.pixelSize: 11
                            color: textMuted
                        }
                    }

                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: 1
                        color: borderSoft
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: surfaceCard

                    ScrollView {
                        anchors.fill: parent
                        clip: true

                        TextArea {
                            id: queryResults
                            readOnly: true
                            placeholderText: qsTr("Results will appear here.")
                            color: Material.foreground
                            font.family: fontMono
                            font.pixelSize: 13
                            wrapMode: TextArea.NoWrap
                            selectByMouse: true
                            background: null
                            leftPadding: 16
                            rightPadding: 16
                            topPadding: 14
                            bottomPadding: 14
                        }
                    }
                }
            }
        }
    }   // end Execute Query view (StackLayout child 1)

    // ── Schema view (StackLayout child 2) ─────────────────────────────
    Item {
        id: schemaView

        property var schemaData: []
        property int selectedTable: -1
        property var currentTable: selectedTable >= 0 && selectedTable < schemaData.length
                                   ? schemaData[selectedTable] : null

        function reload() {
            backend.loadSchema()
        }

        Connections {
            target: backend
            function onDbConnectedChanged() { if (backend.dbConnected) schemaView.reload() }
            function onPluginLoadedChanged() { if (backend.pluginLoaded) schemaView.reload() }
        }

        RowLayout {
            anchors.fill: parent
            anchors.margins: 20
            spacing: 16

            // ── Tables list ──────────────────────────────────────────
            Pane {
                Layout.preferredWidth: 260
                Layout.fillHeight: true
                Material.elevation: 1
                Material.background: surfaceCard
                padding: 0

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0

                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: 52
                        color: surfaceCard

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 16
                            anchors.rightMargin: 8
                            spacing: 8

                            ColumnLayout {
                                spacing: 0
                                Label {
                                    text: qsTr("Tables")
                                    font.family: fontSans
                                    font.pixelSize: 13
                                    font.weight: Font.DemiBold
                                    color: Material.foreground
                                }
                                Label {
                                    text: qsTr("%1 table(s)").arg(schemaView.schemaData.length)
                                    font.family: fontSans
                                    font.pixelSize: 11
                                    color: textMuted
                                }
                            }
                            Item { Layout.fillWidth: true }
                            ToolButton {
                                text: "⟳"
                                font.pixelSize: 16
                                ToolTip.visible: hovered
                                ToolTip.text: qsTr("Reload schema")
                                onClicked: schemaView.reload()
                            }
                        }

                        Rectangle {
                            anchors.bottom: parent.bottom
                            width: parent.width
                            height: 1
                            color: borderSoft
                        }
                    }

                    ListView {
                        id: tablesList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: schemaView.schemaData
                        currentIndex: schemaView.selectedTable
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                        delegate: ItemDelegate {
                            id: tblItem
                            required property int index
                            required property var modelData
                            width: ListView.view.width
                            height: 44
                            highlighted: schemaView.selectedTable === index
                            onClicked: schemaView.selectedTable = index

                            background: Rectangle {
                                color: schemaView.selectedTable === tblItem.index
                                       ? "#EEF2FF"
                                       : (tblItem.hovered ? surfaceSubtle : "transparent")
                                Rectangle {
                                    visible: schemaView.selectedTable === tblItem.index
                                    width: 3
                                    anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
                                    color: Material.primary
                                }
                            }

                            contentItem: RowLayout {
                                spacing: 10
                                Label {
                                    text: "▦"
                                    color: textMuted
                                    font.pixelSize: 13
                                    Layout.leftMargin: 8
                                }
                                Label {
                                    text: tblItem.modelData.name
                                    color: Material.foreground
                                    font.pixelSize: 13
                                    font.family: fontMono
                                    Layout.fillWidth: true
                                    elide: Label.ElideRight
                                }
                                Chip {
                                    label: tblItem.modelData.columns.length + " cols"
                                    bg: surfaceSubtle
                                    fg: textMuted
                                }
                            }
                        }

                        ColumnLayout {
                            anchors.centerIn: parent
                            visible: tablesList.count === 0
                            spacing: 6
                            Label {
                                Layout.alignment: Qt.AlignHCenter
                                text: "◌"
                                color: textMuted
                                font.pixelSize: 28
                            }
                            Label {
                                Layout.alignment: Qt.AlignHCenter
                                text: backend.dbConnected
                                      ? qsTr("No tables found.")
                                      : qsTr("Connect to browse schema.")
                                color: textMuted
                                font.pixelSize: 12
                                font.family: fontSans
                            }
                        }
                    }
                }
            }

            // ── Columns detail ───────────────────────────────────────
            Pane {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Material.elevation: 1
                Material.background: surfaceCard
                padding: 0

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0

                    // Detail header
                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: 64
                        color: surfaceCard
                        visible: schemaView.currentTable !== null

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 24
                            anchors.rightMargin: 16
                            spacing: 12

                            Rectangle {
                                implicitWidth: 40; implicitHeight: 40
                                radius: 10
                                color: "#EEF2FF"
                                Label {
                                    anchors.centerIn: parent
                                    text: "▦"
                                    font.pixelSize: 18
                                    color: Material.primary
                                }
                            }
                            ColumnLayout {
                                spacing: 0
                                Label {
                                    text: schemaView.currentTable ? schemaView.currentTable.name : ""
                                    font.family: fontMono
                                    font.pixelSize: 16
                                    font.weight: Font.DemiBold
                                    color: Material.foreground
                                }
                                Label {
                                    text: schemaView.currentTable
                                          ? qsTr("%1 column(s) · %2 index(es)")
                                              .arg(schemaView.currentTable.columns.length)
                                              .arg(schemaView.currentTable.indexCount)
                                          : ""
                                    font.family: fontSans
                                    font.pixelSize: 12
                                    color: textMuted
                                }
                            }
                            Item { Layout.fillWidth: true }

                            Button {
                                text: qsTr("SELECT *")
                                flat: true
                                onClicked: {
                                    queryInput.text = "SELECT * FROM \"" + schemaView.currentTable.name + "\" LIMIT 100;"
                                    queryResults.text = qsTr("Running…")
                                    backend.executeQuery(queryInput.text)
                                    window.currentView = 1
                                }
                            }
                        }

                        Rectangle {
                            anchors.bottom: parent.bottom
                            width: parent.width
                            height: 1
                            color: borderSoft
                        }
                    }

                    // Column header row
                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: 38
                        color: surfaceSubtle
                        visible: schemaView.currentTable !== null

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 24
                            anchors.rightMargin: 24
                            spacing: 16

                            Label {
                                text: qsTr("Column")
                                color: textMuted
                                font.pixelSize: 10
                                font.weight: Font.DemiBold
                                font.capitalization: Font.AllUppercase
                                font.letterSpacing: 0.8
                                Layout.preferredWidth: 200
                            }
                            Label {
                                text: qsTr("Type")
                                color: textMuted
                                font.pixelSize: 10
                                font.weight: Font.DemiBold
                                font.capitalization: Font.AllUppercase
                                font.letterSpacing: 0.8
                                Layout.preferredWidth: 170
                            }
                            Label {
                                text: qsTr("Null")
                                color: textMuted
                                font.pixelSize: 10
                                font.weight: Font.DemiBold
                                font.capitalization: Font.AllUppercase
                                font.letterSpacing: 0.8
                                Layout.preferredWidth: 80
                            }
                            Label {
                                text: qsTr("Attributes")
                                color: textMuted
                                font.pixelSize: 10
                                font.weight: Font.DemiBold
                                font.capitalization: Font.AllUppercase
                                font.letterSpacing: 0.8
                                Layout.fillWidth: true
                            }
                        }
                    }

                    // Column rows
                    ListView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                        visible: schemaView.currentTable !== null
                        model: schemaView.currentTable ? schemaView.currentTable.columns : []

                        delegate: Rectangle {
                            id: colRow
                            required property int index
                            required property var modelData
                            width: ListView.view.width
                            height: 44
                            color: index % 2 === 0 ? surfaceCard : surfaceSubtle

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 24
                                anchors.rightMargin: 24
                                spacing: 16

                                RowLayout {
                                    spacing: 6
                                    Layout.preferredWidth: 200
                                    Label {
                                        text: colRow.modelData.primaryKey
                                              ? "🔑"
                                              : (colRow.modelData.foreignKey ? "🔗" : "  ")
                                        font.pixelSize: 12
                                    }
                                    Label {
                                        text: colRow.modelData.name
                                        color: Material.foreground
                                        font.pixelSize: 13
                                        font.family: fontMono
                                        font.weight: colRow.modelData.primaryKey ? Font.DemiBold : Font.Normal
                                        Layout.fillWidth: true
                                        elide: Label.ElideRight
                                    }
                                }

                                Label {
                                    text: colRow.modelData.type
                                    color: textMuted
                                    font.pixelSize: 12
                                    font.family: fontMono
                                    Layout.preferredWidth: 170
                                    elide: Label.ElideRight
                                }

                                Label {
                                    text: colRow.modelData.nullable ? "NULL" : "NOT NULL"
                                    color: colRow.modelData.nullable ? textMuted : warningColor
                                    font.pixelSize: 11
                                    font.family: fontMono
                                    font.weight: Font.Medium
                                    Layout.preferredWidth: 80
                                }

                                RowLayout {
                                    spacing: 6
                                    Layout.fillWidth: true
                                    Chip {
                                        visible: colRow.modelData.primaryKey
                                        label: "PK"
                                        bg: "#DBEAFE"
                                        fg: "#1D4ED8"
                                    }
                                    Chip {
                                        visible: colRow.modelData.autoIncrement
                                        label: "AUTO"
                                        bg: "#FCE7F3"
                                        fg: "#BE185D"
                                    }
                                    Chip {
                                        visible: colRow.modelData.unique && !colRow.modelData.primaryKey
                                        label: "UNIQUE"
                                        bg: "#F3E8FF"
                                        fg: "#6B21A8"
                                    }
                                    Chip {
                                        visible: colRow.modelData.foreignKey
                                        label: colRow.modelData.fkTarget ? "→ " + colRow.modelData.fkTarget : "FK"
                                        bg: "#FEF3C7"
                                        fg: "#92400E"
                                    }
                                    Item { Layout.fillWidth: true }
                                }
                            }

                            Rectangle {
                                anchors.bottom: parent.bottom
                                width: parent.width
                                height: 1
                                color: borderSoft
                            }
                        }
                    }

                    // Empty detail state
                    Item {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: schemaView.currentTable === null

                        ColumnLayout {
                            anchors.centerIn: parent
                            spacing: 6
                            Label {
                                Layout.alignment: Qt.AlignHCenter
                                text: "⌘"
                                color: textMuted
                                font.pixelSize: 40
                            }
                            Label {
                                Layout.alignment: Qt.AlignHCenter
                                text: backend.dbConnected
                                      ? qsTr("Select a table to see its columns.")
                                      : qsTr("Connect to view schema.")
                                color: textMuted
                                font.pixelSize: 13
                                font.family: fontSans
                            }
                        }
                    }
                }
            }
        }
    }   // end Schema view (StackLayout child 2)

    // ── Backup / Restore view (StackLayout child 3) ───────────────────
    ColumnLayout {
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.margins: 20
        spacing: 16

        // Backup card
        Pane {
            Layout.fillWidth: true
            Material.elevation: 1
            Material.background: surfaceCard
            padding: 20

            ColumnLayout {
                anchors.fill: parent
                spacing: 14

                RowLayout {
                    spacing: 12
                    Rectangle {
                        implicitWidth: 36; implicitHeight: 36
                        radius: 10
                        color: "#DCFCE7"
                        Label {
                            anchors.centerIn: parent
                            text: "⬆"
                            color: successColor
                            font.pixelSize: 18
                        }
                    }
                    ColumnLayout {
                        spacing: 0
                        Label {
                            text: qsTr("Create Backup")
                            font.family: fontSans
                            font.pixelSize: 15
                            font.weight: Font.DemiBold
                            color: Material.foreground
                        }
                        Label {
                            text: qsTr("Export the current database to a zip archive (metadata + msgpack chunks)")
                            font.family: fontSans
                            font.pixelSize: 12
                            color: textMuted
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    TextField {
                        id: backupPathField
                        Layout.fillWidth: true
                        placeholderText: qsTr("Target file — e.g. chinook-backup.zip")
                        font.family: fontMono
                        font.pixelSize: 13
                        enabled: !backend.busy
                        Material.accent: Material.primary
                    }
                    Button {
                        text: qsTr("Browse")
                        flat: true
                        enabled: !backend.busy
                        onClicked: backupTargetDialog.open()
                    }
                }

                RowLayout {
                    spacing: 18
                    CheckBox {
                        id: backupSchemaOnly
                        text: qsTr("Schema only (no table data)")
                        enabled: !backend.busy
                    }
                    Item { Layout.fillWidth: true }
                    Button {
                        text: qsTr("▶  Create Backup")
                        highlighted: true
                        Material.background: successColor
                        enabled: !backend.busy && backupPathField.text.length > 0
                        onClicked: backend.runBackup(backupPathField.text, backupSchemaOnly.checked)
                    }
                }
            }
        }

        // Restore card
        Pane {
            Layout.fillWidth: true
            Material.elevation: 1
            Material.background: surfaceCard
            padding: 20

            ColumnLayout {
                anchors.fill: parent
                spacing: 14

                RowLayout {
                    spacing: 12
                    Rectangle {
                        implicitWidth: 36; implicitHeight: 36
                        radius: 10
                        color: "#FEF3C7"
                        Label {
                            anchors.centerIn: parent
                            text: "⬇"
                            color: warningColor
                            font.pixelSize: 18
                        }
                    }
                    ColumnLayout {
                        spacing: 0
                        Label {
                            text: qsTr("Restore From Backup")
                            font.family: fontSans
                            font.pixelSize: 15
                            font.weight: Font.DemiBold
                            color: Material.foreground
                        }
                        Label {
                            text: qsTr("Import a zip archive into the current database. Overwrites matching tables.")
                            font.family: fontSans
                            font.pixelSize: 12
                            color: textMuted
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: restoreWarningText.implicitHeight + 20
                    color: "#FEF3C7"
                    radius: 8
                    border.width: 1
                    border.color: "#FDE68A"

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 14
                        anchors.rightMargin: 14
                        spacing: 10
                        Label {
                            text: "⚠"
                            color: warningColor
                            font.pixelSize: 16
                        }
                        Label {
                            id: restoreWarningText
                            text: qsTr("Destructive: existing table data in the target database will be replaced.")
                            color: warningColor
                            font.pixelSize: 12
                            font.family: fontSans
                            Layout.fillWidth: true
                            wrapMode: Label.Wrap
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    TextField {
                        id: restorePathField
                        Layout.fillWidth: true
                        placeholderText: qsTr("Backup archive — e.g. chinook-backup.zip")
                        font.family: fontMono
                        font.pixelSize: 13
                        enabled: !backend.busy
                        Material.accent: Material.primary
                    }
                    Button {
                        text: qsTr("Browse")
                        flat: true
                        enabled: !backend.busy
                        onClicked: restoreSourceDialog.open()
                    }
                }

                RowLayout {
                    spacing: 18
                    CheckBox {
                        id: restoreSchemaOnly
                        text: qsTr("Schema only (no table data)")
                        enabled: !backend.busy
                    }
                    Item { Layout.fillWidth: true }
                    Button {
                        text: qsTr("▶  Restore")
                        highlighted: true
                        Material.background: warningColor
                        enabled: !backend.busy && restorePathField.text.length > 0
                        onClicked: backend.runRestore(restorePathField.text, restoreSchemaOnly.checked)
                    }
                }
            }
        }

        // Progress panel
        Pane {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Material.elevation: 1
            Material.background: surfaceCard
            padding: 20

            ColumnLayout {
                anchors.fill: parent
                spacing: 12

                RowLayout {
                    spacing: 10
                    Rectangle {
                        implicitWidth: 10; implicitHeight: 10
                        radius: 5
                        color: backend.busy ? Material.primary : "#D1D5DB"
                        SequentialAnimation on opacity {
                            running: backend.busy
                            loops: Animation.Infinite
                            NumberAnimation { from: 1.0; to: 0.35; duration: 700; easing.type: Easing.InOutQuad }
                            NumberAnimation { from: 0.35; to: 1.0; duration: 700; easing.type: Easing.InOutQuad }
                        }
                    }
                    Label {
                        text: backend.busy ? qsTr("In progress") : qsTr("Idle")
                        font.family: fontSans
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                        color: Material.foreground
                    }
                    Item { Layout.fillWidth: true }
                    Label {
                        text: backend.busy
                              ? qsTr("%1%").arg(Math.round(backend.operationProgress * 100))
                              : ""
                        font.family: fontMono
                        font.pixelSize: 13
                        font.weight: Font.Medium
                        color: textMuted
                    }
                }

                ProgressBar {
                    Layout.fillWidth: true
                    from: 0
                    to: 1
                    value: backend.operationProgress
                    indeterminate: backend.busy && backend.operationProgress === 0
                    Material.accent: Material.primary
                }

                Label {
                    text: backend.operationLabel.length > 0 ? backend.operationLabel : qsTr("No operation running.")
                    font.family: fontMono
                    font.pixelSize: 12
                    color: textMuted
                    Layout.fillWidth: true
                    elide: Label.ElideRight
                }

                Item { Layout.fillHeight: true }
            }
        }
    }   // end Backup / Restore view (StackLayout child 3)

    // ── Log view (StackLayout child 4) ───────────────────────────────────
    Pane {
        Material.background: "#0F172A"
        padding: 0

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            // Toolbar
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 48
                color: "#1E293B"

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 16
                    anchors.rightMargin: 12
                    spacing: 12

                    Label {
                        text: qsTr("Connection Log")
                        font.family: fontSans
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                        color: "#E5E7EB"
                    }

                    Item { Layout.fillWidth: true }

                    Button {
                        text: qsTr("Clear")
                        flat: true
                        Material.foreground: "#9CA3AF"
                        font.pixelSize: 12
                        onClicked: backend.clearLog()
                    }
                }
            }

            // Log output
            ScrollView {
                id: logScroll
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true

                TextArea {
                    id: logArea
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextArea.NoWrap
                    text: backend.log
                    font.family: fontMono
                    font.pixelSize: 12
                    color: "#D1FAE5"
                    background: null
                    leftPadding: 16
                    rightPadding: 16
                    topPadding: 12
                    bottomPadding: 12

                    onTextChanged: {
                        // auto-scroll to bottom
                        logArea.cursorPosition = logArea.length
                    }
                }
            }
        }
    }   // end Log view (StackLayout child 4)

        }   // end StackLayout
    }   // end outer RowLayout

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
                    color: backend.dbConnected ? successColor : "#9CA3AF"

                    SequentialAnimation on opacity {
                        running: backend.dbConnected
                        loops: Animation.Infinite
                        NumberAnimation { from: 1.0; to: 0.4; duration: 1200; easing.type: Easing.InOutQuad }
                        NumberAnimation { from: 0.4; to: 1.0; duration: 1200; easing.type: Easing.InOutQuad }
                    }
                }
                Label {
                    text: backend.dbConnected ? qsTr("Connected") : qsTr("Idle")
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
                visible: backend.pluginLoaded && backend.dbConnected
            }

            // Plugin name
            RowLayout {
                spacing: 6
                visible: backend.pluginLoaded && backend.dbConnected
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
                visible: backend.dbConnected
            }

            // Progress
            RowLayout {
                spacing: 8
                visible: backend.dbConnected

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
