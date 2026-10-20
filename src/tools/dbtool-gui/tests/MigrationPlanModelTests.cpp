// SPDX-License-Identifier: Apache-2.0

#include "../Models/MigrationListModel.hpp"
#include "../Models/MigrationPlanModel.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <ranges>
#include <vector>

#include <QtTest/QSignalSpy>

using DbtoolGui::MigrationListModel;
using DbtoolGui::MigrationPlanModel;
using DbtoolGui::MigrationRow;

namespace
{

constexpr std::uint64_t FirstTimestamp = 20240101000000;

/// @return @p count rows, newest first; the oldest @p applied of them are applied, the others pending.
[[nodiscard]] std::vector<MigrationRow> Rows(int count, int applied)
{
    auto rows = std::vector<MigrationRow> {};
    for (auto const index: std::views::iota(0, count))
    {
        rows.push_back(MigrationRow {
            .timestamp = FirstTimestamp + static_cast<std::uint64_t>(count - index),
            .title = QStringLiteral("Migration %1").arg(index),
            .status = index >= count - applied ? QStringLiteral("applied") : QStringLiteral("pending"),
            .releaseVersion = {},
        });
    }
    return rows;
}

[[nodiscard]] QString Timestamp(std::uint64_t offset)
{
    return QString::number(FirstTimestamp + offset);
}

} // namespace

TEST_CASE("a pending plan lists the pending rows and drops them as they leave", "[dbtool-gui][MigrationPlanModel]")
{
    auto migrations = MigrationListModel {};
    migrations.ReplaceRows(Rows(10, 3));
    auto plan = MigrationPlanModel {};
    plan.setSourceModel(&migrations);
    CHECK(plan.rowCount() == 7);

    auto const removed = QSignalSpy { &plan, &MigrationPlanModel::rowsRemoved };
    auto const reset = QSignalSpy { &plan, &MigrationPlanModel::modelReset };
    REQUIRE(migrations.SetRowStatusByTimestamp(FirstTimestamp + 4, QStringLiteral("running")));
    CHECK(plan.rowCount() == 6);
    CHECK(removed.count() == 1);
    CHECK(reset.count() == 0);
}

TEST_CASE("a run plan keeps the rows the run touched once they are applied", "[dbtool-gui][MigrationPlanModel]")
{
    auto migrations = MigrationListModel {};
    migrations.ReplaceRows(Rows(10, 3));
    auto plan = MigrationPlanModel {};
    plan.setScope(MigrationPlanModel::Scope::Run);
    plan.setSourceModel(&migrations);
    CHECK(plan.rowCount() == 7);

    auto const removed = QSignalSpy { &plan, &MigrationPlanModel::rowsRemoved };
    auto const inserted = QSignalSpy { &plan, &MigrationPlanModel::rowsInserted };
    REQUIRE(migrations.SetRowStatusByTimestamp(FirstTimestamp + 4, QStringLiteral("running")));
    plan.touch(Timestamp(4));
    REQUIRE(migrations.SetRowStatusByTimestamp(FirstTimestamp + 4, QStringLiteral("applied")));
    CHECK(plan.rowCount() == 7);
    CHECK(removed.count() == 0);
    CHECK(inserted.count() == 0);

    // An applied row the run did not touch joins only when touched.
    plan.touch(Timestamp(1));
    CHECK(plan.rowCount() == 8);

    plan.clearTouched();
    CHECK(plan.rowCount() == 6);
}
