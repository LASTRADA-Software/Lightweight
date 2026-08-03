// SPDX-License-Identifier: Apache-2.0

#include "BackupStatusListModel.hpp"

namespace DbtoolGui
{

void BackupStatusListModel::resetProfiles(QStringList const& names)
{
    beginResetModel();
    // Qt only frees a parented child when the PARENT is destroyed; this
    // model outlives many reloads, so the previous rows' table models must
    // be released explicitly here or they leak as unreachable orphans.
    for (auto const& row: _rows)
        if (row.tables)
            row.tables->deleteLater();
    _rows.clear();
    _rows.reserve(names.size());
    for (auto const& name: names)
        _rows.push_back(Row { .name = name, .tables = new BackupTableListModel(this) });
    endResetModel();
}

BackupTableListModel* BackupStatusListModel::tablesFor(QString const& name) const
{
    auto const row = RowIndexOf(name);
    return row < 0 ? nullptr : _rows[row].tables;
}

int BackupStatusListModel::RowIndexOf(QString const& name) const
{
    for (qsizetype i = 0; i < _rows.size(); ++i)
        if (_rows[i].name == name)
            return static_cast<int>(i);
    return -1;
}

void BackupStatusListModel::setArchiveStatus(QString const& name, bool exists, qulonglong sizeBytes, QDateTime const& mtime)
{
    auto const row = RowIndexOf(name);
    if (row < 0)
        return;
    _rows[row].archiveExists = exists;
    _rows[row].archiveSize = sizeBytes;
    _rows[row].archiveMtime = mtime;
    emit dataChanged(index(row, 0), index(row, 0), { ArchiveExistsRole, ArchiveSizeRole, ArchiveMtimeRole });
}

void BackupStatusListModel::setRunState(QString const& name, QString const& state, QString const& errorText)
{
    auto const row = RowIndexOf(name);
    if (row < 0)
        return;
    _rows[row].runState = state;
    _rows[row].errorText = errorText;
    emit dataChanged(index(row, 0), index(row, 0), { RunStateRole, ErrorTextRole });
}

int BackupStatusListModel::rowCount(QModelIndex const& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(_rows.size());
}

QVariant BackupStatusListModel::data(QModelIndex const& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= _rows.size())
        return {};
    auto const& row = _rows[index.row()];
    switch (role)
    {
        case NameRole:
            return row.name;
        case ArchiveExistsRole:
            return row.archiveExists;
        case ArchiveSizeRole:
            return row.archiveSize;
        case ArchiveMtimeRole:
            return row.archiveMtime;
        case RunStateRole:
            return row.runState;
        case ErrorTextRole:
            return row.errorText;
        case TablesRole:
            return QVariant::fromValue(row.tables);
        default:
            return {};
    }
}

QHash<int, QByteArray> BackupStatusListModel::roleNames() const
{
    return {
        { NameRole, "name" },
        { ArchiveExistsRole, "archiveExists" },
        { ArchiveSizeRole, "archiveSize" },
        { ArchiveMtimeRole, "archiveMtime" },
        { RunStateRole, "runState" },
        { ErrorTextRole, "errorText" },
        { TablesRole, "tables" },
    };
}

} // namespace DbtoolGui
