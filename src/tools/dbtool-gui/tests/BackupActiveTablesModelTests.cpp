// SPDX-License-Identifier: Apache-2.0

#include "../Models/BackupActiveTablesModel.hpp"
#include "../Models/BackupTableListModel.hpp"

#include <catch2/catch_test_macros.hpp>

using DbtoolGui::BackupActiveTablesModel;
using DbtoolGui::BackupTableListModel;

namespace
{
[[nodiscard]] QString nameAt(BackupActiveTablesModel const& proxy, int row)
{
    return proxy.data(proxy.index(row, 0), BackupTableListModel::TableNameRole).toString();
}
} // namespace

TEST_CASE("active proxy is empty without a source", "[dbtool-gui][active-proxy]")
{
    BackupActiveTablesModel proxy;
    CHECK(proxy.rowCount() == 0);
}

TEST_CASE("active proxy surfaces only running/error/warning rows", "[dbtool-gui][active-proxy]")
{
    BackupTableListModel source;
    source.applyProgress("done_a", 10, 10, "done", "");
    source.applyProgress("run_a", 3, 10, "running", "");
    source.applyProgress("queued_a", 0, 10, "queued", "");
    source.applyProgress("err_a", 0, 10, "error", "boom");
    source.applyProgress("warn_a", 5, 10, "warning", "hmm");
    source.applyProgress("done_b", 10, 10, "done", "");
    source.applyProgress("run_b", 1, 10, "running", "");

    BackupActiveTablesModel proxy;
    proxy.setSourceModel(&source);

    // Only running/error/warning survive: run_a, err_a, warn_a, run_b.
    REQUIRE(proxy.rowCount() == 4);
    QStringList names;
    for (int i = 0; i < proxy.rowCount(); ++i)
        names << nameAt(proxy, i);
    CHECK(names.contains("run_a"));
    CHECK(names.contains("err_a"));
    CHECK(names.contains("warn_a"));
    CHECK(names.contains("run_b"));
    CHECK_FALSE(names.contains("done_a"));
    CHECK_FALSE(names.contains("queued_a"));
}

TEST_CASE("active proxy re-filters when a table transitions state", "[dbtool-gui][active-proxy]")
{
    BackupTableListModel source;
    BackupActiveTablesModel proxy;
    proxy.setSourceModel(&source);

    source.applyProgress("orders", 0, 100, "queued", "");
    CHECK(proxy.rowCount() == 0); // queued is filtered out

    source.applyProgress("orders", 40, 100, "running", "");
    CHECK(proxy.rowCount() == 1); // now visible
    CHECK(nameAt(proxy, 0) == "orders");

    source.applyProgress("orders", 100, 100, "done", "");
    CHECK(proxy.rowCount() == 0); // done -> filtered out again
}

TEST_CASE("active proxy forwards the source role names to QML", "[dbtool-gui][active-proxy]")
{
    BackupTableListModel source;
    BackupActiveTablesModel proxy;
    proxy.setSourceModel(&source);

    auto const roles = proxy.roleNames();
    CHECK(roles.value(BackupTableListModel::TableNameRole) == "tableName");
    CHECK(roles.value(BackupTableListModel::StateRole) == "state");
}

TEST_CASE("active proxy clears when the source resets", "[dbtool-gui][active-proxy]")
{
    BackupTableListModel source;
    BackupActiveTablesModel proxy;
    proxy.setSourceModel(&source);

    source.applyProgress("run_a", 3, 10, "running", "");
    REQUIRE(proxy.rowCount() == 1);

    source.clearTables();
    CHECK(proxy.rowCount() == 0);
}
