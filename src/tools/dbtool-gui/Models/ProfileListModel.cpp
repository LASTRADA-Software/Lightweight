// SPDX-License-Identifier: Apache-2.0

#include "ProfileListModel.hpp"

#include <QtCore/QByteArray>

namespace DbtoolGui
{

ProfileListModel::ProfileListModel(QObject* parent):
    QAbstractListModel(parent)
{
}

int ProfileListModel::rowCount(QModelIndex const& parent) const
{
    if (parent.isValid())
        return 0;
    return static_cast<int>(_profiles.size());
}

QVariant ProfileListModel::data(QModelIndex const& index, int role) const
{
    auto const row = index.row();
    if (row < 0 || row >= static_cast<int>(_profiles.size()))
        return {};
    auto const& profile = _profiles[static_cast<size_t>(row)];

    switch (role)
    {
        case Qt::DisplayRole:
        case NameRole:
            return QString::fromStdString(profile.name);
        case PluginsDirRole:
            return QString::fromStdString(profile.pluginsDir.string());
        case SchemaRole:
            return QString::fromStdString(profile.schema);
        case ConnectionStringRole:
            return QString::fromStdString(profile.connectionString);
        case DsnRole:
            return QString::fromStdString(profile.dsn);
        case UidRole:
            return QString::fromStdString(profile.uid);
        case SecretRefRole:
            return QString::fromStdString(profile.secretRef);
        case IsDefaultRole:
            return QString::fromStdString(profile.name) == _defaultProfile;
        default:
            return {};
    }
}

QHash<int, QByteArray> ProfileListModel::roleNames() const
{
    return {
        { NameRole, "name" },
        { PluginsDirRole, "pluginsDir" },
        { SchemaRole, "schema" },
        { ConnectionStringRole, "connectionString" },
        { DsnRole, "dsn" },
        { UidRole, "uid" },
        { SecretRefRole, "secretRef" },
        { IsDefaultRole, "isDefault" },
    };
}

void ProfileListModel::ReplaceFrom(Lightweight::Config::ProfileStore const& store)
{
    beginResetModel();
    _profiles.assign(store.Profiles().begin(), store.Profiles().end());
    _defaultProfile = QString::fromStdString(store.DefaultProfileName());
    endResetModel();
}

Lightweight::Config::Profile const* ProfileListModel::Find(QString const& name) const
{
    auto const nameStd = name.toStdString();
    for (auto const& p: _profiles)
        if (p.name == nameStd)
            return &p;
    return nullptr;
}

} // namespace DbtoolGui
