// SPDX-License-Identifier: Apache-2.0

#include "BackupTableListModel.hpp"

namespace DbtoolGui
{

int BackupTableListModel::RowIndexOf(QString const& table) const
{
    for (qsizetype i = 0; i < _rows.size(); ++i)
        if (_rows[i].table == table)
            return static_cast<int>(i);
    return -1;
}

void BackupTableListModel::applyProgress(
    QString const& table, quint64 current, qint64 total, QString const& state, QString const& message)
{
    if (auto const row = RowIndexOf(table); row >= 0)
    {
        _rows[row].current = current;
        _rows[row].total = total;
        _rows[row].state = state;
        _rows[row].message = message;
        emit dataChanged(index(row, 0), index(row, 0), { CurrentRowsRole, TotalRowsRole, StateRole, MessageRole });
        RecomputeSummary();
        return;
    }
    auto const at = static_cast<int>(_rows.size());
    beginInsertRows(QModelIndex {}, at, at);
    _rows.push_back(Row { .table = table, .current = current, .total = total, .state = state, .message = message });
    endInsertRows();
    RecomputeSummary();
}

void BackupTableListModel::clearTables()
{
    beginResetModel();
    _rows.clear();
    // Drop the previous run's announced total too. Leaving it set would show the
    // old denominator against the new run's rows ("0 / 700" before the new run
    // has announced anything), and if the new run covers fewer tables the stale
    // larger value would persist for the whole run.
    _announcedTotal = 0;
    endResetModel();
    RecomputeSummary();
}

void BackupTableListModel::setTotalTables(int totalTables)
{
    auto const sanitised = totalTables > 0 ? totalTables : 0;
    if (_announcedTotal == sanitised)
        return;
    _announcedTotal = sanitised;
    // totalCount() is derived from this, so the summary header must re-read.
    emit summaryChanged();
}

void BackupTableListModel::RecomputeSummary()
{
    _doneCount = _runningCount = _queuedCount = _errorCount = _warningCount = 0;
    for (auto const& row: _rows)
    {
        if (row.state == QStringLiteral("done"))
            ++_doneCount;
        else if (row.state == QStringLiteral("running"))
            ++_runningCount;
        else if (row.state == QStringLiteral("error"))
            ++_errorCount;
        else if (row.state == QStringLiteral("warning"))
            ++_warningCount;
        else
            ++_queuedCount; // "queued" and any unrecognised state
    }
    // Every caller either inserts a row or resets, so some summary field always
    // moves — notify unconditionally. (totalCount() prefers the announced total
    // set by setTotalTables(), which emits summaryChanged() in its own right.)
    emit summaryChanged();
}

int BackupTableListModel::rowCount(QModelIndex const& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(_rows.size());
}

QVariant BackupTableListModel::data(QModelIndex const& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= _rows.size())
        return {};
    auto const& row = _rows[index.row()];
    switch (role)
    {
        case TableNameRole:
            return row.table;
        case CurrentRowsRole:
            return row.current;
        case TotalRowsRole:
            return row.total;
        case StateRole:
            return row.state;
        case MessageRole:
            return row.message;
        default:
            return {};
    }
}

QHash<int, QByteArray> BackupTableListModel::roleNames() const
{
    return {
        { TableNameRole, "tableName" }, { CurrentRowsRole, "currentRows" }, { TotalRowsRole, "totalRows" },
        { StateRole, "state" },         { MessageRole, "message" },
    };
}

} // namespace DbtoolGui
