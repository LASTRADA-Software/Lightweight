// SPDX-License-Identifier: Apache-2.0

#include "ControllerJobs.hpp"
#include "ExceptionMessage.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>
#include <Lightweight/SqlConnectInfo.hpp>
#include <Lightweight/SqlConnection.hpp>
#include <Lightweight/SqlMigration.hpp>
#include <Lightweight/SqlServerType.hpp>

#include <format>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <tuple>

#include <PluginIngestion.hpp>
#include <PluginLoader.hpp>
#include <QtCore/QDir>
#include <QtCore/QSaveFile>
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

ConnectOutcome RunConnect(ConnectRequest const& request,
                          std::vector<Lightweight::Tools::LoadedPlugin>& plugins,
                          Lightweight::SqlMigration::MigrationManager& manager)
{
    auto outcome = ConnectOutcome {};
    outcome.generation = request.generation;
    auto const log = [&outcome, &request](LogLevel level, QString line) {
        if (request.log)
            request.log(level, std::move(line));
        else
            outcome.log.emplace_back(std::move(line), level);
    };
    auto const stale = [&request] {
        return request.isStale && request.isStale();
    };
    if (stale())
        return outcome;

    // Rescan on every connect, so DLLs dropped into the directory between clicks are picked up; the
    // post-init hooks run below, on the new connection, not on the one being replaced.
    auto reload = RunPluginReload(PluginReloadRequest { .directories = request.pluginDirectories,
                                                        .state = AppliedState::Unknown,
                                                        .generation = request.generation },
                                  plugins,
                                  manager);
    for (auto& [line, level]: reload.log)
        log(level, std::move(line));
    outcome.pluginError = std::move(reload.error);
    outcome.snapshot = std::move(reload.snapshot);
    if (stale())
        return outcome;

    auto const defaultConnectionString = Lightweight::SqlConnectionString { request.connectionString };
    // Creates a SQLite file that does not exist yet, as dbtool does; a no-op for every other driver.
    (void) Lightweight::EnsureSqliteDatabaseFileExists(defaultConnectionString);
    Lightweight::SqlConnection::SetDefaultConnectionString(defaultConnectionString);

    // Before the data mapper connects, so the schema's post-connect hook is in place for it.
    try
    {
        manager.SetDefaultSchema(request.schema);
    }
    catch (std::invalid_argument const& e)
    {
        outcome.error = QString::fromStdString(e.what());
        return outcome;
    }

    // The default connection string was just replaced, so this reconnects the manager's data mapper
    // to it - now, where a failure can be reported, rather than on the manager's first use.
    try
    {
        std::ignore = manager.GetDataMapper();
    }
    catch (std::exception const& e)
    {
        auto const failure = ExceptionMessage(e);
        log(LogLevel::Error, QStringLiteral("Connection failed: %1").arg(failure));
        outcome.error = QStringLiteral("Could not open the database connection: %1").arg(failure);
        return outcome;
    }

    // What the driver negotiated, not what the connection string asked for.
    try
    {
        auto const& connection = manager.GetDataMapper().Connection();
        outcome.reachedDatabase = QString::fromStdString(connection.DatabaseName());
        log(LogLevel::Info,
            QStringLiteral("Connected: %1 · %2 · %3")
                .arg(QString::fromStdString(std::format("{}", connection.ServerType())),
                     outcome.reachedDatabase,
                     QString::fromStdString(connection.ServerVersion())));
        if (!request.schema.empty())
            log(LogLevel::Info, QStringLiteral("Schema search path: %1").arg(QString::fromStdString(request.schema)));
    }
    catch (std::exception const&)
    {
        // Best effort: the connect itself succeeded, so a failed metadata read only skips the line.
    }

    if (stale())
        return outcome;
    // Installed before the hooks run, so their first banner reaches the GUI's log.
    manager.SetLogSink(request.logSink);

    // A plugin's post-init hook may, for instance, fill a virtual applied overlay from a legacy
    // version table; until it ran, that range would show as pending. A failing hook is logged and
    // never aborts the connect. The history table is not created here: Lightweight's write paths
    // create it lazily, so an introspection-only connect leaves a fresh database untouched.
    RunPostInitHooks(plugins, manager, [&log](QString line) { log(LogLevel::Error, std::move(line)); });

    // Reads the history merged with the plugins' overlay; an unreadable history falls back to the
    // plugins alone and says why in the snapshot's warning.
    outcome.snapshot = CaptureMigrationSnapshot(manager, AppliedState::Database);
    outcome.defaultSchema = std::string { manager.DefaultSchema() };
    outcome.ok = true;
    return outcome;
}

QStringList PreviewMigrationStatements(Lightweight::SqlMigration::MigrationManager const& manager, std::uint64_t timestamp)
{
    auto const* migration = manager.GetMigration(Lightweight::SqlMigration::MigrationTimestamp { timestamp });
    if (!migration)
        return {};
    // The preview queries the connected database for the dialect and column widths.
    try
    {
        auto statements = QStringList {};
        for (auto const& statement: manager.PreviewMigration(*migration))
            statements.push_back(QString::fromStdString(statement));
        return statements;
    }
    catch (std::exception const& e)
    {
        return { QStringLiteral("-- Preview failed: %1").arg(ExceptionMessage(e)) };
    }
}

QString LocalFilePath(QUrl const& url)
{
    if (!url.isLocalFile())
        return {};
    return QDir::toNativeSeparators(url.toLocalFile());
}

QString WriteFileAtomically(QString const& path, QByteArray const& content)
{
    auto file = QSaveFile { path };
    if (!file.open(QIODevice::WriteOnly))
        return file.errorString();
    if (file.write(content) != content.size())
        return file.errorString();
    if (!file.commit())
        return file.errorString();
    return {};
}

} // namespace DbtoolGui
