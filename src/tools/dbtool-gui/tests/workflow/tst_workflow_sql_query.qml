// SPDX-License-Identifier: Apache-2.0
//
// The SQL query tab of the bottom panel: a query typed into the editor and run with the Execute
// button shows its result table, and a failing query shows its error instead.

import QtQuick
import QtTest
import Lightweight.Migrations
import DbtoolGuiWorkflow

WorkflowTestCase {
    name: "WorkflowSqlQuery"

    /// Connects profile `p` on a new SQLite database and opens the SQL tab of the Expert view.
    function openSqlTab() {
        const cs = WorkflowFixture.newSqliteDatabase()
        const plugins = WorkflowFixture.newPluginsDir(["workflow"])
        const path = WorkflowFixture.writeProfileFile([{ name: "p", connectionString: cs, pluginsDir: plugins }], "p")
        WorkflowFixture.setSetting("config/profileStorePath", path)
        WorkflowFixture.setSetting("ui/viewMode", "expert")
        openMain()
        tryCompare(AppController, "busy", false, 15000)
        clickControl("connectButton")
        tryCompare(AppController, "connected", true, 15000)
        tryCompare(AppController, "busy", false, 15000)
        clickControl("sqlTab")
    }

    /// Types @p sql into the editor, clicks Execute and waits for the run to end.
    function runQuery(sql) {
        typeText("sqlEditor", sql)
        clickControl("runQueryButton")
        tryCompare(AppController.sqlQueryRunner, "busy", false, 15000)
    }

    /// @return The text of the label inside the cell delegate @p cell.
    function cellText(cell) {
        verify(cell !== null && cell !== undefined, "the cell must exist")
        for (const child of cell.children) {
            if (child.text !== undefined)
                return child.text
        }
        fail("the cell has no label")
        return ""
    }

    function test_a_query_shows_its_result() {
        openSqlTab()
        runQuery("SELECT 1 AS one")
        compare(findRequired("sqlError").visible, false)
        const table = findRequired("sqlResultTable")
        tryCompare(table, "rows", 1, 5000)
        compare(table.columns, 1)
        const header = findRequired("sqlResultHeader")
        tryVerify(() => header.itemAtCell(Qt.point(0, 0)) !== null, 5000, "the header cell must appear")
        compare(cellText(header.itemAtCell(Qt.point(0, 0))), "one")
        tryVerify(() => table.itemAtCell(Qt.point(0, 0)) !== null, 5000, "the result cell must appear")
        compare(cellText(table.itemAtCell(Qt.point(0, 0))), "1")
    }

    function test_a_failing_query_shows_its_error() {
        openSqlTab()
        runQuery("SELECT * FROM missing_table")
        const error = findRequired("sqlError")
        tryCompare(error, "visible", true, 5000)
        verify(error.text.includes("missing_table"), error.text)
    }
}
