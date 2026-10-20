// SPDX-License-Identifier: Apache-2.0

#include "AppController.hpp"
#include "BackupRunner.hpp"
#include "CloseGuard.hpp"
#include "ManagedBackupController.hpp"
#include "MigrationRunner.hpp"

#include <QtGui/QGuiApplication>
#include <QtGui/QWindow>
#include <QtQml/QQmlEngine>

namespace DbtoolGui
{

QString CloseBlockedReason(MigrationRunner const& runner,
                           BackupRunner const& backupRunner,
                           ManagedBackupController const& managedBackups)
{
    switch (runner.phase())
    {
        case MigrationRunner::Phase::Running:
            return QStringLiteral("A migration is running. Cancel it first or wait until it finishes.");
        case MigrationRunner::Phase::Cancelling:
            return QStringLiteral("A migration is being cancelled. Wait until it stops.");
        case MigrationRunner::Phase::Idle:
            break;
    }
    auto const backupRunnerBusy = backupRunner.phase() != BackupRunner::Phase::Idle;
    auto const managedBusy = managedBackups.phase() != ManagedBackupController::Phase::Idle;
    auto const restore = (backupRunnerBusy && backupRunner.operation() == BackupRunner::Operation::Restore)
                         || (managedBusy && managedBackups.runKind() == ManagedBackupController::RunKind::Restore);
    if (restore)
        return QStringLiteral("A restore is running. Wait until it finishes.");
    if (backupRunnerBusy || managedBusy)
        return QStringLiteral("A backup is running. Wait until it finishes.");
    return {};
}

QuitRequestOutcome RequestQuit(std::function<bool()> const& closeBlocked, std::function<void()> const& onRefused)
{
    if (closeBlocked())
    {
        onRefused();
        return QuitRequestOutcome::Refused;
    }
    for (auto* const window: QGuiApplication::topLevelWindows())
        window->close();
    return QuitRequestOutcome::WindowsClosed;
}

void InstallQuitGuard(QQmlEngine& engine)
{
    QObject::disconnect(&engine, &QQmlEngine::quit, nullptr, nullptr);
    QObject::connect(&engine, &QQmlEngine::quit, &engine, [&engine] {
        auto* const controller = engine.singletonInstance<AppController*>(QStringLiteral("Lightweight.Migrations"),
                                                                          QStringLiteral("AppController"));
        (void) RequestQuit([controller] { return controller != nullptr && controller->closeBlocked(); },
                           [controller] {
                               if (controller != nullptr)
                                   emit controller->quitRefused();
                           });
    });
}

} // namespace DbtoolGui
