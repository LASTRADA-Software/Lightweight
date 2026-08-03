// SPDX-License-Identifier: Apache-2.0
//
// Orchestrates the managed backup folder: one archive per profile, written
// sequentially by a single worker thread; per-profile status is published
// through BackupStatusListModel. All naming / scanning / atomic-replace
// decisions live in ManagedBackupCore (pure, separately unit-tested).

#pragma once

#include "LogLevel.hpp"
#include "ManagedBackupCore.hpp"
#include "Models/BackupStatusListModel.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <Config/ProfileStore.hpp>
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QThreadPool>
#include <QtQmlIntegration/QtQmlIntegration>

// Forward declaration keeps the Lightweight/SqlBackup.hpp chain out of this
// moc-processed header (same approach as BackupRunner.hpp).
namespace Lightweight::SqlBackup
{
struct ProgressManager;
}

namespace DbtoolGui
{

/// Owns the managed backup folder setting, drives archive-status scans, and
/// (in later tasks) sequences backup-all / restore runs on a single worker
/// thread. QML-facing state (folder, phase, per-profile status) lives here;
/// pure decisions (naming, scanning, atomic replace) are delegated to
/// `ManagedBackup::*` in `ManagedBackupCore.hpp`.
class ManagedBackupController: public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("owned by AppController")

  public:
    /// Whether the controller is idle or currently running a backup/restore.
    enum class Phase : std::uint8_t
    {
        Idle,
        Running,
    };
    Q_ENUM(Phase)

    /// The operation that writes one profile's archive. Injected so the
    /// failure semantics (a run that reports table-level errors without
    /// throwing) can be driven deterministically from tests; production wiring
    /// forwards to `Lightweight::SqlBackup::Backup`.
    /// @param outputFile Archive to write — the temporary sibling, only
    ///        promoted over the live archive when the run reported no errors.
    /// @param connectionString Resolved ODBC connection string of the source.
    /// @param schema Schema to back up ("" = server default).
    /// @param progress Progress sink. Table-level failures are reported
    ///        through it as `Progress::State::Error`, not thrown.
    using BackupOperation = std::function<void(std::filesystem::path const& outputFile,
                                               std::string const& connectionString,
                                               std::string const& schema,
                                               Lightweight::SqlBackup::ProgressManager& progress)>;

    /// The operation that reads one archive back into a database. Same
    /// injection rationale as `BackupOperation`; production wiring forwards to
    /// `Lightweight::SqlBackup::Restore`.
    /// @param archiveFile Archive to read.
    /// @param connectionString Resolved ODBC connection string of the target.
    /// @param schema Schema override ("" = server default).
    /// @param progress Progress sink. Table-level failures are reported
    ///        through it as `Progress::State::Error`, not thrown.
    using RestoreOperation = std::function<void(std::filesystem::path const& archiveFile,
                                                std::string const& connectionString,
                                                std::string const& schema,
                                                Lightweight::SqlBackup::ProgressManager& progress)>;

    Q_PROPERTY(QString backupFolder READ backupFolder WRITE setBackupFolder NOTIFY backupFolderChanged)
    Q_PROPERTY(QString effectiveBackupFolder READ effectiveBackupFolder NOTIFY backupFolderChanged)
    Q_PROPERTY(QString defaultBackupFolder READ defaultBackupFolder CONSTANT)
    Q_PROPERTY(Phase phase READ phase NOTIFY phaseChanged)
    Q_PROPERTY(DbtoolGui::BackupStatusListModel* status READ status CONSTANT)
    Q_PROPERTY(QString folderProblem READ folderProblem NOTIFY folderProblemChanged)

    explicit ManagedBackupController(QObject* parent = nullptr);

    /// User-configured backup folder ("" = use defaultBackupFolder()).
    [[nodiscard]] QString const& backupFolder() const noexcept
    {
        return _backupFolder;
    }

    /// Sets and persists the backup folder (QSettings key "backup/folder");
    /// re-scans archive status. Empty resets to the platform default.
    /// @param folder Absolute directory path or empty.
    Q_INVOKABLE void setBackupFolder(QString const& folder);

    /// The folder actually used: backupFolder() when set, else the default.
    [[nodiscard]] QString effectiveBackupFolder() const;

    /// Platform default: <AppDataLocation>/backups.
    [[nodiscard]] QString defaultBackupFolder() const;

    /// Current run phase (idle vs. running a backup/restore).
    [[nodiscard]] Phase phase() const noexcept
    {
        return _phase.load(std::memory_order_acquire);
    }

    /// The per-profile status model backing the backups view.
    [[nodiscard]] BackupStatusListModel* status() noexcept
    {
        return &_status;
    }

    /// Human-readable reason the managed folder cannot be used, or "".
    [[nodiscard]] QString const& folderProblem() const noexcept
    {
        return _folderProblem;
    }

    /// Replaces the profile snapshot that backup-all iterates over and
    /// resets the status model. Called by AppController on profile load.
    /// @param profiles Copied store profiles, in store order.
    void setProfiles(std::vector<Lightweight::Config::Profile> profiles);

    /// Injects the "is some other runner busy?" probe consulted before any
    /// backup/restore starts (e.g. migration in flight). Defaults to
    /// "never busy".
    /// @param probe Callable returning true to veto a new run.
    void setBusyProbe(std::function<bool()> probe);

    /// Replaces the operation used to write archives. Must not be called
    /// while a run is in flight; the current value is copied into the worker
    /// task when a run starts.
    /// @param operation New operation; an empty callable restores the default
    ///        (`Lightweight::SqlBackup::Backup`).
    void setBackupOperation(BackupOperation operation);

    /// Replaces the operation used to read archives back. Same contract as
    /// `setBackupOperation`.
    /// @param operation New operation; an empty callable restores the default
    ///        (`Lightweight::SqlBackup::Restore`).
    void setRestoreOperation(RestoreOperation operation);

    /// Re-scans <folder>/<profile>.zip for every profile and updates the
    /// model's archive columns plus folderProblem.
    Q_INVOKABLE void refreshStatus();

    /// Backs up every configured profile into the managed folder,
    /// sequentially, continuing past failures. No-op with a warning when
    /// already running, the busy probe vetoes, or the folder is unusable.
    Q_INVOKABLE void backupAll();

    /// Backs up a single profile into the managed folder. Same guards as
    /// backupAll().
    /// @param name Profile name as shown in the status model.
    Q_INVOKABLE void backupProfile(QString const& name);

    /// Restores `<archiveProfile>.zip` from the managed folder into the
    /// database of `targetProfile` (may equal archiveProfile). Destructive
    /// for the target. Guards mirror backupAll(); missing archive or
    /// unknown target log an error and do nothing. Secret resolution for the
    /// target profile happens off the calling thread (see `RunRestore`).
    /// @param archiveProfile Profile whose managed archive is the source.
    /// @param targetProfile Profile whose database is the destination.
    Q_INVOKABLE void restoreArchive(QString const& archiveProfile, QString const& targetProfile);

    /// Restores `<archiveProfile>.zip` into an explicitly-given ODBC
    /// connection string (used by the dialog's "Custom connection string"
    /// choice).
    /// @param archiveProfile Profile whose managed archive is the source.
    /// @param connectionString Raw ODBC connection string of the target.
    /// @param schema Optional schema override, like dbtool's --schema.
    Q_INVOKABLE void restoreArchiveToConnectionString(QString const& archiveProfile,
                                                      QString const& connectionString,
                                                      QString const& schema);

  signals:
    void logLine(QString line, DbtoolGui::LogLevel level);
    /// Structured per-table progress for the live details panel. Emitted
    /// (queued) from the backup/restore worker thread; routed into the
    /// profile's BackupTableListModel on the GUI thread.
    /// @param profile Profile whose backup/restore produced this update.
    /// @param table Table name.
    /// @param current Rows processed so far.
    /// @param total Total rows, or -1 when unknown.
    /// @param state "queued"/"running"/"done"/"error"/"warning".
    /// @param message Latest progress message.
    void tableProgress(QString profile, QString table, quint64 current, qint64 total, QString state, QString message);
    /// Emitted once per run, before any `tableProgress`, with the number of
    /// tables the run will process (from SqlBackup's post-schema-scan count).
    /// Lets the detail panel show a fixed denominator instead of one that grows
    /// as tables are discovered.
    /// @param profile Profile whose run this total belongs to.
    /// @param totalTables Number of tables the run will process.
    void tableTotalKnown(QString profile, int totalTables);
    void finished(bool ok, QString summary);
    void phaseChanged();
    void backupFolderChanged();
    void folderProblemChanged();

  private:
    /// Updates `_folderProblem` and emits `folderProblemChanged()` only when
    /// the value actually changes.
    /// @param problem New problem text ("" clears it).
    void SetFolderProblem(QString problem);

    /// GUI-thread slot: routes a tableProgress emission into the matching
    /// profile's per-table model. Unknown profiles are ignored.
    void RouteTableProgress(QString const& profile,
                            QString const& table,
                            quint64 current,
                            qint64 total,
                            QString const& state,
                            QString const& message);

    /// GUI-thread slot: routes a tableTotalKnown emission into the matching
    /// profile's per-table model. Unknown profiles are ignored.
    /// @param profile Profile whose run this total belongs to.
    /// @param totalTables Number of tables the run will process.
    void RouteTableTotal(QString const& profile, int totalTables);

    /// Shared entry guard for backupAll()/backupProfile(): refuses (with a
    /// logged Warning/Error line) when a run is already in progress, the
    /// busy probe vetoes, or the managed folder is unusable.
    /// @param what Short label used in the logged refusal message.
    /// @return True when it is safe to start a run.
    [[nodiscard]] bool StartRunGuard(char const* what);

    /// Plans archive names over the FULL `_profiles` list and returns the
    /// entry for `name`, so the per-profile backup / restore / status paths
    /// see the same collision flag a backup-all run would. Planning against
    /// only the profile(s) being acted on would never flag a collision,
    /// letting two profiles that sanitize to the same file name silently
    /// overwrite one archive (and restore the wrong profile's data).
    /// @param name Profile name to look up.
    /// @return The full-list plan entry for `name`, or nullopt when `name` is
    ///         not a configured profile.
    [[nodiscard]] std::optional<ManagedBackup::ArchivePlanEntry> PlanEntryFor(std::string const& name) const;

    /// Runs the sequential backup worker for `profiles` on the pool thread,
    /// publishing per-profile status via `_status` and emitting `finished`
    /// on completion. Called by backupAll() (all profiles) and
    /// backupProfile() (a one-profile subset).
    /// @param profiles Profiles to back up, in the order they run.
    /// @param what Short label used in guard-refusal log lines.
    void RunBackups(std::vector<Lightweight::Config::Profile> profiles, char const* what);

    /// Shared worker for restoreArchive()/restoreArchiveToConnectionString().
    /// Runs the busy/folder guard first (so an in-progress run always wins
    /// over a target-profile lookup failure), then, when `targetProfileName`
    /// is non-empty, looks it up in `_profiles` synchronously (unknown name
    /// -> synchronous error log, phase untouched) and uses its schema;
    /// otherwise `rawConnectionString` + `schema` are used as given (the
    /// custom-connection-string path). The missing-archive check also stays
    /// synchronous. Once dispatched to the pool thread, a looked-up target
    /// profile has its secret resolved there (`MakeDefaultResolver()` +
    /// `ManagedBackup::ResolveConnectionString`) — never on the calling
    /// (typically GUI) thread — before `SqlBackup::Restore` runs; a resolve
    /// failure emits a queued `logLine(Error)` and `finished(false, ...)` and
    /// resets phase to Idle.
    /// @param archiveProfile Profile whose managed archive is the source.
    /// @param targetProfileName Name of the target profile, or empty to use
    ///        `rawConnectionString` + `schema` directly.
    /// @param rawConnectionString Target ODBC connection string, used only
    ///        when `targetProfileName` is empty.
    /// @param schema Schema override, used only when `targetProfileName` is
    ///        empty (a non-empty target profile supplies its own schema).
    void RunRestore(QString const& archiveProfile,
                    QString const& targetProfileName,
                    QString const& rawConnectionString,
                    QString const& schema);

    BackupOperation _backupOperation;
    RestoreOperation _restoreOperation;
    std::atomic<Phase> _phase { Phase::Idle };
    BackupStatusListModel _status;
    std::vector<Lightweight::Config::Profile> _profiles;
    std::function<bool()> _busyProbe;
    QString _backupFolder;
    QString _folderProblem;

    // `_pool` MUST be declared LAST so it is destroyed FIRST: non-static
    // members are destroyed in reverse declaration order, and ~QThreadPool
    // blocks until the running worker returns. The worker touches the members
    // above (`_phase`, `_status`, the injected operations), so the pool has to
    // join before any of them go away. Declaring it first would do the exact
    // opposite — it would be destroyed last, after everything it protects.
    QThreadPool _pool;
};

} // namespace DbtoolGui
