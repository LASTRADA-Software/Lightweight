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
#include "ManagedBackupController.hpp"
#include "MigrationRunner.hpp"
#include "Models/LogLineModel.hpp"
#include "Models/MigrationListModel.hpp"
#include "Models/OdbcDataSourceListModel.hpp"
#include "Models/ProfileListModel.hpp"
#include "Models/ReleaseListModel.hpp"
#include "SerialWorker.hpp"
#include "SqlQueryRunner.hpp"

#include <Config/ConfigDiscovery.hpp>
#include <Config/ProfileStore.hpp>
#include <QtCore/QByteArray>
#include <QtCore/QList>
#include <QtCore/QObject>
#include <QtCore/QPair>
#include <QtCore/QSettings>
#include <QtCore/QString>
#include <QtCore/QTimer>
#include <QtCore/QUrl>
#include <QtCore/QVariantMap>
#include <QtQml/QQmlEngine>
#include <QtQuick/QQuickWindow>

// `_plugins` below is a pimpl pointer (defined in AppController.cpp) so
// this moc-parsed header avoids pulling in <PluginIngestion.hpp>. The
// reason is concrete: moc 6.11's scanner mis-counts braces around the
// C++17 `namespace A::B { ... }` syntax used by tools_shared headers,
// which leaks subsequent declarations (e.g. `DbtoolGui`) into the
// outer namespace and breaks the generated meta-object.

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace DbtoolGui
{

struct BackupRequirementState;
struct ConnectOutcome;
struct ConnectRequest;
struct HistoryRefreshOutcome;
struct MigrationSnapshot;
struct PluginReloadOutcome;

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
    Q_PROPERTY(DbtoolGui::ManagedBackupController* managedBackups READ managedBackups CONSTANT)
    /// The log pane's lines, bounded to `LogLineModel::Capacity`.
    Q_PROPERTY(DbtoolGui::LogLineModel* logLines READ logLines CONSTANT)
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

    /// What the open connection is: the profile name, the DSN name, or — for a custom
    /// connection string — the database it reached. Empty while not connected. Unlike
    /// `currentProfile` (the *selected* profile, which a DSN or custom connection leaves
    /// untouched) this always names the connection that is actually open, so labels that
    /// say "connected to …" cannot name a stale profile.
    Q_PROPERTY(QString connectedTarget READ connectedTarget NOTIFY connectedChanged)

    /// How the open connection was made: "profile" | "dsn" | "custom"; empty while not connected.
    Q_PROPERTY(QString connectedMode READ connectedMode NOTIFY connectedChanged)

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
    /// Custom path to the dbtool.yml profile store. Empty means "discover it":
    /// the nearest dbtool.yml above the working or executable directory, else
    /// the per-user default (`Config::FindConfigFile`). Persisted
    /// under `config/profileStorePath` so site installs that ship the YAML
    /// outside `%APPDATA%` / `$XDG_CONFIG_HOME` can point the GUI at it
    /// without sym-links.
    Q_PROPERTY(QString profileStorePath READ profileStorePath WRITE setProfileStorePath NOTIFY profileStorePathChanged)
    /// The `dbtool.yml` that discovery picks when no custom path is set, exposed
    /// so the QML Settings page can render it as the placeholder. CONSTANT —
    /// discovery inputs (working and executable directory) are fixed at startup.
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
    /// Pre-migrate steps the plugins registered for the connected database; they run with the next apply.
    Q_PROPERTY(int preMigrateStepCount READ preMigrateStepCount NOTIFY migrationsChanged)
    /// True while a release or pre-migrate step of the pending migrations requires a backup that does
    /// not exist yet. The migrate buttons are disabled meanwhile; dry runs stay available.
    Q_PROPERTY(bool backupRequired READ backupRequired NOTIFY backupRequirementChanged)
    /// What requires the backup while `backupRequired` is true, e.g. `release 3.0.0` or `step "Widen text columns"`.
    Q_PROPERTY(QStringList backupRequirementReasons READ backupRequirementReasons NOTIFY backupRequirementChanged)
    /// Why the newest backup does not count, why the last backup could not be recorded, or why the
    /// requirement could not be checked; empty when there is nothing to add.
    Q_PROPERTY(QString backupRequirementNote READ backupRequirementNote NOTIFY backupRequirementChanged)
    /// Whether the user ticked "Migrate without a backup": the next apply run goes ahead although a
    /// required backup is missing and records the waiver. False at start, never persisted, and reset
    /// whenever a migration run finishes, on every connect and disconnect, and as soon as no backup
    /// is required any more (e.g. after Back up now), so it never carries over to a later requirement.
    Q_PROPERTY(
        bool migrateWithoutBackup READ migrateWithoutBackup WRITE setMigrateWithoutBackup NOTIFY migrateWithoutBackupChanged)
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

    /// True while the plugins load on the worker: from construction until the first snapshot is
    /// applied, and again for every reload until its snapshot arrives.
    Q_PROPERTY(bool pluginsLoading READ pluginsLoading NOTIFY activityChanged)
    /// True while the ODBC data sources are enumerated on a worker.
    Q_PROPERTY(bool odbcRefreshing READ odbcRefreshing NOTIFY activityChanged)
    /// True while any controller job is queued or running; controls that need its result stay disabled.
    Q_PROPERTY(bool busy READ busy NOTIFY activityChanged)
    /// True while a migration, rollback or pre-migrate run, a backup or restore, or a managed backup or
    /// restore is in flight; the main window refuses to close meanwhile.
    Q_PROPERTY(bool closeBlocked READ closeBlocked NOTIFY closeBlockedChanged)
    /// Why the window refuses to close, naming the run in flight; empty while it closes.
    Q_PROPERTY(QString closeBlockedReason READ closeBlockedReason NOTIFY closeBlockedChanged)
    /// True while the run that blocks closing can be cancelled (a migration run not yet cancelling).
    Q_PROPERTY(bool closeBlockingRunCancellable READ closeBlockingRunCancellable NOTIFY closeBlockedChanged)
    /// What the running job does, e.g. "Loading plugins…"; empty while idle.
    Q_PROPERTY(QString activityText READ activityText NOTIFY activityChanged)
    /// True from an accepted `connectToProfile` until its job, the backup-requirement check included, is done.
    Q_PROPERTY(bool connecting READ connecting NOTIFY activityChanged)
    /// True while the backup requirement is evaluated on the worker; `backupRequired` is not final meanwhile.
    Q_PROPERTY(bool backupRequirementPending READ backupRequirementPending NOTIFY activityChanged)
    /// True from an accepted `applyAfterBackup` until its backup finished and the apply was started or dropped.
    Q_PROPERTY(bool chainedBackupPending READ chainedBackupPending NOTIFY chainedBackupPendingChanged)

  public:
    /// How a change of the profile file on disk was handled.
    enum class ProfileFileReaction : int
    {
        /// The profiles were reloaded from the file.
        Reloaded,
        /// The file holds what the controller last loaded or wrote (an encrypted password, say), so the
        /// store is current already; a repeated notification of one save ends here too.
        IgnoredOwnWrite,
        /// A connect, reload or run was in flight; the profiles reload once the controller is idle.
        Deferred,
    };
    Q_ENUM(ProfileFileReaction)

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
    /// @return The log pane's lines.
    [[nodiscard]] LogLineModel* logLines() noexcept
    {
        return &_logLines;
    }
    [[nodiscard]] MigrationRunner* runner() noexcept
    {
        return &_runner;
    }
    [[nodiscard]] BackupRunner* backupRunner() noexcept
    {
        return &_backupRunner;
    }
    /// The managed-backup-folder controller (one archive per profile, backed
    /// by `ManagedBackupController`). Profiles and the busy probe are wired
    /// up in the constructor / `loadProfiles`; QML binds to this directly.
    [[nodiscard]] ManagedBackupController* managedBackups() noexcept
    {
        return &_managedBackups;
    }
    /// Seeds the initial value of `_verbose` before the QML engine
    /// instantiates the singleton. Must be called before any QML access to
    /// `AppController`. Threads the parsed `--verbose`/`-v` flag into the controller's first
    /// plugin load. Idempotent; later changes take effect on the next
    /// plugin load only if the value is updated via `SetVerbose`.
    ///
    /// Calling `SeedVerbose(false)` is equivalent to not calling
    /// `SeedVerbose` at all — `_verbose` defaults to `false`. The `bool`
    /// argument exists so callers can forward a parsed CLI flag directly
    /// (`SeedVerbose(parser.isSet(verboseOpt))`) without an explicit `if`.
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
    [[nodiscard]] QString const& connectedTarget() const noexcept
    {
        return _connectedTarget;
    }
    [[nodiscard]] QString const& connectedMode() const noexcept
    {
        return _connectedMode;
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
    /// @return The number of pre-migrate steps the plugins registered for the connected database;
    ///         they run with the next apply.
    [[nodiscard]] int preMigrateStepCount() const;

    /// @return Whether a required backup is missing for the pending migrations (see the property).
    [[nodiscard]] bool backupRequired() const noexcept
    {
        return _backupRequired;
    }

    /// @return What requires the missing backup; empty while none is missing.
    [[nodiscard]] QStringList const& backupRequirementReasons() const noexcept
    {
        return _backupRequirementReasons;
    }

    /// @return The extra line of the backup requirement alert; empty when there is nothing to add.
    [[nodiscard]] QString const& backupRequirementNote() const noexcept
    {
        return _backupRequirementNote;
    }

    /// @return Whether the next apply run may go ahead without a required backup.
    [[nodiscard]] bool migrateWithoutBackup() const noexcept
    {
        return _migrateWithoutBackup;
    }

    /// Ticks or clears "Migrate without a backup" for the next apply run; never persisted.
    /// @param value Whether the next apply run may go ahead without a required backup.
    Q_INVOKABLE void setMigrateWithoutBackup(bool value);

    /// Backs up the connected database into the backup folder under a generated
    /// `<target>-<timestamp>.zip` name, without a dialog. The backup is recorded, so it satisfies
    /// the backup requirement once it finished. Does nothing unless connected and every runner is idle.
    Q_INVOKABLE void backUpNow();

    /// Backs up the connected database and, once the backup succeeded, applies migrations: the
    /// timestamps @p selectedTimestamps when there are any, else everything up to @p targetTimestamp.
    /// A backup that fails or cannot start applies nothing, logs an error and emits `chainedBackupFailed`.
    /// Does nothing, beyond a log line, unless connected and every runner is idle.
    /// @param archivePath The archive to write; empty writes a generated `<target>-<timestamp>.zip`
    ///        into the backup folder, as `backUpNow` does.
    /// @param targetTimestamp The apply target as for `MigrationRunner::applyUpTo`; empty applies everything pending.
    /// @param selectedTimestamps The migrations to apply as for `MigrationRunner::applySelected`.
    Q_INVOKABLE void applyAfterBackup(QString const& archivePath,
                                      QString const& targetTimestamp,
                                      QStringList const& selectedTimestamps);

    /// @return Whether a backup started by `applyAfterBackup` is in flight.
    [[nodiscard]] bool chainedBackupPending() const noexcept
    {
        return _chainedBackupPending;
    }

    /// Sets whether the apply, apply-selected and rollback runs take the migration lock.
    /// @param mode `"skip"` runs them without the lock; anything else, `"acquire"` included, takes it.
    Q_INVOKABLE void setMigrationLocking(QString const& mode);
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

    /// @return Whether the plugins are loading (see the property).
    [[nodiscard]] bool pluginsLoading() const noexcept;

    /// @return Whether the ODBC data sources are being enumerated.
    [[nodiscard]] bool odbcRefreshing() const noexcept
    {
        return _odbcRefreshing;
    }

    /// @return Whether any controller job is queued or running.
    [[nodiscard]] bool busy() const noexcept;

    /// @return Whether a run is in flight, so the window must not close.
    [[nodiscard]] bool closeBlocked() const;

    /// @return Why the window must not close; empty while it may.
    [[nodiscard]] QString closeBlockedReason() const;

    /// @return Whether the run that blocks closing can be cancelled.
    [[nodiscard]] bool closeBlockingRunCancellable() const;

    /// Asks the run that blocks closing to stop, when it can be cancelled.
    Q_INVOKABLE void cancelBlockingRun();

    /// @return What the running job does; empty while idle.
    [[nodiscard]] QString activityText() const;

    /// @return Whether a connect job is in flight (see the property).
    [[nodiscard]] bool connecting() const noexcept
    {
        return _connecting;
    }

    /// @return Whether the backup requirement is being evaluated (see the property).
    [[nodiscard]] bool backupRequirementPending() const noexcept;

    /// Assembles the multi-line diagnostic bundle surfaced on the Simple
    /// view's Failure card (env + connection summary + current/target
    /// versions + failed migration + last ~200 log lines). QML calls this
    /// to feed the Copy / Save buttons. Contains no passwords: only the
    /// redacted `connectionSummary` is included, never the raw
    /// `connectionStringOverride` or `dsnPassword`.
    Q_INVOKABLE QString buildFailureReport() const;

    /// Writes `buildFailureReport()` to the local file @p file on the worker, replacing it; the outcome
    /// arrives as `failureReportSaved`.
    /// @param file The destination, as a file dialog returns it.
    Q_INVOKABLE void saveFailureReport(QUrl const& file);

    /// @param url A URL a file dialog returned.
    /// @return Its local path in the platform's separators (UNC paths included); empty when it is no local file.
    Q_INVOKABLE QString localFilePath(QUrl const& url) const;

    /// Timestamps of the rows the user has ticked in the migration list.
    /// Exposed so the ActionsPanel's primary button can hand them straight
    /// to `MigrationRunner::applySelected` / `dryRunSelected`.
    Q_INVOKABLE QStringList selectedMigrationTimestamps() const;

    /// Toggles the selected state of a row by timestamp, in O(1). No-op for
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

    /// Builds, on the worker, the SQL statements applying the migration @p timestamp would run on the
    /// open connection, and emits `migrationSqlPreviewReady` with them. While not connected, during a
    /// migration run, and for a timestamp that is no number, the reply (a placeholder line, or nothing) is
    /// emitted at once. When the target changed before the statements arrive, they are replaced by a line
    /// saying so.
    /// @param timestamp The migration's timestamp as a decimal string.
    Q_INVOKABLE void requestMigrationSqlPreview(QString const& timestamp);

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

    /// Connects through the current connection mode: the selected profile, DSN or connection string.
    /// Validates the input here and starts one worker job that reloads the plugins, opens the connection,
    /// runs the post-init hooks, reads the history and evaluates the backup requirement; `connecting` is
    /// true until `connectFinished`. A result whose target changed meanwhile is dropped.
    /// Refused with a warning while a migration run, a backup or a restore is active, and refused while
    /// a connect is in flight.
    /// @return Whether a connect job started.
    Q_INVOKABLE bool connectToProfile();

    /// Re-enumerates the system ODBC DSNs on a worker and refreshes the `odbcDataSources` model when
    /// done; `odbcRefreshing` is true meanwhile. A call during a refresh runs one more afterwards.
    Q_INVOKABLE void refreshOdbcDataSources();

    /// Opens the loaded profile file with the system's default handler
    /// (e.g. Notepad on Windows, the user's editor on macOS/Linux). Does
    /// nothing if no profile file has been loaded yet.
    Q_INVOKABLE void openProfileFileExternally();

    /// Describes the open connection so it can be saved as a profile ("Save as profile…").
    /// Keys: `available` (true only for a DSN or custom-string connection that is open — a
    /// profile connection is already saved), `kind` ("dsn" | "connectionString"), `dsn`,
    /// `connectionString` (with any inline password removed), `user`, `schema`, `hasPassword`
    /// (whether the connection used one — the password itself never reaches QML),
    /// `suggestedName` (one not used in the store yet) and `targetFile` (the dbtool.yml it
    /// would be written to).
    Q_INVOKABLE QVariantMap connectionDraft() const;

    /// Why `name` cannot be used for a new profile.
    /// @param name The name as typed.
    /// @return A user-displayable reason, or an empty string when the name is fine.
    Q_INVOKABLE QString profileNameProblem(QString const& name) const;

    /// Adds a profile to the profile file (the one in use, else the default location) with its
    /// password encrypted, then reloads the profile list. Neither the open connection nor the
    /// selected profile changes. Refused while a migration, backup or restore is running, since
    /// the reload would reset the status those runs report into.
    /// @param fields `name`, `kind` ("dsn" | "connectionString"), `dsn`, `connectionString`,
    ///        `user`, `schema`, `password` (plaintext; may be empty), `useOpenConnectionPassword`
    ///        (take the open connection's password when `password` is empty), `makeDefault`.
    /// @return An empty string on success, otherwise a user-displayable error.
    Q_INVOKABLE QString addProfile(QVariantMap const& fields);

    /// Opens the folder that contains `path` in the system file browser.
    /// Used by the "Show in folder" action of a finished backup or restore;
    /// does nothing for an empty path.
    /// @param path A file (typically the archive) whose folder is opened.
    Q_INVOKABLE void revealInFolder(QString const& path);

    /// Called from `LogPanel.qml`'s `Component.onCompleted` to flush any
    /// startup/plugin/connection log messages produced *before* the QML
    /// engine wired up its `Connections { target: AppController }` block.
    /// Idempotent — repeated calls after the first one are no-ops, so
    /// re-instantiating the panel (e.g. after a tab swap) doesn't replay
    /// the buffered banner.
    Q_INVOKABLE void attachLogSink();

    /// Holds the first plugin load back until @p window has shown its first frame, so the window
    /// appears before any plugin library is mapped. Called by `Main.qml` when it is created; a
    /// controller nobody calls this on (a test without a window) loads right after its startup.
    /// Gives up waiting after two seconds, for a window that never renders (e.g. minimized).
    /// @param window The main window.
    Q_INVOKABLE void awaitFirstFrame(QQuickWindow* window);

    /// Stops starting jobs, hides the windows, asks a migration run and an ad-hoc query to cancel, writes
    /// the settings, and waits for the jobs in flight (a plugin load, a connect, a run, a backup or
    /// restore, a query, an ODBC enumeration, a backup-folder scan) to finish. Runs on
    /// `QCoreApplication::aboutToQuit`, so quitting never tears down the process while a job still uses
    /// the plugins or the manager, and again from the destructor. Idempotent.
    /// While a migration, backup or restore is in flight it waits for as long as that takes, since ending
    /// it midway would leave the database or the archive half-written; otherwise the wait is bounded
    /// (see `SetShutdownDrainLimit`), as a read job may block in a driver call that ignores cancellation.
    void Shutdown();

    /// The longest `Shutdown` waits for the jobs in flight by default when no run is in flight.
    static constexpr std::chrono::milliseconds DefaultShutdownDrainLimit = std::chrono::seconds { 30 };

    /// Bounds the wait of `Shutdown` while no migration, backup or restore is in flight. When a job still
    /// runs after @p limit, @p onExceeded runs and `Shutdown` returns. Without a handler the process then
    /// ends at once, with `TerminateProcess` on Windows and `std::quick_exit` elsewhere: neither runs the
    /// static or DLL destructors a still-running job could be using.
    /// A caller passing a handler must keep this controller alive until the jobs have finished, for
    /// instance by waiting on `runner()->Worker()`: the jobs use its members.
    /// @param limit How long `Shutdown` waits for the jobs in flight.
    /// @param onExceeded Runs when a job still runs after @p limit; empty restores the default.
    void SetShutdownDrainLimit(std::chrono::milliseconds limit, std::function<void()> onExceeded);

    /// Size and SHA-256 of a profile file's content.
    struct ProfileFileFingerprint
    {
        /// The file.
        QString path;
        /// Its size in bytes.
        qint64 size = 0;
        /// The SHA-256 of its content.
        QByteArray sha256;

        bool operator==(ProfileFileFingerprint const&) const = default;
    };

    /// @return The fingerprint of the file @p path; empty when it cannot be read.
    [[nodiscard]] static std::optional<ProfileFileFingerprint> FingerprintOf(QString const& path);

    /// A profile store and the fingerprint of the very bytes it was parsed from.
    struct LoadedProfileStore
    {
        /// The parsed store; empty when the file does not exist.
        Lightweight::Config::ProfileStore store;
        /// Empty when the file does not exist (the store is then empty).
        std::optional<ProfileFileFingerprint> fingerprint;
    };

    /// Reads the profile file @p path once, then parses and fingerprints those bytes, so an edit made
    /// in between can never be taken for content already loaded. A missing file is an empty store.
    /// @param requestedPath The profile file; empty means `ProfileStore::DefaultPath()`, as for `LoadOrDefault`.
    /// @return The store and its fingerprint, or the read or parse error.
    [[nodiscard]] static std::expected<LoadedProfileStore, std::string> LoadProfileStore(QString const& requestedPath);

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
    void backupRequirementChanged();
    void migrateWithoutBackupChanged();
    /// `chainedBackupPending` changed.
    void chainedBackupPendingChanged();
    /// A `saveFailureReport` finished.
    /// @param path The local path written, or the URL's text when it is no local file.
    /// @param error Empty on success, otherwise why the report could not be written.
    void failureReportSaved(QString path, QString error);
    /// The backup `applyAfterBackup` started failed or could not start, so no migration ran.
    /// @param summary Why, e.g. the backup's own summary.
    void chainedBackupFailed(QString summary);
    /// The backup succeeded but the migrations after it could not start, so none ran.
    /// @param summary Why.
    void chainedApplyFailed(QString summary);
    void selectionChanged();
    void logVisibleChanged();
    void viewModeChanged();
    /// `pluginsLoading`, `odbcRefreshing`, `connecting`, `backupRequirementPending`, `busy` or
    /// `activityText` changed.
    void activityChanged();
    /// `closeBlocked`, `closeBlockedReason` or `closeBlockingRunCancellable` changed.
    void closeBlockedChanged();
    /// A quit request (QML `Qt.quit()`) was refused because a run is in flight; nothing closed.
    void quitRefused();
    void error(QString message);
    /// A connect ended: its job finished, its backup-requirement check included, or the target it was
    /// connecting to changed. `connected` tells whether a connection is open.
    void connectFinished();
    /// The reply to `requestMigrationSqlPreview`.
    /// @param timestamp The timestamp the preview was asked for.
    /// @param statements The statements; empty for an unknown migration; one comment line when there is
    ///        nothing to preview (not connected, a run in flight, the connection changed meanwhile).
    void migrationSqlPreviewReady(QString timestamp, QStringList statements);
    /// A change of the profile file on disk was handled.
    /// @param reaction What was done about it.
    void profileFileChangeHandled(DbtoolGui::AppController::ProfileFileReaction reaction);

  private slots:
    /// File-system watcher callback — fires when the profile YAML changes on disk. Changes are
    /// debounced into one `HandleProfileFileChange`.
    void OnProfileFileChanged(QString const& path);

  private:
    /// Handles the debounced change of the watched profile file: drops a change of a file that is no
    /// longer the profile file, ignores content the controller loaded or wrote last, defers while a
    /// connect, reload or run is in flight, and reloads the profiles otherwise.
    void HandleProfileFileChange();

    /// Reloads the profiles for a change deferred earlier, once nothing is in flight any more.
    void ApplyDeferredProfileFileChange();

    /// True while the profile store must not be swapped: a connect, reload, check or run is in flight.
    [[nodiscard]] bool ProfileReloadBlocked() const;

    void ReportError(QString const& message);

    /// Second half of the constructor, queued so the window shows first: loads the discovered profile
    /// store unless one is loaded already, logs the banner, and starts the ODBC refresh and the plugin load.
    void BeginStartup();

    /// Emits `activityChanged` when any of the activity properties changed.
    void UpdateActivity();

    /// Puts a snapshot's rows into the models and keeps its derived figures; emits `migrationsChanged`.
    void ApplySnapshot(MigrationSnapshot snapshot);

    /// Applies the outcome of a plugin reload job: its log always, its snapshot only while its
    /// generation is current; then checks the backup requirement once no reload is left.
    void ApplyPluginReload(PluginReloadOutcome outcome);

    /// Applies the connection part of a connect job: its log always; while its generation is current,
    /// its snapshot, and on success the connected state and the encryption of a plaintext password.
    /// @param request The request the job ran.
    /// @param outcome What it produced.
    /// @param plaintextProfile The profile whose plaintext password the connect used; encrypted on success.
    void ApplyConnect(ConnectRequest const& request,
                      ConnectOutcome outcome,
                      std::optional<Lightweight::Config::Profile> const& plaintextProfile);

    /// Ends a connect job: applies its backup requirement while current, clears `connecting`, and
    /// emits `connectFinished`.
    /// @param requirement The requirement the job evaluated; empty when it did not connect.
    /// @param generation The generation of the connect.
    void FinishConnect(std::optional<BackupRequirementState> requirement, std::uint64_t generation);

    /// Evaluates the backup requirement on the worker while connected, and clears it while not.
    /// Leaves it alone while a migration run is in flight.
    void RequestBackupRequirementCheck();

    /// Shows @p state as the backup requirement; resets the waiver when nothing is required.
    void ApplyBackupRequirement(BackupRequirementState state);

    /// Applies a current history re-read: its snapshot and, while connected, its requirement.
    void ApplyHistoryRefresh(HistoryRefreshOutcome outcome);

    /// Makes every job result for the target the UI showed until now stale: ends a connect in flight at
    /// once (its job drops out at its next check) and stops counting the checks and re-reads in flight.
    void BumpTargetGeneration();

    /// An input of the connection mode @p mode changed (a DSN, its credentials or schema, a connection
    /// string or its schema). While that mode is active and a connect is in flight, that connect targets
    /// the old input, so it ends and its result becomes stale.
    /// @param mode "dsn" or "custom".
    void TargetInputChanged(QString const& mode);

    /// Stores @p value under @p key in the settings; they are written to disk shortly after, in one go.
    void PersistSetting(char const* key, QVariant const& value);

    /// Waits up to @p limit for every worker to finish its jobs.
    /// @return Whether none still runs.
    [[nodiscard]] bool DrainWorkers(std::chrono::milliseconds limit);

    /// True while a migration, backup or restore is in flight.
    [[nodiscard]] bool AnyRunInFlight() const;

    /// The dbtool.yml a new profile is written to: the loaded file, else the one chosen in
    /// Settings, else the discovered or per-user default.
    [[nodiscard]] QString ProfileTargetPath() const;

    /// Plaintext password of the open DSN or custom connection; empty when it has none.
    [[nodiscard]] std::string OpenConnectionPassword() const;
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

    /// The generated `<target>-<timestamp>.zip` archive path in the backup folder; touches no disk.
    /// @return The archive path, or why there is none, e.g. "no backup folder is configured; choose one in Settings."
    [[nodiscard]] std::expected<QString, QString> GeneratedBackupArchive() const;

    /// Receives the archive path once its folder exists, or why it could not be created.
    using BackupFolderReady = std::function<void(std::expected<QString, QString>)>;

    /// Creates the folder of @p archivePath on the worker and calls @p ready on the GUI thread with the
    /// archive path, or the failure; also a failure when the connection changed meanwhile.
    void PrepareBackupFolder(QString const& archivePath, BackupFolderReady ready);

    /// Ends the apply `applyAfterBackup` put off without running it: logs @p reason and emits `chainedApplyFailed`.
    void FailChainedApply(QString const& reason);

    /// Ends a pending `applyAfterBackup` without applying: logs @p reason and emits `chainedBackupFailed`.
    void FailChainedBackup(QString const& reason);

    /// Clears `chainedBackupPending`, emitting its change signal when it was set.
    void EndChainedBackup();

    /// Starts the apply `applyAfterBackup` put off, once its backup finished successfully.
    void ApplyChainedRun();

    /// Encrypts the plaintext password of `profile` in the loaded dbtool.yml after a
    /// successful connection, logging the outcome; never fails the connect. The store is re-read in
    /// place, and the file watcher's echo of the write is ignored.
    void EncryptPlaintextPassword(Lightweight::Config::Profile const& profile);

    /// Computes the effective plugin search directories for the current
    /// connection mode: the user override wins in `dsn` / `custom` modes,
    /// whereas `profile` mode is locked to the profile's own `pluginsDir`.
    /// May return more than one path when `defaultPluginsDir` in the YAML
    /// store is a list. Returns an empty vector when nothing is configured.
    [[nodiscard]] std::vector<std::filesystem::path> EffectivePluginsDir() const;

    /// Asks for the plugins to be reloaded from `EffectivePluginsDir()` on the worker, so the
    /// migration list follows the plugins directory, profile and mode without a connect. Every
    /// request made before control returns to the event loop becomes one job (`FlushPluginReload`).
    /// While a migration run is active it only marks the reload as pending; the run's `finished`
    /// handler requests it again.
    void RequestPluginReload();

    /// Starts the one reload job the requests since the last flush asked for.
    void FlushPluginReload();

    /// Starts the first plugin load, once: after `BeginStartup`, and after the first frame when
    /// `awaitFirstFrame` asked for one.
    void StartFirstPluginLoad();

    /// The main window showed its first frame, or waiting for it gave up.
    void OnFirstFrame();

    /// Drops the "connected" flag (emitting `connectedChanged`) without
    /// tearing down the underlying `SqlConnection` — those are reclaimed
    /// lazily by the pool. Call this whenever the property that identifies
    /// the *target database* changes (profile switch, connection-mode
    /// swap) so the next plugin reload goes through the "unknown" path
    /// instead of re-reading whatever applied-set the prior profile's DB
    /// exposed.
    void InvalidateConnectionTarget();

    /// Re-reads the models, and while connected the backup requirement, in one worker job after a run
    /// that may have changed the database's applied-set (a finished migration run, or a managed restore
    /// into the connected profile). An unreadable history falls back to a plugin-only view and surfaces
    /// the reason as a warning.
    void RefreshMigrationModelsAfterRun();

    /// Opaque bundle of `Lightweight::Tools::LoadedPlugin` keeping every
    /// loaded shared library alive. Defined in `AppController.cpp` so the
    /// header doesn't pull in `<PluginIngestion.hpp>` (see the comment at
    /// the top of this file for the moc-parser reason). Declared before
    /// `_runner`, so the runner's worker has finished before the plugins unload.
    struct PluginsBundle;
    std::unique_ptr<PluginsBundle> _plugins;

    ProfileListModel _profiles;
    OdbcDataSourceListModel _odbcDataSources;
    LogLineModel _logLines;
    MigrationListModel _migrations;
    ReleaseListModel _releases;
    MigrationRunner _runner;
    BackupRunner _backupRunner;
    ManagedBackupController _managedBackups;
    SqlQueryRunner _sqlQueryRunner;

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
    /// User-customised `dbtool.yml` location; empty means "use the discovered
    /// dbtool.yml" (see `Config::FindConfigFile`).
    QString _profileStorePath;
    /// True once a connect routed the migration manager's log sink into this controller; the
    /// destructor removes it again so it cannot outlive us.
    bool _installedMigrationLogSink = false;
    /// True when a plugin reload was put off during a migration run or a connect and must run once it finishes.
    bool _pluginReloadPending = false;
    /// True from construction until `BeginStartup` ran.
    bool _startupPending = true;
    /// True while a `FlushPluginReload` is queued.
    bool _pluginReloadFlushQueued = false;
    /// Plugin reload jobs whose outcome has not been applied yet.
    int _pluginReloadJobsInFlight = 0;
    /// The directories of the last plugin reload; empty before the first.
    std::optional<std::vector<std::filesystem::path>> _requestedPluginDirectories;
    /// Identifies the target the UI shows: bumped by every profile, mode, plugins-directory or
    /// connection change. A job outcome of an older generation is stale and dropped.
    std::atomic<std::uint64_t> _targetGeneration { 0 };
    /// True once a profile store was loaded; the startup then leaves it alone.
    bool _storeLoaded = false;
    /// True once the startup's "Ready" line was logged.
    bool _readyLogged = false;
    /// See the `odbcRefreshing` property.
    bool _odbcRefreshing = false;
    /// True when `refreshOdbcDataSources` was called during a refresh; one more runs afterwards.
    bool _odbcRefreshAgain = false;
    /// True once `BeginStartup` ran.
    bool _startupBegun = false;
    /// True from `awaitFirstFrame` until the first frame (or the fallback) arrived.
    bool _awaitingFirstFrame = false;
    /// True once `Shutdown` ran; no job starts any more.
    bool _shuttingDown = false;
    /// See the `connecting` property.
    bool _connecting = false;
    /// True while the connect job in flight evaluates the backup requirement of its new connection.
    bool _connectRequirementPending = false;
    /// Backup-requirement checks queued or running on the worker, the after-run re-reads included.
    int _backupRequirementChecksInFlight = 0;
    /// History re-reads queued or running on the worker.
    int _historyRefreshesInFlight = 0;
    /// The activity `UpdateActivity` last announced: loading, ODBC, connecting, requirement, history.
    std::array<bool, 5> _announcedActivity { true, false, false, false, false };
    /// How long `Shutdown` waits for the jobs in flight.
    std::chrono::milliseconds _shutdownDrainLimit = DefaultShutdownDrainLimit;
    /// Runs when the jobs are still running after `_shutdownDrainLimit`.
    std::function<void()> _onShutdownDrainExceeded;
    /// The profile store discovery picked when the controller was created, and how it was found.
    QString _discoveredStorePath;
    QString _discoveredStoreSource;
    /// The pre-migrate step count of the last applied snapshot.
    int _preMigrateStepCount = 0;
    /// The target release label of the last applied snapshot.
    QString _targetReleaseLabel;
    QString _profilePath;
    QString _lastError;
    QString _lastWarning;
    bool _connected = false;
    /// See the `backupRequired` property.
    bool _backupRequired = false;
    /// See the `backupRequirementReasons` property.
    QStringList _backupRequirementReasons;
    /// See the `backupRequirementNote` property.
    QString _backupRequirementNote;
    /// Why the last Back up or Restore could not be recorded; empty when it was.
    QString _lastRecordError;
    /// See the `migrateWithoutBackup` property.
    bool _migrateWithoutBackup = false;
    /// See the `chainedBackupPending` property.
    bool _chainedBackupPending = false;
    /// The apply target `applyAfterBackup` runs once its backup succeeded.
    QString _chainedApplyTarget;
    /// The migrations `applyAfterBackup` applies instead of the target when not empty.
    QStringList _chainedApplySelection;
    /// The target generation `applyAfterBackup` started under; a later one applies nothing.
    std::uint64_t _chainedApplyGeneration = 0;
    /// True while a backup folder is created on the worker, so no second backup starts meanwhile.
    bool _backupFolderPreparing = false;
    QString _connectedTarget;
    QString _connectedMode;
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
    /// Collects the watcher's notifications of one save into one `HandleProfileFileChange`.
    QTimer* _profileFileDebounce = nullptr;
    /// The watched file the last notification named.
    QString _changedProfileFile;
    /// The profile file as this controller last loaded or wrote it; a change to that content is ignored.
    std::optional<ProfileFileFingerprint> _knownFingerprint;
    /// The profile file whose change waits for the controller to be idle; empty when none does.
    QString _deferredProfileFile;
    /// True while an `ApplyDeferredProfileFileChange` is queued.
    bool _deferredProfileFileQueued = false;

    /// How long the counters may lag a run's row status changes.
    static constexpr std::chrono::milliseconds RowStatusNotifyInterval { 100 };
    /// Emits `migrationsChanged` once for the row status changes of the last `RowStatusNotifyInterval`.
    QTimer* _migrationsChangedThrottle = nullptr;

    /// The application's settings, written to disk by `_settingsSync` rather than once per setter.
    QSettings* _settings = nullptr;
    /// Writes `_settings` shortly after the last change; `Shutdown` writes what is left.
    QTimer* _settingsSync = nullptr;

    /// Enumerates the ODBC data sources; declared last, so it has finished before any member goes.
    SerialWorker _odbcWorker;
    /// Does the file I/O that may stall on a network share, so it never queues behind database jobs;
    /// declared last for the same reason as `_odbcWorker`.
    SerialWorker _fileWorker;
};

} // namespace DbtoolGui
