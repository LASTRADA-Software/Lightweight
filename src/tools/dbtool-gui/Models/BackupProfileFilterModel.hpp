// SPDX-License-Identifier: Apache-2.0
//
// A QSortFilterProxyModel that surfaces only the BackupStatusListModel rows
// whose profile name contains a user-typed search string (case-insensitive
// substring). The Backups page binds its profile master-list to an instance of
// this so a large profile set (hundreds of entries) can be narrowed to the one
// the user wants without scrolling. Filtering happens in C++, re-evaluated
// automatically on every model change, rather than through fragile
// zero-height delegate collapsing in QML.
//
// One instance lives in BackupsPage.qml, its source set to the controller's
// status model and its `filterText` bound to the search field.

#pragma once

#include <QtCore/QSortFilterProxyModel>
#include <QtCore/QString>
#include <QtQmlIntegration/QtQmlIntegration>

namespace DbtoolGui
{

/// Proxy exposing only the profile rows whose name matches `filterText`
/// (case-insensitive substring; empty text matches everything). Creatable from
/// QML so the Backups page can own one and point it at the status model.
class BackupProfileFilterModel: public QSortFilterProxyModel
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString filterText READ filterText WRITE setFilterText NOTIFY filterTextChanged)

  public:
    explicit BackupProfileFilterModel(QObject* parent = nullptr):
        QSortFilterProxyModel(parent)
    {
    }

    /// The current case-insensitive substring the profile names are matched
    /// against ("" matches all rows).
    [[nodiscard]] QString const& filterText() const noexcept
    {
        return _filterText;
    }

    /// Sets the search substring and re-runs the filter. Leading/trailing
    /// whitespace is trimmed so a stray space does not hide every row. A no-op
    /// (no signal, no re-filter) when the trimmed value is unchanged.
    /// @param text New search substring.
    void setFilterText(QString const& text);

  signals:
    void filterTextChanged();

  protected:
    /// Accepts a source row only when its NameRole contains `_filterText`
    /// (case-insensitive). Read through the source model's NameRole so this
    /// proxy stays decoupled from the concrete row storage.
    /// @param sourceRow Row index in the source model.
    /// @param sourceParent Always the invalid root for a flat list model.
    /// @return True to keep the row, false to hide it.
    [[nodiscard]] bool filterAcceptsRow(int sourceRow, QModelIndex const& sourceParent) const override;

  private:
    QString _filterText;
};

} // namespace DbtoolGui
