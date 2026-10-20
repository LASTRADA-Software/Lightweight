// SPDX-License-Identifier: Apache-2.0
//
// The main window refuses to close while a run is in flight. This unit names the run that blocks
// closing and routes QML's `Qt.quit()` through the same guard instead of quitting at once.

#pragma once

#include <cstdint>
#include <functional>

#include <QtCore/QString>

class QQmlEngine;

namespace DbtoolGui
{

class BackupRunner;
class ManagedBackupController;
class MigrationRunner;

/// @return Why the main window must not close, naming the run in flight; empty when none is.
[[nodiscard]] QString CloseBlockedReason(MigrationRunner const& runner,
                                         BackupRunner const& backupRunner,
                                         ManagedBackupController const& managedBackups);

/// What a quit request did.
enum class QuitRequestOutcome : std::uint8_t
{
    /// A run is in flight: nothing was closed.
    Refused,
    /// Every top-level window was asked to close; closing the last one quits.
    WindowsClosed,
};

/// Handles a quit request as closing the main window would. While @p closeBlocked reports a run in
/// flight it closes nothing, not even other top-level windows, and calls @p onRefused; otherwise it
/// asks every top-level window to close.
/// @param closeBlocked Whether a run is in flight.
/// @param onRefused Says why nothing closed.
/// @return What the request did.
QuitRequestOutcome RequestQuit(std::function<bool()> const& closeBlocked, std::function<void()> const& onRefused);

/// Routes @p engine's `quit` (QML `Qt.quit()`) through `RequestQuit` with the engine's
/// `AppController` singleton, replacing the default of quitting at once.
void InstallQuitGuard(QQmlEngine& engine);

} // namespace DbtoolGui
