// SPDX-License-Identifier: Apache-2.0
//
// Centre pane: filter bar on top (tabs + search + bulk controls) and a
// migration timeline below. Consumed directly by `Main.qml` and exported
// from the `Lightweight.Migrations 1.0` module so downstream apps can embed
// the whole migrations panel verbatim.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Lightweight.Migrations

Item {
    id: root

    property string filterTab: "all" // all | pending | applied | issues
    property string search: ""

    // These are bound to notify-enabled AppController properties so the
    // tab badges re-evaluate automatically when the migration model is
    // refreshed. Calling `model.rowCount()` / `model.data(...)` from a
    // QML binding does not trigger on model resets.
    readonly property int countAll: AppController.migrationCount
    readonly property int countPending: AppController.pendingCount
    readonly property int countApplied: AppController.appliedCount
    readonly property int countIssues: AppController.issuesCount

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        // Filter bar — wraps onto multiple rows on narrow windows so the tabs,
        // search box, and bulk controls never overflow horizontally.
        Flow {
            Layout.fillWidth: true
            spacing: 10

            FilterTabs {
                current: root.filterTab
                tabs: [
                    { key: "all",     label: qsTr("All"),     count: root.countAll,
                      tip: qsTr("Show every registered migration — both applied and pending — in chronological order. The count badge reflects the total migration set discovered in the plugin directory.") },
                    { key: "pending", label: qsTr("Pending"), count: root.countPending,
                      tip: qsTr("Show only migrations that exist on disk but have not yet been applied to the connected database. These are the migrations the next <b>Apply</b> run will execute.") },
                    { key: "applied", label: qsTr("Applied"), count: root.countApplied,
                      tip: qsTr("Show only migrations already recorded in <b>schema_migrations</b>. Useful to audit the deployed schema history or pick a release to roll back to.") },
                    { key: "issues",  label: qsTr("Issues"),  count: root.countIssues,
                      tip: qsTr("Show only migrations in an abnormal state: applied to the database but missing from disk (<b>unknown</b>), or present in both but with a different checksum (<b>checksum-mismatch</b> — the SQL was edited after it was applied). Investigate these before running new migrations.") },
                ]
                onActivated: key => root.filterTab = key
            }

            Rectangle {
                width: Math.min(320, root.width - 20)
                height: 30
                color: Theme.bgSubtle
                border.color: Theme.border
                radius: 6
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 10
                    spacing: 6
                    Label { text: "🔍"; color: Theme.textMuted; font.pixelSize: 12 }
                    TextField {
                        Layout.fillWidth: true
                        placeholderText: qsTr("Search by title or timestamp…")
                        background: null
                        color: Theme.text
                        font.pixelSize: 13
                        onTextChanged: root.search = text
                    }
                }
            }

            BulkControls {
                leftLabel: qsTr("Select all")
                rightLabel: qsTr("Deselect all")
                leftTip: qsTr("Tick every migration currently showing the <b>pending</b> status. Applied migrations are skipped — their checkboxes are disabled so the bulk action never tries to re-run committed work. When any row is ticked, the target picker in the right pane is bypassed and actions operate on the selection only.")
                rightTip: qsTr("Clear all ticked checkboxes. The target picker in the right pane (Latest / Release / Timestamp) takes over again so the next <b>Apply</b> run uses your chosen target instead of a row selection.")
                activeLeft: AppController.selectionCount > 0
                    && AppController.selectionCount === AppController.pendingCount
                activeRight: AppController.selectionCount === 0
                onLeftClicked: AppController.selectAllPending(true)
                onRightClicked: AppController.selectAllPending(false)
            }
        }

        // Migration timeline
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.bgPanel
            border.color: Theme.border
            radius: 10

            // Column header strip — uses explicit anchors to match
            // MigrationRow's anchor-based layout so the columns line up on
            // every window width.
            Rectangle {
                id: listHeader
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.topMargin: 1
                anchors.leftMargin: 1
                anchors.rightMargin: 1
                height: 32
                color: Theme.bgChrome
                radius: 10

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 1
                    color: Theme.border
                }

                Label {
                    id: hdrTimestamp
                    anchors.left: parent.left
                    anchors.leftMargin: 52     // 10 + 28 (checkbox) + 4 offset in MigrationRow
                    anchors.verticalCenter: parent.verticalCenter
                    width: 130
                    text: qsTr("TIMESTAMP")
                    color: Theme.textFaint
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                    font.letterSpacing: 0.5
                }
                Label {
                    id: hdrStatus
                    anchors.right: parent.right
                    anchors.rightMargin: 14
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("STATUS")
                    color: Theme.textFaint
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                    font.letterSpacing: 0.5
                }
                Label {
                    anchors.left: hdrTimestamp.right
                    anchors.leftMargin: 10
                    anchors.right: hdrStatus.left
                    anchors.rightMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("MIGRATION")
                    color: Theme.textFaint
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                    font.letterSpacing: 0.5
                    elide: Text.ElideRight
                }
            }

            KineticListView {
                id: listView
                anchors.top: listHeader.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.leftMargin: 1
                anchors.rightMargin: 1
                anchors.bottomMargin: 1
                clip: true
                spacing: 0

                model: AppController.migrations
                delegate: MigrationRow {
                    // Bind via `model.<role>` rather than required properties
                    // so the delegate is indifferent to whether the inherited
                    // MigrationRow property is marked required — the
                    // explicit form always picks up the role values
                    // reliably on Qt 6's model/view pipeline.
                    timestamp: model.timestamp
                    title: model.title
                    status: model.status
                    checksumMismatch: model.checksumMismatch
                    selected: model.selected

                    onDoubleClicked: (ts, ttl) => previewDialog.showFor(ts, ttl)

                    visible: {
                        if (root.filterTab === "pending" && status !== "pending") return false;
                        if (root.filterTab === "applied" && status !== "applied") return false;
                        if (root.filterTab === "issues"
                            && status !== "unknown"
                            && status !== "checksum-mismatch")
                            return false;
                        if (root.search.length > 0
                            && title.toLowerCase().indexOf(root.search.toLowerCase()) < 0
                            && timestamp.indexOf(root.search) < 0)
                            return false;
                        return true;
                    }
                    height: visible ? 40 : 0
                }
            }

            // Previews the SQL for a migration. Opened by the MigrationRow
            // `doubleClicked` signal — keeping the dialog here (rather than
            // at Main.qml level) scopes it to the migration list view, so
            // the same pattern can be reused if MigrationView is embedded
            // standalone in a downstream app.
            SqlPreviewDialog {
                id: previewDialog
                anchors.centerIn: Overlay.overlay
            }

            // Empty-state overlay
            ColumnLayout {
                anchors.centerIn: parent
                visible: listView.count === 0
                spacing: 6
                Label {
                    text: AppController.connected
                        ? qsTr("No migrations registered")
                        : qsTr("Not connected")
                    color: Theme.textMuted
                    font.pixelSize: 13
                    Layout.alignment: Qt.AlignHCenter
                }
                Label {
                    text: AppController.connected
                        ? qsTr("Point a profile at a plugin directory to see migrations here.")
                        : qsTr("Pick a profile in the left pane and press Connect.")
                    color: Theme.textFaint
                    font.pixelSize: 11
                    Layout.alignment: Qt.AlignHCenter
                }
            }
        }
    }
}
