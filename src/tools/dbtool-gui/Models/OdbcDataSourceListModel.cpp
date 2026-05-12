// SPDX-License-Identifier: Apache-2.0

#include "OdbcDataSourceListModel.hpp"

#include <QtCore/QByteArray>

namespace DbtoolGui
{

OdbcDataSourceListModel::OdbcDataSourceListModel(QObject* parent):
    QAbstractListModel(parent)
{
}

int OdbcDataSourceListModel::rowCount(QModelIndex const& parent) const
{
    if (parent.isValid())
        return 0;
    return static_cast<int>(_sources.size());
}

QVariant OdbcDataSourceListModel::data(QModelIndex const& index, int role) const
{
    auto const row = index.row();
    if (row < 0 || row >= static_cast<int>(_sources.size()))
        return {};
    auto const& source = _sources[static_cast<size_t>(row)];

    switch (role)
    {
        case Qt::DisplayRole:
        case NameRole:
            return QString::fromStdString(source.name);
        case DescriptionRole:
            return QString::fromStdString(source.description);
        case ScopeRole:
            return source.scope == Lightweight::Odbc::DataSourceInfo::Scope::User ? QStringLiteral("user")
                                                                                  : QStringLiteral("system");
        default:
            return {};
    }
}

QHash<int, QByteArray> OdbcDataSourceListModel::roleNames() const
{
    return {
        { NameRole, "name" },
        { DescriptionRole, "description" },
        { ScopeRole, "scope" },
    };
}

void OdbcDataSourceListModel::Replace(std::vector<Lightweight::Odbc::DataSourceInfo> sources)
{
    beginResetModel();
    _sources = std::move(sources);
    endResetModel();
}

} // namespace DbtoolGui
