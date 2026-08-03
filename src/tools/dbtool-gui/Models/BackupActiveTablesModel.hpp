// SPDX-License-Identifier: Apache-2.0
//
// A QSortFilterProxyModel that surfaces only the "active" tables of a
// BackupTableListModel — those in the running, error, or warning state. The
// detail panel binds its live list to an instance of this so the (potentially
// hundreds of) done and queued rows never reach the view: filtering happens in
// C++, re-evaluated automatically on every dataChanged, rather than through
// fragile zero-height delegate collapsing in QML.
//
// One instance lives in the QML panel and is re-pointed (setSourceModel) at
// whichever profile's table model is currently selected.

#pragma once

#include <QtCore/QSortFilterProxyModel>
#include <QtQmlIntegration/QtQmlIntegration>

namespace DbtoolGui
{

/// Proxy exposing only running/error/warning rows of a BackupTableListModel.
/// Creatable from QML so the detail panel can own one and swap its source.
class BackupActiveTablesModel: public QSortFilterProxyModel
{
    Q_OBJECT
    QML_ELEMENT

  public:
    explicit BackupActiveTablesModel(QObject* parent = nullptr):
        QSortFilterProxyModel(parent)
    {
    }

  protected:
    /// Accepts a source row only when its StateRole is one of the active
    /// states. The row's state is read through the source model's StateRole so
    /// this proxy stays decoupled from the concrete row storage.
    /// @param sourceRow Row index in the source model.
    /// @param sourceParent Always the invalid root for a flat list model.
    /// @return True to keep the row, false to hide it.
    [[nodiscard]] bool filterAcceptsRow(int sourceRow, QModelIndex const& sourceParent) const override;
};

} // namespace DbtoolGui
