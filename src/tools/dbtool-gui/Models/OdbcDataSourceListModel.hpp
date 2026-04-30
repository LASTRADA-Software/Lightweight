// SPDX-License-Identifier: Apache-2.0
//
// Adapter around `Lightweight::Odbc::EnumerateDataSources` so the QML DSN
// picker in the connection panel can bind to a model instead of calling into
// C++ imperatively. The enumeration itself is synchronous; the owning
// controller is responsible for kicking `Refresh()` off a worker thread when
// the DSN list is expected to be large.

#pragma once

#include <Lightweight/Odbc/DataSourceEnumerator.hpp>

#include <vector>

#include <QtCore/QAbstractListModel>
#include <QtQmlIntegration/QtQmlIntegration>

namespace DbtoolGui
{

class OdbcDataSourceListModel: public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
  public:
    enum Roles : int
    {
        NameRole = Qt::UserRole + 1,
        DescriptionRole,
        ScopeRole,
    };
    Q_ENUM(Roles)

    explicit OdbcDataSourceListModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(QModelIndex const& parent = {}) const override;
    [[nodiscard]] QVariant data(QModelIndex const& index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    /// Replaces the backing snapshot. Callers typically run
    /// `EnumerateDataSources()` on a worker and post the result back via
    /// `QMetaObject::invokeMethod` to this call.
    void Replace(std::vector<Lightweight::Odbc::DataSourceInfo> sources);

  private:
    std::vector<Lightweight::Odbc::DataSourceInfo> _sources;
};

} // namespace DbtoolGui
