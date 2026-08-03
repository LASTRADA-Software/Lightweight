// SPDX-License-Identifier: Apache-2.0

#include "BackupProfileFilterModel.hpp"
#include "BackupStatusListModel.hpp"

namespace DbtoolGui
{

void BackupProfileFilterModel::setFilterText(QString const& text)
{
    auto const trimmed = text.trimmed();
    if (trimmed == _filterText)
        return;
    _filterText = trimmed;
    emit filterTextChanged();
    invalidateFilter();
}

bool BackupProfileFilterModel::filterAcceptsRow(int sourceRow, QModelIndex const& sourceParent) const
{
    if (_filterText.isEmpty())
        return true;
    auto const* src = sourceModel();
    if (!src)
        return false;
    auto const idx = src->index(sourceRow, 0, sourceParent);
    auto const name = src->data(idx, BackupStatusListModel::NameRole).toString();
    return name.contains(_filterText, Qt::CaseInsensitive);
}

} // namespace DbtoolGui
