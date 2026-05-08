// SPDX-License-Identifier: Apache-2.0
//
// `MigrationRunner` is the GUI's asynchronous wrapper around
// `Lightweight::SqlMigration::MigrationManager`. It fans work out to a
// dedicated `QThreadPool` so the UI thread stays responsive during long
// ALTER statements; each operation's progress is surfaced via Qt signals the
// view-model marshals onto the GUI thread.
//
// The runner does *not* own a `MigrationManager`: the global singleton is
// reused so the CLI (`dbtool`) and GUI see the same state when they share a
// process. Consumers are responsible for wiring the plugin directory +
// database connection before kicking off any run.

#pragma once

#include <atomic>
#include <functional>

#include <QtCore/QObject>
#include <QtCore/QStringList>
#include <QtCore/QThreadPool>
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

    explicit MigrationRunner(QObject* parent = nullptr);

    void SetManager(Lightweight::SqlMigration::MigrationManager* manager) noexcept;

    /// Dry-run / apply up to an inclusive target timestamp. An empty or
    /// "0" target applies every pending migration — same semantics as
    /// `dbtool migrate`. Non-empty targets filter pending migrations to
    /// those whose timestamp is `<= target`.
    Q_INVOKABLE void dryRunUpTo(QString const& targetTimestamp);
    Q_INVOKABLE void applyUpTo(QString const& targetTimestamp);

    /// Dry-run / apply the explicit list of migration timestamps. Only
    /// timestamps that are currently pending are acted on; already-applied
    /// timestamps are silently skipped. Used when the user has ticked
    /// individual rows in the migration list.
    Q_INVOKABLE void dryRunSelected(QStringList const& timestamps);
    Q_INVOKABLE void applySelected(QStringList const& timestamps);

    /// Rolls back migrations applied after the release named `version`.
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

  signals:
    void progress(qulonglong timestamp, QString title, int index, int total);
    void logLine(QString line, int level);
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

    Lightweight::SqlMigration::MigrationManager* _manager = nullptr;
    QThreadPool _pool;
    std::atomic<Phase> _phase { Phase::Idle };
    std::atomic<bool> _cancelRequested { false };
};

} // namespace DbtoolGui
