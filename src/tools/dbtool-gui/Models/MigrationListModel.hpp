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
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <QtCore/QAbstractListModel>
#include <QtCore/QHash>
#include <QtCore/QVariantMap>
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

/// Whether a row is selected for a custom apply set.
enum class SelectionState : std::uint8_t
{
    Selected,
    NotSelected,
};

/// Number of rows per status, kept current as rows change.
struct StatusCounts
{
    /// Rows with status "applied".
    int applied = 0;
    /// Rows with status "pending".
    int pending = 0;
    /// Pending rows that belong to no release.
    int pendingUnreleased = 0;
    /// Rows with status "unknown".
    int unknown = 0;
    /// Rows with status "checksum-mismatch".
    int checksumMismatch = 0;
};

/// The rows of one release, kept current as rows change.
struct ReleaseTally
{
    /// Rows of the release that are not "unknown" (unknown rows belong to no declared migration set).
    int total = 0;
    /// Rows that are "applied" or "checksum-mismatch".
    int applied = 0;
    /// The newest timestamp of any row of the release; 0 when it has none.
    std::uint64_t newest = 0;

    bool operator==(ReleaseTally const&) const = default;
};

class MigrationListModel: public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT

    /// How often the counts and release tallies were rebuilt from every row (on `ReplaceRows`).
    Q_PROPERTY(int tallyRebuildCount READ tallyRebuildCount NOTIFY tallyRebuilt)
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
    /// the whole model between migrations. O(1); keeps `Counts()` and
    /// `AppliedTimestamps()` current.
    bool SetRowStatusByTimestamp(uint64_t timestamp, QString const& status);

    /// Selects or deselects the pending row with @p timestamp in O(1); other rows are left alone.
    /// @return Whether a pending row with that timestamp exists.
    bool SetSelectedByTimestamp(std::uint64_t timestamp, SelectionState state);

    /// @return The row with @p timestamp, in O(1); empty when there is none.
    [[nodiscard]] std::optional<int> RowOfTimestamp(std::uint64_t timestamp) const;

    /// @return The title of the row with @p timestamp; empty when there is none.
    [[nodiscard]] QString TitleByTimestamp(std::uint64_t timestamp) const;

    /// @return The number of rows per status, in O(1).
    [[nodiscard]] StatusCounts Counts() const noexcept;

    /// Sets the `selected` flag for every pending row in one shot and emits
    /// a single `dataChanged` for the whole range. Used by the "Select all /
    /// Deselect all" bulk controls.
    void SelectAllPending(bool selected);

    /// Timestamps of currently-selected pending rows, in row order. Empty if
    /// nothing is selected.
    [[nodiscard]] QStringList SelectedTimestamps() const;

    /// Number of selected pending rows, in O(1).
    [[nodiscard]] int SelectedCount() const noexcept;

    /// Timestamps of rows whose status reflects the migration as applied —
    /// `"applied"` and `"checksum-mismatch"` (applied, with a stored checksum
    /// that disagrees with the computed one). Kept current per row change, so
    /// UI-thread readers never query the database the worker is using.
    [[nodiscard]] std::set<std::uint64_t> const& AppliedTimestamps() const noexcept;

    /// @return The tally of the release @p version ("" is the unreleased group), in O(1).
    [[nodiscard]] ReleaseTally ReleaseTallyOf(QString const& version) const;

    /// @return The tally of the release @p version as `{ present, total, applied, newest }`: `present`
    ///         whether any row belongs to it, `newest` a timestamp string ("" when it has no rows).
    Q_INVOKABLE QVariantMap releaseTally(QString const& version) const;

    /// @return How often the tallies were rebuilt from every row.
    [[nodiscard]] int tallyRebuildCount() const noexcept;

  signals:
    /// Emitted in addition to `dataChanged` so QML consumers that only care
    /// about "how many are selected" can connect a single slot.
    void selectionChanged();

    /// The tally of the release @p version changed by a row's status change; a reset changes all.
    void releaseTallyChanged(QString version);

    /// The counts and release tallies were rebuilt from every row.
    void tallyRebuilt();

  private:
    /// Whether `Tally` adds a row's share or takes it away.
    enum class TallyDirection : std::uint8_t
    {
        Add,
        Remove,
    };

    /// Adds @p row's status to, or removes it from, the counts, its release's tally, the applied set
    /// and the selection count.
    void Tally(MigrationRow const& row, TallyDirection direction);
    /// Rebuilds the timestamp index, the counts, the release tallies, the applied set and the selection
    /// count from `_rows`.
    void RebuildIndex();
    /// Sets the selection of the pending row at @p row; emits the change when there is one.
    void SetSelectedAt(std::size_t row, SelectionState state);

    std::vector<MigrationRow> _rows;
    QHash<std::uint64_t, int> _rowByTimestamp;
    StatusCounts _counts;
    std::set<std::uint64_t> _appliedTimestamps;
    int _selectedCount = 0;
    QHash<QString, ReleaseTally> _releaseTallies;
    int _tallyRebuildCount = 0;
};

} // namespace DbtoolGui
