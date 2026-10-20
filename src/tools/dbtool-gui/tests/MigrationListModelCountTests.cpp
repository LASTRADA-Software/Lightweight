// SPDX-License-Identifier: Apache-2.0

#include "../Models/MigrationListModel.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <ranges>
#include <utility>
#include <vector>

#include <QtTest/QSignalSpy>

using DbtoolGui::MigrationListModel;
using DbtoolGui::MigrationRow;
using DbtoolGui::SelectionState;

namespace
{

constexpr std::uint64_t FirstTimestamp = 20240101000000;

/// @return @p count pending rows, newest first; the first @p unreleased of them belong to no release.
[[nodiscard]] std::vector<MigrationRow> PendingRows(int count, int unreleased)
{
    auto rows = std::vector<MigrationRow> {};
    for (auto const index: std::views::iota(0, count))
    {
        rows.push_back(MigrationRow {
            .timestamp = FirstTimestamp + static_cast<std::uint64_t>(count - index),
            .title = QStringLiteral("Migration %1").arg(index),
            .status = QStringLiteral("pending"),
            .releaseVersion = index < unreleased ? QString {} : QStringLiteral("1.0.0"),
        });
    }
    return rows;
}

} // namespace

TEST_CASE("status counts follow single-row status changes", "[dbtool-gui][MigrationListModel]")
{
    auto model = MigrationListModel {};
    model.ReplaceRows(PendingRows(100, 5));
    CHECK(model.Counts().pending == 100);
    CHECK(model.Counts().pendingUnreleased == 5);
    CHECK(model.Counts().applied == 0);

    auto const changed = QSignalSpy { &model, &MigrationListModel::dataChanged };
    for (auto const offset: std::views::iota(1, 11))
        REQUIRE(
            model.SetRowStatusByTimestamp(FirstTimestamp + static_cast<std::uint64_t>(offset), QStringLiteral("applied")));
    REQUIRE(model.SetRowStatusByTimestamp(FirstTimestamp + 100, QStringLiteral("checksum-mismatch")));

    auto const counts = model.Counts();
    CHECK(counts.applied == 10);
    CHECK(counts.pending == 89);
    CHECK(counts.checksumMismatch == 1);
    CHECK(counts.unknown == 0);
    // The newest row (offset 100) was one of the five unreleased ones.
    CHECK(counts.pendingUnreleased == 4);
    CHECK(model.AppliedTimestamps().size() == 11);
    CHECK(*model.AppliedTimestamps().rbegin() == FirstTimestamp + 100);
    CHECK(changed.count() == 11);

    REQUIRE(model.SetRowStatusByTimestamp(FirstTimestamp + 1, QStringLiteral("running")));
    CHECK(model.Counts().applied == 9);
    CHECK(model.Counts().pending == 89);
    CHECK_FALSE(model.SetRowStatusByTimestamp(FirstTimestamp, QStringLiteral("applied")));
}

TEST_CASE("the timestamp index survives ReplaceRows", "[dbtool-gui][MigrationListModel]")
{
    auto model = MigrationListModel {};
    model.ReplaceRows(PendingRows(10, 0));
    REQUIRE(model.SetSelectedByTimestamp(FirstTimestamp + 3, SelectionState::Selected));
    CHECK(model.SelectedCount() == 1);

    // The same timestamps at other rows, and one gone: lookups follow the new rows.
    auto rows = PendingRows(10, 0);
    rows.erase(rows.begin());
    std::ranges::reverse(rows);
    rows.front().status = QStringLiteral("applied");
    model.ReplaceRows(std::move(rows));

    CHECK(model.SelectedCount() == 0);
    CHECK(model.Counts().applied == 1);
    CHECK(model.Counts().pending == 8);
    CHECK(model.TitleByTimestamp(FirstTimestamp + 1) == QStringLiteral("Migration 9"));
    CHECK(model.TitleByTimestamp(FirstTimestamp + 10).isEmpty());

    REQUIRE(model.SetRowStatusByTimestamp(FirstTimestamp + 2, QStringLiteral("applied")));
    CHECK(model.data(model.index(1, 0), MigrationListModel::StatusRole).toString() == QStringLiteral("applied"));
    CHECK(model.Counts().applied == 2);

    REQUIRE(model.SetSelectedByTimestamp(FirstTimestamp + 5, SelectionState::Selected));
    CHECK(model.data(model.index(4, 0), MigrationListModel::SelectedRole).toBool());
    CHECK(model.SelectedCount() == 1);
    CHECK_FALSE(model.SetSelectedByTimestamp(FirstTimestamp + 1, SelectionState::Selected));
    CHECK(model.SelectedCount() == 1);
    REQUIRE(model.SetSelectedByTimestamp(FirstTimestamp + 5, SelectionState::NotSelected));
    CHECK(model.SelectedCount() == 0);
}

TEST_CASE("incremental release tallies match a fresh rebuild", "[dbtool-gui][MigrationListModel]")
{
    // Three releases plus the unreleased group, newest first, with an unknown row in release 1.0.0.
    auto rows = PendingRows(30, 6);
    for (auto const& [index, row]: std::views::enumerate(rows))
        row.releaseVersion = index < 6 ? QString {} : index < 16 ? QStringLiteral("2.0.0") : QStringLiteral("1.0.0");
    rows.back().status = QStringLiteral("unknown");

    auto model = MigrationListModel {};
    model.ReplaceRows(rows);
    auto changed = QSignalSpy { &model, &MigrationListModel::releaseTallyChanged };
    auto const changes = std::vector<std::pair<std::uint64_t, QString>> {
        { FirstTimestamp + 2, QStringLiteral("running") },  { FirstTimestamp + 2, QStringLiteral("applied") },
        { FirstTimestamp + 3, QStringLiteral("applied") },  { FirstTimestamp + 20, QStringLiteral("checksum-mismatch") },
        { FirstTimestamp + 29, QStringLiteral("applied") }, { FirstTimestamp + 3, QStringLiteral("pending") },
        { FirstTimestamp + 1, QStringLiteral("applied") },
    };
    for (auto const& [timestamp, status]: changes)
    {
        REQUIRE(model.SetRowStatusByTimestamp(timestamp, status));
        auto const row = std::ranges::find(rows, timestamp, &MigrationRow::timestamp);
        row->status = status;
    }
    CHECK(model.tallyRebuildCount() == 1);
    CHECK(changed.count() > 0);

    auto fresh = MigrationListModel {};
    fresh.ReplaceRows(rows);
    for (auto const& version: { QString {}, QStringLiteral("1.0.0"), QStringLiteral("2.0.0") })
    {
        INFO("release " << version.toStdString());
        CHECK(model.ReleaseTallyOf(version) == fresh.ReleaseTallyOf(version));
    }
    CHECK(model.ReleaseTallyOf(QStringLiteral("1.0.0")).applied == 2);
    CHECK(model.ReleaseTallyOf(QStringLiteral("1.0.0")).total == 14);
    auto const counts = model.Counts();
    auto const freshCounts = fresh.Counts();
    CHECK(counts.applied == freshCounts.applied);
    CHECK(counts.pending == freshCounts.pending);
    CHECK(counts.pendingUnreleased == freshCounts.pendingUnreleased);
    CHECK(counts.unknown == freshCounts.unknown);
    CHECK(counts.checksumMismatch == freshCounts.checksumMismatch);
    CHECK(model.AppliedTimestamps() == fresh.AppliedTimestamps());
}
