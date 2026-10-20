// SPDX-License-Identifier: Apache-2.0
//
// `MigrationListModel` exposes the union of `MigrationManager`'s registered
// migrations and the applied-timestamp set stored in the database so QML
// consumers can render a single timeline where each row knows its own status
// (applied / pending / unknown / checksum-mismatch) without cross-referencing
// multiple collections.
//
// The model is a snapshot: rows change only when `ReplaceRows()` is called. That
// mirrors the CLI (`dbtool status`) behaviour and keeps the model cheap even
// when the migration set is large.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <QtCore/QAbstractListModel>
#include <QtQmlIntegration/QtQmlIntegration>

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

    /// Replaces every row, e.g. with those of a `MigrationSnapshot`. Clears any existing selection.
    /// @param rows The new rows, newest first.
    void ReplaceRows(std::vector<MigrationRow> rows);

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

    /// Timestamps of rows whose status reflects the migration as applied —
    /// `"applied"` and `"checksum-mismatch"` (the latter is "applied with a
    /// stored checksum that disagrees with the freshly-computed one", which
    /// is still applied as far as the DB is concerned). Order matches the
    /// row order produced by `Refresh()`. Used by UI-thread accessors that
    /// must NOT issue SQL while a worker-thread migration run is in flight
    /// (a re-query on the shared `SqlConnection` would race the worker and
    /// surface as HY000 "Connection is busy"). See
    /// `AppController::currentReleaseLabel`.
    [[nodiscard]] std::vector<uint64_t> AppliedTimestamps() const;

  signals:
    /// Emitted in addition to `dataChanged` so QML consumers that only care
    /// about "how many are selected" can connect a single slot.
    void selectionChanged();

  private:
    std::vector<MigrationRow> _rows;
};

} // namespace DbtoolGui
