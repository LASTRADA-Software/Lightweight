// SPDX-License-Identifier: Apache-2.0

#include "BackupRequirementSupport.hpp"
#include "BackupRunner.hpp"
#include "TableState.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>
#include <Lightweight/SqlBackup.hpp>
#include <Lightweight/SqlConnectInfo.hpp>
#include <Lightweight/SqlMigration.hpp>

#include <algorithm>
#include <chrono>
#include <exception>
#include <filesystem>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

#include <BackupRecording.hpp>
#include <QtCore/QDeadlineTimer>
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

    /// Whether a progress event's table name names a table. SqlBackup reports failures that
    /// belong to the run rather than to one table (archive creation, a crashed worker) with no
    /// name, or under the placeholder name "Unknown"; neither is a table, and counting them as
    /// one made such a run read "1 of 1 tables failed" with an "Unknown" row.
    /// @param tableName The name carried by the event.
    /// @return True for a real table name.
    [[nodiscard]] bool NamesATable(std::string const& tableName) noexcept
    {
        return !tableName.empty() && tableName != "Unknown";
    }

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
                auto const named = NamesATable(p.tableName);
                auto const isTableEvent = named && _announcedTotal > 0;
                if (isTableEvent)
                    progressTable = table;
                // Errors always show: one that names no table lands on a "(general)" row so
                // it is visible beside the tables rather than only in the outcome banner.
                if (isTableEvent || p.state == State::Error)
                    modelTable = named ? p.tableName : std::string { "(general)" };
                if (p.state == State::Error)
                {
                    NoteFailureLocked(named ? p.tableName : std::string {}, named ? table : QString {}, message);
                    if (named)
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

    /// Default `Recorder::captureStart`: the migration history, read on a mapper of its own.
    Lightweight::Tools::BackupStart CaptureBackupStartOn(std::string const& connectionString, std::string const& schema)
    {
        auto mapper =
            Lightweight::Tools::ConnectRecordDataMapper(Lightweight::SqlConnectionString { connectionString }, schema);
        return Lightweight::Tools::CaptureBackupStart(mapper);
    }

    /// Default `Recorder::record`: verifies the archive and writes its record on a mapper of its own.
    std::expected<Lightweight::SqlMigration::BackupRecord, Lightweight::Tools::BackupRecordError> RecordRunOn(
        BackupRunner::Operation operation,
        std::filesystem::path const& archive,
        std::string const& connectionString,
        std::string const& schema,
        Lightweight::Tools::BackupStart const& start)
    {
        try
        {
            auto mapper =
                Lightweight::Tools::ConnectRecordDataMapper(Lightweight::SqlConnectionString { connectionString }, schema);
            auto const toolVersion = Lightweight::Tools::ToolVersion(RecordToolName);
            return operation == BackupRunner::Operation::Backup
                       ? Lightweight::Tools::RecordBackup(mapper, archive, start, toolVersion)
                       : Lightweight::Tools::RecordRestore(mapper, archive, toolVersion);
        }
        catch (std::exception const& e)
        {
            return std::unexpected(Lightweight::Tools::BackupRecordError {
                .kind = Lightweight::Tools::BackupRecordError::Kind::WriteFailed, .message = e.what() });
        }
    }

    /// The sentence that says why a run was not recorded.
    QString DescribeUnrecorded(BackupRunner::Operation operation, Lightweight::Tools::BackupRecordError const& error)
    {
        return QString::fromStdString(operation == BackupRunner::Operation::Backup
                                          ? Lightweight::Tools::DescribeUnrecordedBackup(error)
                                          : Lightweight::Tools::DescribeUnrecordedRestore(error));
    }

    /// Records a clean run; a backup archive that fails verification fails the run. A restore that fails
    /// verification has already rewritten the database, so it stays a success that only warns.
    /// @param recorder Writes the record.
    /// @param operation Which kind of run this was.
    /// @param archive The archive written or restored.
    /// @param connectionString The database the run was against.
    /// @param schema The schema whose `schema_backups` table receives the record.
    /// @param start What a backup captured before it started; ignored for a restore.
    /// @param outcome The run's outcome, failed here when the archive fails verification.
    /// @return Why the run was not recorded; empty when it was.
    QString RecordRun(BackupRunner::Recorder const& recorder,
                      BackupRunner::Operation operation,
                      std::filesystem::path const& archive,
                      std::string const& connectionString,
                      std::string const& schema,
                      Lightweight::Tools::BackupStart const& start,
                      RunSummary& outcome)
    {
        auto recorded = std::expected<Lightweight::SqlMigration::BackupRecord, Lightweight::Tools::BackupRecordError> {};
        try
        {
            recorded = recorder.record(operation, archive, connectionString, schema, start);
        }
        catch (std::exception const& e)
        {
            recorded = std::unexpected(Lightweight::Tools::BackupRecordError {
                .kind = Lightweight::Tools::BackupRecordError::Kind::WriteFailed, .message = e.what() });
        }
        if (recorded)
            return {};
        auto const message = DescribeUnrecorded(operation, recorded.error());
        if (operation == BackupRunner::Operation::Backup
            && recorded.error().kind == Lightweight::Tools::BackupRecordError::Kind::VerificationFailed)
        {
            outcome.ok = false;
            outcome.summary = message;
            outcome.result.insert(QStringLiteral("ok"), false);
            outcome.result.insert(QStringLiteral("summary"), message);
        }
        return message;
    }

} // namespace

bool BackupRunner::WaitForDone(std::chrono::milliseconds timeout)
{
    if (timeout == std::chrono::milliseconds::max())
        return _pool.waitForDone(QDeadlineTimer { QDeadlineTimer::Forever });
    return _pool.waitForDone(QDeadlineTimer { timeout });
}

BackupRunner::BackupRunner(QObject* parent):
    QObject(parent),
    _tables(this),
    _backupOperation(&RunSqlBackup),
    _restoreOperation(&RunSqlRestore),
    _recorder { .captureStart = &CaptureBackupStartOn, .record = &RecordRunOn }
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

void BackupRunner::setRecorder(Recorder recorder)
{
    _recorder.captureStart = recorder.captureStart ? std::move(recorder.captureStart) : &CaptureBackupStartOn;
    _recorder.record = recorder.record ? std::move(recorder.record) : &RecordRunOn;
}

void BackupRunner::setRecordSchema(QString const& schema)
{
    _recordSchema = schema.toStdString();
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

    _pool.start(new FunctionTask(
        [runner, operation, archiveFile, operationFn, cs, target, recorder = _recorder, recordSchema = _recordSchema] {
            auto const archive = std::filesystem::path { archiveFile.toStdU16String() };
            auto const started = std::chrono::steady_clock::now();
            // A backup's record keeps the migration history as it was before the backup read any data.
            auto start = std::optional<Lightweight::Tools::BackupStart> {};
            auto recordError = QString {};
            if (operation == Operation::Backup)
            {
                try
                {
                    start = recorder.captureStart(cs, recordSchema);
                }
                catch (std::exception const& e)
                {
                    recordError = DescribeUnrecorded(
                        operation,
                        { .kind = Lightweight::Tools::BackupRecordError::Kind::WriteFailed,
                          .message = std::string { "could not read the migration history: " } + e.what() });
                }
            }

            auto error = QString {};
            EmittingProgressManager pm(runner);
            try
            {
                operationFn(archive, cs, pm);
            }
            catch (std::exception const& e)
            {
                error = QString::fromUtf8(e.what());
            }
            auto const durationMs =
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
            auto outcome = pm.Summarize(operation, archiveFile, error, static_cast<qint64>(durationMs));
            outcome.result.insert(QStringLiteral("target"), target);

            // Only a clean run is recorded; a failed one already says why in its summary.
            auto recorded = false;
            if (!outcome.ok)
                recordError.clear();
            else if (operation == Operation::Restore || start)
            {
                recordError = RecordRun(recorder,
                                        operation,
                                        archive,
                                        cs,
                                        recordSchema,
                                        start.value_or(Lightweight::Tools::BackupStart {}),
                                        outcome);
                recorded = recordError.isEmpty();
            }
            if (outcome.ok && !recordError.isEmpty())
                QMetaObject::invokeMethod(runner,
                                          "logLine",
                                          Qt::QueuedConnection,
                                          Q_ARG(QString, recordError),
                                          Q_ARG(DbtoolGui::LogLevel, LogLevel::Warning));
            outcome.result.insert(QStringLiteral("recorded"), recorded);
            outcome.result.insert(QStringLiteral("recordError"), recordError);

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
