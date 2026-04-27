// SPDX-License-Identifier: Apache-2.0
//
// `MigrationListModel` exposes the union of `MigrationManager`'s registered
// migrations and the applied-timestamp set stored in the database so QML
// consumers can render a single timeline where each row knows its own status
// (applied / pending / unknown / checksum-mismatch) without cross-referencing
// multiple collections.
//
// The model is a snapshot: rows change only when `Refresh()` is called. That
// mirrors the CLI (`dbtool status`) behaviour and keeps the model cheap even
// when the migration set is large.

#pragma once

#include <QtCore/QAbstractListModel>
#include <QtQmlIntegration/QtQmlIntegration>

#include <cstdint>
#include <string>
#include <vector>

// Forward declaration: see MigrationRunner.hpp for why the full include is
// kept out of moc-parsed headers.
namespace Lightweight
{
namespace SqlMigration
{
class MigrationManager;
}
} // namespace Lightweight

namespace DbtoolGui
{

/// In-memory row representation — one entry per migration on disk, plus any
/// "unknown" rows for applied migrations with no corresponding registered
/// migration (common after a plugin rename or migration-file deletion).
struct MigrationRow
{
    uint64_t timestamp;
    QString title;
    /// Status string matching the mockup: "applied", "pending", "unknown",
    /// "checksum-mismatch". QML binds this to a badge / colour directly.
    QString status;
    /// Release version containing this migration, or empty for the
    /// "unreleased" group (timestamp > every declared release).
    QString releaseVersion;
    /// `true` iff the stored checksum differs from the freshly computed one.
    bool checksumMismatch = false;
    /// User-selected state. Only meaningful for `pending` rows; the
    /// ActionsPanel uses it to build a custom apply set.
    bool selected = false;
};

class MigrationListModel: public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
  public:
    enum Roles : int
    {
        TimestampRole = Qt::UserRole + 1,
        TitleRole,
        StatusRole,
        ReleaseVersionRole,
        ChecksumMismatchRole,
        SelectedRole,
    };
    Q_ENUM(Roles)

    explicit MigrationListModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(QModelIndex const& parent = {}) const override;
    [[nodiscard]] QVariant data(QModelIndex const& index, int role = Qt::DisplayRole) const override;
    bool setData(QModelIndex const& index, QVariant const& value, int role) override;
    [[nodiscard]] Qt::ItemFlags flags(QModelIndex const& index) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    /// Rebuilds rows from a freshly-queried `MigrationManager`. Safe to call
    /// multiple times. Clears any existing selection — callers that want to
    /// preserve selection across refreshes must capture it beforehand.
    void Refresh(Lightweight::SqlMigration::MigrationManager const& manager);

    /// Degraded-mode refresh that only enumerates the plugin-declared
    /// migrations and skips `GetAppliedMigrationIds` / `VerifyChecksums`.
    /// Used when the DB has no `schema_migrations` table yet so the UI
    /// still shows the full migration plan instead of going blank.
    void RefreshPluginsOnly(Lightweight::SqlMigration::MigrationManager const& manager);

    /// Same shape as `RefreshPluginsOnly` but every row is staged with
    /// status `"unknown"`. Used whenever the connection's applied-set has
    /// not (yet) been consulted for the currently-loaded plugin set — e.g.
    /// on startup before the first Connect, or right after the user
    /// switches to a different profile / connection mode. Showing
    /// `"pending"` in those cases would be a lie: we haven't asked the new
    /// database yet, so we do not know whether each migration is applied.
    void RefreshAsUnknown(Lightweight::SqlMigration::MigrationManager const& manager);

    /// Overwrites the `status` field of the row whose `timestamp` matches,
    /// emits `dataChanged` for that row, and returns `true` if a row was
    /// updated. Used by the controller to surface live "running" /
    /// "applied" state from `MigrationRunner`'s signals without rebuilding
    /// the whole model between migrations.
    bool SetRowStatusByTimestamp(uint64_t timestamp, QString const& status);

    /// Sets the `selected` flag for every pending row in one shot and emits
    /// a single `dataChanged` for the whole range. Used by the "Select all /
    /// Deselect all" bulk controls.
    void SelectAllPending(bool selected);

    /// Timestamps of currently-selected pending rows, in row order. Empty if
    /// nothing is selected.
    [[nodiscard]] QStringList SelectedTimestamps() const;

    /// Number of selected pending rows.
    [[nodiscard]] int SelectedCount() const noexcept;

  signals:
    /// Emitted in addition to `dataChanged` so QML consumers that only care
    /// about "how many are selected" can connect a single slot.
    void selectionChanged();

  private:
    std::vector<MigrationRow> _rows;
};

} // namespace DbtoolGui
