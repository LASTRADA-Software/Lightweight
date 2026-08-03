// SPDX-License-Identifier: Apache-2.0
//
// One row per configured profile: managed-archive presence (exists / size
// / mtime) plus the live per-run state driven by ManagedBackupController.

#pragma once

#include "BackupTableListModel.hpp"

#include <cstdint>

#include <QtCore/QAbstractListModel>
#include <QtCore/QDateTime>
#include <QtCore/QList>
#include <QtCore/QString>
#include <QtQmlIntegration/QtQmlIntegration>

namespace DbtoolGui
{

class BackupStatusListModel: public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("owned by ManagedBackupController")

  public:
    enum Role : std::uint16_t
    {
        NameRole = Qt::UserRole + 1,
        ArchiveExistsRole,
        ArchiveSizeRole,
        ArchiveMtimeRole,
        RunStateRole,
        ErrorTextRole,
        TablesRole,
    };
    Q_ENUM(Role)

    explicit BackupStatusListModel(QObject* parent = nullptr):
        QAbstractListModel(parent)
    {
    }

    /// Replaces all rows with one `idle` row per profile name.
    /// @param names Profile names in store order.
    void resetProfiles(QStringList const& names);

    /// Updates the on-disk archive columns of the row named `name`.
    /// Unknown names are ignored.
    /// @param name Profile name.
    /// @param exists Whether `<profile>.zip` is present.
    /// @param sizeBytes Archive size in bytes (0 when missing).
    /// @param mtime Archive modification time (invalid when missing).
    void setArchiveStatus(QString const& name, bool exists, qulonglong sizeBytes, QDateTime const& mtime);

    /// Updates the run-state column (`idle|queued|running|ok|failed`) and
    /// its error text. Unknown names are ignored.
    /// @param name Profile name.
    /// @param state New state string.
    /// @param errorText Failure detail; cleared when empty.
    void setRunState(QString const& name, QString const& state, QString const& errorText = {});

    /// Returns the per-profile table-progress model for `name`, or nullptr
    /// when `name` is not a known profile.
    /// @param name Profile name.
    /// @return Owned BackupTableListModel*, or nullptr.
    [[nodiscard]] BackupTableListModel* tablesFor(QString const& name) const;

    [[nodiscard]] int rowCount(QModelIndex const& parent = {}) const override;
    [[nodiscard]] QVariant data(QModelIndex const& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

  private:
    struct Row
    {
        QString name;
        bool archiveExists = false;
        qulonglong archiveSize = 0;
        QDateTime archiveMtime;
        QString runState = QStringLiteral("idle");
        QString errorText;
        BackupTableListModel* tables = nullptr; // parented to this model
    };

    [[nodiscard]] int RowIndexOf(QString const& name) const;

    QList<Row> _rows;
};

} // namespace DbtoolGui
