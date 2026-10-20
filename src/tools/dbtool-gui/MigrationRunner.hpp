// SPDX-License-Identifier: Apache-2.0
//
// `MigrationRunner` is the GUI's asynchronous wrapper around
// `Lightweight::SqlMigration::MigrationManager`. It fans work out to a
// dedicated `SerialWorker` so the UI thread stays responsive during long
// ALTER statements; each operation's progress is surfaced via Qt signals the
// view-model marshals onto the GUI thread.
//
// The runner does *not* own a `MigrationManager`: the global singleton is
// reused so the CLI (`dbtool`) and GUI see the same state when they share a
// process. Consumers are responsible for wiring the plugin directory +
// database connection before kicking off any run.

#pragma once

#include "LogLevel.hpp"
#include "Models/ProgressTaskModel.hpp"
#include "SerialWorker.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string_view>
#include <utility>

#include <QtCore/QObject>
#include <QtCore/QStringList>
#include <QtCore/QTimer>
#include <QtQmlIntegration/QtQmlIntegration>

// Forward declaration keeps the Lightweight/SqlMigration.hpp chain out of
// this moc-processed header.
namespace Lightweight
{
namespace SqlMigration
{
    class MigrationManager;
}
} // namespace Lightweight

namespace DbtoolGui
{

/// Whether an apply run may go ahead although the work it runs requires a backup that does not exist.
enum class BackupWaiver : std::uint8_t
{
    NotWaived, ///< The run refuses while a required backup is missing.
    Waived,    ///< The run goes ahead, logs a warning and records a `waived` row in `schema_backups`.
};

/// Whether an apply, apply-selected or rollback run takes the migration lock; dry runs never take it.
enum class MigrationLocking : std::uint8_t
{
    Acquire, ///< The run holds the migration lock and refuses when another migrator holds it.
    Skip,    ///< The run goes ahead without the lock and says so in the log.
};

class MigrationRunner: public QObject
{
    Q_OBJECT
    QML_ELEMENT
  public:
    enum class Phase : int
    {
        Idle,
        Running,
        Cancelling,
    };
    Q_ENUM(Phase)

    Q_PROPERTY(Phase phase READ phase NOTIFY phaseChanged)
    /// The tasks of the current or last run: its pre-migrate steps and, for an apply run, its migrations.
    /// They stay after the run ended, failed rows included, until the next run (also a rollback) starts.
    Q_PROPERTY(DbtoolGui::ProgressTaskModel* tasks READ tasks CONSTANT)

    /// Runs the plugins' post-migrate hooks against the manager it is given. Returns
    /// false when a hook failed, after reporting each failure through the log callback.
    /// An exception it throws is logged and fails the run like a `false` result.
    using PostMigrateHook = std::function<bool(Lightweight::SqlMigration::MigrationManager& manager,
                                               std::function<void(std::string_view)> const& logError)>;

    explicit MigrationRunner(QObject* parent = nullptr);

    void SetManager(Lightweight::SqlMigration::MigrationManager* manager) noexcept;

    /// Injects the "is some other runner busy?" probe consulted before any
    /// *mutating* run (apply / rollback) starts. Migrations, ad-hoc backups
    /// and managed backups all touch the same database, so a migration that
    /// starts mid-backup tears the archive being written. Defaults to
    /// "never busy". Dry-runs are read-only and are not gated.
    /// @param probe Callable returning true to veto a new run.
    void setBusyProbe(std::function<bool()> probe);

    /// Injects the hook run on the worker thread after every successful apply run
    /// (also when nothing was pending), while the migration lock is still held. A
    /// failing hook fails the run. Defaults to none. Dry-runs never invoke it.
    /// @param hook Callable running the plugins' post-migrate hooks.
    void setPostMigrateHook(PostMigrateHook hook);

    /// Sets the waiver of the apply runs started from now on; each run reads it once when it starts.
    /// The runner never resets it; the caller does after each run.
    /// @param waiver Whether a run may go ahead without a required backup.
    void setBackupWaived(BackupWaiver waiver) noexcept;

    /// How long a run waits for the migration lock by default, as `SqlScopedLock::TryConstruct` does.
    static constexpr std::chrono::milliseconds DefaultLockTimeout { 30000 };

    /// Sets whether the apply, apply-selected and rollback runs started from now on take the migration
    /// lock; each run reads it once when it starts. Defaults to `MigrationLocking::Acquire`. The backup
    /// requirement is checked the same way either way.
    /// @param locking Whether the runs take the lock.
    void setLocking(MigrationLocking locking) noexcept;

    /// Sets how long the runs started from now on wait for the migration lock before they refuse.
    /// @param timeout The wait; `DefaultLockTimeout` unless set.
    void setLockTimeout(std::chrono::milliseconds timeout) noexcept;

    /// The one worker thread every use of the shared migration connection runs on: the runs, and the
    /// controller's plugin and database jobs. A single thread, because apply runs hold the migration
    /// lock on that connection and an ODBC connection has one user at a time.
    [[nodiscard]] SerialWorker& Worker() noexcept
    {
        return _worker;
    }

    /// @return The tasks of the current or last run; cleared when the next run starts.
    [[nodiscard]] ProgressTaskModel* tasks() noexcept
    {
        return &_tasks;
    }

    /// Dry-run / apply up to an inclusive target timestamp. An empty or
    /// "0" target applies every pending migration — same semantics as
    /// `dbtool migrate`. Non-empty targets filter pending migrations to
    /// those whose timestamp is `<= target`. Both run the plugins'
    /// pre-migrate steps first; the apply run holds the migration lock for
    /// them unless locking is skipped (see `setLocking`), and a cancel or a
    /// failing step ends the run before any migration. Before the steps, the
    /// apply run refuses when the migrations it runs (or a step) require a
    /// backup that does not exist, unless the run is waived (see
    /// `setBackupWaived`); a dry run reports the requirement and goes on.
    Q_INVOKABLE void dryRunUpTo(QString const& targetTimestamp);
    Q_INVOKABLE void applyUpTo(QString const& targetTimestamp);

    /// Dry-run / apply the explicit list of migration timestamps. Only
    /// timestamps that are currently pending are acted on; already-applied
    /// timestamps are silently skipped. Used when the user has ticked
    /// individual rows in the migration list. The pre-migrate steps run first,
    /// as for `dryRunUpTo` / `applyUpTo`.
    Q_INVOKABLE void dryRunSelected(QStringList const& timestamps);
    Q_INVOKABLE void applySelected(QStringList const& timestamps);

    /// Rolls back migrations applied after the release named `version`, under the migration lock
    /// unless locking is skipped (see `setLocking`).
    Q_INVOKABLE void rollbackToRelease(QString const& version);

    /// Best-effort cooperative cancellation. Currently-executing SQL is not
    /// interrupted (ODBC `SQLCancel` is unreliable across drivers); the flag
    /// is checked between per-migration callbacks.
    Q_INVOKABLE void cancel();

    [[nodiscard]] Phase phase() const noexcept
    {
        return _phase.load(std::memory_order_acquire);
    }

    /// Worker-thread helpers (in the .cpp) call this to flip the state
    /// machine back to Idle when a run ends. Public because the helpers
    /// live in an anonymous namespace outside the class — keeping it
    /// private would require friend declarations or extra plumbing.
    void SetPhase(Phase newPhase);

    /// What a run reports from the worker to the GUI thread.
    struct Notification
    {
        /// Which signals the notification emits.
        enum class Kind : std::uint8_t
        {
            /// `logLine(text, level)`.
            LogLine,
            /// `migrationStarted(timestamp)`, `progress(timestamp, title, index, total)` and `logLine(text)`.
            MigrationStarted,
            /// `migrationCompleted(timestamp, text)`; `text` is the new status.
            MigrationCompleted,
            /// `migrationFailed(timestamp)`.
            MigrationFailed,
            /// The phase goes back to Idle, then `finished(ok, text)`.
            Finished,
        };
        /// @return A `LogLine` notification of @p text at @p level.
        [[nodiscard]] static Notification Line(QString text, LogLevel level)
        {
            auto notification = Notification {};
            notification.text = std::move(text);
            notification.level = level;
            return notification;
        }

        Kind kind = Kind::LogLine;
        QString text;
        LogLevel level = LogLevel::Info;
        qulonglong timestamp = 0;
        QString title;
        int index = 0;
        int total = 0;
        bool ok = false;
    };

    /// How often the GUI thread takes the notifications the worker queued.
    static constexpr std::chrono::milliseconds DrainInterval { 16 };

    /// The GUI-thread time one drain may spend; what is left waits for the next one.
    static constexpr std::chrono::milliseconds DrainBudget { 8 };

    /// Worker side: queues @p notification for the GUI thread, which takes queued notifications in
    /// order, in timed batches of at most `DrainBudget`. However fast a run goes, the GUI thread gets
    /// one wake-up per batch, not one event per notification.
    void Notify(Notification notification);

  signals:
    void progress(qulonglong timestamp, QString title, int index, int total);
    void logLine(QString line, DbtoolGui::LogLevel level);
    void finished(bool ok, QString summary);
    void phaseChanged();

    /// Emitted immediately before a migration starts executing (apply) or
    /// gets previewed (dry-run). Consumers flip the row's visible status to
    /// "running" so the user sees live progress per row.
    void migrationStarted(qulonglong timestamp);

    /// Emitted after a migration's Up()/Down() call returns successfully.
    /// Consumers flip the row to "applied" / "pending" accordingly.
    /// `newStatus` is either "applied" (for `applyUpTo` / `applySelected`)
    /// or "pending" (for `rollbackToRelease`).
    void migrationCompleted(qulonglong timestamp, QString newStatus);

    /// Emitted after a migration fails. The row is reset to "pending" so
    /// the user can retry.
    void migrationFailed(qulonglong timestamp);

  private:
    void Enqueue(std::function<void()> task);

    /// Entry guard shared by every mutating run: refuses when no manager is
    /// wired, a run is already in flight, or the busy probe vetoes (logging a
    /// warning in the latter case so the refusal is visible).
    /// @param what Short label used in the logged refusal message.
    /// @return True when it is safe to start a run.
    [[nodiscard]] bool CanStartMutatingRun(char const* what);

    /// Starts a run on the GUI thread: Running, no cancel request, and the last run's tasks cleared.
    void BeginRun();

    /// Emits the queued notifications in order until the queue is empty or `DrainBudget` is spent.
    void DrainNotifications();

    /// Emits the signals of @p notification.
    void Emit(Notification const& notification);

    Lightweight::SqlMigration::MigrationManager* _manager = nullptr;
    std::function<bool()> _busyProbe;
    PostMigrateHook _postMigrateHook;
    BackupWaiver _backupWaiver = BackupWaiver::NotWaived;
    MigrationLocking _locking = MigrationLocking::Acquire;
    std::chrono::milliseconds _lockTimeout = DefaultLockTimeout;
    /// Declared before `_worker`, so the queued work has finished before the model goes.
    ProgressTaskModel _tasks;
    /// The notifications the worker queued for the GUI thread; declared before `_worker` like `_tasks`.
    std::mutex _notificationMutex;
    std::deque<Notification> _notifications;
    QTimer _drainTimer;
    /// Declared before `_worker`, so a queued job never reads them after they are gone.
    std::atomic<Phase> _phase { Phase::Idle };
    std::atomic<bool> _cancelRequested { false };
    SerialWorker _worker;
};

} // namespace DbtoolGui
