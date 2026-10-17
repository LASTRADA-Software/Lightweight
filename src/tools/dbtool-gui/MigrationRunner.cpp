// SPDX-License-Identifier: Apache-2.0

#include "MigrationRunner.hpp"

#include <Lightweight/SqlMigration.hpp>
#include <Lightweight/SqlScopedLock.hpp>

#include <cstdint>
#include <exception>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <PluginIngestion.hpp>
#include <QtCore/QMetaObject>
#include <QtCore/QRunnable>
#include <QtCore/QSet>

namespace DbtoolGui
{

namespace
{

    /// Emit one log line per diagnostic field. The GUI's rich-text log panel
    /// renders each appended entry as its own block, so splitting the migration
    /// context, driver message, and failing SQL across several calls is the
    /// only way to keep them visually separated — embedded `\n` characters get
    /// collapsed to whitespace once the text is wrapped in a `<span>` tag.
    void PostMigrationFailureLines(class MigrationRunner* runner, Lightweight::SqlMigration::MigrationException const& ex);

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
            catch (std::exception const&)
            {
                // The task is responsible for posting a "finished" signal from its
                // own catch handler; swallowing here just prevents an uncaught
                // exception from terminating the worker thread.
            }
        }

      private:
        std::function<void()> _fn;
    };

    /// Collects the list of pending migration pointers whose timestamp is
    /// `<= targetTs` (0 means no upper bound). Returned in timestamp-ascending
    /// order — `MigrationManager::GetPending()` already produces that order.
    std::vector<Lightweight::SqlMigration::MigrationBase const*> CollectPendingUpTo(
        Lightweight::SqlMigration::MigrationManager& manager, uint64_t targetTs)
    {
        std::vector<Lightweight::SqlMigration::MigrationBase const*> out;
        for (auto const* migration: manager.GetPending())
        {
            auto const ts = migration->GetTimestamp().value;
            if (targetTs != 0 && ts > targetTs)
                continue;
            out.push_back(migration);
        }
        return out;
    }

    /// Collects the list of migration pointers matching the explicit timestamp
    /// set. Skips already-applied timestamps silently. Order matches
    /// `GetPending()` so earlier migrations apply first.
    std::vector<Lightweight::SqlMigration::MigrationBase const*> CollectSelected(
        Lightweight::SqlMigration::MigrationManager& manager, QStringList const& timestamps)
    {
        QSet<uint64_t> wanted;
        for (auto const& s: timestamps)
            wanted.insert(s.toULongLong());

        std::vector<Lightweight::SqlMigration::MigrationBase const*> out;
        for (auto const* migration: manager.GetPending())
        {
            auto const ts = migration->GetTimestamp().value;
            if (wanted.contains(ts))
                out.push_back(migration);
        }
        return out;
    }

} // namespace

MigrationRunner::MigrationRunner(QObject* parent):
    QObject(parent)
{
    // One worker is enough — every operation shares the manager's single
    // connection, and apply runs hold the migration advisory lock on it
    // (SqlScopedLock on "lightweight_migration"), so two concurrent runs
    // would deadlock that connection.
    _pool.setMaxThreadCount(1);
}

void MigrationRunner::SetManager(Lightweight::SqlMigration::MigrationManager* manager) noexcept
{
    _manager = manager;
}

void MigrationRunner::setBusyProbe(std::function<bool()> probe)
{
    _busyProbe = std::move(probe);
}

void MigrationRunner::setPostMigrateHook(PostMigrateHook hook)
{
    _postMigrateHook = std::move(hook);
}

bool MigrationRunner::CanStartMutatingRun(char const* what)
{
    if (!_manager || phase() != Phase::Idle)
        return false;
    if (_busyProbe && _busyProbe())
    {
        emit logLine(
            QStringLiteral("%1: another operation is busy (backup or restore in progress?).").arg(QLatin1String(what)),
            LogLevel::Warning);
        return false;
    }
    return true;
}

void MigrationRunner::Enqueue(std::function<void()> task)
{
    _pool.start(new FunctionTask(std::move(task)));
}

void MigrationRunner::SetPhase(Phase newPhase)
{
    auto const oldPhase = _phase.exchange(newPhase, std::memory_order_acq_rel);
    if (oldPhase != newPhase)
        QMetaObject::invokeMethod(this, &MigrationRunner::phaseChanged, Qt::QueuedConnection);
}

namespace
{

    /// Pushes a plain log line onto the runner from a worker thread, marshalling
    /// through the event loop so QML consumers see it on the GUI thread.
    void PostLogLine(MigrationRunner* runner, QString const& line, LogLevel level)
    {
        QMetaObject::invokeMethod(
            runner, [runner, line, level] { emit runner->logLine(line, level); }, Qt::QueuedConnection);
    }

    /// Emits the standard `[i/n] <ts> <title>` progress line plus a `progress`
    /// and `migrationStarted` signal. Shape matches the dbtool CLI output.
    void PostProgress(MigrationRunner* runner,
                      Lightweight::SqlMigration::MigrationBase const& migration,
                      size_t i,
                      size_t n,
                      QString const& verb)
    {
        auto const ts = migration.GetTimestamp().value;
        auto const title = QString::fromStdString(std::string { migration.GetTitle() });
        auto const idx = static_cast<int>(i + 1);
        auto const total = static_cast<int>(n);
        QMetaObject::invokeMethod(
            runner,
            [runner, ts, title, idx, total, verb] {
                emit runner->migrationStarted(static_cast<qulonglong>(ts));
                emit runner->progress(static_cast<qulonglong>(ts), title, idx, total);
                emit runner->logLine(QStringLiteral("[%1/%2] %3 %4 %5").arg(idx).arg(total).arg(verb).arg(ts).arg(title),
                                     LogLevel::Info);
            },
            Qt::QueuedConnection);
    }

    /// Emits `migrationCompleted` on the GUI thread.
    void PostMigrationCompleted(MigrationRunner* runner, uint64_t timestamp, QString const& newStatus)
    {
        QMetaObject::invokeMethod(
            runner,
            [runner, timestamp, newStatus] {
                emit runner->migrationCompleted(static_cast<qulonglong>(timestamp), newStatus);
            },
            Qt::QueuedConnection);
    }

    /// Emits `migrationFailed` on the GUI thread.
    void PostMigrationFailed(MigrationRunner* runner, uint64_t timestamp)
    {
        QMetaObject::invokeMethod(
            runner,
            [runner, timestamp] { emit runner->migrationFailed(static_cast<qulonglong>(timestamp)); },
            Qt::QueuedConnection);
    }

    void PostMigrationFailureLines(MigrationRunner* runner, Lightweight::SqlMigration::MigrationException const& ex)
    {
        auto const verb = ex.GetOperation() == Lightweight::SqlMigration::MigrationException::Operation::Apply
                              ? QStringLiteral("Apply")
                              : QStringLiteral("Rollback");
        auto const title = QString::fromStdString(ex.GetMigrationTitle());
        auto const driverMsg = QString::fromStdString(ex.GetDriverMessage());
        auto const failedSql = QString::fromStdString(ex.GetFailedSql());
        auto const sqlState = QString::fromStdString(ex.info().sqlState);
        auto const nativeError = static_cast<qlonglong>(ex.info().nativeErrorCode);
        auto const ts = static_cast<qulonglong>(ex.GetMigrationTimestamp().value);
        auto const step = static_cast<qulonglong>(ex.GetStepIndex());

        PostLogLine(runner,
                    QStringLiteral("!! %1 failed: %2 - %3 (step %4)").arg(verb).arg(ts).arg(title).arg(step),
                    LogLevel::Error);
        if (!sqlState.isEmpty() && sqlState != QStringLiteral("     "))
            PostLogLine(runner,
                        QStringLiteral("   SQL State: %1, Native error: %2").arg(sqlState).arg(nativeError),
                        LogLevel::Error);
        if (!driverMsg.isEmpty())
        {
            PostLogLine(runner, QStringLiteral("   Driver message:"), LogLevel::Error);
            for (auto const& line: driverMsg.split(QLatin1Char('\n'), Qt::KeepEmptyParts))
                PostLogLine(runner, QStringLiteral("     %1").arg(line), LogLevel::Error);
        }
        if (!failedSql.isEmpty())
        {
            PostLogLine(runner, QStringLiteral("   Failed SQL:"), LogLevel::Error);
            for (auto const& line: failedSql.split(QLatin1Char('\n'), Qt::KeepEmptyParts))
                PostLogLine(runner, QStringLiteral("     %1").arg(line), LogLevel::Error);
        }
    }

    void PostFinished(MigrationRunner* runner, bool ok, QString const& summary)
    {
        QMetaObject::invokeMethod(
            runner,
            [runner, ok, summary] {
                runner->SetPhase(MigrationRunner::Phase::Idle);
                emit runner->finished(ok, summary);
            },
            Qt::QueuedConnection);
    }

    /// How an apply run ended. Reported with `PostFinished` only once the migration lock is released.
    struct ApplyOutcome
    {
        bool ok = false;
        QString summary;
    };

    /// Applies `toRun` in order, stopping at the first failure or at a cancellation request.
    ApplyOutcome ApplyMigrations(MigrationRunner* runner,
                                 Lightweight::SqlMigration::MigrationManager& manager,
                                 std::vector<Lightweight::SqlMigration::MigrationBase const*> const& toRun)
    {
        size_t applied = 0;
        for (size_t i = 0; i < toRun.size(); ++i)
        {
            if (runner->phase() == MigrationRunner::Phase::Cancelling)
            {
                PostLogLine(runner, QStringLiteral("-- Cancelled after %1 migration(s).").arg(applied), LogLevel::Warning);
                return { .ok = false, .summary = QStringLiteral("Cancelled after %1 migrations.").arg(applied) };
            }
            auto const ts = toRun[i]->GetTimestamp().value;
            try
            {
                PostProgress(runner, *toRun[i], i, toRun.size(), QStringLiteral("applying"));
                manager.ApplySingleMigration(*toRun[i]);
                PostMigrationCompleted(runner, ts, QStringLiteral("applied"));
                ++applied;
            }
            catch (Lightweight::SqlMigration::MigrationException const& e)
            {
                PostMigrationFailed(runner, ts);
                PostMigrationFailureLines(runner, e);
                return { .ok = false,
                         .summary =
                             QStringLiteral("Applied %1 of %2 migrations before failing.").arg(applied).arg(toRun.size()) };
            }
            catch (std::exception const& e)
            {
                PostMigrationFailed(runner, ts);
                PostLogLine(runner,
                            QStringLiteral("!! %1 %2 — %3")
                                .arg(ts)
                                .arg(QString::fromStdString(std::string { toRun[i]->GetTitle() }))
                                .arg(QString::fromUtf8(e.what())),
                            LogLevel::Error);
                return { .ok = false,
                         .summary =
                             QStringLiteral("Applied %1 of %2 migrations before failing.").arg(applied).arg(toRun.size()) };
            }
        }
        return { .ok = true, .summary = QStringLiteral("Applied %1 migration(s).").arg(applied) };
    }

    /// Runs `applyRun` under the migration advisory lock and, when it succeeded, the post-migrate
    /// hook. The lock is released before this returns, so the caller reports the outcome only once
    /// the shared connection is idle again. Any exception becomes a failed outcome, so a run can
    /// never stay `Running`.
    ApplyOutcome RunLockedApply(MigrationRunner* runner,
                                Lightweight::SqlMigration::MigrationManager& manager,
                                MigrationRunner::PostMigrateHook const& postMigrateHook,
                                std::function<ApplyOutcome()> const& applyRun)
    {
        try
        {
            auto const lock = Lightweight::SqlScopedLock::TryConstruct(manager.GetDataMapper().Connection(),
                                                                       Lightweight::Tools::MigrationLockName);
            if (!lock)
            {
                PostLogLine(
                    runner, QStringLiteral("!! %1").arg(QString::fromStdString(lock.error().message)), LogLevel::Error);
                return { .ok = false, .summary = QStringLiteral("Could not acquire the migration lock.") };
            }

            auto outcome = applyRun();
            if (!outcome.ok || !postMigrateHook)
                return outcome;

            auto const logError = [runner](std::string_view message) {
                PostLogLine(
                    runner, QString::fromUtf8(message.data(), static_cast<qsizetype>(message.size())), LogLevel::Error);
            };
            if (!postMigrateHook(manager, logError))
                return { .ok = false,
                         .summary = QStringLiteral("%1 A plugin post-migrate hook failed.").arg(outcome.summary) };
            return outcome;
        }
        catch (std::exception const& e)
        {
            PostLogLine(runner, QStringLiteral("!! %1").arg(QString::fromUtf8(e.what())), LogLevel::Error);
            return { .ok = false, .summary = QString::fromUtf8(e.what()) };
        }
    }

} // namespace

void MigrationRunner::dryRunUpTo(QString const& targetTimestamp)
{
    if (!_manager || phase() != Phase::Idle)
        return;
    SetPhase(Phase::Running);
    _cancelRequested.store(false, std::memory_order_release);

    auto* runner = this;
    auto* manager = _manager;
    auto const targetTs = targetTimestamp.toULongLong();

    Enqueue([runner, manager, targetTs, targetLabel = targetTimestamp] {
        auto const toRun = CollectPendingUpTo(*manager, targetTs);
        if (toRun.empty())
        {
            PostLogLine(runner,
                        targetTs == 0 ? QStringLiteral("No pending migrations to dry-run.")
                                      : QStringLiteral("No pending migrations up to %1.").arg(targetLabel),
                        LogLevel::Warning);
            PostFinished(runner, true, QStringLiteral("Dry-run produced 0 migrations."));
            return;
        }

        PostLogLine(
            runner,
            targetTs == 0
                ? QStringLiteral("-- Dry-run: would apply %1 pending migration(s).").arg(toRun.size())
                : QStringLiteral("-- Dry-run: would apply %1 migration(s) up to %2.").arg(toRun.size()).arg(targetLabel),
            LogLevel::Info);

        for (size_t i = 0; i < toRun.size(); ++i)
        {
            if (runner->phase() == Phase::Cancelling)
            {
                PostLogLine(runner, QStringLiteral("-- Cancelled."), LogLevel::Warning);
                PostFinished(runner, false, QStringLiteral("Dry-run cancelled."));
                return;
            }
            PostProgress(runner, *toRun[i], i, toRun.size(), QStringLiteral("would apply"));
        }
        PostFinished(runner, true, QStringLiteral("Dry-run produced %1 migrations.").arg(toRun.size()));
    });
}

void MigrationRunner::applyUpTo(QString const& targetTimestamp)
{
    if (!CanStartMutatingRun("Apply migrations"))
        return;
    SetPhase(Phase::Running);
    _cancelRequested.store(false, std::memory_order_release);

    auto* runner = this;
    auto* manager = _manager;
    auto const targetTs = targetTimestamp.toULongLong();

    Enqueue([runner, manager, targetTs, targetLabel = targetTimestamp, postMigrateHook = _postMigrateHook] {
        auto const outcome = RunLockedApply(runner, *manager, postMigrateHook, [&] {
            auto const toRun = CollectPendingUpTo(*manager, targetTs);
            if (toRun.empty())
            {
                PostLogLine(runner,
                            targetTs == 0 ? QStringLiteral("No pending migrations to apply.")
                                          : QStringLiteral("No pending migrations up to %1.").arg(targetLabel),
                            LogLevel::Warning);
                return ApplyOutcome { .ok = true, .summary = QStringLiteral("Applied 0 migrations.") };
            }
            PostLogLine(runner, QStringLiteral("Applying %1 migration(s)...").arg(toRun.size()), LogLevel::Info);
            return ApplyMigrations(runner, *manager, toRun);
        });
        PostFinished(runner, outcome.ok, outcome.summary);
    });
}

void MigrationRunner::dryRunSelected(QStringList const& timestamps)
{
    if (!_manager || phase() != Phase::Idle)
        return;
    SetPhase(Phase::Running);
    _cancelRequested.store(false, std::memory_order_release);

    auto* runner = this;
    auto* manager = _manager;
    auto const snapshot = timestamps; // captured by value

    Enqueue([runner, manager, snapshot] {
        auto const toRun = CollectSelected(*manager, snapshot);
        if (toRun.empty())
        {
            PostLogLine(runner, QStringLiteral("No pending migrations in the current selection."), LogLevel::Warning);
            PostFinished(runner, true, QStringLiteral("Dry-run produced 0 migrations."));
            return;
        }
        PostLogLine(
            runner, QStringLiteral("-- Dry-run: would apply %1 selected migration(s).").arg(toRun.size()), LogLevel::Info);
        for (size_t i = 0; i < toRun.size(); ++i)
            PostProgress(runner, *toRun[i], i, toRun.size(), QStringLiteral("would apply"));
        PostFinished(runner, true, QStringLiteral("Dry-run produced %1 migrations.").arg(toRun.size()));
    });
}

void MigrationRunner::applySelected(QStringList const& timestamps)
{
    if (!CanStartMutatingRun("Apply migrations"))
        return;
    SetPhase(Phase::Running);
    _cancelRequested.store(false, std::memory_order_release);

    auto* runner = this;
    auto* manager = _manager;
    auto const snapshot = timestamps;

    Enqueue([runner, manager, snapshot, postMigrateHook = _postMigrateHook] {
        auto const outcome = RunLockedApply(runner, *manager, postMigrateHook, [&] {
            auto const toRun = CollectSelected(*manager, snapshot);
            if (toRun.empty())
            {
                PostLogLine(runner, QStringLiteral("No pending migrations in the current selection."), LogLevel::Warning);
                return ApplyOutcome { .ok = true, .summary = QStringLiteral("Applied 0 migrations.") };
            }
            PostLogLine(runner, QStringLiteral("Applying %1 selected migration(s)...").arg(toRun.size()), LogLevel::Info);
            return ApplyMigrations(runner, *manager, toRun);
        });
        PostFinished(runner, outcome.ok, outcome.summary);
    });
}

void MigrationRunner::rollbackToRelease(QString const& version)
{
    if (!CanStartMutatingRun("Rollback"))
        return;
    SetPhase(Phase::Running);
    _cancelRequested.store(false, std::memory_order_release);

    auto* runner = this;
    auto* manager = _manager;
    auto const versionStd = version.toStdString();

    Enqueue([runner, manager, versionStd] {
        auto const* release = manager->FindReleaseByVersion(versionStd);
        if (!release)
        {
            auto const msg = QStringLiteral("Release '%1' is not declared").arg(QString::fromStdString(versionStd));
            PostLogLine(runner, msg, LogLevel::Error);
            PostFinished(runner, false, msg);
            return;
        }

        try
        {
            auto const result = manager->RevertToMigration(
                release->highestTimestamp,
                [runner](Lightweight::SqlMigration::MigrationBase const& migration, size_t i, size_t n) {
                    PostProgress(runner, migration, i, n, QStringLiteral("rolling back"));
                });

            // RevertToMigration hands us the list of successfully reverted
            // timestamps; mirror that into per-row `pending` updates so the
            // main list flips without waiting for a full refresh.
            for (auto const& ts: result.revertedTimestamps)
                PostMigrationCompleted(runner, ts.value, QStringLiteral("pending"));

            if (result.failedAt.has_value())
            {
                PostMigrationFailed(runner, result.failedAt->value);
                // Mirror the CLI's multi-line breakdown in the log panel so
                // users see title/step/SQL state/driver message/failing SQL
                // as separate log entries — embedded newlines collapse
                // inside the rich-text spans, so we split them here.
                auto const ts = static_cast<qulonglong>(result.failedAt->value);
                auto const title = QString::fromStdString(result.failedTitle);
                PostLogLine(runner, QStringLiteral("!! Rollback failed: %1 - %2").arg(ts).arg(title), LogLevel::Error);
                if (!result.failedSql.empty())
                {
                    PostLogLine(runner,
                                QStringLiteral("   Step: %1").arg(static_cast<qulonglong>(result.failedStepIndex)),
                                LogLevel::Error);
                    if (!result.sqlState.empty() && result.sqlState != "     ")
                        PostLogLine(runner,
                                    QStringLiteral("   SQL State: %1, Native error: %2")
                                        .arg(QString::fromStdString(result.sqlState))
                                        .arg(static_cast<qlonglong>(result.nativeErrorCode)),
                                    LogLevel::Error);
                    PostLogLine(runner, QStringLiteral("   Driver message:"), LogLevel::Error);
                    auto const driverMsg = QString::fromStdString(result.errorMessage);
                    for (auto const& line: driverMsg.split(QLatin1Char('\n'), Qt::KeepEmptyParts))
                        PostLogLine(runner, QStringLiteral("     %1").arg(line), LogLevel::Error);
                    PostLogLine(runner, QStringLiteral("   Failed SQL:"), LogLevel::Error);
                    auto const failedSql = QString::fromStdString(result.failedSql);
                    for (auto const& line: failedSql.split(QLatin1Char('\n'), Qt::KeepEmptyParts))
                        PostLogLine(runner, QStringLiteral("     %1").arg(line), LogLevel::Error);
                }
                else
                {
                    PostLogLine(
                        runner, QStringLiteral("   %1").arg(QString::fromStdString(result.errorMessage)), LogLevel::Error);
                }
                PostFinished(runner, false, QStringLiteral("Rollback of migration %1 '%2' failed").arg(ts).arg(title));
            }
            else
                PostFinished(runner,
                             true,
                             QStringLiteral("Rolled back %1 migration(s) to release '%2'")
                                 .arg(result.revertedTimestamps.size())
                                 .arg(QString::fromStdString(versionStd)));
        }
        catch (Lightweight::SqlMigration::MigrationException const& e)
        {
            PostMigrationFailed(runner, e.GetMigrationTimestamp().value);
            PostMigrationFailureLines(runner, e);
            PostFinished(runner,
                         false,
                         QStringLiteral("Rollback of migration %1 failed")
                             .arg(static_cast<qulonglong>(e.GetMigrationTimestamp().value)));
        }
        catch (std::exception const& e)
        {
            PostLogLine(runner, QString::fromUtf8(e.what()), LogLevel::Error);
            PostFinished(runner, false, QString::fromUtf8(e.what()));
        }
    });
}

void MigrationRunner::cancel()
{
    if (phase() == Phase::Running)
    {
        _cancelRequested.store(true, std::memory_order_release);
        SetPhase(Phase::Cancelling);
    }
}

} // namespace DbtoolGui
