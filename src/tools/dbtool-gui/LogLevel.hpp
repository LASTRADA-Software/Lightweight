// SPDX-License-Identifier: Apache-2.0
//
// Shared severity classification for messages routed to the GUI's log pane.
// `AppController::logLine`, `MigrationRunner::logLine`, and
// `BackupRunner::logLine` all carry this enum so QML and C++ agree on the
// vocabulary without relying on magic integers (0/1/2).
//
// Lives in its own header so the runner classes do not have to depend on
// `AppController.hpp` (which would create an include cycle).

#pragma once

#include <QtCore/QObject>
#include <QtQmlIntegration/QtQmlIntegration>

namespace DbtoolGui
{

Q_NAMESPACE

/// Severity classification for log-pane messages. Renderer-side colour and
/// any future filtering UI key off these values; C++ producers should pick
/// the level that matches the message's intent rather than reusing a raw
/// integer that has to be remembered.
enum class LogLevel : int
{
    /// Routine progress, neutral terminal-grey in the panel.
    Info,
    /// Soft anomaly the user should notice (e.g. shadowed plugin, missing
    /// `schema_migrations` table). Amber.
    Warning,
    /// Hard failure (load error, failed migration, connection refused). Red.
    Error,
};
Q_ENUM_NS(LogLevel)

} // namespace DbtoolGui
