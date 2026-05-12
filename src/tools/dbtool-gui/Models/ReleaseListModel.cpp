// SPDX-License-Identifier: Apache-2.0

#include "ReleaseListModel.hpp"

#include <Lightweight/SqlMigration.hpp>

#include <algorithm>
#include <ranges>
#include <unordered_set>

#include <QtCore/QByteArray>

namespace DbtoolGui
{

ReleaseListModel::ReleaseListModel(QObject* parent):
    QAbstractListModel(parent)
{
}

int ReleaseListModel::rowCount(QModelIndex const& parent) const
{
    if (parent.isValid())
        return 0;
    return static_cast<int>(_rows.size());
}

QVariant ReleaseListModel::data(QModelIndex const& index, int role) const
{
    auto const row = index.row();
    if (row < 0 || row >= static_cast<int>(_rows.size()))
        return {};
    auto const& r = _rows[static_cast<size_t>(row)];

    switch (role)
    {
        case Qt::DisplayRole:
        case VersionRole:
            return r.version;
        case HighestTimestampRole:
            return QString::number(r.highestTimestamp);
        case MigrationCountRole:
            return r.migrationCount;
        case StatusRole:
            return r.status;
        default:
            return {};
    }
}

QHash<int, QByteArray> ReleaseListModel::roleNames() const
{
    return {
        { VersionRole, "version" },
        { HighestTimestampRole, "highestTimestamp" },
        { MigrationCountRole, "migrationCount" },
        { StatusRole, "status" },
    };
}

void ReleaseListModel::Refresh(Lightweight::SqlMigration::MigrationManager const& manager)
{
    using namespace Lightweight::SqlMigration;

    // Every throwing call (GetAppliedMigrationIds in particular — which
    // blows up on an unmanaged database) happens before the reset block
    // so we never leave the model in a half-reset state. See the same
    // comment in MigrationListModel.cpp.
    auto const& releases = manager.GetAllReleases();

    std::vector<ReleaseRow> staged;
    std::unordered_set<uint64_t> appliedSet;

    // Try to fetch the applied-ids set. If the table doesn't exist yet we
    // fall through with an empty set, which is the right mental model for
    // an unmanaged database: every release's status becomes "pending".
    try
    {
        auto const applied = manager.GetAppliedMigrationIds();
        appliedSet.reserve(applied.size());
        for (auto const& ts: applied)
            appliedSet.insert(ts.value);
    }
    catch (std::exception const&)
    {
        // Intentional: leave appliedSet empty.
    }

    staged.reserve(releases.size());
    for (auto const& release: releases)
    {
        auto const migrations = manager.GetMigrationsForRelease(release.version);
        auto const isApplied = [&](auto const* m) {
            return appliedSet.contains(m->GetTimestamp().value);
        };

        QString status;
        if (migrations.empty())
            status = QStringLiteral("empty");
        else if (std::ranges::all_of(migrations, isApplied))
            status = QStringLiteral("applied");
        else if (std::ranges::any_of(migrations, isApplied))
            status = QStringLiteral("partial");
        else
            status = QStringLiteral("pending");

        staged.push_back(ReleaseRow {
            .version = QString::fromStdString(release.version),
            .highestTimestamp = release.highestTimestamp.value,
            .migrationCount = static_cast<int>(migrations.size()),
            .status = status,
        });
    }

    // Sort newest release on top: the plugin registers releases in
    // declaration order (typically oldest first), which surfaces the
    // oldest release at the top of the sidebar — the opposite of what a
    // deploy-focused UI wants. Sorting by `highestTimestamp` keeps the
    // "whatever is closest to HEAD" release where the eye lands first.
    std::ranges::sort(staged,
                      [](ReleaseRow const& a, ReleaseRow const& b) { return a.highestTimestamp > b.highestTimestamp; });

    beginResetModel();
    _rows = std::move(staged);
    endResetModel();
}

} // namespace DbtoolGui
