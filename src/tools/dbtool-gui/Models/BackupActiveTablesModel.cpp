// SPDX-License-Identifier: Apache-2.0

#include "BackupActiveTablesModel.hpp"
#include "BackupTableListModel.hpp"

namespace DbtoolGui
{

bool BackupActiveTablesModel::filterAcceptsRow(int sourceRow, QModelIndex const& sourceParent) const
{
    auto const* src = sourceModel();
    if (!src)
        return false;
    auto const idx = src->index(sourceRow, 0, sourceParent);
    auto const state = src->data(idx, BackupTableListModel::StateRole).toString();
    return state == QStringLiteral("running") || state == QStringLiteral("error") || state == QStringLiteral("warning");
}

} // namespace DbtoolGui
