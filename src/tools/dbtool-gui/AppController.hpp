// SPDX-License-Identifier: Apache-2.0
//
// `AppController` is the QML-facing singleton that holds every piece of
// long-lived state the UI binds against: the loaded profile store, the
// current profile selection, the ODBC data-source list, the migration /
// release models, the migration runner, and the backup runner.
//
// Registered as a QML singleton via `QML_ELEMENT + QML_SINGLETON` so QML can
// reach it without context-property boilerplate.

#pragma once

#include "BackupRunner.hpp"
#include "LogLevel.hpp"
#include "MigrationRunner.hpp"
#include "Models/MigrationListModel.hpp"
#include "Models/OdbcDataSourceListModel.hpp"
#include "Models/ProfileListModel.hpp"
#include "Models/ReleaseListModel.hpp"
#include "SqlQueryRunner.hpp"

#include <Config/ProfileStore.hpp>
#include <QtCore/QList>
#include <QtCore/QObject>
#include <QtCore/QPair>
#include <QtCore/QString>
#include <QtQml/QQmlEngine>

// `_plugins` below is a pimpl pointer (defined in AppController.cpp) so
// this moc-parsed header avoids pulling in <PluginIngestion.hpp>. The
// reason is concrete: moc 6.11's scanner mis-counts braces around the
// C++17 `namespace A::B { ... }` syntax used by tools_shared headers,
// which leaks subsequent declarations (e.g. `DbtoolGui`) into the
// outer namespace and breaks the generated meta-object.

#include <filesystem>
#include <memory>
#include <vector>

namespace DbtoolGui
{

class AppController: public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(ProfileListModel* profiles READ profiles CONSTANT)
    Q_PROPERTY(OdbcDataSourceListModel* odbcDataSources READ odbcDataSources CONSTANT)
    Q_PROPERTY(MigrationListModel* migrations READ migrations CONSTANT)
    Q_PROPERTY(ReleaseListModel* releases READ releases CONSTANT)
    Q_PROPERTY(MigrationRunner* runner READ runner CONSTANT)
    Q_PROPERTY(BackupRunner* backupRunner READ backupRunner CONSTANT)
    /// Whether the experimental backup/restore UI surfaces are enabled.
    /// Driven by the `--enable-backup-restore` CLI flag in `main.cpp`;
    /// false by default. CONSTANT because the value is fixed at startup —
    /// toggling at runtime is not supported.
    Q_PROPERTY(bool backupRestoreEnabled READ backupRestoreEnabled CONSTANT)
    /// Ad-hoc SQL execution helper exposed to the Expert view's SQL Query
    /// tab. Stays construction-empty until the user issues a query — picks
    /// up the global default connection string set by `connectToProfile`.
    Q_PROPERTY(SqlQueryRunner* sqlQueryRunner READ sqlQueryRunner CONSTANT)
    Q_PROPERTY(QString currentProfile READ currentProfile WRITE setCurrentProfile NOTIFY currentProfileChanged)
    Q_PROPERTY(QString connectionStringOverride READ connectionStringOverride NOTIFY connectionStringOverrideChanged)
    /// Optional default schema for the custom-connection-string tab. Mirrors
    /// the per-profile `Profile.schema` field so users connecting via raw
    /// ODBC string (no profile) can still pin the migration tool to a
    /// non-default schema (e.g. `lasa`). Empty means "rely on the server's
    /// own default schema" (login-level `DEFAULT_SCHEMA` on SQL Server).
    Q_PROPERTY(QString customSchema READ customSchema WRITE setCustomSchema NOTIFY customSchemaChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    /// Non-fatal message surfaced after a successful-but-degraded connect.
    /// The canonical example: connecting to a database that has not been
    /// migration-managed before — `schema_migrations` is missing, but we
    /// still want the UI to show the plugin-declared migration plan so the
    /// user can run them to bootstrap the history table.
    Q_PROPERTY(QString lastWarning READ lastWarning NOTIFY lastWarningChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)

    // Connection mode ("profile" | "dsn" | "custom") drives which input is
    // read by `connectToProfile`. A single source-of-truth for the UI so
    // there is never ambiguity about "am I connecting via the profile
    // dropdown or via the raw connection string?".
    Q_PROPERTY(QString connectionMode READ connectionMode WRITE setConnectionMode NOTIFY connectionModeChanged)
    Q_PROPERTY(QString selectedDsn READ selectedDsn WRITE setSelectedDsn NOTIFY selectedDsnChanged)
    /// Optional username override for DSN mode. Appended to the connection
    /// string as `UID=<value>` so we can drive DSNs whose saved credentials
    /// are incomplete — common with "ODBC Driver 18 for SQL Server" where
    /// the admin dialog's Test button prompts for creds interactively but
    /// never persists them into the DSN itself.
    Q_PROPERTY(QString dsnUser READ dsnUser WRITE setDsnUser NOTIFY dsnUserChanged)
    /// Optional password override for DSN mode. Persisted in QSettings —
    /// same plaintext-in-HKCU trade-off as `connectionStringOverride`,
    /// which already accepts full connection strings containing secrets.
    /// Leave empty to rely on whatever the DSN has saved (or Windows auth).
    Q_PROPERTY(QString dsnPassword READ dsnPassword WRITE setDsnPassword NOTIFY dsnPasswordChanged)
    /// Optional default-schema override for DSN mode. Threaded into
    /// `MigrationManager::SetDefaultSchema` so DSN-mode connections that
    /// target a non-default schema (e.g. `lasa` on SQL Server / PostgreSQL)
    /// land migrations and `schema_migrations` there. Persisted in
    /// QSettings under `connection/dsnSchema`. Empty means "use the
    /// server-side default schema".
    Q_PROPERTY(QString dsnSchema READ dsnSchema WRITE setDsnSchema NOTIFY dsnSchemaChanged)
    Q_PROPERTY(QString pluginsDir READ pluginsDir WRITE setPluginsDir NOTIFY pluginsDirChanged)
    /// Custom path to the dbtool.yml profile store. Empty means "use the
    /// platform default returned by `ProfileStore::DefaultPath()`". Persisted
    /// under `config/profileStorePath` so site installs that ship the YAML
    /// outside `%APPDATA%` / `$XDG_CONFIG_HOME` can point the GUI at it
    /// without sym-links.
    Q_PROPERTY(QString profileStorePath READ profileStorePath WRITE setProfileStorePath NOTIFY profileStorePathChanged)
    /// Platform default for the `dbtool.yml` location, exposed so the QML
    /// Settings page can render it as the placeholder when the user hasn't
    /// picked a custom path. CONSTANT — the platform default doesn't change
    /// over the life of the process.
    Q_PROPERTY(QString defaultProfileStorePath READ defaultProfileStorePath CONSTANT)
    Q_PROPERTY(QString connectionSummary READ connectionSummary NOTIFY connectionSummaryChanged)
    /// Absolute path of the profile file currently driving the profile
    /// dropdown, or empty if no file has been loaded. Exposed so the
    /// ConnectionPanel can both label the source and open it on click.
    Q_PROPERTY(QString profilePath READ profilePath NOTIFY profilePathChanged)
    /// Display-friendly variant of `profilePath` — collapses the user's
    /// home directory prefix to `~`. UI-only; never pass this to file APIs.
    Q_PROPERTY(QString profilePathDisplay READ profilePathDisplay NOTIFY profilePathChanged)

    // Status counters — exposed as notify-enabled properties so the sidebar
    // cards and filter-tab badges re-evaluate automatically when the model
    // is refreshed. Calling `model.rowCount()` directly from QML bindings
    // does NOT notify on model resets, so we mirror the useful counts here.
    Q_PROPERTY(int migrationCount READ migrationCount NOTIFY migrationsChanged)
    Q_PROPERTY(int appliedCount READ appliedCount NOTIFY migrationsChanged)
    Q_PROPERTY(int pendingCount READ pendingCount NOTIFY migrationsChanged)
    /// Subset of `pendingCount`: pending migrations that sit *past* the
    /// highest declared release (i.e. their `releaseVersion` is empty).
    /// Surfaced separately in the Simple view so a user whose DB is at the
    /// latest tagged release still sees that newer, untagged migrations are
    /// piling up — otherwise current and target labels both read the same
    /// release and the asymmetry only shows in a small `(+N unreleased)`
    /// suffix that's easy to miss.
    Q_PROPERTY(int pendingUnreleasedCount READ pendingUnreleasedCount NOTIFY migrationsChanged)
    Q_PROPERTY(int unknownCount READ unknownCount NOTIFY migrationsChanged)
    Q_PROPERTY(int checksumMismatchCount READ checksumMismatchCount NOTIFY migrationsChanged)
    Q_PROPERTY(int issuesCount READ issuesCount NOTIFY migrationsChanged)
    Q_PROPERTY(int selectionCount READ selectionCount NOTIFY selectionChanged)

    /// Human-readable label for the release that the DB is currently at.
    /// Resolved from the highest applied timestamp via
    /// `MigrationManager::FindReleaseForTimestamp`. Returns
    /// `"(fresh database)"` when nothing is applied. Appends
    /// ` (+N unreleased)` when applied timestamps sit past the highest
    /// declared release. Backs the Simple view's "Current version" label.
    Q_PROPERTY(QString currentReleaseLabel READ currentReleaseLabel NOTIFY migrationsChanged)
    /// Human-readable label for the release the DB would be at after
    /// applying every pending migration. Returns the highest declared
    /// release version, or `"latest"` when no releases are declared.
    /// Appends ` (+N unreleased)` when tip migrations live above it.
    Q_PROPERTY(QString targetReleaseLabel READ targetReleaseLabel NOTIFY migrationsChanged)

    /// Active UI mode: `"simple"` (default for new users) or `"expert"`.
    /// Persisted in QSettings under `ui/viewMode`. QML `StackLayout` /
    /// `TabBar` bindings read + write this directly.
    Q_PROPERTY(QString viewMode READ viewMode WRITE setViewMode NOTIFY viewModeChanged)

    /// Whether the bottom log panel is visible. Persisted so the user's
    /// "I don't want to see that right now" choice survives restarts.
    Q_PROPERTY(bool logVisible READ logVisible WRITE setLogVisible NOTIFY logVisibleChanged)

  public:
    explicit AppController(QObject* parent = nullptr);
    ~AppController() override;

    static AppController* create(QQmlEngine* engine, QJSEngine* scriptEngine);

    [[nodiscard]] ProfileListModel* profiles() noexcept
    {
        return &_profiles;
    }
    [[nodiscard]] OdbcDataSourceListModel* odbcDataSources() noexcept
    {
        return &_odbcDataSources;
    }
    [[nodiscard]] MigrationListModel* migrations() noexcept
    {
        return &_migrations;
    }
    [[nodiscard]] ReleaseListModel* releases() noexcept
    {
        return &_releases;
    }
    [[nodiscard]] MigrationRunner* runner() noexcept
    {
        return &_runner;
    }
    [[nodiscard]] BackupRunner* backupRunner() noexcept
    {
        return &_backupRunner;
    }
    [[nodiscard]] bool backupRestoreEnabled() const noexcept
    {
        return _backupRestoreEnabled;
    }

    /// Seeds the initial value of `backupRestoreEnabled` before the QML
    /// engine instantiates the singleton. Must be called before any QML
    /// access to `AppController`. Mirrors `ThemeController::SeedInitialMode`.
    /// @param enabled Whether backup/restore UI surfaces should be enabled.
    static void SeedBackupRestoreEnabled(bool enabled) noexcept;

    /// Seeds the initial value of `_verbose` before the QML engine
    /// instantiates the singleton. Must be called before any QML access to
    /// `AppController`. Mirrors `SeedBackupRestoreEnabled` so `main.cpp` can
    /// thread the parsed `--verbose`/`-v` flag into the controller's first
    /// `ReloadPlugins()`. Idempotent; later changes take effect on the next
    /// `ReloadPlugins()` only if the value is updated via `SetVerbose`.
    ///
    /// Calling `SeedVerbose(false)` is equivalent to not calling
    /// `SeedVerbose` at all — `_verbose` defaults to `false`. The `bool`
    /// argument exists so callers can forward a parsed CLI flag directly
    /// (`SeedVerbose(parser.isSet(verboseOpt))`) without an explicit `if`,
    /// matching the adjacent `SeedBackupRestoreEnabled` call site.
    ///
    /// @param enabled Whether shadow-plugin notices should be emitted via
    ///                `qInfo()`.
    static void SeedVerbose(bool enabled) noexcept;
    [[nodiscard]] SqlQueryRunner* sqlQueryRunner() noexcept
    {
        return &_sqlQueryRunner;
    }

    [[nodiscard]] QString const& currentProfile() const noexcept
    {
        return _currentProfile;
    }
    [[nodiscard]] QString const& connectionStringOverride() const noexcept
    {
        return _connectionStringOverride;
    }
    [[nodiscard]] QString const& customSchema() const noexcept
    {
        return _customSchema;
    }
    [[nodiscard]] QString const& lastError() const noexcept
    {
        return _lastError;
    }
    [[nodiscard]] QString const& lastWarning() const noexcept
    {
        return _lastWarning;
    }
    [[nodiscard]] bool connected() const noexcept
    {
        return _connected;
    }
    [[nodiscard]] QString const& connectionMode() const noexcept
    {
        return _connectionMode;
    }
    [[nodiscard]] QString const& selectedDsn() const noexcept
    {
        return _selectedDsn;
    }
    [[nodiscard]] QString const& dsnUser() const noexcept
    {
        return _dsnUser;
    }
    [[nodiscard]] QString const& dsnPassword() const noexcept
    {
        return _dsnPassword;
    }
    [[nodiscard]] QString const& dsnSchema() const noexcept
    {
        return _dsnSchema;
    }
    [[nodiscard]] QString const& pluginsDir() const noexcept
    {
        return _pluginsDir;
    }
    [[nodiscard]] QString const& profileStorePath() const noexcept
    {
        return _profileStorePath;
    }
    [[nodiscard]] QString defaultProfileStorePath() const;
    [[nodiscard]] QString connectionSummary() const;
    [[nodiscard]] QString const& profilePath() const noexcept
    {
        return _profilePath;
    }
    [[nodiscard]] QString profilePathDisplay() const;

    [[nodiscard]] int migrationCount() const;
    [[nodiscard]] int appliedCount() const;
    [[nodiscard]] int pendingCount() const;
    [[nodiscard]] int pendingUnreleasedCount() const;
    [[nodiscard]] int unknownCount() const;
    [[nodiscard]] int checksumMismatchCount() const;
    [[nodiscard]] int issuesCount() const;
    [[nodiscard]] int selectionCount() const;

    [[nodiscard]] QString currentReleaseLabel() const;
    [[nodiscard]] QString targetReleaseLabel() const;

    [[nodiscard]] QString const& viewMode() const noexcept
    {
        return _viewMode;
    }
    Q_INVOKABLE void setViewMode(QString const& mode);

    [[nodiscard]] bool logVisible() const noexcept
    {
        return _logVisible;
    }
    Q_INVOKABLE void setLogVisible(bool visible);

    /// Assembles the multi-line diagnostic bundle surfaced on the Simple
    /// view's Failure card (env + connection summary + current/target
    /// versions + failed migration + last ~200 log lines). QML calls this
    /// to feed the Copy / Save buttons. Contains no passwords: only the
    /// redacted `connectionSummary` is included, never the raw
    /// `connectionStringOverride` or `dsnPassword`.
    Q_INVOKABLE QString buildFailureReport() const;

    /// Timestamps of the rows the user has ticked in the migration list.
    /// Exposed so the ActionsPanel's primary button can hand them straight
    /// to `MigrationRunner::applySelected` / `dryRunSelected`.
    Q_INVOKABLE QStringList selectedMigrationTimestamps() const;

    /// Toggles the selected state of a row by timestamp. No-op for
    /// already-applied / unknown rows (see `MigrationListModel::setData`).
    Q_INVOKABLE void setMigrationSelected(QString const& timestamp, bool selected);

    /// Bulk select / deselect all pending rows — wired to the "Select all /
    /// Deselect all" toolbar above the migration list.
    Q_INVOKABLE void selectAllPending(bool selected);

    /// Resolves a release version string to the release's `highestTimestamp`
    /// as a decimal string. Returns empty if `version` is not declared.
    /// Exposed to QML so the ActionsPanel can build an applyUpTo() target
    /// without duplicating the lookup in JavaScript.
    Q_INVOKABLE QString releaseHighestTimestamp(QString const& version) const;

    /// Builds the list of SQL statements that `PreviewMigration` would emit
    /// for the migration with the given timestamp. Returns an empty list if
    /// the timestamp is unknown or parsing fails. The preview is dialect-
    /// aware: it uses whatever connection is currently active, so the rendered
    /// SQL matches what an actual apply on that database would run.
    Q_INVOKABLE QStringList previewMigrationSql(QString const& timestamp) const;

    void setCurrentProfile(QString const& name);

    /// Overrides the connection string used when `connectionMode == "custom"`.
    Q_INVOKABLE void setConnectionStringOverride(QString const& connectionString);

    /// Sets the default-schema override applied to migration connections in
    /// `custom` mode. Persisted in QSettings. The plumbing into the
    /// migration runner happens in `connectToProfile()` via
    /// `MigrationManager::SetDefaultSchema`. Empty disables.
    Q_INVOKABLE void setCustomSchema(QString const& schema);

    /// Switches the connection source. Valid values: `"profile"`,
    /// `"dsn"`, `"custom"`. Any other value is coerced to `"profile"`.
    Q_INVOKABLE void setConnectionMode(QString const& mode);

    /// Sets the DSN to use when `connectionMode == "dsn"`. Bound from the
    /// ODBC Data Source dropdown in the connection panel.
    Q_INVOKABLE void setSelectedDsn(QString const& dsn);

    /// Sets the optional DSN-mode username. Persisted in QSettings.
    Q_INVOKABLE void setDsnUser(QString const& user);

    /// Sets the optional DSN-mode password. Persisted in QSettings.
    Q_INVOKABLE void setDsnPassword(QString const& password);

    /// Sets the optional DSN-mode default schema. Persisted in QSettings.
    /// Plumbed into `MigrationManager::SetDefaultSchema` from
    /// `connectToProfile()` when `connectionMode == "dsn"`.
    Q_INVOKABLE void setDsnSchema(QString const& schema);

    /// Global plugins directory configured from the Settings page. Used as
    /// the fallback for any connection — a profile's own non-empty
    /// `pluginsDir` still overrides this when it is selected. Persisted in
    /// QSettings under `config/pluginsDir`.
    Q_INVOKABLE void setPluginsDir(QString const& pluginsDir);

    /// Sets the custom `dbtool.yml` path. Empty resets to the platform
    /// default. Triggers an immediate `loadProfiles()` unless a migration
    /// run is in flight, in which case the reload is deferred and the user
    /// is told via `lastWarning`.
    Q_INVOKABLE void setProfileStorePath(QString const& path);

    /// Loads a ProfileStore from disk and repopulates the profiles model.
    /// `path` may be empty to pick the default location.
    Q_INVOKABLE bool loadProfiles(QString const& path = {});

    /// Connects to the currently-selected profile (or to the override
    /// connection string if no profile is selected). Loads migration plugins
    /// from the profile's `pluginsDir`, creates the `schema_migrations`
    /// table if it is missing, and refreshes the migration / release models.
    Q_INVOKABLE bool connectToProfile();

    /// Re-enumerates the system ODBC DSNs and refreshes the
    /// `odbcDataSources` model. Safe to call repeatedly; synchronous.
    Q_INVOKABLE void refreshOdbcDataSources();

    /// Opens the loaded profile file with the system's default handler
    /// (e.g. Notepad on Windows, the user's editor on macOS/Linux). Does
    /// nothing if no profile file has been loaded yet.
    Q_INVOKABLE void openProfileFileExternally();

    /// Called from `LogPanel.qml`'s `Component.onCompleted` to flush any
    /// startup/plugin/connection log messages produced *before* the QML
    /// engine wired up its `Connections { target: AppController }` block.
    /// Idempotent — repeated calls after the first one are no-ops, so
    /// re-instantiating the panel (e.g. after a tab swap) doesn't replay
    /// the buffered banner.
    Q_INVOKABLE void attachLogSink();

  signals:
    /// Unified log-pane feed: every startup banner, plugin discovery event,
    /// connection-state change, and migration progress line emerges here.
    /// `MigrationRunner::logLine` and `BackupRunner::logLine` are forwarded
    /// into this signal so QML only has to listen to one source.
    void logLine(QString line, DbtoolGui::LogLevel level);

    void currentProfileChanged();
    void connectedChanged();
    void connectionStringOverrideChanged();
    void customSchemaChanged();
    void connectionModeChanged();
    void selectedDsnChanged();
    void dsnUserChanged();
    void dsnPasswordChanged();
    void dsnSchemaChanged();
    void pluginsDirChanged();
    void profileStorePathChanged();
    void connectionSummaryChanged();
    void profilePathChanged();
    void lastErrorChanged();
    void lastWarningChanged();
    void migrationsChanged();
    void selectionChanged();
    void logVisibleChanged();
    void viewModeChanged();
    void error(QString message);

  private slots:
    /// File-system watcher callback — fires when the profile YAML changes
    /// on disk. We reload only when a run is idle; otherwise we defer to
    /// the next user-triggered reconnect to avoid swapping migrations out
    /// from under an in-flight operation.
    void OnProfileFileChanged(QString const& path);

  private:
    void ReportError(QString const& message);
    void ClearError();

    /// Routes `line` at severity `level` into the unified `logLine` signal.
    /// When the QML panel has not yet called `attachLogSink()` the message
    /// is buffered and replayed when the panel comes online — important
    /// because plugin discovery runs in the constructor, before any QML
    /// receiver exists.
    void Log(LogLevel level, QString line);
    void LogInfo(QString line);
    void LogWarn(QString line);
    void LogError(QString line);

    /// Computes the effective plugin search directories for the current
    /// connection mode: the user override wins in `dsn` / `custom` modes,
    /// whereas `profile` mode is locked to the profile's own `pluginsDir`.
    /// May return more than one path when `defaultPluginsDir` in the YAML
    /// store is a list. Returns an empty vector when nothing is configured.
    [[nodiscard]] std::vector<std::filesystem::path> EffectivePluginsDir() const;

    /// Rebuilds the plugin list from `EffectivePluginsDir()` and refreshes
    /// the migration / release models. Triggered at startup and whenever a
    /// property that influences the effective directory (plugins override,
    /// current profile, connection mode) changes, so the migration list is
    /// always visible as soon as a directory is known — without waiting for
    /// a successful DB connect.
    void ReloadPlugins();

    /// Drops the "connected" flag (emitting `connectedChanged`) without
    /// tearing down the underlying `SqlConnection` — those are reclaimed
    /// lazily by the pool. Call this whenever the property that identifies
    /// the *target database* changes (profile switch, connection-mode
    /// swap) so the next `ReloadPlugins()` goes through the "unknown"
    /// path instead of re-running `Refresh` against whatever applied-set
    /// the prior profile's DB exposed.
    void InvalidateConnectionTarget();

    ProfileListModel _profiles;
    OdbcDataSourceListModel _odbcDataSources;
    MigrationListModel _migrations;
    ReleaseListModel _releases;
    MigrationRunner _runner;
    BackupRunner _backupRunner;
    SqlQueryRunner _sqlQueryRunner;

    /// Gates the experimental backup/restore UI surfaces. Seeded from
    /// `SeedBackupRestoreEnabled` at startup, then never changes.
    bool _backupRestoreEnabled;

    Lightweight::Config::ProfileStore _store;
    QString _currentProfile;
    QString _connectionStringOverride;
    /// Per-tab schema override for `custom` connection mode. Mirrors the
    /// per-profile `Profile.schema` so the direct-ODBC tab reaches the same
    /// downstream wiring (`MigrationManager::SetDefaultSchema`).
    QString _customSchema;
    QString _connectionMode = QStringLiteral("profile");
    QString _selectedDsn;
    QString _dsnUser;
    QString _dsnPassword;
    /// Optional default-schema override active when `connectionMode == "dsn"`.
    /// Plumbed into `MigrationManager::SetDefaultSchema` in `connectToProfile`.
    QString _dsnSchema;
    QString _pluginsDir;
    /// User-customised `dbtool.yml` location; empty means "use the platform
    /// default from `ProfileStore::DefaultPath()`".
    QString _profileStorePath;
    QString _profilePath;
    QString _lastError;
    QString _lastWarning;
    bool _connected = false;
    bool _logVisible = true;
    /// Set by `SetVerbose()` from `main.cpp` when the user passes
    /// `--verbose`/`-v`. Currently only gates the shadowed-plugin notices
    /// in `LoadPluginsInto`; never persisted across runs.
    bool _verbose = false;
    /// Active UI mode — default is `"simple"` for new installs so non-expert
    /// downstream users are not greeted by the full timeline on first launch.
    QString _viewMode = QStringLiteral("simple");

    /// Ring buffer of the last N log lines emitted by the runners. Used by
    /// `buildFailureReport` to include real execution context in the
    /// copy/save bundle without requiring the user to scroll + select the
    /// log panel manually.
    QStringList _recentLog;
    /// Cap for `_recentLog`. Low enough to keep memory trivial, high enough
    /// to capture a realistic failure's trailing output (one migration's
    /// worth of DDL, the failing SQL, and the driver error).
    static constexpr int kRecentLogCapacity = 200;

    /// Identity of the last migration that failed during a run. Cleared on
    /// the start of a new run and on a successful run's completion. Backs
    /// the "Failed migration: TS  title" line in the failure report.
    QString _lastFailedTimestamp;
    QString _lastFailedTitle;
    /// `true` once the first successful connect has happened in this
    /// process. Used to distinguish "lazy-init the thread-local
    /// DataMapper" (first connect) from "rebuild the cached instance
    /// because the connection string just changed" (every subsequent
    /// connect).
    bool _everConnected = false;

    /// Log messages produced before the QML `LogPanel` connected its
    /// receiver (i.e. before `attachLogSink()` ran). Replayed in order on
    /// first attach and then permanently bypassed.
    QList<QPair<QString, LogLevel>> _pendingLogs;
    /// `true` once `attachLogSink()` has run for the first time. Past that
    /// point `Log()` emits directly without touching `_pendingLogs`.
    bool _logSinkAttached = false;

    /// Forward-declared by the Qt pointer in the .cpp — a QFileSystemWatcher
    /// held by raw pointer to keep `<QtCore/QFileSystemWatcher>` out of this
    /// header. The instance is parented to `this` so Qt reclaims it.
    class QFileSystemWatcher* _fileWatcher = nullptr;

    // Plugins must outlive every MigrationBase pointer the MigrationManager
    // holds — unloading a plugin invalidates its migrations. Keep them for
    // the lifetime of the controller.
    /// Opaque bundle of `Lightweight::Tools::LoadedPlugin` keeping every
    /// loaded shared library alive. Defined in `AppController.cpp` so the
    /// header doesn't pull in `<PluginIngestion.hpp>` (see the comment at
    /// the top of this file for the moc-parser reason).
    struct PluginsBundle;
    std::unique_ptr<PluginsBundle> _plugins;
};

} // namespace DbtoolGui
