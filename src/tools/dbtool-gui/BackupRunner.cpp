// SPDX-License-Identifier: Apache-2.0

#include "BackupRunner.hpp"
#include "TableState.hpp"

#include <Lightweight/SqlBackup.hpp>
#include <Lightweight/SqlConnectInfo.hpp>

#include <algorithm>
#include <chrono>
#include <exception>
#include <filesystem>
#include <mutex>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

#include <QtCore/QDebug>
#include <QtCore/QMetaObject>
#include <QtCore/QRunnable>
#include <QtCore/QVariantList>

namespace DbtoolGui
{

namespace
{

    class FunctionTask final: public QRunnable
    {
      public:
        explicit FunctionTask(std::function<void()> fn):
            _fn(std::move(fn))
        {
            setAutoDelete(true);
        }
        void run() override
        {
            try
            {
                _fn();
            }
            catch (...)
            {
            }
        }

      private:
        std::function<void()> _fn;
    };

    /// Longest failed-table list kept in a result; further failures are only counted.
    constexpr std::size_t kMaxListedFailures = 50;

    /// What one finished run amounts to, computed on the worker thread.
    struct RunSummary
    {
        QString summary;
        QVariantMap result;
        bool ok = true;
    };

    /// ProgressManager that forwards SqlBackup progress onto a BackupRunner's
    /// Qt signals and tallies the run: tables finished, rows copied, and which
    /// tables failed. Kept in the .cpp so Qt headers do not leak into the
    /// runner's public header.
    ///
    /// Deriving from `ErrorTrackingProgressManager` (rather than the bare
    /// `ProgressManager`) is load-bearing: neither `SqlBackup::Backup` nor
    /// `SqlBackup::Restore` throws when an individual table fails — the failure
    /// is reported as `Progress::State::Error` and the call returns normally.
    /// `ErrorCount()` is the only signal that a run was partial, and the base
    /// class' implementation is a hard-coded `0`, so a manager derived straight
    /// from `ProgressManager` reports success for a run that silently lost
    /// tables. `dbtool` gates its exit code on exactly this counter.
    ///
    /// `Update()` is called from the library's worker threads, so the tally is
    /// guarded by a mutex.
    class EmittingProgressManager: public Lightweight::SqlBackup::ErrorTrackingProgressManager
    {
      public:
        explicit EmittingProgressManager(QObject* target):
            _target(target)
        {
        }

        void Update(Lightweight::SqlBackup::Progress const& p) override
        {
            using State = Lightweight::SqlBackup::Progress::State;

            auto const table = QString::fromStdString(std::string { p.tableName });
            auto const message = QString::fromStdString(std::string { p.message });
            auto const level = p.state == State::Error     ? LogLevel::Error
                               : p.state == State::Warning ? LogLevel::Warning
                                                           : LogLevel::Info;

            int done = 0;
            int total = 0;
            qulonglong rows = 0;
            // Name shown as "the table in flight": only real tables, never a phase.
            auto progressTable = QString {};
            // Row of the per-table model this event updates; empty when it is not about a table.
            auto modelTable = std::string {};
            {
                std::scoped_lock const lock(_mutex);
                Lightweight::SqlBackup::ErrorTrackingProgressManager::Update(p);
                // The library also reports phases that are not tables: schema
                // scanning ("Scanning schema", all before SetTotalTables), and
                // run-level events with no table name ("Restore complete").
                // Only a named table reported after the table count was
                // announced is table progress. Errors are different: every one
                // is a failure whatever it names or when it arrives, so none is
                // dropped by that filter.
                auto const isTableEvent = !p.tableName.empty() && _announcedTotal > 0;
                if (isTableEvent)
                    progressTable = table;
                // Errors always show: one that names no table lands on a "(general)" row so
                // it is visible beside the tables rather than only in the outcome banner.
                if (isTableEvent || p.state == State::Error)
                    modelTable = p.tableName.empty() ? std::string { "(general)" } : p.tableName;
                if (p.state == State::Error)
                {
                    NoteFailureLocked(p.tableName, table, message);
                    if (!p.tableName.empty())
                        _terminal.insert(p.tableName);
                }
                else if (p.state == State::Finished && isTableEvent)
                {
                    _terminal.insert(p.tableName);
                    _rows += p.currentRows;
                }
                done = static_cast<int>(_terminal.size());
                total = TotalLocked();
                rows = _rows;
            }

            auto const msg = QStringLiteral("[%1] %2 rows: %3").arg(table).arg(p.currentRows).arg(message);
            // Queue the signals by name directly on the target: the pointer is
            // read now (on the worker thread), and the QString/LogLevel args
            // are copied into the queued event. The manager is a stack local
            // in the run that may be destroyed the instant Backup()/Restore()
            // returns, so capturing `this` in a deferred lambda (as an earlier
            // version did) would dereference freed storage on the GUI thread.
            // Nothing here outlives this call.
            QMetaObject::invokeMethod(
                _target, "logLine", Qt::QueuedConnection, Q_ARG(QString, msg), Q_ARG(DbtoolGui::LogLevel, level));
            QMetaObject::invokeMethod(_target,
                                      "tableProgress",
                                      Qt::QueuedConnection,
                                      Q_ARG(QString, progressTable),
                                      Q_ARG(int, done),
                                      Q_ARG(int, total),
                                      Q_ARG(qulonglong, rows));
            if (!modelTable.empty())
                QMetaObject::invokeMethod(
                    _target,
                    "tableUpdate",
                    Qt::QueuedConnection,
                    Q_ARG(QString, QString::fromStdString(modelTable)),
                    Q_ARG(qulonglong, static_cast<qulonglong>(p.currentRows)),
                    Q_ARG(qlonglong, p.totalRows ? static_cast<qlonglong>(*p.totalRows) : qlonglong { -1 }),
                    Q_ARG(QString, TableStateString(p.state)),
                    Q_ARG(QString, message));
        }

        void AllDone() override {}

        /// Fixes the denominator of "table x of y" for the whole run: Backup
        /// knows the table set after its schema scan and Restore after reading
        /// the archive manifest, before any data moves.
        /// @param totalTables Number of tables the run will process.
        void SetTotalTables(size_t totalTables) override
        {
            {
                std::scoped_lock const lock(_mutex);
                _announcedTotal = totalTables;
            }
            // Same queued, copy-everything delivery as in Update().
            QMetaObject::invokeMethod(
                _target, "tableTotalKnown", Qt::QueuedConnection, Q_ARG(int, static_cast<int>(totalTables)));
            QMetaObject::invokeMethod(_target,
                                      "tableProgress",
                                      Qt::QueuedConnection,
                                      Q_ARG(QString, QString {}),
                                      Q_ARG(int, 0),
                                      Q_ARG(int, static_cast<int>(totalTables)),
                                      Q_ARG(qulonglong, qulonglong { 0 }));
        }

        /// Turns the tally into the run's outcome.
        /// @param operation Which kind of run this was.
        /// @param archive The archive file written or read.
        /// @param error Text of the exception that aborted the run, empty when it returned normally.
        /// @param durationMs Wall-clock duration of the run.
        /// @return The summary line, the `lastResult` map (minus `finishedAt`) and the verdict.
        [[nodiscard]] RunSummary Summarize(BackupRunner::Operation operation,
                                           QString const& archive,
                                           QString const& error,
                                           qint64 durationMs) const
        {
            std::scoped_lock const lock(_mutex);
            auto const isBackup = operation == BackupRunner::Operation::Backup;
            auto const tables = TotalLocked();
            auto const failed = static_cast<int>(_failedTables);
            auto const other = static_cast<int>(_otherErrors);
            auto const otherNote = other > 0 ? QStringLiteral(", and %1 other error(s)").arg(other) : QString {};

            auto summary = QString {};
            if (!error.isEmpty())
                summary = error;
            else if (failed > 0 && isBackup)
                summary = QStringLiteral("Backup incomplete — %1 of %2 tables failed%3; the archive is missing them.")
                              .arg(failed)
                              .arg(tables)
                              .arg(otherNote);
            else if (failed > 0)
                summary = QStringLiteral("Restore incomplete — %1 of %2 tables failed%3. The restore is not "
                                         "transactional, so the database may be left incomplete.")
                              .arg(failed)
                              .arg(tables)
                              .arg(otherNote);
            else if (other > 0 && isBackup)
                summary = QStringLiteral("Backup finished with %1 error(s); the archive may be incomplete.").arg(other);
            else if (other > 0)
                summary = QStringLiteral("Restore finished with %1 error(s). The restore is not transactional, so "
                                         "the database may be left incomplete.")
                              .arg(other);
            else if (isBackup)
                summary = QStringLiteral("Backup written — %1 tables").arg(tables);
            else
                summary = QStringLiteral("Restored — %1 tables").arg(tables);

            auto failedTables = QVariantList {};
            for (auto const& failure: _failures)
                failedTables.push_back(QVariantMap { { QStringLiteral("table"), failure.table },
                                                     { QStringLiteral("reason"), failure.reason } });

            // `ErrorCount()` is the library-wide verdict: it also covers errors this tally
            // has no row for, so a run is only ok when it saw none at all.
            auto ok = error.isEmpty() && ErrorCount() == 0;
            auto result = QVariantMap {
                { QStringLiteral("operation"), isBackup ? QStringLiteral("backup") : QStringLiteral("restore") },
                { QStringLiteral("ok"), ok },
                { QStringLiteral("archive"), archive },
                { QStringLiteral("summary"), summary },
                { QStringLiteral("error"), error },
                { QStringLiteral("tables"), tables },
                { QStringLiteral("failedCount"), failed },
                { QStringLiteral("otherErrors"), other },
                { QStringLiteral("failedTables"), failedTables },
                { QStringLiteral("rows"), static_cast<qulonglong>(_rows) },
                { QStringLiteral("durationMs"), durationMs },
            };
            return { std::move(summary), std::move(result), ok };
        }

      private:
        struct Failure
        {
            QString table;
            QString reason;
        };

        /// Tables the run covers: the announced count, or the tables that
        /// reached a verdict if more of them did (a table that failed to be
        /// created is not part of the announced set but is still a table).
        [[nodiscard]] int TotalLocked() const
        {
            return static_cast<int>(std::max(_announcedTotal, _terminal.size()));
        }

        /// Records one error. The library may report the same table failing
        /// once per chunk, so a table counts once; an error that names no
        /// table (index restoration, "No tables could be created") counts once
        /// per distinct message and is kept apart from the table count.
        /// @param name The reported table name, possibly empty.
        /// @param table `name` as a QString, for the listed failure.
        /// @param reason The error message.
        void NoteFailureLocked(std::string const& name, QString const& table, QString const& reason)
        {
            auto const key = name.empty() ? std::string { '\0' } + reason.toStdString() : name;
            if (!_failureKeys.insert(key).second)
                return;
            if (name.empty())
                ++_otherErrors;
            else
                ++_failedTables;
            if (_failures.size() < kMaxListedFailures)
                _failures.push_back({ table, reason });
        }

        QObject* _target;
        mutable std::mutex _mutex;
        std::size_t _announcedTotal = 0;
        std::unordered_set<std::string> _terminal;
        std::unordered_set<std::string> _failureKeys;
        std::size_t _rows = 0;
        std::size_t _failedTables = 0;
        std::size_t _otherErrors = 0;
        std::vector<Failure> _failures;
    };

    /// Worker-thread count handed to `SqlBackup::Backup` / `Restore`. The
    /// library spawns this many internal threads to process tables in
    /// parallel; MS SQL is internally clamped to 1 by the library because of
    /// the driver's data races, so the GUI does not need a per-DBMS check.
    /// The 8-thread cap mirrors a conservative `dbtool --jobs` default and
    /// avoids saturating shared database servers.
    [[nodiscard]] unsigned BackupConcurrency() noexcept
    {
        auto const hw = std::thread::hardware_concurrency();
        return std::clamp(hw == 0 ? 1U : hw, 1U, 8U);
    }

    /// Default backup operation: the real `SqlBackup::Backup` call.
    void RunSqlBackup(std::filesystem::path const& archive,
                      std::string const& connectionString,
                      Lightweight::SqlBackup::ProgressManager& progress)
    {
        Lightweight::SqlBackup::Backup(
            archive, Lightweight::SqlConnectionString { connectionString }, BackupConcurrency(), progress);
    }

    /// Default restore operation: the real `SqlBackup::Restore` call.
    void RunSqlRestore(std::filesystem::path const& archive,
                       std::string const& connectionString,
                       Lightweight::SqlBackup::ProgressManager& progress)
    {
        Lightweight::SqlBackup::Restore(
            archive, Lightweight::SqlConnectionString { connectionString }, BackupConcurrency(), progress);
    }

} // namespace

BackupRunner::BackupRunner(QObject* parent):
    QObject(parent),
    _tables(this),
    _backupOperation(&RunSqlBackup),
    _restoreOperation(&RunSqlRestore)
{
    // Serialise GUI-initiated backup / restore operations so the progress
    // pane and cancel button stay coherent. Worker-level parallelism lives
    // inside `SqlBackup::Backup` / `Restore` (see `BackupConcurrency`).
    _pool.setMaxThreadCount(1);

    // Per-table state for the detail panel, fed from the worker the same way.
    connect(this, &BackupRunner::tableUpdate, &_tables, &BackupTableListModel::applyProgress);
    connect(this, &BackupRunner::tableTotalKnown, &_tables, &BackupTableListModel::setTotalTables);

    // `tableProgress` is emitted queued from the worker; this connection
    // applies it on the GUI thread (same-thread delivery). An empty table
    // name is the "total announced" event: it fixes the denominator without
    // naming a table.
    connect(this, &BackupRunner::tableProgress, this, [this](QString const& table, int done, int total, qulonglong rows) {
        if (phase() != Phase::Running)
            return; // a late event of a run that already ended
        if (!table.isEmpty())
            _currentTable = table;
        _tablesDone = std::max(_tablesDone, done);
        _tablesTotal = std::max(_tablesTotal, total);
        _rowsDone = std::max(_rowsDone, rows);
        emit progressChanged();
    });
}

void BackupRunner::setConnectionString(QString const& connectionString)
{
    _connectionString = connectionString;
}

void BackupRunner::setTargetLabel(QString const& label)
{
    _targetLabel = label;
}

void BackupRunner::setBusyProbe(std::function<bool()> probe)
{
    _busyProbe = std::move(probe);
}

void BackupRunner::setBackupOperation(RunOperation operation)
{
    _backupOperation = operation ? std::move(operation) : RunOperation { &RunSqlBackup };
}

void BackupRunner::setRestoreOperation(RunOperation operation)
{
    _restoreOperation = operation ? std::move(operation) : RunOperation { &RunSqlRestore };
}

bool BackupRunner::CanStartRun(char const* what)
{
    if (phase() != Phase::Idle)
        return false;
    if (_busyProbe && _busyProbe())
    {
        emit logLine(QStringLiteral("%1: another operation is busy (migration or managed backup in progress?).")
                         .arg(QLatin1String(what)),
                     LogLevel::Warning);
        return false;
    }
    return true;
}

void BackupRunner::runBackup(QString const& outputFile)
{
    if (CanStartRun("Backup"))
        Run(Operation::Backup, outputFile, _backupOperation);
}

void BackupRunner::runRestore(QString const& inputFile)
{
    if (CanStartRun("Restore"))
        Run(Operation::Restore, inputFile, _restoreOperation);
}

void BackupRunner::Run(Operation operation, QString const& archiveFile, RunOperation const& operationFn)
{
    _operation = operation;
    _startedAt = QDateTime::currentDateTime();
    _currentTable.clear();
    _tablesDone = 0;
    _tablesTotal = 0;
    _rowsDone = 0;
    _tables.clearTables();
    if (!_lastResult.isEmpty())
    {
        _lastResult.clear();
        emit lastResultChanged();
    }
    _phase.store(Phase::Running, std::memory_order_release);
    emit phaseChanged();
    emit progressChanged();

    auto* runner = this;
    auto const cs = _connectionString.toStdString();
    auto const target = _targetLabel;

    _pool.start(new FunctionTask([runner, operation, archiveFile, operationFn, cs, target] {
        auto const started = std::chrono::steady_clock::now();
        auto error = QString {};
        EmittingProgressManager pm(runner);
        try
        {
            operationFn(std::filesystem::path(archiveFile.toStdString()), cs, pm);
        }
        catch (std::exception const& e)
        {
            error = QString::fromUtf8(e.what());
        }
        auto const durationMs =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
        auto outcome = pm.Summarize(operation, archiveFile, error, static_cast<qint64>(durationMs));
        outcome.result.insert(QStringLiteral("target"), target);

        QMetaObject::invokeMethod(
            runner,
            [runner, outcome = std::move(outcome)]() mutable {
                outcome.result.insert(QStringLiteral("finishedAt"), QDateTime::currentDateTime());
                runner->_lastResult = std::move(outcome.result);
                runner->_phase.store(Phase::Idle, std::memory_order_release);
                emit runner->lastResultChanged();
                emit runner->phaseChanged();
                emit runner->finished(outcome.ok, outcome.summary);
            },
            Qt::QueuedConnection);
    }));
}

} // namespace DbtoolGui
