// SPDX-License-Identifier: Apache-2.0
//
// The bodies of `AppController`'s worker jobs. Each takes its inputs by value in a request, runs on the
// migration runner's `SerialWorker`, and returns an outcome the controller applies on the GUI thread.
// Nothing here touches a QObject, so a job's log lines travel back inside its outcome.

#pragma once

#include "BackupRequirementSupport.hpp"
#include "LogLevel.hpp"
#include "Models/MigrationSnapshot.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <QtCore/QString>
#include <QtCore/QStringList>

namespace Lightweight
{
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

/// What a connect opens, with which plugins.
struct ConnectRequest
{
    /// The connection string, with any password folded in.
    std::string connectionString;
    /// The default schema to set; empty uses the server's default.
    std::string schema;
    /// The directories to load the plugins from.
    std::vector<std::filesystem::path> pluginDirectories;
    /// The connection mode the connect was made in: "profile" | "dsn" | "custom".
    QString mode;
    /// The profile or DSN name; empty for a custom connection string, which is named by the database it reaches.
    QString target;
    /// The controller's target generation when the connect was requested.
    std::uint64_t generation = 0;
    /// Receives the manager's log lines, such as the banners of the post-init hooks; runs on the worker.
    std::function<void(std::string_view)> logSink;
    /// Receives the connect's own log lines as they happen, on the worker; when empty they are
    /// collected in `ConnectOutcome::log` instead.
    std::function<void(LogLevel, QString)> log;
    /// Whether the target changed since the request; the job then stops at its next step. Runs on the worker.
    std::function<bool()> isStale;
};

/// What a connect produced.
struct ConnectOutcome
{
    /// Whether the connection is open.
    bool ok = false;
    /// Why the connect failed; empty when it succeeded.
    QString error;
    /// Why the plugins could not be loaded at all; the connect goes on without them.
    QString pluginError;
    /// The database the connection reached; empty when it could not be read.
    QString reachedDatabase;
    /// The default schema the manager uses after the connect.
    std::string defaultSchema;
    /// The models' rows: of the database while connected, else of the plugins alone.
    MigrationSnapshot snapshot;
    /// The connect's log lines, unless the request passed them on as they happened.
    JobLog log;
    /// The generation of the request; an outcome of an older generation is stale.
    std::uint64_t generation = 0;
};

/// Reloads the plugins, opens the connection, runs the post-init hooks on it and captures the snapshot.
/// Stops early, with `ok` false and no error, once the request turned stale: before the reload, before
/// opening the connection and before the hooks. Never throws; a failure is in the outcome.
/// @param request What to open and load.
/// @param plugins The loaded plugins; replaced by the new set.
/// @param manager The process-wide manager.
/// @return The snapshot and log of the connect.
[[nodiscard]] ConnectOutcome RunConnect(ConnectRequest const& request,
                                        std::vector<Lightweight::Tools::LoadedPlugin>& plugins,
                                        Lightweight::SqlMigration::MigrationManager& manager);

/// What a re-read of the database after a run produced.
struct HistoryRefreshOutcome
{
    /// The models' rows.
    MigrationSnapshot snapshot;
    /// The backup requirement; set when the refresh was made while connected.
    std::optional<BackupRequirementState> requirement;
    /// The controller's target generation when the refresh was requested.
    std::uint64_t generation = 0;
};

/// The statements applying the migration @p timestamp would run on the manager's connection.
/// @param manager The process-wide manager.
/// @param timestamp The migration.
/// @return The statements; empty for an unknown migration, one comment line when the preview failed.
[[nodiscard]] QStringList PreviewMigrationStatements(Lightweight::SqlMigration::MigrationManager const& manager,
                                                     std::uint64_t timestamp);

} // namespace DbtoolGui
