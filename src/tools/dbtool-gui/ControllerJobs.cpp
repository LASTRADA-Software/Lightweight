// SPDX-License-Identifier: Apache-2.0

#include "ControllerJobs.hpp"
#include "ExceptionMessage.hpp"

#include <Lightweight/SqlMigration.hpp>

#include <string_view>
#include <system_error>

#include <PluginIngestion.hpp>
#include <PluginLoader.hpp>
#include <QtCore/QStringList>

namespace DbtoolGui
{

namespace
{

    [[nodiscard]] QString FromUtf8View(std::string_view message)
    {
        return QString::fromUtf8(message.data(), static_cast<qsizetype>(message.size()));
    }

    [[nodiscard]] QString JoinedDirectories(std::vector<std::filesystem::path> const& directories)
    {
        auto display = QStringList {};
        display.reserve(static_cast<qsizetype>(directories.size()));
        for (auto const& directory: directories)
            display.append(QString::fromStdString(directory.string()));
        return display.join(QStringLiteral("; "));
    }

} // namespace

void RunPostInitHooks(std::vector<Lightweight::Tools::LoadedPlugin> const& plugins,
                      Lightweight::SqlMigration::MigrationManager& manager,
                      std::function<void(QString)> const& logError)
{
    try
    {
        Lightweight::Tools::RunPluginPostInitHooks(
            plugins, manager.GetDataMapper().Connection(), manager, [&logError](std::string_view message) {
                logError(FromUtf8View(message));
            });
    }
    catch (std::exception const& e)
    {
        logError(QStringLiteral("Plugin post-init hooks failed: %1").arg(ExceptionMessage(e)));
    }
}

PluginReloadOutcome RunPluginReload(PluginReloadRequest const& request,
                                    std::vector<Lightweight::Tools::LoadedPlugin>& plugins,
                                    Lightweight::SqlMigration::MigrationManager& manager)
{
    auto outcome = PluginReloadOutcome {};
    outcome.generation = request.generation;
    auto const log = [&outcome](LogLevel level, QString line) {
        outcome.log.emplace_back(std::move(line), level);
    };

    // Wipe first, so a profile without plugins clears the previous list instead of leaving it showing.
    Lightweight::Tools::UnloadPlugins(plugins, manager);

    if (request.directories.empty())
        log(LogLevel::Info, QStringLiteral("Plugins directory: none configured"));
    else
    {
        log(LogLevel::Info, QStringLiteral("Plugins directory: %1").arg(JoinedDirectories(request.directories)));
        for (auto const& directory: request.directories)
        {
            auto error = std::error_code {};
            if (!std::filesystem::is_directory(directory, error))
                log(LogLevel::Warning,
                    QStringLiteral("Plugins directory not found: %1").arg(QString::fromStdString(directory.string())));
        }
        auto options = Lightweight::Tools::PluginIngestOptions {};
        options.logLoading = [&log](std::string_view message) {
            log(LogLevel::Info, QStringLiteral("Loaded plugin: %1").arg(FromUtf8View(message)));
        };
        options.logShadowed = [&log](std::string_view message) {
            log(LogLevel::Warning, QStringLiteral("Plugin shadowed: %1").arg(FromUtf8View(message)));
        };
        options.logError = [&log](std::string_view message) {
            log(LogLevel::Error, QStringLiteral("Plugin failed: %1").arg(FromUtf8View(message)));
        };
        options.throwOnLoadError = false;
        try
        {
            plugins = Lightweight::Tools::IngestPlugins(
                request.directories, Lightweight::Tools::DefaultPluginLoader(), manager, options);
        }
        catch (std::exception const& e)
        {
            // Only contract violations and directory-iteration errors get here, since load errors are logged.
            outcome.error = QStringLiteral("Failed to load plugins from %1: %2")
                                .arg(JoinedDirectories(request.directories), QString::fromUtf8(e.what()));
            log(LogLevel::Error, outcome.error);
        }
    }

    // Unloading dropped what the post-init hooks registered on connect (pre-migrate steps, the
    // virtual-applied overlay), so a reload while connected runs them again on the live connection.
    if (request.state == AppliedState::Database)
        RunPostInitHooks(plugins, manager, [&log](QString line) { log(LogLevel::Error, std::move(line)); });

    outcome.snapshot = CaptureMigrationSnapshot(manager, request.state);
    log(LogLevel::Info,
        QStringLiteral("Migrations available: %1 · Releases declared: %2")
            .arg(outcome.snapshot.migrations.size())
            .arg(outcome.snapshot.releases.size()));
    return outcome;
}

} // namespace DbtoolGui
