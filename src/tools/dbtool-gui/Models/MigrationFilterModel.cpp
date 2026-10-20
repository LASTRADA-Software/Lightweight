// SPDX-License-Identifier: Apache-2.0

#include "MigrationFilterModel.hpp"

namespace DbtoolGui
{

MigrationFilterModel::MigrationFilterModel(QObject* parent):
    QSortFilterProxyModel(parent)
{
    // A status change re-filters only the changed row.
    setFilterRole(MigrationListModel::StatusRole);
}

QString MigrationFilterModel::filterTab() const
{
    return _filterTab;
}

void MigrationFilterModel::setFilterTab(QString const& tab)
{
    if (_filterTab == tab)
        return;
    ChangeRowFilter([this, &tab] { _filterTab = tab; });
    emit filterTabChanged();
    ++_expansionRevision;
    emit expansionChanged();
}

QString MigrationFilterModel::search() const
{
    return _search;
}

void MigrationFilterModel::setSearch(QString const& text)
{
    if (_search == text)
        return;
    ChangeRowFilter([this, &text] { _search = text; });
    emit searchChanged();
    ++_expansionRevision;
    emit expansionChanged();
}

int MigrationFilterModel::expansionRevision() const noexcept
{
    return _expansionRevision;
}

bool MigrationFilterModel::Filtering() const
{
    return _filterTab != QStringLiteral("all") || !_search.isEmpty();
}

bool MigrationFilterModel::isExpanded(QString const& version) const
{
    return Filtering() || !_collapsed.contains(version);
}

void MigrationFilterModel::toggleExpanded(QString const& version)
{
    if (Filtering())
        return;
    ChangeRowFilter([this, &version] {
        if (!_collapsed.remove(version))
            _collapsed.insert(version);
    });
    ++_expansionRevision;
    emit expansionChanged();
}

void MigrationFilterModel::ChangeRowFilter(std::function<void()> const& change)
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

bool MigrationFilterModel::filterAcceptsRow(int sourceRow, QModelIndex const& sourceParent) const
{
    auto const* const source = sourceModel();
    if (source == nullptr)
        return false;
    auto const index = source->index(sourceRow, 0, sourceParent);
    auto const status = index.data(MigrationListModel::StatusRole).toString();
    if (_filterTab == QStringLiteral("pending") && status != QStringLiteral("pending"))
        return false;
    if (_filterTab == QStringLiteral("applied") && status != QStringLiteral("applied"))
        return false;
    if (_filterTab == QStringLiteral("issues") && status != QStringLiteral("unknown")
        && status != QStringLiteral("checksum-mismatch"))
        return false;
    if (!_search.isEmpty() && !index.data(MigrationListModel::TitleRole).toString().contains(_search, Qt::CaseInsensitive)
        && !index.data(MigrationListModel::TimestampRole).toString().contains(_search))
        return false;

    auto const version = index.data(MigrationListModel::ReleaseVersionRole).toString();
    if (Filtering() || !_collapsed.contains(version))
        return true;
    // A collapsed release keeps its first row, which carries the section header.
    return sourceRow == 0
           || source->index(sourceRow - 1, 0, sourceParent).data(MigrationListModel::ReleaseVersionRole).toString()
                  != version;
}

} // namespace DbtoolGui
