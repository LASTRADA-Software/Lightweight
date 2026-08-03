// SPDX-License-Identifier: Apache-2.0

#include "../Models/BackupProfileFilterModel.hpp"
#include "../Models/BackupStatusListModel.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QtTest/QSignalSpy>

using DbtoolGui::BackupProfileFilterModel;
using DbtoolGui::BackupStatusListModel;

namespace
{
[[nodiscard]] QString nameAt(BackupProfileFilterModel const& proxy, int row)
{
    return proxy.data(proxy.index(row, 0), BackupStatusListModel::NameRole).toString();
}
} // namespace

TEST_CASE("profile filter is empty without a source", "[dbtool-gui][profile-filter]")
{
    BackupProfileFilterModel proxy;
    CHECK(proxy.rowCount() == 0);
}

TEST_CASE("profile filter passes every row through when the text is empty", "[dbtool-gui][profile-filter]")
{
    BackupStatusListModel source;
    source.resetProfiles({ "prod", "staging", "dev" });

    BackupProfileFilterModel proxy;
    proxy.setSourceModel(&source);

    CHECK(proxy.rowCount() == 3);
}

TEST_CASE("profile filter keeps only case-insensitive substring matches", "[dbtool-gui][profile-filter]")
{
    BackupStatusListModel source;
    source.resetProfiles({ "prod-eu", "prod-us", "staging", "DevBox" });

    BackupProfileFilterModel proxy;
    proxy.setSourceModel(&source);

    proxy.setFilterText("prod");
    REQUIRE(proxy.rowCount() == 2);
    QStringList names;
    for (int i = 0; i < proxy.rowCount(); ++i)
        names << nameAt(proxy, i);
    CHECK(names.contains("prod-eu"));
    CHECK(names.contains("prod-us"));

    // Case-insensitive: "dev" matches "DevBox".
    proxy.setFilterText("dev");
    REQUIRE(proxy.rowCount() == 1);
    CHECK(nameAt(proxy, 0) == "DevBox");
}

TEST_CASE("profile filter matches nothing when no name contains the text", "[dbtool-gui][profile-filter]")
{
    BackupStatusListModel source;
    source.resetProfiles({ "prod", "staging" });

    BackupProfileFilterModel proxy;
    proxy.setSourceModel(&source);

    proxy.setFilterText("nonexistent");
    CHECK(proxy.rowCount() == 0);
}

TEST_CASE("profile filter re-shows all rows when the text is cleared", "[dbtool-gui][profile-filter]")
{
    BackupStatusListModel source;
    source.resetProfiles({ "prod", "staging", "dev" });

    BackupProfileFilterModel proxy;
    proxy.setSourceModel(&source);

    proxy.setFilterText("prod");
    REQUIRE(proxy.rowCount() == 1);

    proxy.setFilterText("");
    CHECK(proxy.rowCount() == 3);
}

TEST_CASE("profile filter trims surrounding whitespace before matching", "[dbtool-gui][profile-filter]")
{
    BackupStatusListModel source;
    source.resetProfiles({ "prod", "staging" });

    BackupProfileFilterModel proxy;
    proxy.setSourceModel(&source);

    // A stray leading/trailing space would otherwise hide every row.
    proxy.setFilterText("  prod  ");
    CHECK(proxy.filterText() == "prod");
    REQUIRE(proxy.rowCount() == 1);
    CHECK(nameAt(proxy, 0) == "prod");
}

TEST_CASE("profile filter emits filterTextChanged only on a real change", "[dbtool-gui][profile-filter]")
{
    BackupProfileFilterModel proxy;
    QSignalSpy spy(&proxy, &BackupProfileFilterModel::filterTextChanged);

    proxy.setFilterText("prod");
    CHECK(spy.count() == 1);

    // Same trimmed value -> no-op, no extra signal.
    proxy.setFilterText("  prod  ");
    CHECK(spy.count() == 1);

    proxy.setFilterText("dev");
    CHECK(spy.count() == 2);
}

TEST_CASE("profile filter re-evaluates when the source model changes", "[dbtool-gui][profile-filter]")
{
    BackupStatusListModel source;
    source.resetProfiles({ "prod" });

    BackupProfileFilterModel proxy;
    proxy.setSourceModel(&source);
    proxy.setFilterText("prod");
    REQUIRE(proxy.rowCount() == 1);

    // Reloading the profile file (a new dbtool.yml) must re-run the filter.
    source.resetProfiles({ "prod-a", "prod-b", "staging" });
    CHECK(proxy.rowCount() == 2);
}
