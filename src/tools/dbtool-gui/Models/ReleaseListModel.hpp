// SPDX-License-Identifier: Apache-2.0
//
// `ReleaseListModel` exposes the per-release summary used by the right-pane
// target picker and the left-pane "Releases" card: version, highest timestamp,
// total migration count, and an aggregate status string matching the
// `dbtool releases` output (applied / partial / pending / empty).

#pragma once

#include <cstdint>
#include <vector>

#include <QtCore/QAbstractListModel>
#include <QtQmlIntegration/QtQmlIntegration>

namespace Lightweight
{
namespace SqlMigration
{
    class MigrationManager;
}
} // namespace Lightweight

namespace DbtoolGui
{

struct ReleaseRow
{
    QString version;
    uint64_t highestTimestamp;
    int migrationCount;
    /// "applied" | "partial" | "pending" | "empty".
    QString status;
};

class ReleaseListModel: public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
  public:
    enum Roles : int
    {
        VersionRole = Qt::UserRole + 1,
        HighestTimestampRole,
        MigrationCountRole,
        StatusRole,
    };
    Q_ENUM(Roles)

    explicit ReleaseListModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(QModelIndex const& parent = {}) const override;
    [[nodiscard]] QVariant data(QModelIndex const& index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    void Refresh(Lightweight::SqlMigration::MigrationManager const& manager);

  private:
    std::vector<ReleaseRow> _rows;
};

} // namespace DbtoolGui
