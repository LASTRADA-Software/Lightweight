// SPDX-License-Identifier: Apache-2.0
//
// The bodies of `AppController`'s worker jobs. Each takes its inputs by value in a request, runs on the
// migration runner's `SerialWorker`, and returns an outcome the controller applies on the GUI thread.
// Nothing here touches a QObject, so a job's log lines travel back inside its outcome.

#pragma once

#include "LogLevel.hpp"
#include "Models/MigrationSnapshot.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <utility>
#include <vector>

#include <QtCore/QString>

namespace Lightweight
{
namespace SqlMigration
{
    class MigrationManager;
}
namespace Tools
{
    struct LoadedPlugin;
}
} // namespace Lightweight

namespace DbtoolGui
{

/// Log lines a job produced, in order, for the controller to emit on the GUI thread.
using JobLog = std::vector<std::pair<QString, LogLevel>>;

/// What a plugin reload loads, and what it reads of the database afterwards.
struct PluginReloadRequest
{
    /// The directories to load the plugins from; empty unloads every plugin.
    std::vector<std::filesystem::path> directories;
    /// `Database` while connected: the post-init hooks run again and the history is read.
    AppliedState state = AppliedState::Unknown;
    /// The controller's target generation when the reload was requested.
    std::uint64_t generation = 0;
};

/// What a plugin reload produced.
struct PluginReloadOutcome
{
    /// The models' rows after the reload.
    MigrationSnapshot snapshot;
    /// The reload's log lines.
    JobLog log;
    /// Why the plugins could not be loaded at all; empty when they could.
    QString error;
    /// The generation of the request; an outcome of an older generation is stale.
    std::uint64_t generation = 0;
};

/// Runs the plugins' post-init hooks on the manager's connection, reporting every failure.
/// @param plugins The loaded plugins.
/// @param manager The process-wide manager the plugins were merged into.
/// @param logError Receives each failure as a log line.
void RunPostInitHooks(std::vector<Lightweight::Tools::LoadedPlugin> const& plugins,
                      Lightweight::SqlMigration::MigrationManager& manager,
                      std::function<void(QString)> const& logError);

/// Unloads @p plugins, loads those of the request's directories into @p manager, runs their post-init
/// hooks while connected, and captures the snapshot. Never throws; a failure is in the outcome.
/// @param request What to load and read.
/// @param plugins The loaded plugins; replaced by the new set.
/// @param manager The process-wide manager.
/// @return The snapshot and log of the reload.
[[nodiscard]] PluginReloadOutcome RunPluginReload(PluginReloadRequest const& request,
                                                  std::vector<Lightweight::Tools::LoadedPlugin>& plugins,
                                                  Lightweight::SqlMigration::MigrationManager& manager);

} // namespace DbtoolGui
