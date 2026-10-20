// SPDX-License-Identifier: Apache-2.0
//
// `MigrationFilterModel` filters `MigrationListModel` down to the rows the migration table shows: the
// filter tab and the search, and of a collapsed release only its first row, which carries the section
// header. A view over it creates delegates for shown rows only.

#pragma once

#include "MigrationListModel.hpp"

#include <functional>

#include <QtCore/QSet>
#include <QtCore/QSortFilterProxyModel>
#include <QtQmlIntegration/QtQmlIntegration>

namespace DbtoolGui
{

/// The rows of the migration table.
class MigrationFilterModel: public QSortFilterProxyModel
{
    Q_OBJECT
    QML_ELEMENT

    /// "all", "pending", "applied" (applied only) or "issues" (unknown or checksum-mismatch).
    Q_PROPERTY(QString filterTab READ filterTab WRITE setFilterTab NOTIFY filterTabChanged)
    /// Text a row's title (case-insensitively) or timestamp must contain; empty matches every row.
    Q_PROPERTY(QString search READ search WRITE setSearch NOTIFY searchChanged)
    /// Changes whenever a release is collapsed or expanded, or collapsing starts or stops applying.
    Q_PROPERTY(int expansionRevision READ expansionRevision NOTIFY expansionChanged)

  public:
    /// @param parent The QObject parent.
    explicit MigrationFilterModel(QObject* parent = nullptr);

    [[nodiscard]] QString filterTab() const;
    void setFilterTab(QString const& tab);
    [[nodiscard]] QString search() const;
    void setSearch(QString const& text);
    [[nodiscard]] int expansionRevision() const noexcept;

    /// @return Whether the rows of release @p version show. A filter tab or a search expands every
    ///         release, so matches are never hidden behind a header.
    Q_INVOKABLE bool isExpanded(QString const& version) const;

    /// Collapses an expanded release or expands a collapsed one; nothing while a filter or search applies.
    Q_INVOKABLE void toggleExpanded(QString const& version);

  signals:
    /// `filterTab` changed.
    void filterTabChanged();
    /// `search` changed.
    void searchChanged();
    /// `expansionRevision` changed.
    void expansionChanged();

  protected:
    [[nodiscard]] bool filterAcceptsRow(int sourceRow, QModelIndex const& sourceParent) const override;

  private:
    /// @return Whether a filter tab or a search applies, which expands every release.
    [[nodiscard]] bool Filtering() const;

    /// Runs @p change, which alters which rows show, and re-filters the rows.
    void ChangeRowFilter(std::function<void()> const& change);

    QString _filterTab = QStringLiteral("all");
    QString _search;
    QSet<QString> _collapsed;
    int _expansionRevision = 0;
};

} // namespace DbtoolGui
