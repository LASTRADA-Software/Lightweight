// SPDX-License-Identifier: Apache-2.0

#include "../Models/BackupTableListModel.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QtTest/QSignalSpy>

using DbtoolGui::BackupTableListModel;

namespace
{
[[nodiscard]] QString stateAt(BackupTableListModel const& m, int row)
{
    return m.data(m.index(row, 0), BackupTableListModel::StateRole).toString();
}
[[nodiscard]] QString nameAt(BackupTableListModel const& m, int row)
{
    return m.data(m.index(row, 0), BackupTableListModel::TableNameRole).toString();
}
} // namespace

TEST_CASE("BackupTableListModel starts empty", "[dbtool-gui][backup-table-model]")
{
    BackupTableListModel model;
    CHECK(model.rowCount() == 0);
}

TEST_CASE("applyProgress creates a row on first sight of a table", "[dbtool-gui][backup-table-model]")
{
    BackupTableListModel model;
    model.applyProgress("orders", 0, 1000, "running", "started");

    REQUIRE(model.rowCount() == 1);
    CHECK(nameAt(model, 0) == "orders");
    CHECK(model.data(model.index(0, 0), BackupTableListModel::CurrentRowsRole).toULongLong() == 0);
    CHECK(model.data(model.index(0, 0), BackupTableListModel::TotalRowsRole).toLongLong() == 1000);
    CHECK(stateAt(model, 0) == "running");
}

TEST_CASE("applyProgress updates an existing table in place by name", "[dbtool-gui][backup-table-model]")
{
    BackupTableListModel model;
    model.applyProgress("orders", 0, 1000, "running", "started");
    model.applyProgress("orders", 500, 1000, "running", "halfway");
    model.applyProgress("orders", 1000, 1000, "done", "finished");

    REQUIRE(model.rowCount() == 1); // still one row, updated in place
    CHECK(model.data(model.index(0, 0), BackupTableListModel::CurrentRowsRole).toULongLong() == 1000);
    CHECK(stateAt(model, 0) == "done");
    CHECK(model.data(model.index(0, 0), BackupTableListModel::MessageRole).toString() == "finished");
}

TEST_CASE("applyProgress tolerates interleaved updates from multiple tables", "[dbtool-gui][backup-table-model]")
{
    BackupTableListModel model;
    model.applyProgress("orders", 10, 100, "running", "");
    model.applyProgress("customers", 5, 50, "running", "");
    model.applyProgress("orders", 40, 100, "running", "");
    model.applyProgress("customers", 50, 50, "done", "");

    REQUIRE(model.rowCount() == 2);
    // Row order follows first-sight order: orders (0), customers (1).
    CHECK(nameAt(model, 0) == "orders");
    CHECK(model.data(model.index(0, 0), BackupTableListModel::CurrentRowsRole).toULongLong() == 40);
    CHECK(nameAt(model, 1) == "customers");
    CHECK(stateAt(model, 1) == "done");
}

TEST_CASE("applyProgress handles unknown total as -1", "[dbtool-gui][backup-table-model]")
{
    BackupTableListModel model;
    model.applyProgress("orders", 7, -1, "running", "");
    CHECK(model.data(model.index(0, 0), BackupTableListModel::TotalRowsRole).toLongLong() == -1);
    // A later update can fill in a known total.
    model.applyProgress("orders", 7, 700, "running", "");
    CHECK(model.data(model.index(0, 0), BackupTableListModel::TotalRowsRole).toLongLong() == 700);
}

TEST_CASE("clearTables empties the model", "[dbtool-gui][backup-table-model]")
{
    BackupTableListModel model;
    model.applyProgress("orders", 1, 2, "running", "");
    model.applyProgress("customers", 1, 2, "running", "");
    REQUIRE(model.rowCount() == 2);

    model.clearTables();
    CHECK(model.rowCount() == 0);
}

TEST_CASE("data() returns invalid QVariant for bad index or unknown role", "[dbtool-gui][backup-table-model]")
{
    BackupTableListModel model;
    model.applyProgress("orders", 1, 2, "running", "");
    CHECK_FALSE(model.data(QModelIndex {}, BackupTableListModel::TableNameRole).isValid());
    CHECK_FALSE(model.data(model.index(5, 0), BackupTableListModel::TableNameRole).isValid());
    CHECK_FALSE(model.data(model.index(0, 0), Qt::DisplayRole).isValid());
}

TEST_CASE("roleNames exposes the QML-facing names", "[dbtool-gui][backup-table-model]")
{
    BackupTableListModel model;
    auto const roles = model.roleNames();
    CHECK(roles.value(BackupTableListModel::TableNameRole) == "tableName");
    CHECK(roles.value(BackupTableListModel::CurrentRowsRole) == "currentRows");
    CHECK(roles.value(BackupTableListModel::TotalRowsRole) == "totalRows");
    CHECK(roles.value(BackupTableListModel::StateRole) == "state");
    CHECK(roles.value(BackupTableListModel::MessageRole) == "message");
}

TEST_CASE("summary tallies start at zero", "[dbtool-gui][backup-table-model]")
{
    BackupTableListModel model;
    CHECK(model.totalCount() == 0);
    CHECK(model.doneCount() == 0);
    CHECK(model.runningCount() == 0);
    CHECK(model.queuedCount() == 0);
    CHECK(model.errorCount() == 0);
    CHECK(model.warningCount() == 0);
}

TEST_CASE("summary tallies count tables by state", "[dbtool-gui][backup-table-model]")
{
    BackupTableListModel model;
    model.applyProgress("a", 100, 100, "done", "");
    model.applyProgress("b", 50, 100, "running", "");
    model.applyProgress("c", 0, 100, "queued", "");
    model.applyProgress("d", 0, 100, "error", "boom");
    model.applyProgress("e", 0, 100, "warning", "hmm");
    // Second running table.
    model.applyProgress("f", 10, 100, "running", "");

    CHECK(model.totalCount() == 6);
    CHECK(model.doneCount() == 1);
    CHECK(model.runningCount() == 2);
    CHECK(model.queuedCount() == 1);
    CHECK(model.errorCount() == 1);
    CHECK(model.warningCount() == 1);
}

TEST_CASE("summary tallies follow in-place state transitions", "[dbtool-gui][backup-table-model]")
{
    BackupTableListModel model;
    model.applyProgress("orders", 0, 100, "queued", "");
    CHECK(model.queuedCount() == 1);
    CHECK(model.runningCount() == 0);

    model.applyProgress("orders", 40, 100, "running", "");
    CHECK(model.queuedCount() == 0);
    CHECK(model.runningCount() == 1);

    model.applyProgress("orders", 100, 100, "done", "");
    CHECK(model.runningCount() == 0);
    CHECK(model.doneCount() == 1);
    CHECK(model.totalCount() == 1); // still one table, just transitioned
}

TEST_CASE("an unrecognised state falls into the queued bucket", "[dbtool-gui][backup-table-model]")
{
    BackupTableListModel model;
    model.applyProgress("orders", 0, 100, "pending-something", "");
    CHECK(model.queuedCount() == 1);
    CHECK(model.doneCount() == 0);
    CHECK(model.runningCount() == 0);
}

TEST_CASE("summaryChanged fires on inserts, updates, and clear", "[dbtool-gui][backup-table-model]")
{
    BackupTableListModel model;
    QSignalSpy spy(&model, &BackupTableListModel::summaryChanged);

    model.applyProgress("orders", 0, 100, "running", ""); // insert
    CHECK(spy.count() == 1);

    model.applyProgress("orders", 50, 100, "running", ""); // in-place update
    CHECK(spy.count() == 2);

    model.clearTables(); // reset
    CHECK(spy.count() == 3);
    CHECK(model.totalCount() == 0);
    CHECK(model.runningCount() == 0);
}

// --- Announced table total (the "n / total" denominator) ---------------------
//
// Rows are created lazily on each table's first progress report, so counting
// rows made the denominator climb during a run: the panel showed "1 / 1", then
// "2 / 2", … instead of "1 / 700". SqlBackup knows the real count after its
// schema scan and announces it via setTotalTables().

TEST_CASE("setTotalTables fixes the denominator before any progress arrives", "[dbtool-gui][backup-table-model]")
{
    BackupTableListModel model;
    model.setTotalTables(700);

    // Announced up front: the total is already 700 with no rows reported yet.
    CHECK(model.totalCount() == 700);
    CHECK(model.rowCount() == 0);
    CHECK(model.doneCount() == 0);
}

TEST_CASE("the announced total does not grow as tables report in", "[dbtool-gui][backup-table-model]")
{
    BackupTableListModel model;
    model.setTotalTables(700);

    // This is the regression: each of these used to bump totalCount().
    model.applyProgress("a", 10, 100, "running", "");
    CHECK(model.totalCount() == 700);
    model.applyProgress("b", 20, 100, "running", "");
    CHECK(model.totalCount() == 700);
    model.applyProgress("c", 100, 100, "done", "");
    CHECK(model.totalCount() == 700);

    CHECK(model.rowCount() == 3); // only 3 rows exist...
    CHECK(model.doneCount() == 1);
    CHECK(model.totalCount() == 700); // ...but the denominator is the real total
}

TEST_CASE("totalCount falls back to rows seen when no total was announced", "[dbtool-gui][backup-table-model]")
{
    BackupTableListModel model;
    model.applyProgress("a", 10, 100, "running", "");
    model.applyProgress("b", 20, 100, "running", "");

    // No setTotalTables() call (e.g. a progress source predating the hook):
    // counting rows is the only estimate available.
    CHECK(model.totalCount() == 2);
}

TEST_CASE("the denominator is never smaller than the number of tables seen", "[dbtool-gui][backup-table-model]")
{
    BackupTableListModel model;
    model.setTotalTables(2);

    model.applyProgress("a", 1, 1, "done", "");
    model.applyProgress("b", 1, 1, "done", "");
    // More tables report than were announced (a filter/announcement mismatch).
    // "3 / 2 tables done" would be nonsense, so the larger value wins.
    model.applyProgress("c", 1, 1, "done", "");

    CHECK(model.doneCount() == 3);
    CHECK(model.totalCount() == 3);
}

TEST_CASE("clearTables drops the previous run's announced total", "[dbtool-gui][backup-table-model]")
{
    BackupTableListModel model;
    model.setTotalTables(700);
    model.applyProgress("a", 10, 100, "running", "");

    model.clearTables();

    // A stale 700 here would show "0 / 700" for a new run that has not yet
    // announced its own (possibly much smaller) total.
    CHECK(model.totalCount() == 0);
}

TEST_CASE("setTotalTables notifies only on a real change", "[dbtool-gui][backup-table-model]")
{
    BackupTableListModel model;
    QSignalSpy spy(&model, &BackupTableListModel::summaryChanged);

    model.setTotalTables(700);
    CHECK(spy.count() == 1);

    model.setTotalTables(700); // same value — no spurious summary refresh
    CHECK(spy.count() == 1);

    model.setTotalTables(12);
    CHECK(spy.count() == 2);
}

TEST_CASE("a non-positive announced total reverts to counting rows", "[dbtool-gui][backup-table-model]")
{
    BackupTableListModel model;
    model.setTotalTables(700);
    model.applyProgress("a", 1, 1, "done", "");

    model.setTotalTables(0);
    CHECK(model.totalCount() == 1);

    model.setTotalTables(-5); // defensive: a bad cast must not make a negative total
    CHECK(model.totalCount() == 1);
}
