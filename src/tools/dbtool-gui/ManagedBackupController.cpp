// SPDX-License-Identifier: Apache-2.0

#include "ManagedBackupController.hpp"
#include "ManagedBackupCore.hpp"

#include <Lightweight/SqlBackup.hpp>
#include <Lightweight/SqlConnectInfo.hpp>

#include <algorithm>
#include <chrono>
#include <exception>
#include <filesystem>
#include <memory>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <utility>

#include <QtCore/QDateTime>
#include <QtCore/QDebug>
#include <QtCore/QMetaObject>
#include <QtCore/QRunnable>
#include <QtCore/QSettings>
#include <QtCore/QStandardPaths>
#include <Secrets/SecretResolver.hpp>

namespace DbtoolGui
{

namespace
{
    auto const* kKeyBackupFolder = "backup/folder";

    /// Converts a Qt path string into a `std::filesystem::path` losslessly.
    ///
    /// `QString::toStdString()` yields UTF-8, but MSVC's `fs::path(std::string)`
    /// decodes narrow input in the process' active code page — so a non-ASCII
    /// backup folder (`C:\Users\Müller\Sicherungen`) turns into mojibake and the
    /// archives land somewhere the user never picked. UTF-16 is the native
    /// encoding of both `QString` and the Windows path API, and converts
    /// correctly to UTF-8 on POSIX.
    /// @param value Path as held by the GUI.
    /// @return The same path, encoding-preserved.
    [[nodiscard]] std::filesystem::path ToPath(QString const& value)
    {
        return std::filesystem::path { value.toStdU16String() };
    }

    /// Converts a `std::filesystem::path` back into a `QString` for display.
    /// The inverse of `ToPath`: `path::string()` would re-encode into the
    /// active code page on Windows (lossy, and throwing for unrepresentable
    /// characters), so the UTF-16 form is used instead.
    /// @param value Path to render.
    /// @return The path as a Qt string.
    [[nodiscard]] QString FromPath(std::filesystem::path const& value)
    {
        return QString::fromStdU16String(value.u16string());
    }

    /// Converts a std::filesystem file time to QDateTime (UTC-based).
    [[nodiscard]] QDateTime ToQDateTime(std::filesystem::file_time_type tp)
    {
        auto const sys = std::chrono::clock_cast<std::chrono::system_clock>(tp);
        auto const ms = std::chrono::duration_cast<std::chrono::milliseconds>(sys.time_since_epoch()).count();
        return QDateTime::fromMSecsSinceEpoch(ms);
    }

    /// Runs a `std::function<void()>` on a `QThreadPool`. Copied verbatim
    /// from `BackupRunner.cpp` — kept file-local so both runners can carry a
    /// copyable worker closure without sharing a header.
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
            // QRunnable::run() must not let exceptions escape (Qt terminates
            // the process if one does), so every exception is swallowed
            // here; logging keeps a silent worker failure debuggable.
            try
            {
                _fn();
            }
            catch (std::exception const& e)
            {
                qWarning() << "ManagedBackupController: worker task threw:" << e.what();
            }
            catch (...)
            {
                qWarning() << "ManagedBackupController: worker task threw a non-standard exception.";
            }
        }

      private:
        std::function<void()> _fn;
    };

    /// Maps a backup/restore progress state onto the GUI's log-line
    /// severity.
    /// @param state Progress state reported by `SqlBackup`.
    /// @return The corresponding `LogLevel`.
    [[nodiscard]] LogLevel LevelForState(Lightweight::SqlBackup::Progress::State state)
    {
        switch (state)
        {
            case Lightweight::SqlBackup::Progress::State::Error:
                return LogLevel::Error;
            case Lightweight::SqlBackup::Progress::State::Warning:
                return LogLevel::Warning;
            default:
                return LogLevel::Info;
        }
    }

    /// Maps a Progress state onto the per-table model's state string.
    [[nodiscard]] QString TableStateString(Lightweight::SqlBackup::Progress::State state)
    {
        switch (state)
        {
            case Lightweight::SqlBackup::Progress::State::Started:
            case Lightweight::SqlBackup::Progress::State::InProgress:
                return QStringLiteral("running");
            case Lightweight::SqlBackup::Progress::State::Finished:
                return QStringLiteral("done");
            case Lightweight::SqlBackup::Progress::State::Error:
                return QStringLiteral("error");
            case Lightweight::SqlBackup::Progress::State::Warning:
                return QStringLiteral("warning");
        }
        return QStringLiteral("running");
    }

    /// ProgressManager that forwards SqlBackup progress onto a target
    /// QObject's `logLine` / `tableProgress` signals *and* keeps the inherited
    /// error tally.
    ///
    /// Deriving from `ErrorTrackingProgressManager` (rather than the bare
    /// `ProgressManager`) is load-bearing: neither `SqlBackup::Backup` nor
    /// `SqlBackup::Restore` throws when an individual table fails — the failure
    /// is reported as `Progress::State::Error` and the call returns normally.
    /// `ErrorCount()` is the only signal that a run was partial, and the base
    /// class' implementation is a hard-coded `0`, so a manager derived straight
    /// from `ProgressManager` reports success for a backup that silently lost
    /// tables. `dbtool` gates its exit code on exactly this counter.
    class EmittingProgressManager: public Lightweight::SqlBackup::ErrorTrackingProgressManager
    {
      public:
        EmittingProgressManager(QObject* target, QString profileName):
            _target(target),
            _profileName(std::move(profileName))
        {
        }

        void Update(Lightweight::SqlBackup::Progress const& p) override
        {
            Lightweight::SqlBackup::ErrorTrackingProgressManager::Update(p);

            auto const table = QString::fromStdString(std::string { p.tableName });
            auto const msg = QStringLiteral("[%1] %2 rows: %3")
                                 .arg(table)
                                 .arg(p.currentRows)
                                 .arg(QString::fromStdString(std::string { p.message }));
            auto const level = LevelForState(p.state);
            // Both signals are queued: the target pointer is read now (worker
            // thread) and every arg is copied into the queued event. The
            // manager is a stack local in RunBackups()/RunRestore() that may be
            // destroyed the instant Backup()/Restore() returns, so capturing
            // `this` in a deferred lambda (as an earlier version did) would
            // dereference freed storage on the GUI thread. Nothing here
            // outlives this call.
            QMetaObject::invokeMethod(
                _target, "logLine", Qt::QueuedConnection, Q_ARG(QString, msg), Q_ARG(DbtoolGui::LogLevel, level));

            auto const total = p.totalRows ? static_cast<qint64>(*p.totalRows) : qint64 { -1 };
            QMetaObject::invokeMethod(_target,
                                      "tableProgress",
                                      Qt::QueuedConnection,
                                      Q_ARG(QString, _profileName),
                                      Q_ARG(QString, table),
                                      Q_ARG(quint64, static_cast<quint64>(p.currentRows)),
                                      Q_ARG(qint64, total),
                                      Q_ARG(QString, TableStateString(p.state)),
                                      Q_ARG(QString, QString::fromStdString(std::string { p.message })));
        }

        void AllDone() override {}

        /// Forwards SqlBackup's post-schema-scan table count to the GUI, so the
        /// detail panel's "n / total tables" denominator is fixed for the whole
        /// run instead of growing as tables are first reported.
        /// @param totalTables Number of tables the run will process.
        void SetTotalTables(size_t totalTables) override
        {
            // Queued for the same reason as the signals in Update(): this
            // manager is a worker-thread stack local that may die the moment
            // Backup()/Restore() returns, so every argument is copied into the
            // event and nothing here outlives the call.
            QMetaObject::invokeMethod(_target,
                                      "tableTotalKnown",
                                      Qt::QueuedConnection,
                                      Q_ARG(QString, _profileName),
                                      Q_ARG(int, static_cast<int>(totalTables)));
        }

      private:
        QObject* _target;
        QString _profileName;
    };

    /// Worker-thread count for SqlBackup (same policy as BackupRunner).
    [[nodiscard]] unsigned BackupConcurrency() noexcept
    {
        auto const hw = std::thread::hardware_concurrency();
        return std::clamp(hw == 0 ? 1U : hw, 1U, 8U);
    }

    /// Managed-backup archives prefer zstd (fast restore decompress) and
    /// fall back to deflate when libzip lacks zstd support.
    [[nodiscard]] Lightweight::SqlBackup::BackupSettings ManagedBackupSettings()
    {
        using Lightweight::SqlBackup::CompressionMethod;
        auto const method = Lightweight::SqlBackup::IsCompressionMethodSupported(CompressionMethod::Zstd)
                                ? CompressionMethod::Zstd
                                : CompressionMethod::Deflate;
        return Lightweight::SqlBackup::BackupSettings { .method = method, .level = 6 };
    }

    /// Message for a run that finished without throwing but reported
    /// table-level errors — the "silently lost tables" case.
    /// @param errorCount Number of `Progress::State::Error` events observed.
    /// @return One-line, user-displayable explanation.
    [[nodiscard]] QString PartialBackupMessage(std::size_t errorCount)
    {
        return QStringLiteral("%1 table(s) failed to back up — the incomplete archive was discarded and the "
                              "previous one kept. Fix the reported errors and run the backup again.")
            .arg(errorCount);
    }

    /// Message for a restore that reported table-level errors. A restore is not
    /// transactional (each archived table is dropped, recreated and loaded
    /// individually), so a table whose `CREATE TABLE` failed stays dropped —
    /// the user must be told the target is no longer trustworthy.
    /// @param errorCount Number of `Progress::State::Error` events observed.
    /// @return One-line, user-displayable explanation.
    [[nodiscard]] QString PartialRestoreMessage(std::size_t errorCount)
    {
        return QStringLiteral("%1 table(s) failed to restore. The restore is not transactional: tables are dropped "
                              "and recreated one at a time, so the target database may be left INCOMPLETE — do not "
                              "use it before a successful re-run.")
            .arg(errorCount);
    }

    /// Default `BackupOperation`: the real `SqlBackup::Backup` call.
    void RunSqlBackup(std::filesystem::path const& outputFile,
                      std::string const& connectionString,
                      std::string const& schema,
                      Lightweight::SqlBackup::ProgressManager& progress)
    {
        Lightweight::SqlBackup::Backup(outputFile,
                                       Lightweight::SqlConnectionString { connectionString },
                                       BackupConcurrency(),
                                       progress,
                                       schema,
                                       "*",
                                       {},
                                       ManagedBackupSettings());
    }

    /// Default `RestoreOperation`: the real `SqlBackup::Restore` call.
    void RunSqlRestore(std::filesystem::path const& archiveFile,
                       std::string const& connectionString,
                       std::string const& schema,
                       Lightweight::SqlBackup::ProgressManager& progress)
    {
        Lightweight::SqlBackup::Restore(
            archiveFile, Lightweight::SqlConnectionString { connectionString }, BackupConcurrency(), progress, schema);
    }
} // namespace

ManagedBackupController::ManagedBackupController(QObject* parent):
    QObject(parent),
    _backupOperation(&RunSqlBackup),
    _restoreOperation(&RunSqlRestore),
    _backupFolder(QSettings().value(QString::fromLatin1(kKeyBackupFolder)).toString())
{
    // One worker: profiles are backed up strictly sequentially; per-table
    // parallelism happens inside SqlBackup itself.
    _pool.setMaxThreadCount(1);

    // Route structured per-table progress into the per-profile table models.
    // tableProgress is emitted queued from the worker; this connection runs
    // the routing on the GUI thread (auto connection, same-thread delivery).
    connect(this, &ManagedBackupController::tableProgress, this, &ManagedBackupController::RouteTableProgress);
    connect(this, &ManagedBackupController::tableTotalKnown, this, &ManagedBackupController::RouteTableTotal);
}

void ManagedBackupController::setBackupFolder(QString const& folder)
{
    if (_backupFolder == folder)
        return;
    _backupFolder = folder;
    QSettings().setValue(QString::fromLatin1(kKeyBackupFolder), folder);
    emit backupFolderChanged();
    refreshStatus();
}

QString ManagedBackupController::effectiveBackupFolder() const
{
    return _backupFolder.isEmpty() ? defaultBackupFolder() : _backupFolder;
}

QString ManagedBackupController::defaultBackupFolder() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/backups");
}

void ManagedBackupController::setProfiles(std::vector<Lightweight::Config::Profile> profiles)
{
    _profiles = std::move(profiles);
    QStringList names;
    names.reserve(static_cast<int>(_profiles.size()));
    for (auto const& profile: _profiles)
        names << QString::fromStdString(profile.name);
    _status.resetProfiles(names);
    refreshStatus();
}

void ManagedBackupController::setBusyProbe(std::function<bool()> probe)
{
    _busyProbe = std::move(probe);
}

void ManagedBackupController::setBackupOperation(BackupOperation operation)
{
    _backupOperation = operation ? std::move(operation) : BackupOperation { &RunSqlBackup };
}

void ManagedBackupController::setRestoreOperation(RestoreOperation operation)
{
    _restoreOperation = operation ? std::move(operation) : RestoreOperation { &RunSqlRestore };
}

void ManagedBackupController::SetFolderProblem(QString problem)
{
    if (_folderProblem == problem)
        return;
    _folderProblem = std::move(problem);
    emit folderProblemChanged();
}

void ManagedBackupController::RouteTableProgress(QString const& profile,
                                                 QString const& table,
                                                 quint64 current,
                                                 qint64 total,
                                                 QString const& state,
                                                 QString const& message)
{
    if (auto* tables = _status.tablesFor(profile))
        tables->applyProgress(table, current, total, state, message);
}

void ManagedBackupController::RouteTableTotal(QString const& profile, int totalTables)
{
    if (auto* tables = _status.tablesFor(profile))
        tables->setTotalTables(totalTables);
}

void ManagedBackupController::refreshStatus()
{
    auto const folder = ToPath(effectiveBackupFolder());

    if (auto usable = ManagedBackup::CheckWritableFolder(folder); !usable)
        SetFolderProblem(QString::fromStdString(usable.error()));
    else
        SetFolderProblem({});

    std::vector<std::string> names;
    names.reserve(_profiles.size());
    for (auto const& profile: _profiles)
        names.push_back(profile.name);

    for (auto const& entry: ManagedBackup::PlanArchiveNames(names))
    {
        auto const archiveStatus = ManagedBackup::ScanArchive(folder / entry.fileName);
        _status.setArchiveStatus(QString::fromStdString(entry.profileName),
                                 archiveStatus.exists,
                                 static_cast<qulonglong>(archiveStatus.sizeBytes),
                                 archiveStatus.exists ? ToQDateTime(archiveStatus.mtime) : QDateTime {});
    }
}

bool ManagedBackupController::StartRunGuard(char const* what)
{
    if (phase() != Phase::Idle)
    {
        emit logLine(QStringLiteral("%1: a backup/restore run is already in progress.").arg(QLatin1String(what)),
                     LogLevel::Warning);
        return false;
    }
    if (_busyProbe && _busyProbe())
    {
        emit logLine(QStringLiteral("%1: another operation is busy (migration in progress?).").arg(QLatin1String(what)),
                     LogLevel::Warning);
        return false;
    }
    refreshStatus();
    if (!_folderProblem.isEmpty())
    {
        emit logLine(QStringLiteral("%1: %2").arg(QLatin1String(what), _folderProblem), LogLevel::Error);
        return false;
    }
    return true;
}

std::optional<ManagedBackup::ArchivePlanEntry> ManagedBackupController::PlanEntryFor(std::string const& name) const
{
    std::vector<std::string> names;
    names.reserve(_profiles.size());
    for (auto const& profile: _profiles)
        names.push_back(profile.name);
    auto plan = ManagedBackup::PlanArchiveNames(names);
    for (auto& entry: plan)
        if (entry.profileName == name)
            return std::move(entry);
    return std::nullopt;
}

void ManagedBackupController::backupAll()
{
    RunBackups(_profiles, "Backup all");
}

void ManagedBackupController::backupProfile(QString const& name)
{
    auto const stdName = name.toStdString();
    auto const it = std::ranges::find(_profiles, stdName, &Lightweight::Config::Profile::name);
    if (it == _profiles.end())
    {
        emit logLine(QStringLiteral("Backup: unknown profile '%1'.").arg(name), LogLevel::Error);
        return;
    }
    // Honour the collision flag from the full-list plan: a single-profile plan
    // could never collide with itself, so without this check a profile whose
    // sanitized name matches an earlier one would overwrite that profile's
    // archive.
    if (auto const entry = PlanEntryFor(stdName); entry && entry->collision)
    {
        emit logLine(QStringLiteral("Backup: profile '%1' archive name '%2' collides with another profile — "
                                    "rename the profile.")
                         .arg(name, QString::fromStdString(entry->fileName)),
                     LogLevel::Error);
        return;
    }
    RunBackups({ *it }, "Backup");
}

void ManagedBackupController::RunBackups(std::vector<Lightweight::Config::Profile> profiles, char const* what)
{
    if (!StartRunGuard(what))
        return;

    // Plan against the FULL profile list (via PlanEntryFor) so a single-profile
    // run sees the same collision flag backupAll would — a plan built from only
    // the profiles handed in here could never flag a name clash. `profiles` is
    // always a subset of `_profiles`, so every entry resolves; the fallback
    // keeps the vectors aligned defensively.
    std::vector<ManagedBackup::ArchivePlanEntry> plan;
    plan.reserve(profiles.size());
    for (auto const& profile: profiles)
    {
        if (auto entry = PlanEntryFor(profile.name); entry)
            plan.push_back(std::move(*entry));
        else
            plan.push_back(
                ManagedBackup::ArchivePlanEntry { .profileName = profile.name,
                                                  .fileName = ManagedBackup::SanitizeArchiveName(profile.name) + ".zip",
                                                  .collision = false });
    }
    auto const folder = ToPath(effectiveBackupFolder());

    _phase.store(Phase::Running, std::memory_order_release);
    emit phaseChanged();
    for (auto const& entry: plan)
        _status.setRunState(QString::fromStdString(entry.profileName), QStringLiteral("queued"));

    auto* self = this;
    // The operation is copied into the task now, on the GUI thread, so the
    // worker never reads the member while another thread could replace it.
    auto backupOperation = _backupOperation;
    // QThreadPool::start() takes ownership of the raw pointer (it deletes
    // the task after run() returns, since setAutoDelete(true) is set in the
    // constructor); release() makes that ownership transfer explicit.
    auto task = std::make_unique<FunctionTask>([self,
                                                profiles = std::move(profiles),
                                                plan = std::move(plan),
                                                folder,
                                                backupOperation = std::move(backupOperation)] {
        auto resolver = Lightweight::Secrets::MakeDefaultResolver();
        int okCount = 0;
        int failCount = 0;
        for (auto const& [profile, entry]: std::views::zip(profiles, plan))
        {
            auto const qname = QString::fromStdString(profile.name);
            auto const setState = [self, qname](QString const& state, QString const& error = {}) {
                QMetaObject::invokeMethod(
                    self,
                    [self, qname, state, error] { self->_status.setRunState(qname, state, error); },
                    Qt::QueuedConnection);
            };
            if (entry.collision)
            {
                ++failCount;
                setState(QStringLiteral("failed"),
                         QStringLiteral("archive name collides with another profile — rename the profile"));
                continue;
            }
            auto const cs = ManagedBackup::ResolveConnectionString(profile, resolver);
            if (!cs)
            {
                ++failCount;
                setState(QStringLiteral("failed"), QString::fromStdString(cs.error()));
                continue;
            }
            setState(QStringLiteral("running"));
            // Fresh run for this profile: drop any table rows from a previous
            // run before new progress arrives (queued onto the GUI thread).
            QMetaObject::invokeMethod(
                self,
                [self, qname] {
                    if (auto* tables = self->status()->tablesFor(qname))
                        tables->clearTables();
                },
                Qt::QueuedConnection);
            auto const finalPath = folder / entry.fileName;
            auto const tmpPath = ManagedBackup::TempArchivePath(finalPath);
            try
            {
                EmittingProgressManager pm(self, qname);
                backupOperation(tmpPath, *cs, profile.schema, pm);
                // `SqlBackup::Backup` does NOT throw when individual tables
                // fail: the per-chunk exception is caught, the table is marked
                // failed, and the failure is reported only as a
                // `Progress::State::Error` event. Committing regardless would
                // replace the last good archive with one that silently lost
                // tables and mark the profile "ok". The error tally is the only
                // way to notice, so it decides whether the temporary archive is
                // promoted at all.
                if (auto const errorCount = pm.ErrorCount(); errorCount > 0)
                    throw std::runtime_error(PartialBackupMessage(errorCount).toStdString());
                if (auto committed = ManagedBackup::CommitArchive(tmpPath, finalPath); !committed)
                    throw std::runtime_error(committed.error());
                ++okCount;
                setState(QStringLiteral("ok"));
            }
            catch (std::exception const& e)
            {
                ++failCount;
                std::error_code ec;
                std::filesystem::remove(tmpPath, ec);
                auto const reason = QString::fromUtf8(e.what());
                setState(QStringLiteral("failed"), reason);
                QMetaObject::invokeMethod(self,
                                          "logLine",
                                          Qt::QueuedConnection,
                                          Q_ARG(QString, QStringLiteral("Backup of '%1' failed: %2").arg(qname, reason)),
                                          Q_ARG(DbtoolGui::LogLevel, LogLevel::Error));
            }
        }
        auto const summary = QString::fromStdString(ManagedBackup::FormatRunSummary(okCount, failCount));
        auto const ok = failCount == 0;
        QMetaObject::invokeMethod(
            self,
            [self, ok, summary] {
                self->_phase.store(Phase::Idle, std::memory_order_release);
                emit self->phaseChanged();
                self->refreshStatus();
                emit self->finished(ok, summary);
            },
            Qt::QueuedConnection);
    });
    _pool.start(task.release());
}

void ManagedBackupController::restoreArchive(QString const& archiveProfile, QString const& targetProfile)
{
    RunRestore(archiveProfile, targetProfile, {}, {});
}

void ManagedBackupController::restoreArchiveToConnectionString(QString const& archiveProfile,
                                                               QString const& connectionString,
                                                               QString const& schema)
{
    RunRestore(archiveProfile, {}, connectionString, schema);
}

void ManagedBackupController::RunRestore(QString const& archiveProfile,
                                         QString const& targetProfileName,
                                         QString const& rawConnectionString,
                                         QString const& schema)
{
    if (!StartRunGuard("Restore"))
        return;

    std::optional<Lightweight::Config::Profile> targetProfile;
    auto effectiveSchema = schema;
    if (!targetProfileName.isEmpty())
    {
        auto const it = std::ranges::find(_profiles, targetProfileName.toStdString(), &Lightweight::Config::Profile::name);
        if (it == _profiles.end())
        {
            emit logLine(QStringLiteral("Restore: unknown target profile '%1'.").arg(targetProfileName), LogLevel::Error);
            return;
        }
        targetProfile = *it;
        effectiveSchema = QString::fromStdString(it->schema);
    }

    // Resolve the archive file name via the full-list plan so restore agrees
    // with backup on which file belongs to which profile, and refuse before any
    // destructive work when the source profile's name collides with another —
    // otherwise a restore could read (and overwrite the target with) the wrong
    // profile's data. Unknown archive names (not a configured profile) fall
    // back to a plain sanitize, matching the earlier behaviour.
    auto const archiveEntry = PlanEntryFor(archiveProfile.toStdString());
    if (archiveEntry && archiveEntry->collision)
    {
        emit logLine(QStringLiteral("Restore: profile '%1' archive name '%2' collides with another profile — "
                                    "rename the profile.")
                         .arg(archiveProfile, QString::fromStdString(archiveEntry->fileName)),
                     LogLevel::Error);
        return;
    }
    auto const fileName =
        archiveEntry ? archiveEntry->fileName : ManagedBackup::SanitizeArchiveName(archiveProfile.toStdString()) + ".zip";
    auto const archivePath = ToPath(effectiveBackupFolder()) / fileName;
    if (!ManagedBackup::ScanArchive(archivePath).exists)
    {
        emit logLine(
            QStringLiteral("Restore: no managed archive for '%1' (expected %2).").arg(archiveProfile, FromPath(archivePath)),
            LogLevel::Error);
        return;
    }

    _phase.store(Phase::Running, std::memory_order_release);
    emit phaseChanged();

    auto* self = this;
    auto const archiveName = archiveProfile;
    auto const rawCs = rawConnectionString.toStdString();
    auto const schemaStd = effectiveSchema.toStdString();
    // Copied on the GUI thread — see the note in RunBackups().
    auto restoreOperation = _restoreOperation;
    // See the ownership-transfer note in RunBackups(): QThreadPool::start()
    // deletes the task after run() (setAutoDelete(true)); release() makes
    // that explicit.
    auto task = std::make_unique<FunctionTask>(
        [self, archivePath, targetProfile, rawCs, schemaStd, archiveName, restoreOperation = std::move(restoreOperation)] {
            auto cs = rawCs;
            if (targetProfile)
            {
                auto resolver = Lightweight::Secrets::MakeDefaultResolver();
                auto const resolved = ManagedBackup::ResolveConnectionString(*targetProfile, resolver);
                if (!resolved)
                {
                    auto const message = QString::fromStdString(resolved.error());
                    QMetaObject::invokeMethod(
                        self,
                        [self, message] {
                            self->_phase.store(Phase::Idle, std::memory_order_release);
                            emit self->phaseChanged();
                            emit self->logLine(message, LogLevel::Error);
                            emit self->finished(false, message);
                        },
                        Qt::QueuedConnection);
                    return;
                }
                cs = *resolved;
            }
            QString summary;
            bool ok = true;
            try
            {
                EmittingProgressManager pm(self, archiveName);
                restoreOperation(archivePath, cs, schemaStd, pm);
                // Like Backup(), Restore() reports a table it could not
                // recreate as a `Progress::State::Error` and carries on — see
                // `CreateTablesInOrder`, which drops the archived table first
                // and only then tries to create it. Without this check a
                // restore that dropped tables it never recreated would be
                // reported as a success.
                if (auto const errorCount = pm.ErrorCount(); errorCount > 0)
                {
                    ok = false;
                    summary = PartialRestoreMessage(errorCount);
                }
                else
                    summary = QStringLiteral("Restore read %1").arg(FromPath(archivePath));
            }
            catch (std::exception const& e)
            {
                ok = false;
                // A restore that threw part-way through is just as
                // non-transactional as one that reported table errors: whatever
                // it had already dropped stays dropped.
                summary = QStringLiteral("%1 The target database may be left INCOMPLETE — do not use it before a "
                                         "successful re-run.")
                              .arg(QString::fromUtf8(e.what()));
            }
            QMetaObject::invokeMethod(
                self,
                [self, ok, summary] {
                    self->_phase.store(Phase::Idle, std::memory_order_release);
                    emit self->phaseChanged();
                    if (!ok)
                        emit self->logLine(summary, LogLevel::Error);
                    emit self->finished(ok, summary);
                },
                Qt::QueuedConnection);
        });
    _pool.start(task.release());
}

} // namespace DbtoolGui
