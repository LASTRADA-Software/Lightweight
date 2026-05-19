// SPDX-License-Identifier: Apache-2.0

#include "AppController.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>
#include <Lightweight/SqlConnection.hpp>
#include <Lightweight/SqlError.hpp>
#include <Lightweight/SqlMigration.hpp>

#include <PluginIngestion.hpp>
#include <PluginLoader.hpp>

namespace DbtoolGui
{

/// Pimpl payload — concrete vector of loaded plugins. Lives entirely in
/// this translation unit so the moc-parsed `AppController.hpp` doesn't
/// need to see `<PluginIngestion.hpp>`.
struct AppController::PluginsBundle
{
    std::vector<Lightweight::Tools::LoadedPlugin> entries;
};

} // namespace DbtoolGui

#include <algorithm>
#include <exception>
#include <filesystem>
#include <span>

#include <QtCore/QCoreApplication>
#include <QtCore/QDateTime>
#include <QtCore/QDebug>
#include <QtCore/QDir>
#include <QtCore/QFileSystemWatcher>
#include <QtCore/QModelIndex>
#include <QtCore/QSettings>
#include <QtCore/QSysInfo>
#include <QtCore/QTimer>
#include <QtCore/QUrl>
#include <QtGui/QDesktopServices>

namespace DbtoolGui
{

namespace
{

    AppController* g_instance = nullptr;

    /// Initial value for `AppController::_backupRestoreEnabled`. Set by
    /// `AppController::SeedBackupRestoreEnabled` from `main.cpp` after the
    /// command line is parsed, and consumed by the singleton's constructor.
    bool s_initialBackupRestoreEnabled = false;

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

    /// Appends `;{key}={value}` to `cs` when `value` is non-empty. Kept
    /// non-escaping on purpose: ODBC driver managers accept unescaped values as
    /// long as they contain no `;` or `}` — passwords with those characters are
    /// the user's responsibility to escape themselves (typically by picking a
    /// different password). Matches the behaviour of every other dbtool-style
    /// front-end that takes UID/PWD as plain fields.
    void AppendAttr(std::string& cs, std::string_view key, std::string_view value)
    {
        if (value.empty())
            return;
        cs += ';';
        cs += key;
        cs += '=';
        cs += value;
    }

    /// Extracts the friendliest available message from a thrown exception.
    /// Prefers `SqlException::info().message` because ODBC error strings
    /// include the driver-level explanation ("no such table", "column foo is
    /// missing", …) while `what()` usually decays to the bare SQLSTATE.
    QString ExceptionMessage(std::exception const& e)
    {
        if (auto const* sqle = dynamic_cast<Lightweight::SqlException const*>(&e))
        {
            auto const& info = sqle->info();
            QString const msg = QString::fromStdString(info.message).trimmed();
            QString const state = QString::fromStdString(info.sqlState).trimmed();
            if (!msg.isEmpty())
            {
                if (!state.isEmpty() && state != QStringLiteral("00000"))
                    return QStringLiteral("%1 [SQLSTATE %2]").arg(msg, state);
                return msg;
            }
        }
        auto const fallback = QString::fromUtf8(e.what()).trimmed();
        return fallback.isEmpty() ? QStringLiteral("(no details reported)") : fallback;
    }

    /// Builds the `Tools::PluginIngestOptions` the GUI uses for every
    /// `IngestPlugins` call. Verbose mode pipes shadow + loading lines
    /// through `qInfo`; load errors always reach `qWarning` so a broken
    /// plugin file is visible in the log even when verbose is off.
    /// `throwOnLoadError = false` keeps the rest of the migration set
    /// usable when one plugin fails to load.
    [[nodiscard]] Lightweight::Tools::PluginIngestOptions MakeGuiIngestOptions(bool verbose)
    {
        Lightweight::Tools::PluginIngestOptions opts;
        if (verbose)
        {
            opts.logShadowed = [](std::string_view msg) {
                qInfo("%s", std::string(msg).c_str());
            };
            opts.logLoading = [](std::string_view msg) {
                qInfo("%s", std::string(msg).c_str());
            };
        }
        opts.logError = [](std::string_view msg) {
            qWarning("%s", std::string(msg).c_str());
        };
        opts.throwOnLoadError = false; // GUI surfaces the error and keeps running
        return opts;
    }

} // namespace

void AppController::SeedBackupRestoreEnabled(bool enabled) noexcept
{
    s_initialBackupRestoreEnabled = enabled;
}

void AppController::SeedVerbose(bool enabled) noexcept
{
    s_initialVerbose = enabled;
}

AppController::AppController(QObject* parent):
    QObject(parent),
    _backupRestoreEnabled(s_initialBackupRestoreEnabled),
    _verbose(s_initialVerbose),
    _plugins(std::make_unique<PluginsBundle>())
{
    _runner.SetManager(&Lightweight::SqlMigration::MigrationManager::GetInstance());
    _backupRunner.setEnabled(_backupRestoreEnabled);

    QObject::connect(&_migrations, &MigrationListModel::selectionChanged, this, &AppController::selectionChanged);

    // Live per-row status updates from the runner. Instead of calling
    // `_migrations.Refresh()` after every migration (which rebuilds the whole
    // model and is noisy in the UI) we flip individual row statuses as
    // events fire: "pending" → "running" → "applied" (or back to "pending"
    // on failure / rollback).
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
    auto const bufferLogLine = [this](QString const& line, int /*level*/) {
        _recentLog.append(line);
        while (_recentLog.size() > kRecentLogCapacity)
            _recentLog.removeFirst();
    };
    QObject::connect(&_runner, &MigrationRunner::logLine, this, bufferLogLine);
    QObject::connect(&_backupRunner, &BackupRunner::logLine, this, bufferLogLine);
    // After a run finishes, do one authoritative refresh. `Refresh` touches
    // schema_migrations and will throw on an unmanaged database — we fall
    // back to `RefreshPluginsOnly` in that case. An uncaught exception
    // here would propagate into Qt's signal dispatcher and terminate the
    // process (observed as `abort()` on Windows).
    QObject::connect(&_runner, &MigrationRunner::finished, this, [this](bool ok, QString const& /*summary*/) {
        if (ok)
        {
            _lastFailedTimestamp.clear();
            _lastFailedTitle.clear();
        }
        auto& manager = Lightweight::SqlMigration::MigrationManager::GetInstance();
        try
        {
            _migrations.Refresh(manager);
            _releases.Refresh(manager);
        }
        catch (std::exception const& e)
        {
            _lastWarning = QStringLiteral("Could not refresh migration list: %1").arg(ExceptionMessage(e));
            emit lastWarningChanged();
            try
            {
                _migrations.RefreshPluginsOnly(manager);
                _releases.Refresh(manager);
            }
            catch (std::exception const&)
            {
                // Even the plugin-only refresh failed — leave the model alone.
            }
        }
        emit migrationsChanged();
    });

    _fileWatcher = new QFileSystemWatcher(this);
    QObject::connect(_fileWatcher, &QFileSystemWatcher::fileChanged, this, &AppController::OnProfileFileChanged);

    // Restore preferences BEFORE loading profiles. `loadProfiles` only sets
    // a default profile when `_currentProfile` is empty, so feeding the
    // saved value here preserves the user's choice across restarts.
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

    // Honour the user-customised store path (if any); otherwise fall back to
    // the platform default. Both paths run through `loadProfiles` so the file
    // watcher and profile model are wired identically.
    auto const initialStorePath = !_profileStorePath.isEmpty() ? std::filesystem::path(_profileStorePath.toStdString())
                                                               : Lightweight::Config::ProfileStore::DefaultPath();
    if (std::filesystem::exists(initialStorePath))
        (void) loadProfiles(QString::fromStdString(initialStorePath.string()));

    refreshOdbcDataSources();

    // Load the plugin DLLs eagerly so the migration list is populated before
    // the user clicks Connect. This runs after profile + settings restoration
    // because `EffectivePluginsDir()` consults both `_pluginsDir` and the
    // current profile. Safe to run here even if no connection is open —
    // plugin discovery is a pure filesystem operation.
    ReloadPlugins();
}

AppController::~AppController()
{
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
    // Profiles may carry their own `pluginsDir`; switch the loaded plugin
    // set so the UI matches the selection without a reconnect.
    ReloadPlugins();
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
    // Mode changes swap which plugins-dir is authoritative (profile's own
    // vs the user override), so reload to reflect the new effective set.
    ReloadPlugins();
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
    // Users expect dropping a new plugins-dir path to populate the migration
    // list immediately — no explicit reconnect button press.
    ReloadPlugins();
}

QString AppController::defaultProfileStorePath() const
{
    return QString::fromStdString(Lightweight::Config::ProfileStore::DefaultPath().string());
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

    // `loadProfiles({})` resolves to `DefaultPath()`; non-empty `path` is
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
    auto const effectivePath =
        path.isEmpty() ? QString::fromStdString(Lightweight::Config::ProfileStore::DefaultPath().string()) : path;

    auto result = Lightweight::Config::ProfileStore::LoadOrDefault(effectivePath.toStdString());
    if (!result)
    {
        ReportError(QString::fromStdString(result.error()));
        return false;
    }
    _store = std::move(*result);
    _profiles.ReplaceFrom(_store);
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
    ReloadPlugins();
    return true;
}

bool AppController::connectToProfile()
{
    ClearError();

    // Connection-source dispatch — the `connectionMode` property is the
    // single source of truth. This removes the old ambiguity where whichever
    // of "profile", "dsn" or "custom string" had content in a field was the
    // one that won on connect; now the user explicitly picks which mode.
    // Plugin-dir resolution lives in `EffectivePluginsDir()` so the same
    // rules apply on startup, property change, and connect.
    std::string connectionString;
    Lightweight::Config::Profile const* profile = _store.Find(_currentProfile.toStdString());

    if (_connectionMode == QStringLiteral("dsn"))
    {
        if (_selectedDsn.isEmpty())
        {
            ReportError(QStringLiteral("No ODBC DSN selected."));
            return false;
        }
        connectionString = QStringLiteral("DSN=%1").arg(_selectedDsn).toStdString();
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
        if (!profile->connectionString.empty())
        {
            connectionString = profile->connectionString;
        }
        else if (!profile->dsn.empty())
        {
            Lightweight::SqlConnectionDataSource const ds {
                .datasource = profile->dsn,
                .username = profile->uid,
                .password = {},
            };
            connectionString = ds.ToConnectionString().value;
        }
    }

    if (connectionString.empty())
    {
        ReportError(QStringLiteral("Profile '%1' has no connection string or DSN.").arg(_currentProfile));
        return false;
    }

    auto& manager = Lightweight::SqlMigration::MigrationManager::GetInstance();

    // Plugin discovery is a pure filesystem operation independent of ODBC,
    // so a failed connect must not hide which DLLs were picked up. Running
    // `ReloadPlugins` here also guarantees the user always sees the latest
    // set of DLLs even if they dropped new ones into the directory between
    // clicks — same watcher-less "just rescan on action" semantics as dbtool.
    ReloadPlugins();

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

    // `DataMapper::AcquireThreadLocal` caches its `DataMapper` in a
    // `thread_local` that is constructed exactly once per thread using
    // whichever connection string was default at the moment of first
    // access. Subsequent `SetDefaultConnectionString` calls do not reach
    // the cached instance, so switching profiles / DSNs appears to
    // succeed but keeps talking to the old database. We force a rebuild
    // here by move-assigning a fresh `DataMapper` built from the current
    // default, keeping the same storage address so `MigrationManager`'s
    // internal pointer (after `CloseDataMapper`) remains valid once it
    // re-acquires.
    //
    // The first call has already been exercised by the thread-local
    // lazy-init path — skipping the explicit rebuild on the first
    // connect avoids a double construction that could trip ODBC drivers
    // which hold per-connection locks.
    manager.CloseDataMapper();
    try
    {
        auto& threadDm = Lightweight::DataMapper::AcquireThreadLocal();
        if (_everConnected)
            threadDm = Lightweight::DataMapper { Lightweight::SqlConnection::DefaultConnectionString() };
    }
    catch (std::exception const& e)
    {
        ReportError(QStringLiteral("Could not open the database connection: %1").arg(ExceptionMessage(e)));
        return false;
    }
    _everConnected = true;

    // Opening migration history is best-effort. Failure here does NOT abort
    // the connect — the user may be pointing at an unmanaged database (no
    // `schema_migrations` table yet) and they need the GUI populated with
    // the plugin-declared migration plan so they can run them to bootstrap
    // the history table. Any hard failure surfaces as a yellow warning
    // banner; hard errors (connection refused, auth failure, etc.) have
    // already been caught by the `SetDefaultConnectionString` path above.
    bool historyOpen = true;
    try
    {
        manager.CreateMigrationHistory();
    }
    catch (std::exception const& e)
    {
        historyOpen = false;
        _lastWarning = QStringLiteral("Database is currently unmanaged — no schema_migrations table yet. "
                                      "Apply any pending migration to create it. (details: %1)")
                           .arg(ExceptionMessage(e));
        emit lastWarningChanged();
    }

    // `Refresh` reads `GetAppliedMigrationIds` which also touches
    // schema_migrations; fold the same exception path into a warning so
    // the list still shows the plugin-declared migrations even when the
    // table is missing.
    try
    {
        _migrations.Refresh(manager);
        _releases.Refresh(manager);
    }
    catch (std::exception const& e)
    {
        if (historyOpen)
        {
            _lastWarning = QStringLiteral("Could not read migration history: %1").arg(ExceptionMessage(e));
            emit lastWarningChanged();
        }
        // Fall back to a plain-plugin view with no applied rows. This is the
        // right mental model for an unmanaged DB: every plugin migration
        // shows as pending so the user can apply them.
        _migrations.RefreshPluginsOnly(manager);
        _releases.Refresh(manager);
    }
    emit migrationsChanged();

    if (profile && !profile->connectionString.empty())
        _backupRunner.setConnectionString(QString::fromStdString(profile->connectionString));
    else if (!_connectionStringOverride.isEmpty())
        _backupRunner.setConnectionString(_connectionStringOverride);

    _connected = true;
    emit connectedChanged();
    // Release labels and counter bindings depend on the applied-set we
    // just fetched. `Refresh` above already emitted `migrationsChanged`,
    // but that fired while `_connected` was still false — re-emit after
    // the flip so any binding that indirectly reads `_connected` (now or
    // later) gets the final state.
    emit migrationsChanged();
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
    auto const& manager = Lightweight::SqlMigration::MigrationManager::GetInstance();

    // "Current" = state the DB is actually in. We intentionally do NOT
    // gate on `_connected` here: `connectToProfile()` emits
    // `migrationsChanged` *before* flipping `_connected = true`, and we
    // want the label to already show the right version at that point —
    // the Simple view only shows the Status card when connected anyway,
    // so a pre-connect read from this accessor never reaches the user.
    //
    // We deliberately do NOT call `manager.GetAppliedMigrationIds()` here:
    // this accessor is bound from QML on the UI thread, and QML re-evaluates
    // the binding on every `migrationsChanged()` emission — including the
    // one fired between every migration during an `applyUpTo` run. The
    // migration run holds the only `SqlConnection` on a worker thread; an
    // ODBC handle is not safe for concurrent use, so a DB query from the
    // UI thread mid-run surfaces as HY000 "Connection is busy" and aborts
    // the run. Instead, read from `_migrations` — the in-memory model is
    // kept in sync per row by `migrationCompleted` →
    // `SetRowStatusByTimestamp`, so it knows which migrations are applied
    // at any point during a run without touching the database.
    auto const appliedRaw = _migrations.AppliedTimestamps();
    if (appliedRaw.empty())
        return QStringLiteral("(fresh database)");

    std::vector<Lightweight::SqlMigration::MigrationTimestamp> applied;
    applied.reserve(appliedRaw.size());
    for (auto const ts: appliedRaw)
        applied.push_back(Lightweight::SqlMigration::MigrationTimestamp { ts });

    auto const highest = *std::ranges::max_element(applied);
    auto const* release = manager.FindReleaseForTimestamp(highest);
    auto const& releases = manager.GetAllReleases();

    if (release)
    {
        // Count applied timestamps strictly above this release's upper
        // bound — those are "unreleased" tip migrations. Note: a release
        // with `highestTimestamp == highest` yields zero extras.
        int unreleased = 0;
        for (auto const& ts: applied)
            if (ts.value > release->highestTimestamp.value)
                ++unreleased;
        auto label = QString::fromStdString(release->version);
        if (unreleased > 0)
            label += QStringLiteral(" (+%1 unreleased)").arg(unreleased);
        return label;
    }

    // No release contains this timestamp. Two cases:
    //   (a) No releases declared at all  -> show raw timestamp.
    //   (b) Applied ts is past the last  -> "unreleased" beyond latest.
    if (releases.empty())
        return QString::number(highest.value);

    auto const& last = releases.back();
    int unreleased = 0;
    for (auto const& ts: applied)
        if (ts.value > last.highestTimestamp.value)
            ++unreleased;
    return QStringLiteral("%1 (+%2 unreleased)").arg(QString::fromStdString(last.version)).arg(unreleased);
}

QString AppController::targetReleaseLabel() const
{
    auto const& manager = Lightweight::SqlMigration::MigrationManager::GetInstance();
    auto const& releases = manager.GetAllReleases();

    // Total = every migration the plugin set knows about. Releases cover
    // a prefix of that set; anything past the last release's highest
    // timestamp is "unreleased" and will still be applied on "Run".
    auto const& allMigrations = manager.GetAllMigrations();
    if (releases.empty())
    {
        // No release declared — best we can offer is "latest" meaning
        // "apply every pending migration".
        return allMigrations.empty() ? QStringLiteral("(no migrations)") : QStringLiteral("latest");
    }

    auto const& last = releases.back();
    int unreleased = 0;
    for (auto const* m: allMigrations)
        if (m->GetTimestamp().value > last.highestTimestamp.value)
            ++unreleased;

    auto label = QString::fromStdString(last.version);
    if (unreleased > 0)
        label += QStringLiteral(" (+%1 unreleased)").arg(unreleased);
    return label;
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
    auto const& manager = Lightweight::SqlMigration::MigrationManager::GetInstance();
    auto const* release = manager.FindReleaseByVersion(version.toStdString());
    if (!release)
        return {};
    return QString::number(release->highestTimestamp.value);
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
    _odbcDataSources.Replace(Lightweight::Odbc::EnumerateDataSources());
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
    emit connectedChanged();
}

void AppController::ReloadPlugins()
{
    auto const pluginsDirs = EffectivePluginsDir();

    auto& manager = Lightweight::SqlMigration::MigrationManager::GetInstance();
    // Wipe first, so switching to a profile with no plugins-dir clears
    // the stale list instead of leaving the previous one showing.
    manager.RemoveAllMigrations();
    manager.RemoveAllReleases();
    _plugins->entries.clear();

    if (!pluginsDirs.empty())
    {
        try
        {
            _plugins->entries = Lightweight::Tools::IngestPlugins(
                pluginsDirs, Lightweight::Tools::DefaultPluginLoader(), manager, MakeGuiIngestOptions(_verbose));
        }
        catch (std::exception const& e)
        {
            // `throwOnLoadError = false` in GUI options means we usually
            // do not reach this branch — only `IngestPlugins`'s own
            // contract violations (or directory-iteration errors) bubble
            // up. Surface them on the error banner anyway so the user
            // sees something went wrong.
            QStringList paths;
            paths.reserve(static_cast<qsizetype>(pluginsDirs.size()));
            for (auto const& dir: pluginsDirs)
                paths.append(QString::fromStdString(dir.string()));
            ReportError(QStringLiteral("Failed to load plugins from %1: %2")
                            .arg(paths.join(QStringLiteral("; ")), QString::fromUtf8(e.what())));
        }
    }

    // When a connection is live, overlay applied status from schema_migrations;
    // otherwise show every plugin migration as "unknown" — we have not (yet)
    // queried the target DB, so "pending" would be a guess. On any failure
    // (e.g. schema_migrations missing on a live connection), fall back to
    // the plugins-only "pending" view: the schema_migrations lookup succeeded
    // in returning "nothing is applied", so "pending" is an observation, not
    // a guess.
    try
    {
        if (_connected)
            _migrations.Refresh(manager);
        else
            _migrations.RefreshAsUnknown(manager);
    }
    catch (std::exception const&)
    {
        _migrations.RefreshPluginsOnly(manager);
    }
    _releases.Refresh(manager);
    emit migrationsChanged();
}

} // namespace DbtoolGui
