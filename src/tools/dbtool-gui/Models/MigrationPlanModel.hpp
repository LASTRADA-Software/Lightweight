// SPDX-License-Identifier: Apache-2.0
//
// `MigrationPlanModel` filters `MigrationListModel` down to the rows a run plan lists: the pending ones,
// and in `Run` scope also those the current run touched. A view over it creates delegates for listed
// rows only, and a row's status change costs one row's re-filtering, not a pass over every row.

#pragma once

#include "MigrationListModel.hpp"

#include <cstdint>
#include <functional>

#include <QtCore/QSet>
#include <QtCore/QSortFilterProxyModel>
#include <QtQmlIntegration/QtQmlIntegration>

namespace DbtoolGui
{

/// The rows of a migration run plan.
class MigrationPlanModel: public QSortFilterProxyModel
{
    Q_OBJECT
    QML_ELEMENT

    /// Which rows are listed.
    Q_PROPERTY(Scope scope READ scope WRITE setScope NOTIFY scopeChanged)

  public:
    /// Which rows a plan lists.
    enum class Scope : int
    {
        /// The pending rows.
        Pending,
        /// The pending and running rows, and every row `touch` named.
        Run,
    };
    Q_ENUM(Scope)

    /// @param parent The QObject parent.
    explicit MigrationPlanModel(QObject* parent = nullptr);

    /// @return Which rows are listed.
    [[nodiscard]] Scope scope() const noexcept;

    /// Lists the rows @p scope names.
    void setScope(Scope scope);

    /// Keeps the row with @p timestamp listed in `Run` scope whatever its status, e.g. once the run applied it.
    Q_INVOKABLE void touch(QString const& timestamp);

    /// Forgets every row `touch` named.
    Q_INVOKABLE void clearTouched();

  signals:
    /// `scope` changed.
    void scopeChanged();

  protected:
    [[nodiscard]] bool filterAcceptsRow(int sourceRow, QModelIndex const& sourceParent) const override;

  private:
    /// Runs @p change, which alters which rows are listed, and re-filters the rows.
    void ChangeRowFilter(std::function<void()> const& change);

    /// @return Whether a row with @p status and @p timestamp is listed.
    [[nodiscard]] bool Lists(QString const& status, QString const& timestamp) const;

    Scope _scope = Scope::Pending;
    QSet<QString> _touched;
};

} // namespace DbtoolGui
