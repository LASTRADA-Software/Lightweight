// SPDX-License-Identifier: Apache-2.0

#include "MigrationListModel.hpp"

#include <utility>

#include <QtCore/QByteArray>

namespace DbtoolGui
{

MigrationListModel::MigrationListModel(QObject* parent):
    QAbstractListModel(parent)
{
}

int MigrationListModel::rowCount(QModelIndex const& parent) const
{
    if (parent.isValid())
        return 0;
    return static_cast<int>(_rows.size());
}

QVariant MigrationListModel::data(QModelIndex const& index, int role) const
{
    auto const row = index.row();
    if (row < 0 || row >= static_cast<int>(_rows.size()))
        return {};
    auto const& r = _rows[static_cast<size_t>(row)];

    switch (role)
    {
        case Qt::DisplayRole:
        case TitleRole:
            return r.title;
        case TimestampRole:
            return QString::number(r.timestamp);
        case StatusRole:
            return r.status;
        case ReleaseVersionRole:
            return r.releaseVersion;
        case ChecksumMismatchRole:
            return r.checksumMismatch;
        case SelectedRole:
            return r.selected;
        default:
            return {};
    }
}

bool MigrationListModel::setData(QModelIndex const& index, QVariant const& value, int role)
{
    auto const row = index.row();
    if (row < 0 || row >= static_cast<int>(_rows.size()))
        return false;

    if (role != SelectedRole)
        return false;

    auto& r = _rows[static_cast<size_t>(row)];
    // Only pending rows are meaningful to select — already-applied rows would
    // produce a no-op in the runner and would be confusing UX.
    if (r.status != QStringLiteral("pending"))
        return false;

    auto const next = value.toBool();
    if (r.selected == next)
        return true;
    r.selected = next;
    emit dataChanged(index, index, { SelectedRole });
    emit selectionChanged();
    return true;
}

Qt::ItemFlags MigrationListModel::flags(QModelIndex const& index) const
{
    auto const base = QAbstractListModel::flags(index);
    auto const row = index.row();
    if (row < 0 || row >= static_cast<int>(_rows.size()))
        return base;
    // Mark pending rows as user-checkable so QML's `Qt.Checked` semantics work
    // if a delegate binds against the flag set. The explicit `setData` path
    // is what the current UI uses, but this keeps third-party delegates sane.
    if (_rows[static_cast<size_t>(row)].status == QStringLiteral("pending"))
        return base | Qt::ItemIsUserCheckable;
    return base;
}

QHash<int, QByteArray> MigrationListModel::roleNames() const
{
    return {
        { TimestampRole, "timestamp" },
        { TitleRole, "title" },
        { StatusRole, "status" },
        { ReleaseVersionRole, "releaseVersion" },
        { ChecksumMismatchRole, "checksumMismatch" },
        { SelectedRole, "selected" },
    };
}

void MigrationListModel::SelectAllPending(bool selected)
{
    if (_rows.empty())
        return;
    bool changed = false;
    for (auto& r: _rows)
    {
        if (r.status == QStringLiteral("pending") && r.selected != selected)
        {
            r.selected = selected;
            changed = true;
        }
    }
    if (!changed)
        return;
    emit dataChanged(index(0), index(static_cast<int>(_rows.size()) - 1), { SelectedRole });
    emit selectionChanged();
}

QStringList MigrationListModel::SelectedTimestamps() const
{
    QStringList out;
    for (auto const& r: _rows)
        if (r.selected && r.status == QStringLiteral("pending"))
            out.push_back(QString::number(r.timestamp));
    return out;
}

int MigrationListModel::SelectedCount() const noexcept
{
    int n = 0;
    for (auto const& r: _rows)
        if (r.selected && r.status == QStringLiteral("pending"))
            ++n;
    return n;
}

std::vector<uint64_t> MigrationListModel::AppliedTimestamps() const
{
    std::vector<uint64_t> out;
    out.reserve(_rows.size());
    for (auto const& r: _rows)
        if (r.status == QStringLiteral("applied") || r.status == QStringLiteral("checksum-mismatch"))
            out.push_back(r.timestamp);
    return out;
}

bool MigrationListModel::SetRowStatusByTimestamp(uint64_t timestamp, QString const& status)
{
    for (size_t i = 0; i < _rows.size(); ++i)
    {
        if (_rows[i].timestamp != timestamp)
            continue;
        if (_rows[i].status == status)
            return true;
        _rows[i].status = status;
        // Clear selection once the row leaves the pending state — nothing
        // downstream can act on a non-pending row's selection anyway.
        if (status != QStringLiteral("pending"))
            _rows[i].selected = false;
        auto const qidx = index(static_cast<int>(i));
        emit dataChanged(qidx, qidx, { StatusRole, SelectedRole });
        emit selectionChanged();
        return true;
    }
    return false;
}

void MigrationListModel::ReplaceRows(std::vector<MigrationRow> rows)
{
    beginResetModel();
    _rows = std::move(rows);
    endResetModel();
}

} // namespace DbtoolGui
