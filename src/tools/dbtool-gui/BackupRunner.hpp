// SPDX-License-Identifier: Apache-2.0
//
// `BackupRunner` mirrors `MigrationRunner` for the SqlBackup API. The GUI
// uses it to drive the Backup & restore panel on the Backups page (Back up and
// Restore against the current connection) and the Simple view's "back up
// first" step — the same async worker pattern, so progress signals surface in
// the same log panel users already know from migration runs.
//
// Back up and Restore are the same kind of run (a walk over the tables), so
// they share one progress surface (`currentTable` / `tablesDone` /
// `tablesTotal` / `rowsDone`) and one outcome (`lastResult`). Neither
// `SqlBackup::Backup` nor `SqlBackup::Restore` throws when an individual table
// fails; the runner counts those errors and reports such a run as failed, with
// the failing tables listed, instead of as a success.

#pragma once

#include "LogLevel.hpp"
#include "Models/BackupTableListModel.hpp"

#include <atomic>
#include <filesystem>
#include <functional>
#include <string>

#include <QtCore/QDateTime>
#include <QtCore/QObject>
#include <QtCore/QThreadPool>
#include <QtCore/QVariantMap>
#include <QtQmlIntegration/QtQmlIntegration>

namespace Lightweight
{
struct SqlConnectionString;
namespace SqlBackup
{
    struct ProgressManager;
}
} // namespace Lightweight

namespace DbtoolGui
{

class BackupRunner: public QObject
{
    Q_OBJECT
    QML_ELEMENT
  public:
    enum class Phase : int
    {
        Idle,
        Running,
    };
    Q_ENUM(Phase)

    /// Which kind of run is in flight, or was the last one.
    enum class Operation : int
    {
        None,
        Backup,
        Restore,
    };
    Q_ENUM(Operation)

    /// The archive operation a run executes. The default calls
    /// `SqlBackup::Backup` / `SqlBackup::Restore`; tests inject fakes so the
    /// runner's bookkeeping can be driven without a database.
    /// @param archive The archive file written (backup) or read (restore).
    /// @param connectionString The connection string of the database.
    /// @param progress Receives per-table progress; must be used from the call's thread(s) only.
    using RunOperation = std::function<void(std::filesystem::path const& archive,
                                            std::string const& connectionString,
                                            Lightweight::SqlBackup::ProgressManager& progress)>;

    // See MigrationRunner.hpp for the rationale — without Q_PROPERTY the
    // `phase` binding in QML is a stuck method handle, not a live value.
    Q_PROPERTY(Phase phase READ phase NOTIFY phaseChanged)

    /// The kind of the run in flight (or the last one, once idle).
    Q_PROPERTY(Operation operation READ operation NOTIFY phaseChanged)

    /// When the run in flight started; invalid while idle.
    Q_PROPERTY(QDateTime startedAt READ startedAt NOTIFY phaseChanged)

    /// The table most recently reported by the run in flight.
    Q_PROPERTY(QString currentTable READ currentTable NOTIFY progressChanged)

    /// Tables that have finished (successfully or not) in the run in flight.
    Q_PROPERTY(int tablesDone READ tablesDone NOTIFY progressChanged)

    /// Tables the run in flight will process; 0 until the library announces the count.
    Q_PROPERTY(int tablesTotal READ tablesTotal NOTIFY progressChanged)

    /// Rows copied so far by the run in flight.
    Q_PROPERTY(qulonglong rowsDone READ rowsDone NOTIFY progressChanged)

    /// Per-table state of the run in flight (and of the last run once idle), in the same
    /// model the managed backups use, so the Backups page's detail panel shows an ad-hoc
    /// Back up / Restore exactly like a per-profile run: running tables, failed tables with
    /// their messages, and the done / total tallies.
    Q_PROPERTY(DbtoolGui::BackupTableListModel* tables READ tables CONSTANT)

    /// Outcome of the last finished run; empty before the first run ends and
    /// again once a new run starts. Keys: `operation` ("backup" | "restore"),
    /// `ok`, `archive`, `target`, `summary`, `error` (set only when the run threw),
    /// `tables`, `failedCount`, `failedTables` (list of `{table, reason}`),
    /// `rows`, `durationMs`, `finishedAt`.
    Q_PROPERTY(QVariantMap lastResult READ lastResult NOTIFY lastResultChanged)

    explicit BackupRunner(QObject* parent = nullptr);

    /// Connection string used for the SqlBackup / Restore API calls. Left
    /// empty means "use whatever was set via `SqlConnection::SetDefault*`".
    Q_INVOKABLE void setConnectionString(QString const& connectionString);

    /// Name of the database the connection string reaches (a profile, a DSN, a database
    /// name), recorded in each run's `lastResult` as `target`. Remembering it per run keeps an
    /// outcome naming the database it ran against after the user connects to another one.
    /// @param label Display name of the target.
    Q_INVOKABLE void setTargetLabel(QString const& label);

    /// The connection string set by `setConnectionString`.
    [[nodiscard]] QString const& connectionString() const noexcept
    {
        return _connectionString;
    }

    /// Injects the "is some other runner busy?" probe consulted before a
    /// backup or restore starts. Ad-hoc backups, managed backups and
    /// migrations all touch the same database, so an ad-hoc run started during
    /// a managed backup would tear the archive being written (and a restore
    /// would rewrite the database out from under it). Defaults to "never
    /// busy".
    /// @param probe Callable returning true to veto a new run.
    void setBusyProbe(std::function<bool()> probe);

    /// Replaces the operation `runBackup` executes (tests).
    /// @param operation The replacement; empty restores the default.
    void setBackupOperation(RunOperation operation);

    /// Replaces the operation `runRestore` executes (tests).
    /// @param operation The replacement; empty restores the default.
    void setRestoreOperation(RunOperation operation);

    Q_INVOKABLE void runBackup(QString const& outputFile);
    Q_INVOKABLE void runRestore(QString const& inputFile);

    [[nodiscard]] Phase phase() const noexcept
    {
        return _phase.load(std::memory_order_acquire);
    }

    [[nodiscard]] Operation operation() const noexcept
    {
        return _operation;
    }

    [[nodiscard]] QDateTime const& startedAt() const noexcept
    {
        return _startedAt;
    }

    [[nodiscard]] QString const& currentTable() const noexcept
    {
        return _currentTable;
    }

    [[nodiscard]] int tablesDone() const noexcept
    {
        return _tablesDone;
    }

    [[nodiscard]] int tablesTotal() const noexcept
    {
        return _tablesTotal;
    }

    [[nodiscard]] qulonglong rowsDone() const noexcept
    {
        return _rowsDone;
    }

    [[nodiscard]] QVariantMap const& lastResult() const noexcept
    {
        return _lastResult;
    }

    [[nodiscard]] BackupTableListModel* tables() noexcept
    {
        return &_tables;
    }

  signals:
    void logLine(QString line, DbtoolGui::LogLevel level);
    void finished(bool ok, QString summary);
    void phaseChanged();
    void progressChanged();
    void lastResultChanged();

    /// Emitted (queued, from the worker) after every per-table update of the
    /// run in flight. Drives `currentTable` / `tablesDone` / `tablesTotal` /
    /// `rowsDone`; not meant for QML.
    void tableProgress(QString table, int done, int total, qulonglong rows);

    /// Emitted (queued, from the worker) for every update of a table (or of the run itself,
    /// for an error that names no table); feeds `tables`. Not meant for QML.
    void tableUpdate(QString table, qulonglong current, qlonglong total, QString state, QString message);

    /// Emitted (queued, from the worker) once the library announces how many tables the run
    /// covers; feeds `tables`. Not meant for QML.
    void tableTotalKnown(int totalTables);

  private:
    void Run(Operation operation, QString const& archiveFile, RunOperation const& operationFn);

    /// Entry guard shared by runBackup()/runRestore(): refuses when a run is
    /// already in flight or the busy probe vetoes (logging a warning in the
    /// latter case so the refusal is visible).
    /// @param what Short label used in the logged refusal message.
    /// @return True when it is safe to start a run.
    [[nodiscard]] bool CanStartRun(char const* what);

    QThreadPool _pool;
    std::atomic<Phase> _phase { Phase::Idle };
    Operation _operation = Operation::None;
    QDateTime _startedAt;
    QString _currentTable;
    int _tablesDone = 0;
    int _tablesTotal = 0;
    qulonglong _rowsDone = 0;
    BackupTableListModel _tables;
    QVariantMap _lastResult;
    QString _connectionString;
    QString _targetLabel;
    std::function<bool()> _busyProbe;
    RunOperation _backupOperation;
    RunOperation _restoreOperation;
};

} // namespace DbtoolGui
