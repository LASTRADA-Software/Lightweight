// SPDX-License-Identifier: Apache-2.0

#include "MigrationPlanModel.hpp"

namespace DbtoolGui
{

MigrationPlanModel::MigrationPlanModel(QObject* parent):
    QSortFilterProxyModel(parent)
{
    // A status change re-filters only the changed row.
    setFilterRole(MigrationListModel::StatusRole);
}

MigrationPlanModel::Scope MigrationPlanModel::scope() const noexcept
{
    return _scope;
}

void MigrationPlanModel::setScope(Scope scope)
{
    if (_scope == scope)
        return;
    ChangeRowFilter([this, scope] { _scope = scope; });
    emit scopeChanged();
}

void MigrationPlanModel::touch(QString const& timestamp)
{
    if (_touched.contains(timestamp))
        return;
    // A run touches pending or running rows, which are listed already; only another row needs a re-filter.
    auto const* const migrations = qobject_cast<MigrationListModel const*>(sourceModel());
    auto const row = migrations ? migrations->RowOfTimestamp(timestamp.toULongLong()) : std::nullopt;
    if (_scope != Scope::Run || (row && filterAcceptsRow(*row, {})))
        _touched.insert(timestamp);
    else
        ChangeRowFilter([this, &timestamp] { _touched.insert(timestamp); });
}

void MigrationPlanModel::clearTouched()
{
    if (_touched.isEmpty())
        return;
    ChangeRowFilter([this] { _touched.clear(); });
}

void MigrationPlanModel::ChangeRowFilter(std::function<void()> const& change)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    beginFilterChange();
    change();
    endFilterChange(Direction::Rows);
#else
    change();
    invalidateRowsFilter();
#endif
}

bool MigrationPlanModel::Lists(QString const& status, QString const& timestamp) const
{
    if (status == QStringLiteral("pending"))
        return true;
    if (_scope != Scope::Run)
        return false;
    return status == QStringLiteral("running") || _touched.contains(timestamp);
}

bool MigrationPlanModel::filterAcceptsRow(int sourceRow, QModelIndex const& sourceParent) const
{
    auto const* const source = sourceModel();
    if (source == nullptr)
        return false;
    auto const index = source->index(sourceRow, 0, sourceParent);
    return Lists(index.data(MigrationListModel::StatusRole).toString(),
                 index.data(MigrationListModel::TimestampRole).toString());
}

} // namespace DbtoolGui
