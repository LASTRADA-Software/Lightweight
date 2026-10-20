// SPDX-License-Identifier: Apache-2.0

#include "ReleaseListModel.hpp"

#include <utility>

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

void ReleaseListModel::ReplaceRows(std::vector<ReleaseRow> rows)
{
    beginResetModel();
    _rows = std::move(rows);
    endResetModel();
}

} // namespace DbtoolGui
