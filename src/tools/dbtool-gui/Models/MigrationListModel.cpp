// SPDX-License-Identifier: Apache-2.0

#include "MigrationListModel.hpp"

#include <algorithm>
#include <ranges>
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

    // Only pending rows are meaningful to select — already-applied rows would
    // produce a no-op in the runner and would be confusing UX.
    if (_rows[static_cast<size_t>(row)].status != QStringLiteral("pending"))
        return false;

    SetSelectedAt(static_cast<size_t>(row), value.toBool() ? SelectionState::Selected : SelectionState::NotSelected);
    return true;
}

void MigrationListModel::SetSelectedAt(std::size_t row, SelectionState state)
{
    auto& r = _rows[row];
    auto const next = state == SelectionState::Selected;
    if (r.selected == next)
        return;
    r.selected = next;
    _selectedCount += next ? 1 : -1;
    auto const qidx = index(static_cast<int>(row));
    emit dataChanged(qidx, qidx, { SelectedRole });
    emit selectionChanged();
}

bool MigrationListModel::SetSelectedByTimestamp(std::uint64_t timestamp, SelectionState state)
{
    auto const found = _rowByTimestamp.constFind(timestamp);
    if (found == _rowByTimestamp.cend())
        return false;
    auto const row = static_cast<std::size_t>(*found);
    if (_rows[row].status != QStringLiteral("pending"))
        return false;
    SetSelectedAt(row, state);
    return true;
}

std::optional<int> MigrationListModel::RowOfTimestamp(std::uint64_t timestamp) const
{
    auto const found = _rowByTimestamp.constFind(timestamp);
    return found == _rowByTimestamp.cend() ? std::nullopt : std::optional<int> { *found };
}

QString MigrationListModel::TitleByTimestamp(std::uint64_t timestamp) const
{
    auto const found = _rowByTimestamp.constFind(timestamp);
    return found == _rowByTimestamp.cend() ? QString {} : _rows[static_cast<std::size_t>(*found)].title;
}

StatusCounts MigrationListModel::Counts() const noexcept
{
    return _counts;
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
            _selectedCount += selected ? 1 : -1;
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
    return _selectedCount;
}

std::set<std::uint64_t> const& MigrationListModel::AppliedTimestamps() const noexcept
{
    return _appliedTimestamps;
}

ReleaseTally MigrationListModel::ReleaseTallyOf(QString const& version) const
{
    return _releaseTallies.value(version);
}

QVariantMap MigrationListModel::releaseTally(QString const& version) const
{
    auto const tally = ReleaseTallyOf(version);
    return {
        { QStringLiteral("present"), _releaseTallies.contains(version) },
        { QStringLiteral("total"), tally.total },
        { QStringLiteral("applied"), tally.applied },
        { QStringLiteral("newest"), tally.newest == 0 ? QString {} : QString::number(tally.newest) },
    };
}

int MigrationListModel::tallyRebuildCount() const noexcept
{
    return _tallyRebuildCount;
}

void MigrationListModel::Tally(MigrationRow const& row, TallyDirection direction)
{
    auto const sign = direction == TallyDirection::Add ? 1 : -1;
    auto const& status = row.status;
    auto const applied = status == QStringLiteral("applied") || status == QStringLiteral("checksum-mismatch");
    if (status != QStringLiteral("unknown"))
    {
        auto& release = _releaseTallies[row.releaseVersion];
        release.total += sign;
        if (applied)
            release.applied += sign;
    }
    if (applied)
    {
        if (direction == TallyDirection::Add)
            _appliedTimestamps.insert(row.timestamp);
        else
            _appliedTimestamps.erase(row.timestamp);
        (status == QStringLiteral("applied") ? _counts.applied : _counts.checksumMismatch) += sign;
    }
    else if (status == QStringLiteral("pending"))
    {
        _counts.pending += sign;
        if (row.releaseVersion.isEmpty())
            _counts.pendingUnreleased += sign;
        if (row.selected)
            _selectedCount += sign;
    }
    else if (status == QStringLiteral("unknown"))
        _counts.unknown += sign;
}

void MigrationListModel::RebuildIndex()
{
    _rowByTimestamp.clear();
    _rowByTimestamp.reserve(static_cast<qsizetype>(_rows.size()));
    _counts = {};
    _appliedTimestamps.clear();
    _selectedCount = 0;
    _releaseTallies.clear();
    for (auto const& [row, migration]: std::views::enumerate(_rows))
    {
        _rowByTimestamp.insert(migration.timestamp, static_cast<int>(row));
        Tally(migration, TallyDirection::Add);
        auto& newest = _releaseTallies[migration.releaseVersion].newest;
        newest = std::max(newest, migration.timestamp);
    }
    ++_tallyRebuildCount;
}

bool MigrationListModel::SetRowStatusByTimestamp(uint64_t timestamp, QString const& status)
{
    auto const found = _rowByTimestamp.constFind(timestamp);
    if (found == _rowByTimestamp.cend())
        return false;
    auto& row = _rows[static_cast<std::size_t>(*found)];
    if (row.status == status)
        return true;
    auto const wasSelected = row.selected;
    auto const releaseBefore = ReleaseTallyOf(row.releaseVersion);
    Tally(row, TallyDirection::Remove);
    row.status = status;
    // Clear selection once the row leaves the pending state — nothing
    // downstream can act on a non-pending row's selection anyway.
    if (status != QStringLiteral("pending"))
        row.selected = false;
    Tally(row, TallyDirection::Add);
    auto const qidx = index(*found);
    emit dataChanged(qidx, qidx, { StatusRole, SelectedRole });
    if (wasSelected != row.selected)
        emit selectionChanged();
    if (ReleaseTallyOf(row.releaseVersion) != releaseBefore)
        emit releaseTallyChanged(row.releaseVersion);
    return true;
}

void MigrationListModel::ReplaceRows(std::vector<MigrationRow> rows)
{
    beginResetModel();
    _rows = std::move(rows);
    RebuildIndex();
    endResetModel();
    emit tallyRebuilt();
}

} // namespace DbtoolGui
