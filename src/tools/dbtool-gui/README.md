# dbtool-gui

Qt 6 graphical companion to the `dbtool` CLI. Runs the same migration, ad-hoc
SQL query, and backup/restore workflows declared via the Lightweight
library's `LIGHTWEIGHT_SQL_RELEASE` macro and `dbtool` profile system.

This subdirectory is **opt-in**. It only builds when both of the following are
true:

- `LIGHTWEIGHT_BUILD_GUI=ON` is passed at configure time, and
- A Qt 6 (>= 6.5) install is found by `cmake/LightweightFindQt.cmake` — either via an
  explicit `Qt6_DIR` / `CMAKE_PREFIX_PATH`, or by auto-probing the standard
  install locations (`C:/Qt/6.x/<compiler>_64/`, Homebrew `qt@6`, `~/Qt/`).

If Qt cannot be located, the option is silently downgraded to `OFF` and the
rest of the project (library, dbtool, tests, examples) builds normally.

## Status

**Landed:** scaffolding, shared ProfileStore / DataSourceEnumerator
/ SecretResolver in the library, Qt view-model layer (AppController,
MigrationRunner, BackupRunner, ManagedBackupController, six QAbstractListModels,
QmlProgressManager),
a QML UI, and managed backups (see below). Deferred work is listed under
*What's deferred*.

### Look and feel

The UI follows the **Lastrada UI** design system (the "Lastrada UI" Claude
Design project, pages under `dbtool/`):

- **Shell** — a dark navigation rail (`NavRail.qml`) with *Migrations* and
  *Backups*, *Settings* pinned at the bottom (in both Simple and Expert view, so a
  stale plugins directory or backup folder can be fixed without switching mode). Every page
  opens with a 52 px kit header (`PageHeader.qml`: breadcrumb, title, status
  chips, actions) and the window ends in a 26 px status bar (`StatusBar.qml`).
  The rail starts collapsed in the Simple view and expanded in the Expert view;
  the choice is remembered per view.
- **Simple view** — a guided three-step flow: *Connection* → *Review what will
  change* (current/resulting release and the exact pending list) → *Run*, with
  running and done states that show per-migration progress and the pre-run
  backup.
- **Tokens** — `qml/Theme.qml` carries the design tokens under their
  `_tokens.css` names (`clrPrimary`, `clrContainerLow`, `sp3`, `r2`, `ctlMd`, …).
  The palette is light-only: the rail and the code surfaces (log, SQL editor)
  are the only dark areas. `main.cpp` pins the light colour scheme and seeds the
  Fusion palette from the same tokens so stock controls match.
- **Kit components** — `LsButton` (primary / secondary / ghost / danger), `Card`
  (kit panel with header), `Banner`, `SegmentedControl`, `StepSection`,
  `StatusPill`, `Spinner`, and Lastrada line icons in `Glyph`.

### Responsiveness

The UI thread never waits for the database, the plugins or the disk: no stall over
**1 s** (`UiWatchdog::DefaultBudgetMs`, enforced by the workflow tests). The window's first frame
comes before any plugin load, ODBC enumeration or backup-folder scan. The profile file
(`dbtool.yml`) is read and written on the UI thread, so the guarantee assumes it lies on a local disk.

| Work | Where it runs |
|------|---------------|
| Plugin load and reload (once per real change of plugins directory, profile, mode or connect) | worker |
| Connect, post-init hooks, history refresh, checksum verification, backup-requirement check | worker, one job |
| Migration, pre-migrate step, backup and restore runs | worker (`MigrationRunner`, `BackupRunner`, `ManagedBackupController`) |
| SQL preview, SQL query, ODBC data-source enumeration, backup-folder scan | worker |
| Failure report save, backup-folder creation | file worker, never queued behind a database job |
| Result delivery | queued calls to the UI thread; a result of an earlier profile, connection or reload is dropped |

While a result is pending, the controls that depend on it stay disabled and show the wait:

| Property (`AppController`) | Shown as |
|----------------------------|----------|
| `pluginsLoading` | "Loading plugins…" in the migration view |
| `odbcRefreshing` | busy ODBC list |
| `connecting` | "Connecting…" on the connect button; a second click starts nothing |
| `backupRequirementPending` | *Run*, *Retry* and *Apply* disabled until the check finished |

Also:

- **No echo reload.** The GUI's own password-encryption write to `dbtool.yml` does not reload the
  profiles again; an external change during a run or a connect is deferred, not lost.
- **Cheap updates.** List and count updates cost O(changed rows); a run of thousands of migrations
  does not rebuild the lists. The log is bounded.
- **Close is refused while a run is in flight** (window close, `Ctrl+Q`, `Qt.quit()`); the log says
  why. *Cancel* ends the run, then the window closes.
- **Rollback takes the migration lock**, like Apply (see below); the log says when another migrator
  holds it.

### Expert view options

| Control | Effect |
|---------|--------|
| *Acquire lock* | Apply, Apply selected and Rollback to release take the migration lock when on (default) and run without it when off. Dry run never locks. The Simple view always locks. The backup requirement is checked either way. |
| *Back up first* | Apply backs up the database, then migrates only if the backup succeeded; a failed backup stops the run and is shown. |
| *Dry run* | Shows the SQL and changes nothing. |

### Plugin interface

Plugins export `LightweightMigrationPluginPostInitReported` for the post-init hook (see
[docs/dbtool.md](../../../docs/dbtool.md#optional-post-init-hook)). A plugin that exports only the
old `LightweightMigrationPluginPostInit` is skipped with a warning: rebuild it against the current
`LIGHTWEIGHT_MIGRATION_PLUGIN_POSTINIT` macro. A throwing hook is logged and the connect completes.

### Managed backups

The Backups page manages one `<profile>.zip` archive per configured profile
inside a single folder, instead of a one-off file picker:

- **Folder** — configured on the Settings page (persisted as `backup/folder`
  in `QSettings`). When none is chosen there, the default is the `defaultBackupDir`
  of the loaded `dbtool.yml` if it names one (so an installer can pre-configure it,
  shared with `dbtool backup`), else `<AppDataLocation>/backups`. A per-profile
  `backupDir` is a CLI feature: this page keeps every profile's archive in one folder.
  The folder is validated as writable (created if missing) before any run starts,
  and the current problem, if any, is surfaced inline.
- **Backup all** — backs up every configured profile sequentially into
  `<folder>/<sanitized-profile-name>.zip`, continuing past per-profile
  failures; a single profile can also be backed up on its own from its row.
- **Safe overwrite** — each archive is written to a `.tmp` sibling first, verified, and
  atomically renamed over the final path on success, so a crash, a failure or an archive
  that fails verification never replaces the previous good archive.
- **Partial runs are failures** — `SqlBackup::Backup` does not throw when an
  individual table fails; it reports `Progress::State::Error` and returns.
  The controller therefore gates the atomic rename on the progress manager's
  `ErrorCount()` (the same signal the `dbtool` CLI uses for its exit code): a
  run that lost tables discards its `.tmp`, keeps the previous archive, marks
  the profile `failed`, and reports the whole run as a failure.
- **Restore** — an existing archive can be restored back into the profile it
  came from, into any *other* configured profile, or into an ad-hoc ODBC
  connection string typed in directly (with an optional schema override) —
  useful for restoring into a scratch database without adding a profile.
  Every restore entry point is confirmed through a dialog first.
  A restore is **not** transactional: tables are dropped and recreated one at
  a time. A run that reports table-level errors is therefore surfaced as a
  failure that explicitly warns the target database may be incomplete and must
  not be used before a successful re-run.
- **Mutual busy guard** — the migration runner, the ad-hoc backup runner and
  the managed-backup controller all refuse to start a mutating operation while
  either of the others is running, in C++ and in the QML `enabled` bindings.
  They share the same databases, and a migration (or an ad-hoc restore) running
  during a managed backup would produce a torn archive.
- **Status** — the page lists, per profile, whether an archive exists plus
  its size/mtime, and the live state of any in-flight run (queued / running /
  ok / failed, with the error text on failure).
- **Credential safety** — a profile's password (decrypted from its `password`
  field or resolved from its `secretRef` to connect) is redacted to `***` before it is written into an archive's
  `metadata.json`, so archives never carry plaintext credentials.

This replaces the earlier experimental single-file backup/restore dialog and
the `--enable-backup-restore` gate, which have been removed.

### Backup & restore

The *Backup & restore* panel at the bottom of the Backups page backs up or
restores the **currently connected** database to any `.zip` path
(`AppController.backupRunner`):

- **Connection** — the panel carries the same connection chip as the rest of
  the tool, with a *Connect…* button that opens the Profile | DSN | Custom form
  (`ConnectDialog.qml`) in a dialog, so no other page has to be visited first.
  Back up and Restore… stay disabled until connected (the tooltip says why) and
  while any other run is in flight.
- **One progress, one outcome** — Back up and Restore are the same kind of run,
  so they share `RunProgress.qml` (pill, "Table x of y", elapsed time, rows,
  determinate bar) and `RunOutcome.qml` (tables, rows, duration, "Show in
  folder"). Only the verbs differ.
- **Partial runs are failures** — `SqlBackup::Backup`/`Restore` do not throw for
  a failing table. `BackupRunner` counts those errors and reports such a run
  as failed with the failing tables and their reasons listed
  (`BackupRunner.lastResult`), never as a success.
- **Typed confirmation** — *Restore…* requires typing the name of the database
  being overwritten, like the per-profile restore.
- **Result in the detail panel** — the detail region beside the profile table
  follows a Backup & restore run: while it runs, and until its outcome is
  dismissed, it lists that run's tables and failures (`BackupRunner.tables`, the
  same per-table model the managed runs use). Pinning a profile row, or a managed
  run starting, takes the region back.
- **Which database it names** — every "connected to …" label reads
  `AppController.connectedTarget` (the profile, DSN or database actually open),
  never the *selected* profile, which a DSN or connection-string connection
  leaves untouched. Each run also records its target in `lastResult`, so an
  outcome keeps naming the database it ran against after you connect elsewhere.

### Adding profiles

A database without a profile has no row in the profile table, so it cannot be
backed up or restored from there. Profiles can be added without leaving the GUI:

- **Save as profile…** — shown next to the connection (the connection panel, and
  the Backup & restore panel) once a DSN or connection-string connection is open.
  It keeps exactly that connection; the user only names it and chooses whether
  its password is stored. The password never reaches QML.
- **Add profile…** — in the profile table's header, for a data source or
  connection string that has not been connected yet.

Both go through `AppController.addProfile`, which uses the same
`Config::AddProfileToFile` as `dbtool add-profile`: the password (a `PWD=` inside
a connection string included) is encrypted before it touches the file, the rest
of `dbtool.yml` — comments, ordering — is left as it was, and the write is
atomic. The profile goes into the file the GUI loaded, else the one chosen in
Settings, else the default location. Adding a profile never changes the open
connection or the selected profile, and is refused while a migration, backup or
restore is running (the reload would reset the status those runs report into).

### Task progress and pre-migrate steps

A run shows its tasks as a list (`TaskProgressList.qml`, fed by
`MigrationRunner.tasks`, a `ProgressTaskModel`): one row per pre-migrate step a
plugin registered, indented rows for the units and tables a step reports, and a
*Migrations* row for an apply run. A row without a total sweeps; a finished
sub-task's row disappears, a finished top-level row stays until the next run
(a rollback included) starts, and a failed row stays, red with its reason and
stopped where it failed. The list stays on screen after the run, next to the
Simple view's outcome and above the Expert view's *Apply*, and a failed task
also writes `failed <task>: <reason>` to the log. The worker reports through
queued calls, coalesced to one update per task every 50 ms.

Every apply (Simple view *Run*, Expert view *Apply*, also with nothing pending)
runs the steps first, on the migration connection under the migration lock; a dry
run calls them in dry-run mode, without the lock, so they only report their
plan. With steps registered and nothing pending, the Simple view offers *Run
pre-migrate steps*. A step that throws fails the run before any migration.
*Cancel* is checked before and after each step, and a step itself may stop
between its units; a run cancelled during the steps applies no migration.

### Backup requirement

A release, a migration or a pre-migrate step can require a backup of the
database taken after its last migration (see `docs/sql-migrations.md`). While
the pending migrations need one that does not exist yet
(`AppController.backupRequired`):

- **Disabled buttons, one alert** — *Run* and *Retry* in the Simple view and
  *Apply* in the Expert view are disabled; dry runs stay available. An alert
  next to them (`BackupRequiredAlert.qml`) names what requires the backup
  (`backupRequirementReasons`), says why the newest backup does not count or why
  the last one could not be recorded (`backupRequirementNote`), and asks to stop
  every application that writes to the database until the migrations have
  finished: the migration lock does not stop their writes.
- **Back up now** — writes `<target>-<timestamp>.zip` into the backup folder
  (Settings), with no file dialog, and records it. Once it finished, the buttons
  enable. The Simple view's *Back up the database first* makes the same kind of
  backup before the run.
- **Migrate without a backup** — the alert's checkbox waives the requirement for
  one run. It is unchecked at start, never stored in the settings, and cleared
  after each run, on every connect and disconnect, and as soon as no backup is
  required any more (after *Back up now*, say), so it never carries over to a
  later requirement. The run writes a `waived` row into `schema_backups`, which
  never satisfies a later requirement, and logs a warning that says whether that
  row could be written.
- **The runner re-checks** — the alert judges every pending migration; an apply
  run checks the migrations it actually runs again under the migration lock,
  before the pre-migrate steps, and refuses when a backup is still missing.
- **Which backups count** — *Back up now*, *Back up the database first* and the
  Backup & restore panel's *Back up* and *Restore* record their verified archive
  in `schema_backups` (a restore records the migration history it left). Managed
  per-profile backups are recorded the same way, after the archive was verified;
  a managed restore replaces no archive and is recorded once it finished. A
  backup whose record cannot be written still succeeds, logs a warning that it
  cannot satisfy the requirement and leaves the alert showing why; an archive
  that fails verification fails the backup.

### Known limitations

- **No archive retention / generations.** Exactly one archive exists per
  profile; a successful run replaces it. The failure gate above means a *bad*
  run can no longer destroy the only copy, but there is still no history to
  roll back to.
- **A running backup-all cannot be cancelled**, and `~QThreadPool` blocks
  process exit until the current run finishes.
- **Live-details cosmetics.** A restore does not reset the per-table rows or
  flip the row to "running"; a cross-target restore animates the *source*
  profile's row; the worker count in the details header is hard-coded to 8 in
  QML while C++ uses `min(hardware_concurrency, 8)`; the restore dialog's
  target defaults to the first profile when the archive's own profile is not
  in the list.

### What's deferred

- Native OS-vault secret backend (`keychain:` / `wincred:` /
  `secretservice:` prefixes) — requires adding `qtkeychain` to `vcpkg.json`.
  Tracking as a phase-4 follow-up; the CLI-grade fallback chain
  (`env:` / `file:` / `stdin:`) is wired today and covers every headless / CI
  scenario.
- Windows + macOS GitHub Actions runners for the GUI tests. The Linux job
  (`ubuntu_dbtool_gui` in `.github/workflows/build.yml`) runs the workflow tests, SQL
  Server scenarios included; the same `jurplel/install-qt-action@v4` recipe drops onto the other
  two platforms in a follow-up.
- Postgres fan-out for the GUI test job (the existing
  `dbms_test_matrix` pattern reused with `LIGHTWEIGHT_BUILD_GUI=ON`).

## Building

```bash
# From the project root, with Qt 6.5+ installed in a standard location:
cmake -S . -B build -DLIGHTWEIGHT_BUILD_GUI=ON
cmake --build build --target dbtool-gui

# Or point CMake at a specific Qt install:
cmake -S . -B build -DLIGHTWEIGHT_BUILD_GUI=ON -DQt6_DIR=C:/Qt/6.11.0/msvc2022_64/lib/cmake/Qt6
```

## Configuration file and passwords

With a profile-store path set in Settings, the GUI uses exactly that file; nothing
is searched and nothing is merged. Otherwise it **merges several files**, nearest
first, exactly like `dbtool` (see
[docs/dbtool.md](../../../docs/dbtool.md#where-dbtool-finds-dbtoolyml)):

1. every `dbtool.yml` in the directory of the `dbtool-gui` executable and in each
   of its parent directories, up to the filesystem root. A file whose top level
   sets **`root: true`** ends the search: it is loaded, nothing above it is, and
   the per-user file is not either;
2. the per-user file, as the last layer, only when no loaded file set
   `root: true`.

The nearest file wins, per profile name and per file-wide value (`defaultProfile`,
`defaultPluginsDir`, `defaultBackupDir`); a profile is never mixed from several
files, and relative directories resolve against the file they are written in. The
current directory is no longer searched. `LIGHTWEIGHT_DBTOOL_CONFIG_SEARCH_DIR`,
when set, replaces the executable's directory as the start of the search (for
tests and scripted runs).

To share profiles between installations, follow "Deploying shared profiles" in
[docs/dbtool.md](../../../docs/dbtool.md#where-dbtool-finds-dbtoolyml): `root: true` in an
installation-owned file, no `pluginsDir` on shared profiles, `defaultPluginsDir` in each
installation's own file, and the multi-profile shape in every layered file (a legacy
single-profile file keeps working alone, but as a layer its `PluginsDir` belongs to its
`default` profile and its implicit default shadows the shared file's).

The startup log line `Profile store: <n> file(s): <nearest>, …, <file> (root) — <k> profile(s)`
lists the loaded files in precedence order, and the Settings page shows them as
the placeholder. A change to any of them reloads the profiles, discovering the
files again; a `dbtool.yml` newly created in a parent directory is picked up on
that reload or the next start.

Writes go to one file only, keeping its comments: **Save as profile** writes a
new profile into the `root: true` file when one was loaded, otherwise into the
per-user file (created if missing); a name any loaded file already defines is
refused, naming that file. When "make default" writes `defaultProfile` while a
nearer file sets its own, the nearer one still wins and the GUI warns about it.

Profile passwords follow the dbtool rules in
[docs/dbtool.md](../../../docs/dbtool.md#profile-passwords): `enc:` values are
decrypted, and a plaintext `password` is encrypted in place after the first
successful connect — whether that connect is the interactive one or the start
of a managed backup or restore of that profile. The password is written to the
file the profile came from; if that file cannot be written (read-only, say), the
connect still succeeds, the password stays plaintext on disk, and a warning
banner names the file and the reason.

## Running

```bash
# Launch; profiles come from every dbtool.yml from the executable's directory
# upwards (up to one with `root: true`), else also the per-user file
# (see docs/dbtool.md):
./build/target/dbtool-gui

# Headless smoke test (uses Qt's offscreen platform — no display needed):
QT_QPA_PLATFORM=offscreen ./build/target/dbtool-gui &
```

## Testing

When the project is configured with `LIGHTWEIGHT_BUILD_GUI=ON` **and**
`LIGHTWEIGHT_BUILD_TESTS=ON`, these CTest entries carry the shared `dbtool-gui` label and run
under `QT_QPA_PLATFORM=offscreen`:

| CTest entry | Layer | Runner |
|-------------|-------|--------|
| `dbtool-gui-tests` | C++ view-models, runners, models, close guard, watchdog | Catch2 (`tests/*Tests.cpp`) |
| `dbtool-gui-qmltest` | QML components | Qt `qmltestrunner` (`tests/qml/*.qml`) |
| `dbtool-gui-workflow-<file stem>-<function>` (file stem, e.g. `tst_workflow_simple`) | The real `Main` window, driven by clicks | `dbtool-gui-workflowtest` on `tests/workflow/tst_workflow_*.qml` |
| `dbtool-gui-smoke` | Process startup | `src/tests/test_dbtool_gui.py` |

```bash
ctest --preset clang-debug -L dbtool-gui --output-on-failure
# or, against the Windows build:
ctest --preset clangcl-debug -L dbtool-gui --output-on-failure
# one workflow test:
ctest --preset clang-debug -R dbtool-gui-workflow-tst_workflow_simple-test_run_applies_every_pending_migration
```

The same recipe runs on every push via the `dbtool-gui (Ubuntu, Qt 6.8, headless)` GitHub
Actions job (`ubuntu_dbtool_gui` in `.github/workflows/build.yml`).

### Workflow tests

- **One process per test function.** `AppController` is a process-wide singleton, so CTest runs
  each `test_*` function of a workflow file in a process of its own (`<TestCase name>::<function>`).
  Test functions must be written `    function test_<name>(` (4-space indent); CMake fails the
  configure otherwise.
- **Real window.** Each test opens `Main`, finds controls by `objectName`, clicks them and asserts
  what the user sees plus the database state afterwards (`WorkflowFixture`, `tests/support/`).
- **UI watchdog.** A 10 ms heartbeat runs from the window's first frame; a stall over 1 s fails the
  test. The report names the step of the longest gap. Fixture work runs outside the watchdog.
- **Isolation.** Each test gets its own settings, plugin directory, databases and DSNs
  (`TestIsolation.hpp`); `LIGHTWEIGHT_NO_ERROR_DIALOGS=1` keeps native dialogs away.
- **Fixture databases are SQLite** unless noted. Scenarios that need narrow and wide text columns
  to differ run on SQL Server, via the variable below, and assert the "nothing to upgrade" path
  on SQLite.

### Environment variables

| Variable | Effect |
|----------|--------|
| `QT_QPA_PLATFORM=offscreen` | Required for headless runs (CTest sets it). |
| `LIGHTWEIGHT_NO_ERROR_DIALOGS=1` | Replaces error message boxes with log lines (CTest sets it). |
| `DBTOOL_GUI_TEST_MSSQL_CONNECTION_STRING` | ODBC connection string to a SQL Server master database; enables the SQL Server scenarios, each in a database of its own. CI starts `mssql2022`. |
| `DBTOOL_GUI_WORKFLOW_PROFILE_FILE`, `DBTOOL_GUI_WORKFLOW_PROFILE` | Run the end-to-end test against an existing `dbtool.yml` and profile. Both are needed, plus the expectations and either the plugins directory or `SKIP_FIXTURES=1`: with a profile file of your own the fixture plugins are never used. |
| `DBTOOL_GUI_WORKFLOW_EXPECTATIONS` | JSON file with the expected `pendingBefore` (number), `backupReasons` (array of strings; empty means no alert), `dryRunLogLine` and `secondDryRunLogLine` (strings). |
| `DBTOOL_GUI_WORKFLOW_PLUGINS_DIR` | Plugins directory the end-to-end test loads, and asserts it loaded. With a profile file, the test loads a copy of that file without any `pluginsDir` or `defaultPluginsDir` (relative backup folders made absolute), so this directory wins; the application's own precedence (profile, then file, then Settings page) is unchanged. |
| `DBTOOL_GUI_WORKFLOW_SKIP_FIXTURES` | `1`: no fixture plugins, database or seeding; needs the profile file. `0` or unset: the fixtures are used. |
| `DBTOOL_GUI_WORKFLOW_LONG_RUN_TIMEOUT_MS` | How long a test waits for a run, backup, connect or a `dbtool` call (default 120000). |

A `DBTOOL_GUI_WORKFLOW_*` variable set to something unusable (a timeout that is no positive
integer, `SKIP_FIXTURES` other than `0` or `1`, an unreadable or non-object expectations file)
fails every test function of every workflow file, with a message naming the variable. Nothing
else cuts a long run short: the workflow CTests set no `TIMEOUT`, and `QTEST_FUNCTION_TIMEOUT`
does not apply to Qt Quick Test.

Fixture plugins (`src/tests/post_migrate_plugin`, `src/tests/gui_workflow_fixtures`) simulate slow
or failing work through these variables; sleeps live only there.

| Variable | Fixture behaviour |
|----------|-------------------|
| `LIGHTWEIGHT_TEST_PLUGIN_LOAD_DELAY_MS`, `_PLUGIN_LOAD_LOG`, `_PLUGIN_LOAD_BEGIN_LOG` | Sleep in the plugin's load; log each load. |
| `LIGHTWEIGHT_TEST_POST_INIT_DELAY_MS`, `_POST_INIT_FAIL`, `_POST_INIT_LOG` | Slow, throwing or logged post-init hook. |
| `LIGHTWEIGHT_TEST_PRE_MIGRATE_LOG`, `_PRE_MIGRATE_FAIL`, `_PRE_MIGRATE_REQUIRES_BACKUP`, `_PRE_MIGRATE_REQUIRES_BACKUP_DELAY_MS` | Recorder step; a step that throws; a step that requires a backup, with a slow predicate. |
| `LIGHTWEIGHT_TEST_LONG_STEP_MS`, `_LONG_STEP_UNITS`, `_LONG_STEP_FAIL_AT`, `_LONG_STEP_LOG` | Long step reporting sub-task progress; fails at a given unit. |
| `LIGHTWEIGHT_TEST_POST_MIGRATE_LOG`, `_POST_MIGRATE_FAIL` | Logged or throwing post-migrate hook. |
| `LIGHTWEIGHT_TEST_BULK_MIGRATIONS` | Number of migrations the bulk fixture registers. |
| `LIGHTWEIGHT_TEST_UNICODE_LEGACY_TYPES` | Unicode-candidate fixture creates narrow text columns. |

### Running against your own plugins

```bash
DBTOOL_GUI_WORKFLOW_PROFILE_FILE=/path/to/dbtool.yml \
DBTOOL_GUI_WORKFLOW_PROFILE=my-profile \
DBTOOL_GUI_WORKFLOW_PLUGINS_DIR=/path/to/plugins \
DBTOOL_GUI_WORKFLOW_EXPECTATIONS=/path/to/expectations.json \
DBTOOL_GUI_WORKFLOW_SKIP_FIXTURES=1 \
DBTOOL_GUI_WORKFLOW_LONG_RUN_TIMEOUT_MS=3600000 \
ctest --preset clang-debug -R dbtool-gui-workflow-tst_workflow_end_to_end --output-on-failure
```

The watchdog stays on, so the run also proves the UI stays responsive on your data.
The profile's database is migrated for real: use a copy.

### Capability coverage

| Capability | Workflow test file (`tests/workflow/`) |
|------------|----------------------------------------|
| Startup: first frame before slow plugins, single plugin load, no DB access while disconnected | `tst_workflow_startup.qml`, `tst_workflow_smoke.qml` |
| Shell: rail and pages, Simple/Expert switch, log toggle, theme | `tst_workflow_shell.qml` |
| Connection: profile, DSN and custom modes, ODBC refresh, connect failure, save/add profile, external profile-file change | `tst_workflow_connection.qml` |
| Plugins: directory change, reload, post-init re-run | `tst_workflow_plugins.qml` |
| Responsiveness: slow post-init and backup check, stale results, double connect, async refresh and SQL preview | `tst_workflow_responsiveness.qml` |
| Cheap updates: no echo reload, deferred external change, 2000 migrations, bounded log | `tst_workflow_cheap_updates.qml` |
| Simple view: Run, back-up-first chain, Cancel, outcomes, Retry, copy/save report, view log | `tst_workflow_simple.qml` |
| Expert view: targets (latest, release, timestamp), Apply, Dry run, selection, rollback, SQL preview | `tst_workflow_expert.qml` |
| Expert options: Acquire lock, Back up first, dry-run text, log views | `tst_workflow_expert_options.qml` |
| Pre-migrate steps and progress: order, sub-tasks, failed step, cancel between units | `tst_workflow_steps.qml` |
| Backup requirement: alert, Back up now, waiver, backup and restore records, re-evaluation | `tst_workflow_backup_requirement.qml` |
| Backups page: back up all/one, restore to profile and connection string, custom backup, folder, reveal | `tst_workflow_backups.qml` |
| SQL query tab: result, error | `tst_workflow_sql_query.qml` |
| Settings persisted across a window restart | `tst_workflow_settings.qml` |
| Close guard during a run | `tst_workflow_close.qml` |
| Full upgrade and idempotent rerun | `tst_workflow_end_to_end.qml` |

## QML module

The migrations view is exposed as a reusable QML module:

```qml
import Lightweight.Migrations 1.0

MigrationView {
    anchors.fill: parent
}
```

This is intentional so embedders can drop the same view into their own Qt apps
without depending on the `dbtool-gui` executable. `AppController` is a
QML singleton — `import Lightweight.Migrations` is enough to reach the
migrations / releases / runner bindings from QML.

## Layout

```
dbtool-gui/
  main.cpp                  Minimal Qt entry point; no business logic.
  AppController.{hpp,cpp}   QML singleton holding every view-model binding.
  MigrationRunner.{hpp,cpp} Async wrapper over MigrationManager.
  BackupRunner.{hpp,cpp}    Async wrapper over SqlBackup::Backup/Restore
                            (single-file backup/restore, kept for now).
  ManagedBackupCore.{hpp,cpp}
                            Pure decision logic for managed backups: archive
                            naming/collision detection, disk scanning, atomic
                            temp-file commit, folder writability check,
                            profile-to-connection-string resolution. No Qt
                            types, so it is unit-tested without an event loop.
  ManagedBackupController.{hpp,cpp}
                            QObject orchestrating the managed backup folder:
                            owns the folder setting, sequences backupAll /
                            backupProfile / restoreArchive /
                            restoreArchiveToConnectionString on a worker
                            thread, and publishes per-profile status.
  QmlProgressManager.{hpp,cpp}
                            Mirrors StandardProgressManager's callback shape.
  Models/                   QAbstractListModel subclasses (profiles, DSNs,
                            migrations, releases, SQL results,
                            BackupStatusListModel for managed backups).
  qml/                      UI components (Main, MigrationView, ReleaseGroup,
                            MigrationRow, StatusCard, ReleasesSummary,
                            ActionsPanel, LogPanel, ConnectionPanel,
                            BackupsPage, SettingsPage).
  tests/                    Headless test suites. Built when
                            LIGHTWEIGHT_BUILD_TESTS=ON; see "Testing" above.
                            *Tests.cpp (Catch2), qml/ (component tests),
                            workflow/ (window-level tests), support/
                            (UiWatchdog, WorkflowFixture).
```
