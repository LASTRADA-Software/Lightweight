// SPDX-License-Identifier: Apache-2.0
//
// One row per table processed inside a single profile's backup/restore run:
// live rows-processed progress plus per-table state, driven by
// ManagedBackupController's tableProgress signal on the GUI thread.

#pragma once

#include <algorithm>
#include <cstdint>

#include <QtCore/QAbstractListModel>
#include <QtCore/QList>
#include <QtCore/QString>
#include <QtQmlIntegration/QtQmlIntegration>

namespace DbtoolGui
{

/// GUI-thread-confined model of per-table progress for one profile. Rows are
/// keyed by table name: the first update for a table appends a row, later
/// updates find it by name and update in place, so the up-to-8 concurrent
/// table workers may report in any interleaving.
class BackupTableListModel: public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("owned by BackupStatusListModel")

    // Live tallies for the detail-panel summary header, so QML need not scan
    // every row to render "142 / 700 tables · 3 running · 1 error". Each is
    // recomputed on every applyProgress()/clearTables() and change-notified.
    Q_PROPERTY(int totalCount READ totalCount NOTIFY summaryChanged)
    Q_PROPERTY(int doneCount READ doneCount NOTIFY summaryChanged)
    Q_PROPERTY(int runningCount READ runningCount NOTIFY summaryChanged)
    Q_PROPERTY(int queuedCount READ queuedCount NOTIFY summaryChanged)
    Q_PROPERTY(int errorCount READ errorCount NOTIFY summaryChanged)
    Q_PROPERTY(int warningCount READ warningCount NOTIFY summaryChanged)

  public:
    enum Role : std::uint16_t
    {
        TableNameRole = Qt::UserRole + 1,
        CurrentRowsRole,
        TotalRowsRole,
        StateRole,
        MessageRole,
    };
    Q_ENUM(Role)

    explicit BackupTableListModel(QObject* parent = nullptr):
        QAbstractListModel(parent)
    {
    }

    /// @return Total number of tables this run will process.
    ///
    /// Prefers the count SqlBackup announces up front (after its schema scan /
    /// manifest read, via `setTotalTables`) over the number of rows created so
    /// far. Rows are created lazily on each table's first progress report, so
    /// `_rows.size()` climbs throughout the run — using it as the denominator
    /// made the panel read "12 / 12", then "12 / 47", then "12 / 700" on one
    /// run, i.e. the job appearing to grow rather than progress.
    ///
    /// Falls back to `_rows.size()` when no total has been announced (a run
    /// driven by a progress source that predates the hook), and takes the
    /// larger of the two if more tables somehow report than were announced, so
    /// the denominator can never be less than the numerator.
    [[nodiscard]] int totalCount() const
    {
        auto const seen = static_cast<int>(_rows.size());
        return _announcedTotal > 0 ? std::max(_announcedTotal, seen) : seen;
    }
    /// @return Number of tables in the "done" state.
    [[nodiscard]] int doneCount() const
    {
        return _doneCount;
    }
    /// @return Number of tables in the "running" state.
    [[nodiscard]] int runningCount() const
    {
        return _runningCount;
    }
    /// @return Number of tables still "queued".
    [[nodiscard]] int queuedCount() const
    {
        return _queuedCount;
    }
    /// @return Number of tables in the "error" state.
    [[nodiscard]] int errorCount() const
    {
        return _errorCount;
    }
    /// @return Number of tables in the "warning" state.
    [[nodiscard]] int warningCount() const
    {
        return _warningCount;
    }

    /// Creates the row for `table` on first sight, otherwise updates it in
    /// place, emitting the appropriate model signals.
    /// @param table Table name (the row key).
    /// @param current Rows processed so far.
    /// @param total Total rows, or -1 when unknown.
    /// @param state One of "queued"/"running"/"done"/"error"/"warning".
    /// @param message Latest human-readable progress message.
    void applyProgress(QString const& table, quint64 current, qint64 total, QString const& state, QString const& message);

    /// Records how many tables the run will process, as announced by SqlBackup
    /// before any per-table progress arrives. Makes `totalCount()` a fixed
    /// denominator for the run instead of a count of tables seen so far.
    /// @param totalTables Number of tables the run will process; <= 0 clears
    ///        the announced total and reverts to counting rows.
    void setTotalTables(int totalTables);

    /// Removes every row (called when a new run starts for the profile).
    /// Also clears the announced table total, so a stale denominator from the
    /// previous run cannot be shown against the new run's rows.
    void clearTables();

    [[nodiscard]] int rowCount(QModelIndex const& parent = {}) const override;
    [[nodiscard]] QVariant data(QModelIndex const& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

  signals:
    /// Emitted whenever any per-state tally changes (i.e. on every
    /// applyProgress/clearTables), driving the detail-panel summary header.
    void summaryChanged();

  private:
    struct Row
    {
        QString table;
        quint64 current = 0;
        qint64 total = -1;
        QString state = QStringLiteral("queued");
        QString message;
    };

    [[nodiscard]] int RowIndexOf(QString const& table) const;

    /// Recomputes the per-state tallies from `_rows` and emits summaryChanged.
    /// Called after any row insert/update/clear.
    void RecomputeSummary();

    QList<Row> _rows;

    /// Table count announced by SqlBackup before the run's first progress
    /// report; 0 when unknown. See totalCount() for why this exists.
    int _announcedTotal = 0;

    // Cached per-state tallies, kept in sync by RecomputeSummary().
    int _doneCount = 0;
    int _runningCount = 0;
    int _queuedCount = 0;
    int _errorCount = 0;
    int _warningCount = 0;
};

} // namespace DbtoolGui
