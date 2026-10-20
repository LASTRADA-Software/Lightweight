// SPDX-License-Identifier: Apache-2.0

#include "AppController.hpp"
#include "BackupRequirementSupport.hpp"
#include "ControllerJobs.hpp"
#include "ExceptionMessage.hpp"
#include "Models/MigrationSnapshot.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>
#include <Lightweight/SqlConnectInfo.hpp>
#include <Lightweight/SqlConnection.hpp>
#include <Lightweight/SqlError.hpp>
#include <Lightweight/SqlMigration.hpp>
#include <Lightweight/SqlServerType.hpp>

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
#include <exception>
#include <filesystem>
#include <format>
#include <span>
#include <system_error>

#include <QtCore/QCoreApplication>
#include <QtCore/QDateTime>
#include <QtCore/QDebug>
#include <QtCore/QDir>
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

    /// Maps the internal `SqlServerType` enum to the human-readable display
    /// string already used by the project's `std::formatter` specialization
    /// (which `dbtool` and the docs also use). Keeps the GUI's "Connected:"
    /// log line consistent with every other surface that names a server.
    /// @param serverType Server flavour detected by `SqlConnection`.
    /// @return UTF-16 display string, e.g. `"PostgreSQL"` or `"SQLite"`.
    [[nodiscard]] QString ServerTypeDisplayName(Lightweight::SqlServerType serverType)
    {
        return QString::fromStdString(std::format("{}", serverType));
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
    QObject::connect(&_runner, &MigrationRunner::migrationCompleted, this, [this](qulonglong ts, QString const& newStatus) {
        _migrations.SetRowStatusByTimestamp(ts, newStatus);
        emit migrationsChanged(); // refreshes counters in the sidebar
    });
    QObject::connect(&_runner, &MigrationRunner::migrationFailed, this, [this](qulonglong ts) {
        _migrations.SetRowStatusByTimestamp(ts, QStringLiteral("pending"));
        // Capture the identity of the migration that failed
        // so the Simple view's Failure card can surface
        // "Failed migration: TS  title" without QML having
        // to re-query the list model after `finished`.
        _lastFailedTimestamp = QString::number(ts);
        _lastFailedTitle.clear();
        int const rows = _migrations.rowCount();
        for (int i = 0; i < rows; ++i)
        {
            auto const idx = _migrations.index(i, 0);
            auto const rowTs = _migrations.data(idx, MigrationListModel::TimestampRole).toString().toULongLong();
            if (rowTs == ts)
            {
                _lastFailedTitle = _migrations.data(idx, MigrationListModel::TitleRole).toString();
                break;
            }
        }
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

    // The log pane subscribes to `AppController::logLine` (single source of
    // truth). Forward the runners' streams so migration progress and backup
    // events surface in the same feed as the startup banner.
    QObject::connect(&_runner, &MigrationRunner::logLine, this, &AppController::logLine);
    QObject::connect(&_backupRunner, &BackupRunner::logLine, this, &AppController::logLine);
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
    // A managed backup or restore that connected with a plaintext password has proven it, exactly like
    // the interactive connect: store it encrypted right away. Only the password the run used is encrypted, so an
    // edit made to the file while the run was in flight is never overwritten with the older value.
    _managedBackups.setPlaintextPasswordVerifiedHandler([this](Lightweight::Config::Profile const& verified) {
        auto const* current = _store.Find(verified.name);
        if (current && current->password == verified.password)
            EncryptPlaintextPassword(verified);
    });
    _runner.setBusyProbe([this] {
        return _managedBackups.phase() != ManagedBackupController::Phase::Idle
               || _backupRunner.phase() != BackupRunner::Phase::Idle;
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
        });
    // A recorded Back up or Restore can satisfy the backup requirement; one that could not be recorded says why.
    QObject::connect(&_backupRunner, &BackupRunner::finished, this, [this](bool /*ok*/, QString const& /*summary*/) {
        _lastRecordError = _backupRunner.lastResult().value(QStringLiteral("recordError")).toString();
        RefreshBackupRequirement();
    });

    _fileWatcher = new QFileSystemWatcher(this);
    QObject::connect(_fileWatcher, &QFileSystemWatcher::fileChanged, this, &AppController::OnProfileFileChanged);

    // Restore preferences BEFORE loading profiles. `loadProfiles` only sets
    // a default profile when `_currentProfile` is empty, so feeding the
    // saved value here preserves the user's choice across restarts. Only
    // these reads and the discovery below run before the first frame.
    QSettings settings;
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
    _runner.cancel();
    // A job in flight uses the plugins, the manager and this object's address; let it finish first.
    (void) _runner.Worker().WaitForDone(std::chrono::milliseconds::max());
    (void) _odbcWorker.WaitForDone(std::chrono::milliseconds::max());
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
    QSettings().setValue(kKeyProfile, name);
    emit currentProfileChanged();
    emit connectionSummaryChanged();
    // Target DB has changed — drop the "connected" flag so the reload
    // below shows every migration as "unknown" until the user issues an
    // explicit Connect against the new profile and its applied-set gets
    // queried for real. Otherwise the previous profile's applied/pending
    // labels would keep rendering on top of the new plugin set.
    InvalidateConnectionTarget();
    ++_targetGeneration;
    // Profiles may carry their own `pluginsDir`; switch the loaded plugin
    // set so the UI matches the selection without a reconnect.
    RequestPluginReload();
}

void AppController::setConnectionStringOverride(QString const& connectionString)
{
    if (_connectionStringOverride == connectionString)
        return;
    _connectionStringOverride = connectionString;
    QSettings().setValue(kKeyOverrideString, connectionString);
    emit connectionStringOverrideChanged();
    emit connectionSummaryChanged();
}

void AppController::setCustomSchema(QString const& schema)
{
    if (_customSchema == schema)
        return;
    _customSchema = schema;
    QSettings().setValue(kKeyCustomSchema, schema);
    emit customSchemaChanged();
    emit connectionSummaryChanged();
}

void AppController::setLogVisible(bool visible)
{
    if (_logVisible == visible)
        return;
    _logVisible = visible;
    QSettings().setValue(kKeyLogVisible, visible);
    emit logVisibleChanged();
}

void AppController::setViewMode(QString const& mode)
{
    // Only two legal values — fold anything unexpected back to "simple".
    QString const normalized = (mode == QStringLiteral("expert")) ? mode : QStringLiteral("simple");
    if (_viewMode == normalized)
        return;
    _viewMode = normalized;
    QSettings().setValue(kKeyViewMode, normalized);
    emit viewModeChanged();
}

void AppController::setConnectionMode(QString const& mode)
{
    QString const normalized =
        (mode == QStringLiteral("dsn") || mode == QStringLiteral("custom")) ? mode : QStringLiteral("profile");
    if (_connectionMode == normalized)
        return;
    _connectionMode = normalized;
    QSettings().setValue(kKeyMode, normalized);
    emit connectionModeChanged();
    emit connectionSummaryChanged();
    // Switching the connection source changes the target DB (profile's
    // DSN vs. the DSN dropdown vs. a custom string). Drop the connected
    // flag so the reload path renders "unknown" instead of re-applying
    // the prior source's applied-set to the new target.
    InvalidateConnectionTarget();
    ++_targetGeneration;
    // Mode changes swap which plugins-dir is authoritative (profile's own
    // vs the user override), so reload to reflect the new effective set.
    RequestPluginReload();
}

void AppController::setSelectedDsn(QString const& dsn)
{
    if (_selectedDsn == dsn)
        return;
    _selectedDsn = dsn;
    QSettings().setValue(kKeyDsn, dsn);
    emit selectedDsnChanged();
    emit connectionSummaryChanged();
}

void AppController::setDsnUser(QString const& user)
{
    if (_dsnUser == user)
        return;
    _dsnUser = user;
    QSettings().setValue(kKeyDsnUser, user);
    emit dsnUserChanged();
}

void AppController::setDsnPassword(QString const& password)
{
    if (_dsnPassword == password)
        return;
    _dsnPassword = password;
    QSettings().setValue(kKeyDsnPassword, password);
    emit dsnPasswordChanged();
}

void AppController::setDsnSchema(QString const& schema)
{
    if (_dsnSchema == schema)
        return;
    _dsnSchema = schema;
    QSettings().setValue(kKeyDsnSchema, schema);
    emit dsnSchemaChanged();
    emit connectionSummaryChanged();
}

void AppController::setPluginsDir(QString const& pluginsDir)
{
    if (_pluginsDir == pluginsDir)
        return;
    _pluginsDir = pluginsDir;
    QSettings().setValue(kKeyPluginsDir, pluginsDir);
    emit pluginsDirChanged();
    emit connectionSummaryChanged();
    ++_targetGeneration;
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
    QSettings().setValue(kKeyProfileStorePath, path);
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

    auto result = Lightweight::Config::ProfileStore::LoadOrDefault(effectivePath.toStdString());
    if (!result)
    {
        ReportError(QString::fromStdString(result.error()));
        return false;
    }
    _store = std::move(*result);
    _storeLoaded = true;
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

    LogInfo(QStringLiteral("Connecting %1…").arg(connectionSummary()));

    auto& manager = Lightweight::SqlMigration::MigrationManager::GetInstance();

    // Plugin discovery is a pure filesystem operation independent of ODBC,
    // so a failed connect must not hide which DLLs were picked up. Reloading
    // here also guarantees the user always sees the latest set of DLLs even
    // if they dropped new ones into the directory between clicks — same
    // watcher-less "just rescan on action" semantics as dbtool. The post-init
    // hooks run below, on the new connection, not on the one being replaced.
    ReloadPluginsNow();

    Lightweight::SqlConnectionString const defaultConnectionString { connectionString };

    // Bootstrap a fresh SQLite file if the profile points at one that does not
    // exist yet — mirrors dbtool's behaviour so "create from scratch" works
    // from the GUI too. No-op for every other driver.
    (void) Lightweight::EnsureSqliteDatabaseFileExists(defaultConnectionString);

    Lightweight::SqlConnection::SetDefaultConnectionString(defaultConnectionString);

    // Resolve the effective default schema *before* opening the connection
    // so the post-connect hook installed by `MigrationManager::SetDefaultSchema`
    // is in place when the DataMapper below establishes the link. Each
    // connection mode has its own UI input — profile mode pulls from the
    // profile's own `schema` field, DSN mode reads `dsnSchema`, and custom
    // mode reads `customSchema`.
    std::string effectiveSchema;
    if (_connectionMode == QStringLiteral("custom"))
        effectiveSchema = _customSchema.toStdString();
    else if (_connectionMode == QStringLiteral("dsn"))
        effectiveSchema = _dsnSchema.toStdString();
    else if (_connectionMode == QStringLiteral("profile") && profile)
        effectiveSchema = profile->schema;

    try
    {
        manager.SetDefaultSchema(effectiveSchema);
    }
    catch (std::invalid_argument const& ex)
    {
        ReportError(QString::fromStdString(ex.what()));
        return false;
    }

    // The default connection string was just replaced, so this reconnects the manager's data mapper
    // to it - now, where a failure can be reported, rather than on the manager's first use.
    try
    {
        std::ignore = manager.GetDataMapper();
    }
    catch (std::exception const& e)
    {
        auto const failureMsg = ExceptionMessage(e);
        LogError(QStringLiteral("Connection failed: %1").arg(failureMsg));
        ReportError(QStringLiteral("Could not open the database connection: %1").arg(failureMsg));
        return false;
    }

    // Connection is up — surface server flavour / database / version so the
    // user knows *which* DB they just opened. Pulling these from
    // `SqlConnection` (post-handshake) means we report what the driver
    // actually negotiated, not what was in the typed-in connection string.
    QString reachedDatabase;
    try
    {
        auto const& connection = manager.GetDataMapper().Connection();
        auto const serverDisplay = ServerTypeDisplayName(connection.ServerType());
        auto const databaseName = QString::fromStdString(connection.DatabaseName());
        reachedDatabase = databaseName;
        auto const serverVersion = QString::fromStdString(connection.ServerVersion());
        LogInfo(QStringLiteral("Connected: %1 · %2 · %3").arg(serverDisplay, databaseName, serverVersion));
        if (!effectiveSchema.empty())
            LogInfo(QStringLiteral("Schema search path: %1").arg(QString::fromStdString(effectiveSchema)));
    }
    catch (std::exception const&)
    {
        // Best-effort metadata read — if it fails the connect itself still
        // succeeded, so silently skip the descriptive line rather than
        // contradict the previous "Connecting …" with a misleading error.
    }

    // A plaintext password just proved that it works: store it encrypted. The
    // profile is copied because the rewrite triggers a watcher-driven reload
    // that replaces `_store`. A plaintext origin is only ever set in profile
    // mode, which has already returned when no profile is selected.
    if (passwordOrigin == Lightweight::Config::PasswordOrigin::Plaintext)
        EncryptPlaintextPassword(Lightweight::Config::Profile { *profile });

    // Route status banners emitted by plugin post-init hooks (e.g. the
    // "Transitioning from LASTRADA_PROPERTIES …" line from SqlMigrationsPlugin)
    // through `AppController::LogInfo` so the message lands in the GUI's log
    // pane instead of stderr. Installed before invoking the hooks below so
    // the very first banner is captured.
    manager.SetLogSink(
        [this](std::string_view message) { LogInfo(QString::fromUtf8(message.data(), static_cast<int>(message.size()))); });
    _installedMigrationLogSink = true;

    // Run plugin post-init hooks (e.g. SqlMigrationsPlugin's transition glue
    // that observes `LASTRADA_PROPERTIES NR=4` and populates the virtual
    // applied overlay). Until this fires, the migration list would render the
    // entire legacy range as pending. Per-plugin failures are surfaced via
    // `LogError` but never abort the connect — a misbehaving plugin must not
    // brick the GUI's read paths.
    //
    // We deliberately do NOT call `CreateMigrationHistory()` here: the table
    // is materialised lazily inside Lightweight's write paths
    // (`ApplySingleMigration` / `MarkMigrationAsApplied` / `RevertSingleMigration`),
    // so introspection-only connects against a fresh or unmanaged database
    // leave the schema untouched.
    RunPostInitHooks(_plugins->entries, manager, [this](QString line) { LogError(std::move(line)); });

    // The snapshot reads `GetAppliedMigrationIds`, which merges the plugin-supplied
    // overlay with whatever rows exist in `schema_migrations`. The accessor
    // already swallows a missing-table `SqlException` and returns the overlay
    // alone; any other failure (e.g. malformed row, unreadable column) falls
    // back to a plain-plugin view and surfaces as a warning instead of leaving
    // the GUI stuck on a stale model.
    auto snapshot = CaptureMigrationSnapshot(manager, AppliedState::Database);
    if (!snapshot.warning.isEmpty())
    {
        _lastWarning = snapshot.warning;
        emit lastWarningChanged();
    }
    ApplySnapshot(std::move(snapshot));

    // Backup/restore reuse exactly the connection that just opened — including a
    // decrypted or resolved password and the DSN form, which the raw profile
    // fields would lack.
    _backupRunner.setConnectionString(QString::fromStdString(connectionString));
    _backupRunner.setRecordSchema(QString::fromStdString(std::string { manager.DefaultSchema() }));

    _connected = true;
    _readyLogged = true;
    // The rows of the last run belong to the previous connection.
    _runner.tasks()->clear();
    _connectedMode = _connectionMode;
    _connectedTarget = _connectionMode == QStringLiteral("profile") ? _currentProfile
                       : _connectionMode == QStringLiteral("dsn")
                           ? _selectedDsn
                           : (!reachedDatabase.isEmpty() ? reachedDatabase : QStringLiteral("custom connection"));
    _backupRunner.setTargetLabel(_connectedTarget);
    emit connectedChanged();
    LogInfo(QStringLiteral("Applied migrations: %1 / %2").arg(appliedCount()).arg(_migrations.rowCount()));
    // Release labels and counter bindings depend on the applied-set we
    // just fetched. `Refresh` above already emitted `migrationsChanged`,
    // but that fired while `_connected` was still false — re-emit after
    // the flip so any binding that indirectly reads `_connected` (now or
    // later) gets the final state.
    emit migrationsChanged();
    RefreshBackupRequirement();
    return true;
}

namespace
{

    /// Counts rows in `MigrationListModel` whose `StatusRole` equals `status`.
    /// Duplicating this traversal in the QML layer would miss model refreshes
    /// (see the notify-enabled `Q_PROPERTY` wiring in AppController.hpp).
    int CountWithStatus(MigrationListModel const& model, QString const& status)
    {
        int n = 0;
        int const rows = model.rowCount();
        for (int i = 0; i < rows; ++i)
        {
            auto const idx = model.index(i, 0);
            auto const value = model.data(idx, MigrationListModel::StatusRole).toString();
            if (value == status)
                ++n;
        }
        return n;
    }

} // namespace

int AppController::migrationCount() const
{
    return _migrations.rowCount();
}

int AppController::appliedCount() const
{
    return CountWithStatus(_migrations, QStringLiteral("applied"));
}

int AppController::pendingCount() const
{
    return CountWithStatus(_migrations, QStringLiteral("pending"));
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

namespace
{

    /// What declares a backup requirement, for when it could not be evaluated: the releases of the pending
    /// migrations that require one, the pending migrations that require one themselves (unless their release
    /// is named), and the pre-migrate steps that declare one, in that order.
    QStringList DeclaredBackupRequirements(Lightweight::SqlMigration::MigrationManager const& manager)
    {
        auto pending = std::vector<Lightweight::SqlMigration::MigrationBase const*> {};
        try
        {
            for (auto const* migration: manager.GetPending())
                pending.push_back(migration);
        }
        catch (std::exception const&)
        {
            // The history cannot be read either, so every migration may be pending.
            for (auto const* migration: manager.GetAllMigrations())
                pending.push_back(migration);
        }
        auto releases = QStringList {};
        auto migrations = QStringList {};
        for (auto const* migration: pending)
        {
            if (auto const* release = manager.FindReleaseForTimestamp(migration->GetTimestamp());
                release && release->requiresBackup)
            {
                auto const reason = QStringLiteral("release %1").arg(QString::fromStdString(release->version));
                if (!releases.contains(reason))
                    releases.append(reason);
            }
            else if (migration->RequiresBackup())
                migrations.append(QStringLiteral("migration %1 \"%2\"")
                                      .arg(migration->GetTimestamp().value)
                                      .arg(QString::fromStdString(std::string { migration->GetTitle() })));
        }
        auto steps = QStringList {};
        for (auto const& step: manager.GetPreMigrateSteps())
            if (step.declaresBackupRequirement)
                steps.append(QStringLiteral("step \"%1\"").arg(QString::fromStdString(step.title)));
        return releases + migrations + steps;
    }

} // namespace

void AppController::RefreshBackupRequirement()
{
    // The check reads through the migration connection, which a running migration uses.
    if (_runner.phase() != MigrationRunner::Phase::Idle)
        return;
    // While connected it also reads the plugins' migrations, which a plugin load is replacing.
    if (_connected && pluginsLoading())
    {
        _backupRequirementAfterReloadPending = true;
        return;
    }

    auto required = false;
    auto reasons = QStringList {};
    auto note = QString {};
    if (_connected)
    {
        auto& manager = Lightweight::SqlMigration::MigrationManager::GetInstance();
        try
        {
            auto const requirement = manager.BackupRequirementFor(TimestampsOf(manager.GetPending()));
            required = requirement.required && !requirement.satisfied;
            if (required)
            {
                for (auto const& reason: requirement.reasons)
                    reasons.append(QString::fromStdString(reason));
                if (!_lastRecordError.isEmpty())
                    note = _lastRecordError;
                else if (requirement.lastBackup)
                    note = QString::fromStdString(Lightweight::Tools::DescribeStaleBackup(*requirement.lastBackup));
            }
        }
        catch (std::exception const& e)
        {
            reasons = DeclaredBackupRequirements(manager);
            required = !reasons.isEmpty();
            if (required)
                note = QStringLiteral("Could not check the backup requirement: %1").arg(ExceptionMessage(e));
        }
    }

    // Nothing left to waive, so a waiver ticked earlier must not carry over to a later requirement.
    if (!required)
        setMigrateWithoutBackup(false);

    if (required == _backupRequired && reasons == _backupRequirementReasons && note == _backupRequirementNote)
        return;
    _backupRequired = required;
    _backupRequirementReasons = std::move(reasons);
    _backupRequirementNote = std::move(note);
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
    int n = 0;
    int const rows = _migrations.rowCount();
    for (int i = 0; i < rows; ++i)
    {
        auto const idx = _migrations.index(i, 0);
        if (_migrations.data(idx, MigrationListModel::StatusRole).toString() != QStringLiteral("pending"))
            continue;
        if (_migrations.data(idx, MigrationListModel::ReleaseVersionRole).toString().isEmpty())
            ++n;
    }
    return n;
}

int AppController::unknownCount() const
{
    return CountWithStatus(_migrations, QStringLiteral("unknown"));
}

int AppController::checksumMismatchCount() const
{
    return CountWithStatus(_migrations, QStringLiteral("checksum-mismatch"));
}

int AppController::issuesCount() const
{
    return unknownCount() + checksumMismatchCount();
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
    auto const applied = _migrations.AppliedTimestamps();
    if (applied.empty())
        return QStringLiteral("(fresh database)");

    auto const highest = std::ranges::max(applied);
    auto const& releases = _releases.Rows();
    auto const countAbove = [&applied](std::uint64_t bound) {
        return std::ranges::count_if(applied, [bound](std::uint64_t timestamp) { return timestamp > bound; });
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
    auto const ts = timestamp.toULongLong();
    int const rows = _migrations.rowCount();
    for (int i = 0; i < rows; ++i)
    {
        auto const idx = _migrations.index(i, 0);
        auto const rowTs = _migrations.data(idx, MigrationListModel::TimestampRole).toString().toULongLong();
        if (rowTs == ts)
        {
            _migrations.setData(idx, selected, MigrationListModel::SelectedRole);
            return;
        }
    }
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

QStringList AppController::previewMigrationSql(QString const& timestamp) const
{
    // Parse as uint64_t — timestamps are emitted to QML as decimal strings.
    bool ok = false;
    auto const ts = timestamp.toULongLong(&ok);
    if (!ok)
        return {};

    // Belt-and-suspenders for the same race documented in
    // `currentReleaseLabel`: `PreviewMigration` issues INFORMATION_SCHEMA
    // queries via `MakeWidthLookup` against the shared `SqlConnection`. If
    // QML triggers this accessor while the worker thread is mid-migration,
    // we'd hit "Connection is busy" again. The Simple-view UI disables
    // preview during runs, but explicit-view callers (or future ones) might
    // not — short-circuit to a placeholder when a run is in flight.
    if (_runner.phase() != MigrationRunner::Phase::Idle)
        return { QStringLiteral("-- Preview unavailable during a migration run.") };
    // The worker is swapping the plugins, and with them the migrations, out from under the manager.
    if (pluginsLoading())
        return { QStringLiteral("-- Preview unavailable while the plugins load.") };

    auto const& manager = Lightweight::SqlMigration::MigrationManager::GetInstance();
    auto const* migration = manager.GetMigration(Lightweight::SqlMigration::MigrationTimestamp { ts });
    if (!migration)
        return {};

    // PreviewMigration needs an active default connection (to pick the
    // target dialect). If the user hasn't connected yet, surface a single
    // informational "line" instead of an empty list so the dialog body
    // doesn't look accidentally blank.
    try
    {
        auto const statements = manager.PreviewMigration(*migration);
        QStringList out;
        out.reserve(static_cast<qsizetype>(statements.size()));
        for (auto const& s: statements)
            out.push_back(QString::fromStdString(s));
        return out;
    }
    catch (std::exception const& e)
    {
        return { QStringLiteral("-- Preview failed: %1").arg(ExceptionMessage(e)) };
    }
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
    // Don't swap the profile list out from under a running migration — that
    // would yank migrations the plugins are still enumerated against. The
    // user can re-click Reconnect once the run finishes.
    if (_runner.phase() != MigrationRunner::Phase::Idle)
        return;

    // Editors like Notepad or vim use write-then-rename; the original inode
    // goes away and the watcher drops the file. We debounce via a short
    // singleShot to let the atomic rename settle, then re-add and reload.
    QTimer::singleShot(150, this, [this, path] {
        if (_fileWatcher && !_fileWatcher->files().contains(path) && std::filesystem::exists(path.toStdString()))
        {
            _fileWatcher->addPath(path);
        }
        if (!std::filesystem::exists(path.toStdString()))
            return; // file was deleted, skip reload
        (void) loadProfiles(path);
    });
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
    // The rows of a run in flight still update; those of a finished run belong to the old target.
    if (_runner.phase() == MigrationRunner::Phase::Idle)
        _runner.tasks()->clear();
    _connectedMode.clear();
    _connectedTarget.clear();
    emit connectedChanged();
    setMigrateWithoutBackup(false);
    _lastRecordError.clear();
    RefreshBackupRequirement();
    LogInfo(QStringLiteral("Disconnected."));
}

void AppController::RefreshMigrationModelsAfterRun()
{
    // The worker is swapping the plugins out from under the manager; re-read once that is done.
    if (pluginsLoading())
    {
        _modelRefreshAfterReloadPending = true;
        return;
    }
    // The snapshot touches schema_migrations, which an unmanaged database lacks;
    // it then falls back to the plugin-only view and says why.
    auto snapshot = CaptureMigrationSnapshot(Lightweight::SqlMigration::MigrationManager::GetInstance(),
                                             _connected ? AppliedState::Database : AppliedState::Unknown);
    if (!snapshot.warning.isEmpty())
    {
        _lastWarning = snapshot.warning;
        emit lastWarningChanged();
    }
    ApplySnapshot(std::move(snapshot));
    RefreshBackupRequirement();
}

bool AppController::pluginsLoading() const noexcept
{
    return _startupPending || _pluginReloadFlushQueued || _pluginReloadJobsInFlight > 0;
}

bool AppController::busy() const noexcept
{
    return pluginsLoading() || _odbcRefreshing;
}

QString AppController::activityText() const
{
    if (pluginsLoading())
        return QStringLiteral("Loading plugins…");
    if (_odbcRefreshing)
        return QStringLiteral("Listing ODBC data sources…");
    return {};
}

void AppController::UpdateActivity()
{
    auto const loading = pluginsLoading();
    if (loading == _announcedPluginsLoading && _odbcRefreshing == _announcedOdbcRefreshing)
        return;
    _announcedPluginsLoading = loading;
    _announcedOdbcRefreshing = _odbcRefreshing;
    emit activityChanged();
}

void AppController::ApplySnapshot(MigrationSnapshot snapshot)
{
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
    // A run started after the request; it reloads once it finished.
    if (_runner.phase() != MigrationRunner::Phase::Idle)
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

void AppController::ReloadPluginsNow()
{
    // Every reload requested or in flight, the first one included, is superseded by this one;
    // their outcomes become stale.
    ++_targetGeneration;
    _startupPending = false;
    _pluginReloadFlushQueued = false;
    (void) _runner.Worker().WaitForDone(std::chrono::milliseconds::max());

    auto request = PluginReloadRequest {
        .directories = EffectivePluginsDir(),
        .state = AppliedState::Unknown,
        .generation = _targetGeneration,
    };
    _requestedPluginDirectories = request.directories;
    auto outcome = RunPluginReload(request, _plugins->entries, Lightweight::SqlMigration::MigrationManager::GetInstance());
    for (auto& [line, level]: outcome.log)
        Log(level, std::move(line));
    if (!outcome.error.isEmpty())
        ReportError(outcome.error);
    // The connect reads the history and checks the backup requirement itself, on its new connection.
    ApplySnapshot(std::move(outcome.snapshot));
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
        _backupRequirementAfterReloadPending = true;
        if (!pluginsLoading() && !_readyLogged && !_connected)
        {
            _readyLogged = true;
            LogInfo(QStringLiteral("Ready — not connected. Pick a profile to connect."));
        }
    }
    // A later reload, if any, does what was put off during this one.
    if (!pluginsLoading())
    {
        if (std::exchange(_modelRefreshAfterReloadPending, false) && _connected)
        {
            _backupRequirementAfterReloadPending = false;
            RefreshMigrationModelsAfterRun();
        }
        else if (std::exchange(_backupRequirementAfterReloadPending, false))
            RefreshBackupRequirement();
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
