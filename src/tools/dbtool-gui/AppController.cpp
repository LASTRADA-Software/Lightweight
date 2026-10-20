// SPDX-License-Identifier: Apache-2.0

#include "AppController.hpp"
#include "BackupRequirementSupport.hpp"
#include "CloseGuard.hpp"
#include "ControllerJobs.hpp"
#include "ExceptionMessage.hpp"
#include "Models/MigrationSnapshot.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>
#include <Lightweight/SqlConnectInfo.hpp>
#include <Lightweight/SqlConnection.hpp>
#include <Lightweight/SqlError.hpp>
#include <Lightweight/SqlMigration.hpp>

#include <BackupRecording.hpp>
#include <Config/BackupOutput.hpp>
#include <Config/ConfigDiscovery.hpp>
#include <Config/ProfileAdd.hpp>
#include <Config/ProfilePassword.hpp>
#include <PluginIngestion.hpp>
#include <PluginLoader.hpp>
#include <QtCore/QtGlobal>
#include <Secrets/ProfileCipher.hpp>
#include <Secrets/SecretResolver.hpp>

namespace DbtoolGui
{

/// Pimpl payload — concrete vector of loaded plugins. Lives entirely in
/// this translation unit so the moc-parsed `AppController.hpp` doesn't
/// need to see `<PluginIngestion.hpp>`.
struct AppController::PluginsBundle
{
    PluginsBundle() = default;
    PluginsBundle(PluginsBundle const&) = delete;
    PluginsBundle(PluginsBundle&&) = delete;
    PluginsBundle& operator=(PluginsBundle const&) = delete;
    PluginsBundle& operator=(PluginsBundle&&) = delete;

    /// Unloads the plugins only after the process-wide manager has let go of their code.
    ~PluginsBundle()
    {
        Lightweight::Tools::UnloadPlugins(entries, Lightweight::SqlMigration::MigrationManager::GetInstance());
    }

    std::vector<Lightweight::Tools::LoadedPlugin> entries;
};

} // namespace DbtoolGui

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <format>
#include <span>
#include <string_view>
#include <system_error>
#include <utility>

#include <QtCore/QCoreApplication>
#include <QtCore/QCryptographicHash>
#include <QtCore/QDateTime>
#include <QtCore/QDeadlineTimer>
#include <QtCore/QDebug>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QFileSystemWatcher>
#include <QtCore/QModelIndex>
#include <QtCore/QSettings>
#include <QtCore/QSysInfo>
#include <QtCore/QThread>
#include <QtCore/QTimer>
#include <QtCore/QUrl>
#include <QtCore/QVariantMap>
#include <QtGui/QDesktopServices>
#include <QtGui/QGuiApplication>
#include <QtGui/QWindow>

#if defined(_WIN32)
    #include <windows.h>
#endif

namespace DbtoolGui
{

namespace
{

    AppController* g_instance = nullptr;

    /// Initial value for `AppController::_verbose`. Mirror of the
    /// `--verbose`/`-v` CLI flag; consumed by the singleton's constructor.
    bool s_initialVerbose = false;

    /// Settings keys — scoped to `connection/` so new preference groups can
    /// coexist (e.g. future `ui/splitSizes`) without a schema migration.
    constexpr auto kKeyMode = "connection/mode";
    constexpr auto kKeyProfile = "connection/profile";
    constexpr auto kKeyDsn = "connection/dsn";
    constexpr auto kKeyDsnUser = "connection/dsnUser";
    constexpr auto kKeyDsnPassword = "connection/dsnPassword";
    constexpr auto kKeyDsnSchema = "connection/dsnSchema";
    constexpr auto kKeyOverrideString = "connection/overrideString";
    constexpr auto kKeyCustomSchema = "connection/customSchema";
    /// Global plugins directory configured from the Settings page. Lives
    /// under `config/` (alongside `config/profileStorePath`) because it is a
    /// system-wide preference, not a per-connection-mode override. The legacy
    /// `connection/pluginsDir` key is migrated on first load.
    constexpr auto kKeyPluginsDir = "config/pluginsDir";
    constexpr auto kKeyLegacyPluginsDir = "connection/pluginsDir";
    constexpr auto kKeyProfileStorePath = "config/profileStorePath";
    constexpr auto kKeyLogVisible = "ui/logVisible";
    constexpr auto kKeyViewMode = "ui/viewMode";

    /// The warning shown while a plugin reload waits for the migration run to finish.
    constexpr auto PluginReloadDeferredWarning = "Plugins will reload after the current run finishes.";

    /// Appends `;{key}={value}` to `cs` when `value` is non-empty. The value is
    /// rendered through `FormatConnectionStringValue`, so a password the
    /// connection-string syntax would misread (metacharacters or edge whitespace)
    /// is brace-quoted instead of being cut into a stray keyword (#635); plain
    /// values keep their spelling.
    void AppendAttr(std::string& cs, std::string_view key, std::string_view value)
    {
        if (value.empty())
            return;
        cs += ';';
        cs += key;
        cs += '=';
        cs += Lightweight::FormatConnectionStringValue(value);
    }

    /// A profile's connection string with its password folded in.
    struct ProfileConnection
    {
        std::string connectionString;
        Lightweight::Config::PasswordOrigin passwordOrigin = Lightweight::Config::PasswordOrigin::None;
    };

    /// Builds the connection string for a profile using the same password rules
    /// as dbtool: decrypt `enc:` values, use plaintext as-is (the caller encrypts
    /// it once the connection proves it works), otherwise resolve `secretRef`.
    [[nodiscard]] std::expected<ProfileConnection, std::string> ProfileConnectionString(
        Lightweight::Config::Profile const& profile)
    {
        auto const resolver = Lightweight::Secrets::MakeNonInteractiveResolver();
        return Lightweight::Config::ResolveProfilePassword(profile, Lightweight::Secrets::ProfileCipher::Builtin(), resolver)
            .transform([&](Lightweight::Config::ResolvedPassword const& password) {
                return ProfileConnection {
                    .connectionString =
                        profile.HasConnection() ? std::format("{}", profile.ToConnectInfo(password.value)) : std::string {},
                    .passwordOrigin = password.origin,
                };
            });
    }

    /// The dbtool.yml to use when the user has not picked one in Settings: the
    /// nearest file above the working or executable directory, else the per-user
    /// default (which may not exist yet).
    [[nodiscard]] Lightweight::Config::DiscoveredConfig DiscoverProfileStore()
    {
        namespace Cfg = Lightweight::Config;
        // Only an explicit path can make discovery fail, and none is passed here.
        return Cfg::FindConfigFile(Cfg::DefaultDiscoveryInputs())
            .value_or(Cfg::DiscoveredConfig { .path = Cfg::ProfileStore::DefaultPath(), .source = Cfg::ConfigSource::None });
    }

} // namespace

void AppController::SeedVerbose(bool enabled) noexcept
{
    s_initialVerbose = enabled;
}

AppController::AppController(QObject* parent):
    QObject(parent),
    _plugins(std::make_unique<PluginsBundle>()),
    _verbose(s_initialVerbose)
{
    _runner.SetManager(&Lightweight::SqlMigration::MigrationManager::GetInstance());
    _runner.setPostMigrateHook(
        [this](Lightweight::SqlMigration::MigrationManager& manager, std::function<void(std::string_view)> const& logError) {
            return Lightweight::Tools::RunPluginPostMigrateHooks(
                _plugins->entries, manager.GetDataMapper().Connection(), manager, logError);
        });

    QObject::connect(&_migrations, &MigrationListModel::selectionChanged, this, &AppController::selectionChanged);

    // The runner's events flip single rows ("pending" → "running" → "applied", or back to "pending" on
    // failure), so a run never rebuilds the whole model per migration.
    QObject::connect(&_runner, &MigrationRunner::migrationStarted, this, [this](qulonglong ts) {
        _migrations.SetRowStatusByTimestamp(ts, QStringLiteral("running"));
        // A new run started — clear any stale failure
        // identity so the Simple view's Failure card does
        // not pick up the previous run's failed migration
        // if this one succeeds.
        _lastFailedTimestamp.clear();
        _lastFailedTitle.clear();
    });
    // The counters follow at a bounded rate: each `migrationsChanged` re-evaluates every count binding.
    _migrationsChangedThrottle = new QTimer(this);
    _migrationsChangedThrottle->setSingleShot(true);
    _migrationsChangedThrottle->setInterval(RowStatusNotifyInterval);
    QObject::connect(_migrationsChangedThrottle, &QTimer::timeout, this, &AppController::migrationsChanged);
    QObject::connect(&_runner, &MigrationRunner::migrationCompleted, this, [this](qulonglong ts, QString const& newStatus) {
        _migrations.SetRowStatusByTimestamp(ts, newStatus);
        if (!_migrationsChangedThrottle->isActive())
            _migrationsChangedThrottle->start();
    });
    QObject::connect(&_runner, &MigrationRunner::migrationFailed, this, [this](qulonglong ts) {
        _migrations.SetRowStatusByTimestamp(ts, QStringLiteral("pending"));
        // Capture the identity of the migration that failed
        // so the Simple view's Failure card can surface
        // "Failed migration: TS  title" without QML having
        // to re-query the list model after `finished`.
        _lastFailedTimestamp = QString::number(ts);
        _lastFailedTitle = _migrations.TitleByTimestamp(ts);
        emit migrationsChanged();
    });
    // Ring-buffer the runner's log stream so `buildFailureReport` can
    // include real execution context (the failing DDL, driver error, etc.)
    // without asking QML to read the LogPanel — which lives in a sibling
    // QML tree that may not even be instantiated under the Simple view.
    auto const bufferLogLine = [this](QString const& line, LogLevel /*level*/) {
        _recentLog.append(line);
        while (_recentLog.size() > kRecentLogCapacity)
            _recentLog.removeFirst();
    };
    QObject::connect(&_runner, &MigrationRunner::logLine, this, bufferLogLine);
    QObject::connect(&_backupRunner, &BackupRunner::logLine, this, bufferLogLine);

    // The runners' streams join the startup banner in the one log feed.
    auto const forwardLogLine = [this](QString const& line, LogLevel level) {
        Log(level, line);
    };
    QObject::connect(&_runner, &MigrationRunner::logLine, this, forwardLogLine);
    QObject::connect(&_backupRunner, &BackupRunner::logLine, this, forwardLogLine);
    QObject::connect(&_managedBackups, &ManagedBackupController::logLine, this, [this](QString const& line, LogLevel level) {
        Log(level, line);
    });
    // The three runners all touch the same databases, so each refuses to start
    // while either of the others is in flight. The guard MUST be mutual: a
    // one-way probe (only managed backups checking the others) still lets a
    // migration or an ad-hoc restore run *during* a managed backup, which
    // produces a torn archive that is then committed and marked "ok".
    _managedBackups.setBusyProbe([this] {
        return _runner.phase() != MigrationRunner::Phase::Idle || _backupRunner.phase() != BackupRunner::Phase::Idle;
    });
    // Closing is refused while any of the three runners is in flight.
    QObject::connect(&_runner, &MigrationRunner::phaseChanged, this, &AppController::closeBlockedChanged);
    QObject::connect(&_backupRunner, &BackupRunner::phaseChanged, this, &AppController::closeBlockedChanged);
    QObject::connect(&_managedBackups, &ManagedBackupController::phaseChanged, this, &AppController::closeBlockedChanged);
    // A managed backup or restore that connected with a plaintext password has proven it, exactly like
    // the interactive connect: store it encrypted right away. Only the password the run used is encrypted, so an
    // edit made to the file while the run was in flight is never overwritten with the older value.
    _managedBackups.setPlaintextPasswordVerifiedHandler([this](Lightweight::Config::Profile const& verified) {
        auto const* current = _store.Find(verified.name);
        if (current && current->password == verified.password)
            EncryptPlaintextPassword(verified);
    });
    // A plugin load or a connect replaces the plugins or the connection a run would use. Checks and
    // re-reads only queue ahead of the run on the same worker, so a run may start behind them, e.g. right
    // after a "back up first" backup, whose requirement check is still pending.
    _runner.setBusyProbe([this] {
        return _managedBackups.phase() != ManagedBackupController::Phase::Idle
               || _backupRunner.phase() != BackupRunner::Phase::Idle || pluginsLoading() || _connecting;
    });
    _backupRunner.setBusyProbe([this] {
        return _managedBackups.phase() != ManagedBackupController::Phase::Idle
               || _runner.phase() != MigrationRunner::Phase::Idle;
    });
    // After a run finishes, do one authoritative refresh of the models; an
    // unmanaged database falls back to the plugin-only view with a warning.
    QObject::connect(&_runner, &MigrationRunner::finished, this, [this](bool ok, QString const& /*summary*/) {
        if (ok)
        {
            _lastFailedTimestamp.clear();
            _lastFailedTitle.clear();
        }
        // "Migrate without a backup" covers one run only.
        setMigrateWithoutBackup(false);
        RefreshMigrationModelsAfterRun();
        if (_pluginReloadPending)
        {
            RequestPluginReload();
            if (_lastWarning == QLatin1String { PluginReloadDeferredWarning })
            {
                _lastWarning.clear();
                emit lastWarningChanged();
            }
        }
    });
    // A managed restore rewrites the target database, so a restore into the
    // connected profile leaves the migration list showing the pre-restore
    // applied-set. Mirror the runner's post-run refresh so the list re-reads
    // `schema_migrations` once the restore succeeds. Only on success and only
    // while connected — an unconnected refresh has nothing new to read, and a
    // failed run did not change the database.
    QObject::connect(
        &_managedBackups, &ManagedBackupController::finished, this, [this](bool ok, QString const& /*summary*/) {
            if (ok && _connected)
                RefreshMigrationModelsAfterRun();
            UpdateActivity();
        });
    // A recorded Back up or Restore can satisfy the backup requirement; one that could not be recorded says why.
    QObject::connect(&_backupRunner, &BackupRunner::finished, this, [this](bool /*ok*/, QString const& /*summary*/) {
        _lastRecordError = _backupRunner.lastResult().value(QStringLiteral("recordError")).toString();
        RequestBackupRequirementCheck();
        UpdateActivity();
    });

    _settings = new QSettings(this);
    _settingsSync = new QTimer(this);
    _settingsSync->setSingleShot(true);
    _settingsSync->setInterval(std::chrono::milliseconds { 500 });
    QObject::connect(_settingsSync, &QTimer::timeout, _settings, &QSettings::sync);

    _fileWatcher = new QFileSystemWatcher(this);
    QObject::connect(_fileWatcher, &QFileSystemWatcher::fileChanged, this, &AppController::OnProfileFileChanged);
    // Editors save with write-then-rename and the watcher may report one save several times; let it settle.
    _profileFileDebounce = new QTimer(this);
    _profileFileDebounce->setSingleShot(true);
    _profileFileDebounce->setInterval(std::chrono::milliseconds { 150 });
    QObject::connect(_profileFileDebounce, &QTimer::timeout, this, &AppController::HandleProfileFileChange);

    // Restore preferences BEFORE loading profiles. `loadProfiles` only sets
    // a default profile when `_currentProfile` is empty, so feeding the
    // saved value here preserves the user's choice across restarts. Only
    // these reads and the discovery below run before the first frame.
    auto& settings = *_settings;
    auto const savedMode = settings.value(kKeyMode).toString();
    if (savedMode == QStringLiteral("dsn") || savedMode == QStringLiteral("custom"))
        _connectionMode = savedMode;
    _currentProfile = settings.value(kKeyProfile).toString();
    _selectedDsn = settings.value(kKeyDsn).toString();
    _dsnUser = settings.value(kKeyDsnUser).toString();
    _dsnPassword = settings.value(kKeyDsnPassword).toString();
    _dsnSchema = settings.value(kKeyDsnSchema).toString();
    _pluginsDir = settings.value(kKeyPluginsDir).toString();
    // One-shot migration: pick up any value still parked under the old
    // `connection/pluginsDir` key (where this preference lived before the
    // Settings page made it a global), then evict the legacy entry.
    if (_pluginsDir.isEmpty() && settings.contains(kKeyLegacyPluginsDir))
    {
        _pluginsDir = settings.value(kKeyLegacyPluginsDir).toString();
        if (!_pluginsDir.isEmpty())
            settings.setValue(kKeyPluginsDir, _pluginsDir);
    }
    settings.remove(kKeyLegacyPluginsDir);
    _profileStorePath = settings.value(kKeyProfileStorePath).toString();
    _connectionStringOverride = settings.value(kKeyOverrideString).toString();
    _customSchema = settings.value(kKeyCustomSchema).toString();
    _logVisible = settings.value(kKeyLogVisible, true).toBool();
    // New installs land on "simple" by design — downstream non-technical
    // users should see the stripped, single-button view first. Developers
    // who flip to "expert" get their choice remembered across restarts.
    auto const savedView = settings.value(kKeyViewMode, QStringLiteral("simple")).toString();
    if (savedView == QStringLiteral("expert"))
        _viewMode = savedView;

    // Discovery walks the filesystem, so it runs once; `defaultProfileStorePath` reads the result.
    auto const discoveredStore = DiscoverProfileStore();
    _discoveredStorePath = QString::fromStdString(discoveredStore.path.string());
    _discoveredStoreSource = QString::fromUtf8(Lightweight::Config::ToString(discoveredStore.source).data());

    QObject::connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, &AppController::Shutdown);

    // Everything else waits for the event loop, so the window shows before any slow work starts.
    QMetaObject::invokeMethod(this, &AppController::BeginStartup, Qt::QueuedConnection);
}

void AppController::BeginStartup()
{
    _startupBegun = true;

    // Honour the user-customised store path (if any); otherwise the discovered
    // dbtool.yml. A store loaded before the event loop ran (by a test) wins.
    auto const initialStorePath = !_profileStorePath.isEmpty() ? _profileStorePath : _discoveredStorePath;
    if (!_storeLoaded && std::filesystem::exists(initialStorePath.toStdString()))
        (void) loadProfiles(initialStorePath);

    // Plain English throughout — never the CLI flag name (e.g. "Plugins directory", not "--plugins-dir").
    LogInfo(QStringLiteral("dbtool-gui starting (Qt %1)").arg(QLatin1String(qVersion())));
    LogInfo(QStringLiteral("View mode: %1").arg(_viewMode));
    auto const source = !_profileStorePath.isEmpty() ? QStringLiteral("chosen in Settings") : _discoveredStoreSource;
    LogInfo(QStringLiteral("Profile store: %1 (found via %2, %3 profile(s))")
                .arg(initialStorePath, source)
                .arg(_profiles.rowCount()));

    refreshOdbcDataSources();
    if (!_awaitingFirstFrame)
        StartFirstPluginLoad();
    UpdateActivity();
}

void AppController::awaitFirstFrame(QQuickWindow* window)
{
    if (!window || !_startupPending)
        return;
    _awaitingFirstFrame = true;
    QObject::connect(window,
                     &QQuickWindow::frameSwapped,
                     this,
                     &AppController::OnFirstFrame,
                     static_cast<Qt::ConnectionType>(Qt::QueuedConnection | Qt::SingleShotConnection));
    window->update();
    QTimer::singleShot(std::chrono::seconds { 2 }, this, &AppController::OnFirstFrame);
}

void AppController::OnFirstFrame()
{
    if (!std::exchange(_awaitingFirstFrame, false))
        return;
    if (_startupBegun)
        StartFirstPluginLoad();
}

void AppController::StartFirstPluginLoad()
{
    if (!std::exchange(_startupPending, false))
        return;
    RequestPluginReload();
    UpdateActivity();
}

void AppController::Shutdown()
{
    if (std::exchange(_shuttingDown, true))
        return;
    _pluginReloadFlushQueued = false;
    // Quitting from the keyboard must not leave a frozen window on screen while the jobs finish.
    if (qobject_cast<QGuiApplication*>(QCoreApplication::instance()))
        for (auto* const window: QGuiApplication::topLevelWindows())
            window->hide();
    _settingsSync->stop();
    _settings->sync();
    _runner.cancel();
    _sqlQueryRunner.cancel();
    // A job in flight uses the plugins, the manager and this object's address; let it finish first.
    // A migration, backup or restore is waited for to its end: ending it midway leaves it half-written.
    auto const limit = AnyRunInFlight() ? std::chrono::milliseconds::max() : _shutdownDrainLimit;
    if (DrainWorkers(limit))
        return;
    if (_onShutdownDrainExceeded)
    {
        _onShutdownDrainExceeded();
        return;
    }
    // A normal exit destroys the statics, and on Windows runs the DLL detach handlers, a job still uses.
    qWarning("dbtool-gui: a job was still running %lld ms after quitting; exiting without cleanup.",
             static_cast<long long>(limit.count()));
#if defined(_WIN32)
    TerminateProcess(GetCurrentProcess(), EXIT_FAILURE);
#else
    std::quick_exit(EXIT_FAILURE);
#endif
}

void AppController::SetShutdownDrainLimit(std::chrono::milliseconds limit, std::function<void()> onExceeded)
{
    _shutdownDrainLimit = limit;
    _onShutdownDrainExceeded = std::move(onExceeded);
}

bool AppController::DrainWorkers(std::chrono::milliseconds limit)
{
    auto const deadline =
        limit == std::chrono::milliseconds::max() ? QDeadlineTimer { QDeadlineTimer::Forever } : QDeadlineTimer { limit };
    auto const remaining = [&deadline] {
        return deadline.isForever() ? std::chrono::milliseconds::max()
                                    : std::chrono::milliseconds { std::max<qint64>(deadline.remainingTime(), 0) };
    };
    // Every worker gets its wait, so a stuck one does not keep the others from finishing.
    auto drained = _runner.Worker().WaitForDone(remaining());
    drained = _backupRunner.WaitForDone(remaining()) && drained;
    drained = _managedBackups.WaitForDone(remaining()) && drained;
    drained = _sqlQueryRunner.WaitForDone(remaining()) && drained;
    drained = _odbcWorker.WaitForDone(remaining()) && drained;
    return drained;
}

void AppController::BumpTargetGeneration()
{
    ++_targetGeneration;
    // Checks and re-reads of the old target still in flight are dropped on arrival, so they no longer count.
    _backupRequirementChecksInFlight = 0;
    _historyRefreshesInFlight = 0;
    if (std::exchange(_connecting, false))
    {
        _connectRequirementPending = false;
        if (std::exchange(_pluginReloadPending, false))
            RequestPluginReload();
        emit connectFinished();
    }
    UpdateActivity();
}

void AppController::TargetInputChanged(QString const& mode)
{
    // Only a connect in flight targets the input; a plugin load and an open connection do not depend on it.
    if (_connectionMode == mode && _connecting)
        BumpTargetGeneration();
}

void AppController::PersistSetting(char const* key, QVariant const& value)
{
    _settings->setValue(QLatin1String { key }, value);
    _settingsSync->start();
}

AppController::~AppController()
{
    Shutdown();

    // The sink captures `this`; leaving it on the process-wide manager would let the
    // next controller's first migration query call into a destroyed object.
    if (_installedMigrationLogSink)
        Lightweight::SqlMigration::MigrationManager::GetInstance().SetLogSink({});
    if (g_instance == this)
        g_instance = nullptr;
}

AppController* AppController::create(QQmlEngine* /*engine*/, QJSEngine* /*scriptEngine*/)
{
    if (!g_instance)
        g_instance = new AppController();
    QQmlEngine::setObjectOwnership(g_instance, QQmlEngine::CppOwnership);
    return g_instance;
}

void AppController::setCurrentProfile(QString const& name)
{
    if (_currentProfile == name)
        return;
    _currentProfile = name;
    PersistSetting(kKeyProfile, name);
    emit currentProfileChanged();
    emit connectionSummaryChanged();
    // Target DB has changed — drop the "connected" flag so the reload
    // below shows every migration as "unknown" until the user issues an
    // explicit Connect against the new profile and its applied-set gets
    // queried for real. Otherwise the previous profile's applied/pending
    // labels would keep rendering on top of the new plugin set.
    InvalidateConnectionTarget();
    BumpTargetGeneration();
    // Profiles may carry their own `pluginsDir`; switch the loaded plugin
    // set so the UI matches the selection without a reconnect.
    RequestPluginReload();
}

void AppController::setConnectionStringOverride(QString const& connectionString)
{
    if (_connectionStringOverride == connectionString)
        return;
    _connectionStringOverride = connectionString;
    PersistSetting(kKeyOverrideString, connectionString);
    emit connectionStringOverrideChanged();
    emit connectionSummaryChanged();
    TargetInputChanged(QStringLiteral("custom"));
}

void AppController::setCustomSchema(QString const& schema)
{
    if (_customSchema == schema)
        return;
    _customSchema = schema;
    PersistSetting(kKeyCustomSchema, schema);
    emit customSchemaChanged();
    emit connectionSummaryChanged();
    TargetInputChanged(QStringLiteral("custom"));
}

void AppController::setLogVisible(bool visible)
{
    if (_logVisible == visible)
        return;
    _logVisible = visible;
    PersistSetting(kKeyLogVisible, visible);
    emit logVisibleChanged();
}

void AppController::setViewMode(QString const& mode)
{
    // Only two legal values — fold anything unexpected back to "simple".
    QString const normalized = (mode == QStringLiteral("expert")) ? mode : QStringLiteral("simple");
    if (_viewMode == normalized)
        return;
    _viewMode = normalized;
    PersistSetting(kKeyViewMode, normalized);
    emit viewModeChanged();
}

void AppController::setConnectionMode(QString const& mode)
{
    QString const normalized =
        (mode == QStringLiteral("dsn") || mode == QStringLiteral("custom")) ? mode : QStringLiteral("profile");
    if (_connectionMode == normalized)
        return;
    _connectionMode = normalized;
    PersistSetting(kKeyMode, normalized);
    emit connectionModeChanged();
    emit connectionSummaryChanged();
    // Switching the connection source changes the target DB (profile's
    // DSN vs. the DSN dropdown vs. a custom string). Drop the connected
    // flag so the reload path renders "unknown" instead of re-applying
    // the prior source's applied-set to the new target.
    InvalidateConnectionTarget();
    BumpTargetGeneration();
    // Mode changes swap which plugins-dir is authoritative (profile's own
    // vs the user override), so reload to reflect the new effective set.
    RequestPluginReload();
}

void AppController::setSelectedDsn(QString const& dsn)
{
    if (_selectedDsn == dsn)
        return;
    _selectedDsn = dsn;
    PersistSetting(kKeyDsn, dsn);
    emit selectedDsnChanged();
    emit connectionSummaryChanged();
    TargetInputChanged(QStringLiteral("dsn"));
}

void AppController::setDsnUser(QString const& user)
{
    if (_dsnUser == user)
        return;
    _dsnUser = user;
    PersistSetting(kKeyDsnUser, user);
    emit dsnUserChanged();
    TargetInputChanged(QStringLiteral("dsn"));
}

void AppController::setDsnPassword(QString const& password)
{
    if (_dsnPassword == password)
        return;
    _dsnPassword = password;
    PersistSetting(kKeyDsnPassword, password);
    emit dsnPasswordChanged();
    TargetInputChanged(QStringLiteral("dsn"));
}

void AppController::setDsnSchema(QString const& schema)
{
    if (_dsnSchema == schema)
        return;
    _dsnSchema = schema;
    PersistSetting(kKeyDsnSchema, schema);
    emit dsnSchemaChanged();
    emit connectionSummaryChanged();
    TargetInputChanged(QStringLiteral("dsn"));
}

void AppController::setPluginsDir(QString const& pluginsDir)
{
    if (_pluginsDir == pluginsDir)
        return;
    _pluginsDir = pluginsDir;
    PersistSetting(kKeyPluginsDir, pluginsDir);
    emit pluginsDirChanged();
    emit connectionSummaryChanged();
    BumpTargetGeneration();
    // Users expect dropping a new plugins-dir path to populate the migration
    // list immediately — no explicit reconnect button press.
    RequestPluginReload();
}

void AppController::EncryptPlaintextPassword(Lightweight::Config::Profile const& profile)
{
    auto const configPath = std::filesystem::path { _profilePath.toStdString() };
    auto const upgraded =
        Lightweight::Config::UpgradePlaintextPassword(configPath, profile, Lightweight::Secrets::ProfileCipher::Builtin());
    if (!upgraded)
    {
        LogWarn(QStringLiteral("Could not encrypt the plaintext password of profile ‘%1’: %2")
                    .arg(QString::fromStdString(profile.name), QString::fromStdString(upgraded.error())));
        return;
    }
    LogInfo(QStringLiteral("Encrypted the plaintext password of profile ‘%1’ in %2")
                .arg(QString::fromStdString(profile.name), _profilePath));
    // Only the password changed: re-read the store in place; the watcher's echo of this write is ignored.
    if (auto reloaded = LoadProfileStore(_profilePath); reloaded)
    {
        _knownFingerprint = std::move(reloaded->fingerprint);
        _store = std::move(reloaded->store);
        _profiles.ReplaceFrom(_store);
        _managedBackups.setProfiles(_store.Profiles());
    }
    if (upgraded->insideGitWorkTree)
        LogWarn(QStringLiteral("%1 is inside a git repository; the old plaintext password may remain in its "
                               "history. Consider changing the database password.")
                    .arg(_profilePath));
}

QString AppController::defaultProfileStorePath() const
{
    return _discoveredStorePath;
}

void AppController::setProfileStorePath(QString const& path)
{
    if (_profileStorePath == path)
        return;
    _profileStorePath = path;
    PersistSetting(kKeyProfileStorePath, path);
    emit profileStorePathChanged();

    // Mirror the OnProfileFileChanged guard: do not swap the profile store
    // while a migration run is in flight — the runner is iterating against
    // the current plugin set, and pulling the rug out would invalidate it.
    if (_runner.phase() != MigrationRunner::Phase::Idle)
    {
        _lastWarning = QStringLiteral("Profile-store path will reload after the current run finishes.");
        emit lastWarningChanged();
        return;
    }

    // `loadProfiles({})` resolves to the discovered dbtool.yml; non-empty `path` is
    // honoured verbatim. Missing files are tolerated — the profile dropdown
    // simply goes empty.
    if (path.isEmpty() || std::filesystem::exists(path.toStdString()))
        (void) loadProfiles(path);
}

QString AppController::connectionSummary() const
{
    // A human-readable single line describing *exactly* what will happen on
    // the next `connectToProfile()` call, so the ConnectionPanel can show a
    // "Will connect via …" banner instead of the user having to infer from
    // which widget has text in it.
    if (_connectionMode == QStringLiteral("dsn"))
    {
        if (_selectedDsn.isEmpty())
            return QStringLiteral("Pick an ODBC data source to connect.");
        if (!_dsnSchema.isEmpty())
            return QStringLiteral("via ODBC DSN ‘%1’ (schema ‘%2’)").arg(_selectedDsn, _dsnSchema);
        return QStringLiteral("via ODBC DSN ‘%1’").arg(_selectedDsn);
    }
    if (_connectionMode == QStringLiteral("custom"))
    {
        if (_connectionStringOverride.isEmpty())
            return QStringLiteral("Enter a connection string to connect.");
        if (!_customSchema.isEmpty())
            return QStringLiteral("via custom connection string (schema ‘%1’)").arg(_customSchema);
        return QStringLiteral("via custom connection string");
    }
    // profile mode
    if (_currentProfile.isEmpty())
        return QStringLiteral("Pick a profile to connect.");
    return QStringLiteral("via profile ‘%1’").arg(_currentProfile);
}

void AppController::ReportError(QString const& message)
{
    _lastError = message;
    emit lastErrorChanged();
    emit error(message);
}

void AppController::ClearError()
{
    if (!_lastError.isEmpty())
    {
        _lastError.clear();
        emit lastErrorChanged();
    }
    if (!_lastWarning.isEmpty())
    {
        _lastWarning.clear();
        emit lastWarningChanged();
    }
}

bool AppController::loadProfiles(QString const& path)
{
    auto const effectivePath = path.isEmpty() ? _discoveredStorePath : path;

    auto result = LoadProfileStore(effectivePath);
    if (!result)
    {
        ReportError(QString::fromStdString(result.error()));
        return false;
    }
    _store = std::move(result->store);
    _storeLoaded = true;
    _knownFingerprint = std::move(result->fingerprint);
    _profiles.ReplaceFrom(_store);
    // The folder dbtool.yml names (if any) becomes the default backup folder, so an installer can
    // pre-configure it; a folder chosen in Settings still wins.
    _managedBackups.setConfiguredBackupFolder(QString::fromStdU16String(_store.DefaultBackupDir().u16string()));
    _managedBackups.setProfiles(_store.Profiles());
    if (_currentProfile.isEmpty())
        setCurrentProfile(QString::fromStdString(_store.DefaultProfileName()));

    if (_profilePath != effectivePath)
    {
        // Re-register the watcher against the new path. Editors that save
        // via write-then-rename drop the watch once the inode changes, so
        // we always clear and re-add, not just on path swaps.
        if (_fileWatcher)
        {
            auto const existing = _fileWatcher->files();
            if (!existing.isEmpty())
                _fileWatcher->removePaths(existing);
            if (std::filesystem::exists(effectivePath.toStdString()))
                _fileWatcher->addPath(effectivePath);
        }
        _profilePath = effectivePath;
        emit profilePathChanged();
    }

    // Profile data may carry a new `pluginsDir` (or the same profile with a
    // different value than last load). `setCurrentProfile` only fires when
    // the *name* changes, so handle the same-name-different-dir case here.
    if (EffectivePluginsDir() != _requestedPluginDirectories)
        RequestPluginReload();
    return true;
}

bool AppController::connectToProfile()
{
    // One connect at a time: a second click while one is in flight starts nothing.
    if (_connecting || _shuttingDown)
        return false;
    ClearError();

    // Connecting replaces the default connection and reconnects the mapper a running migration uses.
    if (_runner.phase() != MigrationRunner::Phase::Idle)
    {
        _lastWarning = QStringLiteral("Wait for the current migration run to finish before connecting.");
        emit lastWarningChanged();
        return false;
    }
    // A backup or restore in flight reports its record into the requirement of the database it ran against.
    if (_backupRunner.phase() != BackupRunner::Phase::Idle)
    {
        _lastWarning = QStringLiteral("Wait for the current backup or restore to finish before connecting.");
        emit lastWarningChanged();
        return false;
    }

    // A waiver was given for the previous database; this one starts without it.
    setMigrateWithoutBackup(false);
    _lastRecordError.clear();

    // Connection-source dispatch — the `connectionMode` property is the
    // single source of truth. This removes the old ambiguity where whichever
    // of "profile", "dsn" or "custom string" had content in a field was the
    // one that won on connect; now the user explicitly picks which mode.
    // Plugin-dir resolution lives in `EffectivePluginsDir()` so the same
    // rules apply on startup, property change, and connect.
    std::string connectionString;
    Lightweight::Config::Profile const* profile = _store.Find(_currentProfile.toStdString());
    auto passwordOrigin = Lightweight::Config::PasswordOrigin::None;

    if (_connectionMode == QStringLiteral("dsn"))
    {
        if (_selectedDsn.isEmpty())
        {
            ReportError(QStringLiteral("No ODBC DSN selected."));
            return false;
        }
        connectionString = "DSN=" + Lightweight::FormatConnectionStringValue(_selectedDsn.toStdString());
        // Append overrides only when the user actually typed them, so a DSN
        // with Windows-authentication still works with both fields empty.
        // SQL-Server-auth DSNs often don't persist credentials; this is how
        // the user supplies them without switching to Custom mode.
        AppendAttr(connectionString, "UID", _dsnUser.toStdString());
        AppendAttr(connectionString, "PWD", _dsnPassword.toStdString());
    }
    else if (_connectionMode == QStringLiteral("custom"))
    {
        if (_connectionStringOverride.isEmpty())
        {
            ReportError(QStringLiteral("Enter a connection string first."));
            return false;
        }
        connectionString = _connectionStringOverride.toStdString();
    }
    else // "profile"
    {
        if (!profile)
        {
            ReportError(QStringLiteral("No profile selected."));
            return false;
        }
        auto resolved = ProfileConnectionString(*profile);
        if (!resolved)
        {
            ReportError(QString::fromStdString(resolved.error()));
            return false;
        }
        connectionString = std::move(resolved->connectionString);
        passwordOrigin = resolved->passwordOrigin;
    }

    if (connectionString.empty())
    {
        ReportError(QStringLiteral("Profile '%1' has no connection string or DSN.").arg(_currentProfile));
        return false;
    }

    // The schema of each connection mode comes from its own input: the profile's `schema` field,
    // `dsnSchema`, or `customSchema`.
    auto schema = std::string {};
    if (_connectionMode == QStringLiteral("custom"))
        schema = _customSchema.toStdString();
    else if (_connectionMode == QStringLiteral("dsn"))
        schema = _dsnSchema.toStdString();
    else if (profile)
        schema = profile->schema;

    LogInfo(QStringLiteral("Connecting %1…").arg(connectionSummary()));

    // This connect supersedes every plugin reload requested or in flight; their outcomes become stale.
    _startupPending = false;
    _pluginReloadFlushQueued = false;
    _pluginReloadPending = false;
    BumpTargetGeneration();
    auto const generation = _targetGeneration.load();
    auto request = ConnectRequest {
        .connectionString = std::move(connectionString),
        .schema = std::move(schema),
        .pluginDirectories = EffectivePluginsDir(),
        .mode = _connectionMode,
        .target = _connectionMode == QStringLiteral("profile") ? _currentProfile
                  : _connectionMode == QStringLiteral("dsn")   ? _selectedDsn
                                                               : QString {},
        .generation = generation,
        .logSink =
            [this](std::string_view message) {
                auto line = QString::fromUtf8(message.data(), static_cast<qsizetype>(message.size()));
                // During a run, the manager's lines join the runner's queue, so they keep their order.
                if (_runner.phase() != MigrationRunner::Phase::Idle)
                    _runner.Notify(MigrationRunner::Notification::Line(std::move(line), LogLevel::Info));
                else
                    LogInfo(std::move(line));
            },
        .log = [this](LogLevel level, QString line) { Log(level, std::move(line)); },
        .isStale = [this, generation] { return _targetGeneration.load() != generation; },
    };
    _requestedPluginDirectories = request.pluginDirectories;
    _installedMigrationLogSink = true;
    _connecting = true;

    // A plaintext password that proves to work is stored encrypted; the profile is copied, since the
    // rewrite reloads the store. A plaintext origin is only ever set in profile mode, with a profile.
    auto plaintextProfile = passwordOrigin == Lightweight::Config::PasswordOrigin::Plaintext
                                ? std::optional<Lightweight::Config::Profile> { *profile }
                                : std::nullopt;
    auto* const plugins = &_plugins->entries;
    auto const recordError = _lastRecordError;
    auto const finish = [this](std::optional<BackupRequirementState> requirement, std::uint64_t generation) {
        QMetaObject::invokeMethod(
            this,
            [this, requirement = std::move(requirement), generation]() mutable {
                FinishConnect(std::move(requirement), generation);
            },
            Qt::QueuedConnection);
    };
    _runner.Worker().Enqueue(
        [this, finish, plugins, request, plaintextProfile = std::move(plaintextProfile), recordError]() mutable {
            auto& manager = Lightweight::SqlMigration::MigrationManager::GetInstance();
            auto outcome = RunConnect(request, *plugins, manager);
            auto const ok = outcome.ok;
            QMetaObject::invokeMethod(
                this,
                [this, request, outcome = std::move(outcome), plaintextProfile = std::move(plaintextProfile)]() mutable {
                    ApplyConnect(request, std::move(outcome), plaintextProfile);
                },
                Qt::QueuedConnection);
            // The new connection's requirement is part of the connect, so no other job sees it unchecked.
            auto const evaluate = ok && !request.isStale();
            finish(evaluate ? std::optional { EvaluateBackupRequirement(manager, recordError) } : std::nullopt,
                   request.generation);
        },
        [this, finish, generation](std::exception const& e) {
            QMetaObject::invokeMethod(
                this,
                [this, generation, message = ExceptionMessage(e)] {
                    if (generation == _targetGeneration)
                        ReportError(QStringLiteral("Could not connect: %1").arg(message));
                },
                Qt::QueuedConnection);
            finish(std::nullopt, generation);
        });
    UpdateActivity();
    return true;
}

void AppController::ApplyConnect(ConnectRequest const& request,
                                 ConnectOutcome outcome,
                                 std::optional<Lightweight::Config::Profile> const& plaintextProfile)
{
    for (auto& [line, level]: outcome.log)
        Log(level, std::move(line));
    if (outcome.generation != _targetGeneration)
        return;

    if (!outcome.pluginError.isEmpty())
        ReportError(outcome.pluginError);
    if (!outcome.ok)
    {
        // The default connection now names a database that could not be opened.
        InvalidateConnectionTarget();
        ApplySnapshot(std::move(outcome.snapshot));
        ReportError(outcome.error);
        return;
    }

    // The job goes on to evaluate the backup requirement of the new connection.
    _connectRequirementPending = true;
    if (!outcome.snapshot.warning.isEmpty())
    {
        _lastWarning = outcome.snapshot.warning;
        emit lastWarningChanged();
    }
    ApplySnapshot(std::move(outcome.snapshot));

    if (plaintextProfile)
        EncryptPlaintextPassword(*plaintextProfile);

    // Backup and restore reuse exactly this connection, including a decrypted or resolved password.
    _backupRunner.setConnectionString(QString::fromStdString(request.connectionString));
    _backupRunner.setRecordSchema(QString::fromStdString(outcome.defaultSchema));

    _connected = true;
    _readyLogged = true;
    // The rows of the last run belong to the previous connection.
    _runner.tasks()->clear();
    _connectedMode = request.mode;
    _connectedTarget = !request.target.isEmpty()            ? request.target
                       : !outcome.reachedDatabase.isEmpty() ? outcome.reachedDatabase
                                                            : QStringLiteral("custom connection");
    _backupRunner.setTargetLabel(_connectedTarget);
    emit connectedChanged();
    LogInfo(QStringLiteral("Applied migrations: %1 / %2").arg(appliedCount()).arg(_migrations.rowCount()));
    // Bindings that read `connected` alongside the counters see the final state.
    emit migrationsChanged();
    UpdateActivity();
}

void AppController::FinishConnect(std::optional<BackupRequirementState> requirement, std::uint64_t generation)
{
    // A connect whose target changed ended already, when the generation was bumped.
    if (generation != _targetGeneration)
        return;
    _connecting = false;
    _connectRequirementPending = false;
    if (_connected && requirement)
        ApplyBackupRequirement(*std::move(requirement));
    // A plugin reload put off during the connect runs now, on the state the connect left.
    if (std::exchange(_pluginReloadPending, false))
        RequestPluginReload();
    UpdateActivity();
    emit connectFinished();
}

int AppController::migrationCount() const
{
    return _migrations.rowCount();
}

int AppController::appliedCount() const
{
    return _migrations.Counts().applied;
}

int AppController::pendingCount() const
{
    return _migrations.Counts().pending;
}

int AppController::preMigrateStepCount() const
{
    return _preMigrateStepCount;
}

void AppController::setMigrateWithoutBackup(bool value)
{
    _runner.setBackupWaived(value ? BackupWaiver::Waived : BackupWaiver::NotWaived);
    if (_migrateWithoutBackup == value)
        return;
    _migrateWithoutBackup = value;
    emit migrateWithoutBackupChanged();
}

void AppController::RequestBackupRequirementCheck()
{
    // The check reads through the migration connection, which a running migration uses.
    if (_runner.phase() != MigrationRunner::Phase::Idle)
        return;
    if (!_connected)
    {
        ApplyBackupRequirement({});
        return;
    }
    if (_shuttingDown)
        return;

    ++_backupRequirementChecksInFlight;
    auto const generation = _targetGeneration.load();
    auto const post = [this, generation](std::optional<BackupRequirementState> state) {
        QMetaObject::invokeMethod(
            this,
            [this, state = std::move(state), generation]() mutable {
                // A stale check stopped counting when the generation was bumped.
                if (generation != _targetGeneration)
                    return;
                --_backupRequirementChecksInFlight;
                if (state && _connected)
                    ApplyBackupRequirement(*std::move(state));
                UpdateActivity();
            },
            Qt::QueuedConnection);
    };
    _runner.Worker().Enqueue(
        [this, post, recordError = _lastRecordError, generation] {
            // The step predicates may take long; a check for a target the UI left is not worth it.
            if (_targetGeneration.load() != generation)
                return post(std::nullopt);
            post(EvaluateBackupRequirement(Lightweight::SqlMigration::MigrationManager::GetInstance(), recordError));
        },
        [post](std::exception const&) { post(std::nullopt); });
    UpdateActivity();
}

void AppController::ApplyBackupRequirement(BackupRequirementState state)
{
    // Nothing left to waive, so a waiver ticked earlier must not carry over to a later requirement.
    if (!state.required)
        setMigrateWithoutBackup(false);

    if (state.required == _backupRequired && state.reasons == _backupRequirementReasons
        && state.note == _backupRequirementNote)
        return;
    _backupRequired = state.required;
    _backupRequirementReasons = std::move(state.reasons);
    _backupRequirementNote = std::move(state.note);
    emit backupRequirementChanged();
}

void AppController::backUpNow()
{
    if (!_connected || AnyRunInFlight())
        return;
    auto const profileName = _connectedTarget.toStdString();
    auto const output = Lightweight::Config::ResolveBackupOutput({
        .requested = {},
        .folder = std::filesystem::path { _managedBackups.effectiveBackupFolder().toStdU16String() },
        .profileName = profileName,
        .now = std::chrono::system_clock::now(),
    });
    if (!output)
    {
        LogError(QStringLiteral("Back up now: no backup folder is configured; choose one in Settings."));
        return;
    }
    auto ignored = std::error_code {};
    std::filesystem::create_directories(output->path.parent_path(), ignored);
    if (!std::filesystem::is_directory(output->path.parent_path()))
    {
        LogError(QStringLiteral("Back up now: cannot create the backup folder %1.")
                     .arg(QString::fromStdU16String(output->path.parent_path().u16string())));
        return;
    }
    _backupRunner.runBackup(QString::fromStdU16String(output->path.u16string()));
}

int AppController::pendingUnreleasedCount() const
{
    return _migrations.Counts().pendingUnreleased;
}

int AppController::unknownCount() const
{
    return _migrations.Counts().unknown;
}

int AppController::checksumMismatchCount() const
{
    return _migrations.Counts().checksumMismatch;
}

int AppController::issuesCount() const
{
    auto const counts = _migrations.Counts();
    return counts.unknown + counts.checksumMismatch;
}

int AppController::selectionCount() const
{
    return _migrations.SelectedCount();
}

QString AppController::currentReleaseLabel() const
{
    // "Current" = state the DB is actually in. We intentionally do NOT
    // gate on `_connected` here: the Simple view only shows the Status card
    // when connected anyway, so a pre-connect read never reaches the user.
    //
    // Everything comes from the models, never from the manager: this
    // accessor is bound from QML on the UI thread and re-evaluated on every
    // `migrationsChanged()`, including the ones fired between migrations of
    // a run, while the worker holds the only `SqlConnection`. The model is
    // kept in sync per row by `migrationCompleted` → `SetRowStatusByTimestamp`.
    auto const& applied = _migrations.AppliedTimestamps();
    if (applied.empty())
        return QStringLiteral("(fresh database)");

    auto const highest = *applied.rbegin();
    auto const& releases = _releases.Rows();
    auto const countAbove = [&applied](std::uint64_t bound) {
        return std::ranges::distance(applied.upper_bound(bound), applied.end());
    };

    // The release that contains `highest`: the one with the smallest upper bound at or above it.
    ReleaseRow const* release = nullptr;
    for (auto const& row: releases)
        if (row.highestTimestamp >= highest && (!release || row.highestTimestamp < release->highestTimestamp))
            release = &row;
    if (release)
    {
        // Applied timestamps above this release's upper bound are "unreleased" tip migrations.
        auto label = release->version;
        if (auto const unreleased = countAbove(release->highestTimestamp); unreleased > 0)
            label += QStringLiteral(" (+%1 unreleased)").arg(unreleased);
        return label;
    }

    // No release contains this timestamp: no releases at all (show the raw
    // timestamp), or the applied set runs past the last release.
    if (releases.empty())
        return QString::number(highest);
    auto const& last = *std::ranges::max_element(releases, {}, &ReleaseRow::highestTimestamp);
    return QStringLiteral("%1 (+%2 unreleased)").arg(last.version).arg(countAbove(last.highestTimestamp));
}

QString AppController::targetReleaseLabel() const
{
    return _targetReleaseLabel;
}

QString AppController::buildFailureReport() const
{
    // Keep the bundle copy-paste friendly: plain ASCII separators, no
    // ANSI colour codes, no tabs. Users paste this into support tickets
    // that often run through email clients which mangle exotic glyphs.
    auto const nowUtc = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    auto const appVersion = QCoreApplication::applicationVersion().isEmpty() ? QStringLiteral("(unknown)")
                                                                             : QCoreApplication::applicationVersion();

    QString report;
    report.reserve(4096);
    auto add = [&](QString const& line) {
        report += line;
        report += QLatin1Char('\n');
    };
    auto kv = [&](QString const& key, QString const& value) {
        // Fixed 18-char key column so the values line up even when the
        // user pastes into a proportional editor with tab-stops off.
        add(QStringLiteral("%1 %2").arg(key.leftJustified(18, QLatin1Char(' ')), value));
    };

    add(QStringLiteral("dbtool-gui - failure report"));
    add(QStringLiteral("---------------------------"));
    kv(QStringLiteral("Timestamp (UTC):"), nowUtc);
    kv(QStringLiteral("App version:"), appVersion);
    kv(QStringLiteral("Qt runtime:"), QString::fromUtf8(qVersion()));
    kv(QStringLiteral("OS:"), QSysInfo::prettyProductName());
    add({});
    kv(QStringLiteral("Profile:"), _currentProfile.isEmpty() ? QStringLiteral("(none)") : _currentProfile);
    kv(QStringLiteral("Connection mode:"), _connectionMode);
    kv(QStringLiteral("Connection:"), connectionSummary());
    kv(QStringLiteral("Plugins dir:"), _pluginsDir.isEmpty() ? QStringLiteral("(profile default)") : _pluginsDir);
    add({});
    kv(QStringLiteral("Current version:"), currentReleaseLabel());
    kv(QStringLiteral("Target version:"), targetReleaseLabel());
    kv(QStringLiteral("Applied / Pending:"), QStringLiteral("%1 / %2").arg(appliedCount()).arg(pendingCount()));
    add({});

    if (_lastFailedTimestamp.isEmpty())
    {
        kv(QStringLiteral("Failed migration:"), QStringLiteral("(no per-migration failure recorded)"));
    }
    else
    {
        auto const title = _lastFailedTitle.isEmpty() ? QStringLiteral("(unknown title)") : _lastFailedTitle;
        kv(QStringLiteral("Failed migration:"), QStringLiteral("%1  %2").arg(_lastFailedTimestamp, title));
    }
    kv(QStringLiteral("Error:"), _lastError.isEmpty() ? QStringLiteral("(none)") : _lastError);
    if (!_lastWarning.isEmpty())
        kv(QStringLiteral("Warning:"), _lastWarning);

    add({});
    add(QStringLiteral("-- Run log (last %1 lines) --").arg(_recentLog.size()));
    if (_recentLog.isEmpty())
        add(QStringLiteral("(empty)"));
    else
        for (auto const& line: _recentLog)
            add(line);

    return report;
}

QStringList AppController::selectedMigrationTimestamps() const
{
    return _migrations.SelectedTimestamps();
}

void AppController::setMigrationSelected(QString const& timestamp, bool selected)
{
    (void) _migrations.SetSelectedByTimestamp(timestamp.toULongLong(),
                                              selected ? SelectionState::Selected : SelectionState::NotSelected);
}

void AppController::selectAllPending(bool selected)
{
    _migrations.SelectAllPending(selected);
}

QString AppController::releaseHighestTimestamp(QString const& version) const
{
    auto const& releases = _releases.Rows();
    auto const release = std::ranges::find(releases, version, &ReleaseRow::version);
    if (release == releases.end())
        return {};
    return QString::number(release->highestTimestamp);
}

void AppController::requestMigrationSqlPreview(QString const& timestamp)
{
    // Timestamps reach QML as decimal strings.
    auto ok = false;
    auto const value = timestamp.toULongLong(&ok);
    if (!ok)
    {
        emit migrationSqlPreviewReady(timestamp, {});
        return;
    }
    // Queued behind the run, the preview would only arrive once the run is over.
    if (_runner.phase() != MigrationRunner::Phase::Idle)
    {
        emit migrationSqlPreviewReady(timestamp, { QStringLiteral("-- Preview unavailable during a migration run.") });
        return;
    }
    // Without a connection the preview would open the default connection of a target the UI left.
    if (!_connected)
    {
        emit migrationSqlPreviewReady(timestamp, { QStringLiteral("-- Connect to a database to preview its SQL.") });
        return;
    }
    if (_shuttingDown)
        return;

    auto const generation = _targetGeneration.load();
    auto const post = [this, timestamp, generation](QStringList statements) {
        QMetaObject::invokeMethod(
            this,
            [this, timestamp, generation, statements = std::move(statements)] {
                // The statements of a connection the UI left are dropped; the dialog says so instead.
                emit migrationSqlPreviewReady(
                    timestamp,
                    generation == _targetGeneration
                        ? statements
                        : QStringList { QStringLiteral("-- The connection changed; open the preview again.") });
            },
            Qt::QueuedConnection);
    };
    _runner.Worker().Enqueue(
        [this, post, value, generation] {
            if (_targetGeneration.load() != generation)
                return post({});
            post(PreviewMigrationStatements(Lightweight::SqlMigration::MigrationManager::GetInstance(), value));
        },
        [post](std::exception const& e) { post({ QStringLiteral("-- Preview failed: %1").arg(ExceptionMessage(e)) }); });
}

void AppController::refreshOdbcDataSources()
{
    if (_shuttingDown)
        return;
    if (_odbcRefreshing)
    {
        _odbcRefreshAgain = true;
        return;
    }
    _odbcRefreshing = true;
    UpdateActivity();

    auto const finish = [this](std::vector<Lightweight::Odbc::DataSourceInfo> sources, QString failure) {
        QMetaObject::invokeMethod(
            this,
            [this, sources = std::move(sources), failure = std::move(failure)] {
                if (failure.isEmpty())
                    _odbcDataSources.Replace(sources);
                else
                    LogWarn(QStringLiteral("Could not list the ODBC data sources: %1").arg(failure));
                _odbcRefreshing = false;
                UpdateActivity();
                if (std::exchange(_odbcRefreshAgain, false))
                    refreshOdbcDataSources();
            },
            Qt::QueuedConnection);
    };
    _odbcWorker.Enqueue([finish] { finish(Lightweight::Odbc::EnumerateDataSources(), {}); },
                        [finish](std::exception const& e) { finish({}, QString::fromUtf8(e.what())); });
}

QString AppController::profilePathDisplay() const
{
    if (_profilePath.isEmpty())
        return {};
    // Normalise both sides to forward slashes so the prefix match works on
    // Windows regardless of how the path was originally produced (QDir +
    // std::filesystem disagree on separator on Windows). Only the leading
    // home component is collapsed — `~` inside the path is intentionally
    // left alone in case the user has a literal `~` directory.
    auto const normalized = QDir::fromNativeSeparators(_profilePath);
    auto const home = QDir::fromNativeSeparators(QDir::homePath());
    if (!home.isEmpty()
        && (normalized.compare(home, Qt::CaseInsensitive) == 0
            || normalized.startsWith(home + QLatin1Char('/'), Qt::CaseInsensitive)))
    {
        return QStringLiteral("~") + normalized.mid(home.length());
    }
    return normalized;
}

void AppController::openProfileFileExternally()
{
    if (_profilePath.isEmpty())
        return;
    QDesktopServices::openUrl(QUrl::fromLocalFile(_profilePath));
}

bool AppController::AnyRunInFlight() const
{
    return _runner.phase() != MigrationRunner::Phase::Idle || _backupRunner.phase() != BackupRunner::Phase::Idle
           || _managedBackups.phase() != ManagedBackupController::Phase::Idle;
}

bool AppController::closeBlocked() const
{
    return AnyRunInFlight();
}

QString AppController::closeBlockedReason() const
{
    return CloseBlockedReason(_runner, _backupRunner, _managedBackups);
}

bool AppController::closeBlockingRunCancellable() const
{
    return _runner.phase() == MigrationRunner::Phase::Running;
}

void AppController::cancelBlockingRun()
{
    if (closeBlockingRunCancellable())
        _runner.cancel();
}

QString AppController::ProfileTargetPath() const
{
    if (!_profilePath.isEmpty())
        return _profilePath;
    if (!_profileStorePath.isEmpty())
        return _profileStorePath;
    return _discoveredStorePath;
}

std::string AppController::OpenConnectionPassword() const
{
    if (_connectedMode == QStringLiteral("dsn"))
        return _dsnPassword.toStdString();
    if (_connectedMode == QStringLiteral("custom"))
        return Lightweight::Config::SplitInlinePassword(_connectionStringOverride.toStdString()).password;
    return {};
}

QVariantMap AppController::connectionDraft() const
{
    auto draft = QVariantMap {
        { QStringLiteral("available"), false },
        { QStringLiteral("kind"), QStringLiteral("dsn") },
        { QStringLiteral("dsn"), QString {} },
        { QStringLiteral("connectionString"), QString {} },
        { QStringLiteral("user"), QString {} },
        { QStringLiteral("schema"), QString {} },
        { QStringLiteral("hasPassword"), false },
        { QStringLiteral("suggestedName"), QString {} },
        { QStringLiteral("targetFile"), ProfileTargetPath() },
    };
    // A profile connection is already saved; nothing is open otherwise.
    if (!_connected || _connectedMode == QStringLiteral("profile"))
        return draft;

    draft[QStringLiteral("available")] = true;
    auto base = _connectedTarget;
    if (_connectedMode == QStringLiteral("dsn"))
    {
        draft[QStringLiteral("dsn")] = _selectedDsn;
        draft[QStringLiteral("user")] = _dsnUser;
        draft[QStringLiteral("schema")] = _dsnSchema;
        draft[QStringLiteral("hasPassword")] = !_dsnPassword.isEmpty();
        base = _selectedDsn;
    }
    else
    {
        auto const split = Lightweight::Config::SplitInlinePassword(_connectionStringOverride.toStdString());
        draft[QStringLiteral("kind")] = QStringLiteral("connectionString");
        draft[QStringLiteral("connectionString")] = QString::fromStdString(split.connectionString);
        draft[QStringLiteral("schema")] = _customSchema;
        draft[QStringLiteral("hasPassword")] = !split.password.empty();
    }

    // Suggest a name nobody uses yet: "<base>", then "<base> 2", "<base> 3", …
    auto suggested = base;
    for (auto n = 2; _store.Find(suggested.toStdString()) != nullptr; ++n)
        suggested = QStringLiteral("%1 %2").arg(base).arg(n);
    draft[QStringLiteral("suggestedName")] = suggested;
    return draft;
}

QString AppController::profileNameProblem(QString const& name) const
{
    auto const trimmed = name.trimmed();
    if (trimmed.isEmpty())
        return QStringLiteral("Enter a name for the profile.");
    if (_store.Find(trimmed.toStdString()) != nullptr)
        return QStringLiteral("A profile named ‘%1’ already exists.").arg(trimmed);
    return {};
}

QString AppController::addProfile(QVariantMap const& fields)
{
    namespace Cfg = Lightweight::Config;
    auto const text = [&fields](char const* key) {
        return fields.value(QString::fromLatin1(key)).toString().trimmed();
    };

    if (AnyRunInFlight())
        return QStringLiteral("Wait for the current migration, backup or restore to finish before adding a profile.");

    auto const name = text("name");
    if (auto const problem = profileNameProblem(name); !problem.isEmpty())
        return problem;

    auto const isDsn = text("kind") == QStringLiteral("dsn");
    if (isDsn && text("dsn").isEmpty())
        return QStringLiteral("Choose an ODBC data source.");
    if (!isDsn && text("connectionString").isEmpty())
        return QStringLiteral("Enter a connection string.");

    auto request = Cfg::ProfileRequest {
        .name = name.toStdString(),
        .connectionString = isDsn ? std::string {} : text("connectionString").toStdString(),
        .dsn = isDsn ? text("dsn").toStdString() : std::string {},
        .uid = text("user").toStdString(),
        .schema = text("schema").toStdString(),
        .pluginsDir = {},
        // Not trimmed: a password may legitimately start or end with a space.
        .password = fields.value(QStringLiteral("password")).toString().toStdString(),
    };
    if (request.password.empty() && fields.value(QStringLiteral("useOpenConnectionPassword")).toBool())
        request.password = OpenConnectionPassword();

    auto const path = ProfileTargetPath();
    auto const added = Cfg::AddProfileToFile(std::filesystem::path { path.toStdString() },
                                             request,
                                             Cfg::ReplaceExisting::No,
                                             fields.value(QStringLiteral("makeDefault")).toBool() ? Cfg::MakeDefault::Yes
                                                                                                  : Cfg::MakeDefault::No,
                                             Lightweight::Secrets::ProfileCipher::Builtin());
    if (!added)
        return QString::fromStdString(added.error());

    LogInfo(QStringLiteral("Added profile ‘%1’ to %2").arg(name, path));
    // Reload so the profile list and the managed-backup rows pick it up now rather than when the
    // file watcher notices. The selected profile and the open connection are left alone.
    if (!loadProfiles(path))
        return QStringLiteral("The profile was saved to %1, but the profile list could not be reloaded.").arg(path);
    return {};
}

void AppController::revealInFolder(QString const& path)
{
    if (path.isEmpty())
        return;
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
}

void AppController::OnProfileFileChanged(QString const& path)
{
    _changedProfileFile = path;
    _profileFileDebounce->start();
}

namespace
{
    /// @return The content of the file @p path; empty when it cannot be read.
    std::optional<QByteArray> ReadFile(QString const& path)
    {
        auto file = QFile { path };
        if (!file.open(QIODevice::ReadOnly))
            return std::nullopt;
        return file.readAll();
    }
} // namespace

std::optional<AppController::ProfileFileFingerprint> AppController::FingerprintOf(QString const& path)
{
    auto const content = ReadFile(path);
    if (!content)
        return std::nullopt;
    return ProfileFileFingerprint {
        .path = path,
        .size = content->size(),
        .sha256 = QCryptographicHash::hash(*content, QCryptographicHash::Sha256),
    };
}

std::expected<AppController::LoadedProfileStore, std::string> AppController::LoadProfileStore(QString const& requestedPath)
{
    // As `ProfileStore::LoadOrDefault`, an empty path means the per-user default file.
    auto const filePath = requestedPath.isEmpty() ? Lightweight::Config::ProfileStore::DefaultPath()
                                                  : std::filesystem::path { requestedPath.toStdU16String() };
    auto const path = QString::fromStdU16String(filePath.u16string());
    if (!std::filesystem::exists(filePath))
        return LoadedProfileStore { .store = {}, .fingerprint = std::nullopt };
    auto const content = ReadFile(path);
    if (!content)
        return std::unexpected(std::format("failed to read {}", path.toStdString()));
    auto store = Lightweight::Config::ProfileStore::Parse(
        std::string_view { content->constData(), static_cast<std::size_t>(content->size()) }, filePath);
    if (!store)
        return std::unexpected(std::move(store.error()));
    return LoadedProfileStore {
        .store = *std::move(store),
        .fingerprint =
            ProfileFileFingerprint {
                .path = path,
                .size = content->size(),
                .sha256 = QCryptographicHash::hash(*content, QCryptographicHash::Sha256),
            },
    };
}

void AppController::HandleProfileFileChange()
{
    auto const path = _changedProfileFile;
    // A notification of the file the store was loaded from before a path change is stale.
    if (_shuttingDown || path != _profilePath)
        return;
    // Write-then-rename drops the watch on the old inode; watch the new file.
    auto const exists = std::filesystem::exists(path.toStdString());
    if (_fileWatcher && exists && !_fileWatcher->files().contains(path))
        _fileWatcher->addPath(path);
    if (!exists)
        return;

    if (_knownFingerprint && _knownFingerprint == FingerprintOf(path))
    {
        emit profileFileChangeHandled(ProfileFileReaction::IgnoredOwnWrite);
        return;
    }
    // Swapping the store under a connect or a run would pull profiles and plugins away from it.
    if (ProfileReloadBlocked())
    {
        _deferredProfileFile = path;
        emit profileFileChangeHandled(ProfileFileReaction::Deferred);
        return;
    }
    _deferredProfileFile.clear();
    (void) loadProfiles(path);
    emit profileFileChangeHandled(ProfileFileReaction::Reloaded);
}

bool AppController::ProfileReloadBlocked() const
{
    return busy() || AnyRunInFlight();
}

void AppController::ApplyDeferredProfileFileChange()
{
    _deferredProfileFileQueued = false;
    if (_deferredProfileFile.isEmpty() || _shuttingDown || ProfileReloadBlocked())
        return;
    auto const path = std::exchange(_deferredProfileFile, QString {});
    if (path != _profilePath || !std::filesystem::exists(path.toStdString()))
        return;
    if (_knownFingerprint && _knownFingerprint == FingerprintOf(path))
    {
        emit profileFileChangeHandled(ProfileFileReaction::IgnoredOwnWrite);
        return;
    }
    (void) loadProfiles(path);
    emit profileFileChangeHandled(ProfileFileReaction::Reloaded);
}

std::vector<std::filesystem::path> AppController::EffectivePluginsDir() const
{
    // Resolution order:
    //   1. The active profile's own `pluginsDir`. A profile that ships a
    //      path is self-describing and takes precedence so switching
    //      profiles can swap plugin sets without re-editing globals.
    //   2. The store-wide `defaultPluginsDir` from `dbtool.yml`. Shared with
    //      dbtool so a single YAML entry configures both tools — and a
    //      list of directories at this layer fans out into multi-dir
    //      discovery downstream.
    //   3. The Settings-page global (`config/pluginsDir`) — GUI-only
    //      fallback for users without YAML access.
    //   4. Empty — caller surfaces "no plugins discovered".
    if (auto const* profile = _store.Find(_currentProfile.toStdString()); profile && !profile->pluginsDir.empty())
        return { profile->pluginsDir };
    if (!_store.DefaultPluginsDir().empty())
        return _store.DefaultPluginsDir();
    if (!_pluginsDir.isEmpty())
        return { std::filesystem::path { _pluginsDir.toStdString() } };
    return {};
}

void AppController::InvalidateConnectionTarget()
{
    if (!_connected)
        return;
    _connected = false;
    // Results of jobs for the old target, such as a requirement check, become stale.
    BumpTargetGeneration();
    // The rows of a run in flight still update; those of a finished run belong to the old target.
    if (_runner.phase() == MigrationRunner::Phase::Idle)
        _runner.tasks()->clear();
    _connectedMode.clear();
    _connectedTarget.clear();
    emit connectedChanged();
    setMigrateWithoutBackup(false);
    _lastRecordError.clear();
    RequestBackupRequirementCheck();
    LogInfo(QStringLiteral("Disconnected."));
}

void AppController::RefreshMigrationModelsAfterRun()
{
    if (_shuttingDown)
        return;
    auto const state = _connected ? AppliedState::Database : AppliedState::Unknown;
    auto const withRequirement = _connected && _runner.phase() == MigrationRunner::Phase::Idle;
    auto const generation = _targetGeneration.load();
    ++_historyRefreshesInFlight;
    if (withRequirement)
        ++_backupRequirementChecksInFlight;
    auto const post = [this, withRequirement, generation](std::optional<HistoryRefreshOutcome> outcome) {
        QMetaObject::invokeMethod(
            this,
            [this, withRequirement, generation, outcome = std::move(outcome)]() mutable {
                // A stale re-read stopped counting when the generation was bumped.
                if (generation != _targetGeneration)
                    return;
                --_historyRefreshesInFlight;
                if (withRequirement)
                    --_backupRequirementChecksInFlight;
                if (outcome)
                    ApplyHistoryRefresh(*std::move(outcome));
                UpdateActivity();
            },
            Qt::QueuedConnection);
    };
    // The snapshot touches schema_migrations, which an unmanaged database lacks; it then falls back to
    // the plugin-only view and says why.
    _runner.Worker().Enqueue(
        [this, post, state, withRequirement, recordError = _lastRecordError, generation] {
            if (_targetGeneration.load() != generation)
                return post(std::nullopt);
            auto& manager = Lightweight::SqlMigration::MigrationManager::GetInstance();
            auto outcome = HistoryRefreshOutcome { .snapshot = CaptureMigrationSnapshot(manager, state),
                                                   .requirement = std::nullopt,
                                                   .generation = generation };
            if (withRequirement && _targetGeneration.load() == generation)
                outcome.requirement = EvaluateBackupRequirement(manager, recordError);
            post(std::move(outcome));
        },
        [post](std::exception const&) { post(std::nullopt); });
    UpdateActivity();
}

void AppController::ApplyHistoryRefresh(HistoryRefreshOutcome outcome)
{
    if (!outcome.snapshot.warning.isEmpty())
    {
        _lastWarning = outcome.snapshot.warning;
        emit lastWarningChanged();
    }
    ApplySnapshot(std::move(outcome.snapshot));
    if (outcome.requirement && _connected)
        ApplyBackupRequirement(*std::move(outcome.requirement));
}

bool AppController::pluginsLoading() const noexcept
{
    return _startupPending || _pluginReloadFlushQueued || _pluginReloadJobsInFlight > 0;
}

bool AppController::backupRequirementPending() const noexcept
{
    return _connectRequirementPending || _backupRequirementChecksInFlight > 0;
}

bool AppController::busy() const noexcept
{
    return pluginsLoading() || _odbcRefreshing || _connecting || backupRequirementPending() || _historyRefreshesInFlight > 0;
}

QString AppController::activityText() const
{
    if (_connecting && !_connectRequirementPending)
        return QStringLiteral("Connecting…");
    if (pluginsLoading())
        return QStringLiteral("Loading plugins…");
    if (_historyRefreshesInFlight > 0)
        return QStringLiteral("Reading the migration history…");
    if (backupRequirementPending())
        return QStringLiteral("Checking whether a backup is required…");
    if (_odbcRefreshing)
        return QStringLiteral("Listing ODBC data sources…");
    return {};
}

void AppController::UpdateActivity()
{
    auto const activity = std::array {
        pluginsLoading(), _odbcRefreshing, _connecting, backupRequirementPending(), _historyRefreshesInFlight > 0,
    };
    if (activity != _announcedActivity)
    {
        _announcedActivity = activity;
        emit activityChanged();
    }
    // A profile file change seen while busy applies once the controller is idle.
    if (!_deferredProfileFile.isEmpty() && !_deferredProfileFileQueued && !ProfileReloadBlocked())
    {
        _deferredProfileFileQueued = true;
        QMetaObject::invokeMethod(this, &AppController::ApplyDeferredProfileFileChange, Qt::QueuedConnection);
    }
}

void AppController::ApplySnapshot(MigrationSnapshot snapshot)
{
    _migrationsChangedThrottle->stop();
    _migrations.ReplaceRows(std::move(snapshot.migrations));
    _releases.ReplaceRows(std::move(snapshot.releases));
    _preMigrateStepCount = snapshot.preMigrateStepCount;
    _targetReleaseLabel = std::move(snapshot.targetReleaseLabel);
    emit migrationsChanged();
}

void AppController::RequestPluginReload()
{
    // Unloading the plugins closes the mapper's connection, which a running migration still uses.
    if (_runner.phase() != MigrationRunner::Phase::Idle)
    {
        _pluginReloadPending = true;
        _lastWarning = QString::fromLatin1(PluginReloadDeferredWarning);
        emit lastWarningChanged();
        return;
    }
    _pluginReloadPending = false;
    // The first load is still to come and reads the state of then; after a quit nothing starts.
    if (_startupPending || _shuttingDown || _pluginReloadFlushQueued)
        return;
    _pluginReloadFlushQueued = true;
    QMetaObject::invokeMethod(this, &AppController::FlushPluginReload, Qt::QueuedConnection);
    UpdateActivity();
}

void AppController::FlushPluginReload()
{
    // A connect in between loaded the plugins itself.
    if (!std::exchange(_pluginReloadFlushQueued, false))
        return;
    // A run or a connect started after the request; it reloads once it finished, on the state it left.
    if (_runner.phase() != MigrationRunner::Phase::Idle || _connecting)
    {
        _pluginReloadPending = true;
        UpdateActivity();
        return;
    }

    auto request = PluginReloadRequest {
        .directories = EffectivePluginsDir(),
        .state = _connected ? AppliedState::Database : AppliedState::Unknown,
        .generation = _targetGeneration,
    };
    _requestedPluginDirectories = request.directories;
    ++_pluginReloadJobsInFlight;
    auto* const plugins = &_plugins->entries;
    auto const post = [this](PluginReloadOutcome outcome) {
        QMetaObject::invokeMethod(
            this,
            [this, outcome = std::move(outcome)]() mutable { ApplyPluginReload(std::move(outcome)); },
            Qt::QueuedConnection);
    };
    _runner.Worker().Enqueue(
        [post, plugins, request] {
            post(RunPluginReload(request, *plugins, Lightweight::SqlMigration::MigrationManager::GetInstance()));
        },
        [post, generation = request.generation](std::exception const& e) {
            auto outcome = PluginReloadOutcome {};
            outcome.generation = generation;
            outcome.error = QStringLiteral("Failed to load plugins: %1").arg(ExceptionMessage(e));
            outcome.log.emplace_back(outcome.error, LogLevel::Error);
            post(std::move(outcome));
        });
    UpdateActivity();
}

void AppController::ApplyPluginReload(PluginReloadOutcome outcome)
{
    --_pluginReloadJobsInFlight;
    for (auto& [line, level]: outcome.log)
        Log(level, std::move(line));

    if (outcome.generation == _targetGeneration)
    {
        if (!outcome.error.isEmpty())
            ReportError(outcome.error);
        ApplySnapshot(std::move(outcome.snapshot));
        if (!pluginsLoading() && !_readyLogged && !_connected)
        {
            _readyLogged = true;
            LogInfo(QStringLiteral("Ready — not connected. Pick a profile to connect."));
        }
        // A later reload, if any, checks the requirement against the plugins it leaves; a connect does its own.
        if (!pluginsLoading() && !_connecting)
            RequestBackupRequirementCheck();
    }
    UpdateActivity();
}

void AppController::Log(LogLevel level, QString line)
{
    // Plugin hooks run on the worker and log through the manager's sink; their lines hop to this thread.
    if (QThread::currentThread() != thread())
    {
        QMetaObject::invokeMethod(this, [this, level, line = std::move(line)] { Log(level, line); }, Qt::QueuedConnection);
        return;
    }
    _logLines.Append(line, level);
    if (_logSinkAttached)
    {
        emit logLine(std::move(line), level);
        return;
    }
    // No QML receiver yet — buffer until `attachLogSink()` runs. Plugin
    // discovery happens in the constructor, well before `LogPanel.qml`
    // exists, so emitting now would drop the message on the floor.
    _pendingLogs.append({ std::move(line), level });
}

void AppController::LogInfo(QString line)
{
    Log(LogLevel::Info, std::move(line));
}

void AppController::LogWarn(QString line)
{
    Log(LogLevel::Warning, std::move(line));
}

void AppController::LogError(QString line)
{
    Log(LogLevel::Error, std::move(line));
}

void AppController::attachLogSink()
{
    if (_logSinkAttached)
        return; // Idempotent — re-instantiating the panel must not replay the banner.
    _logSinkAttached = true;
    // Replay in the order events occurred. Move-out so the buffer can drop
    // its allocation; it stays empty for the rest of the process lifetime.
    auto pending = std::move(_pendingLogs);
    _pendingLogs.clear();
    for (auto const& entry: pending)
        emit logLine(entry.first, entry.second);
}

} // namespace DbtoolGui
