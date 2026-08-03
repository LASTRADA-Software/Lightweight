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

**Phases 0–4 landed:** scaffolding, shared ProfileStore / DataSourceEnumerator
/ SecretResolver in the library, Qt view-model layer (AppController,
MigrationRunner, BackupRunner, ManagedBackupController, six QAbstractListModels,
QmlProgressManager),
a QML UI mirroring the mockup in `docs/migrations-gui-mockup.html`, and
managed backups (see below). See `docs/migrations-gui-plan.md` for the full
plan and deferred work.

### Managed backups

The Backups page manages one `<profile>.zip` archive per configured profile
inside a single folder, instead of a one-off file picker:

- **Folder** — configured on the Settings page (persisted as `backup/folder`
  in `QSettings`); defaults to `<AppDataLocation>/backups` when unset. The
  folder is validated as writable (created if missing) before any run starts,
  and the current problem, if any, is surfaced inline.
- **Backup all** — backs up every configured profile sequentially into
  `<folder>/<sanitized-profile-name>.zip`, continuing past per-profile
  failures; a single profile can also be backed up on its own from its row.
- **Safe overwrite** — each archive is written to a `.tmp` sibling first and
  atomically renamed over the final path on success, so a crash or failure
  mid-write never corrupts the previous good archive.
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
- **Credential safety** — a profile's password (resolved from its `secretRef`
  to connect) is redacted to `***` before it is written into an archive's
  `metadata.json`, so archives never carry plaintext credentials.

This replaces the earlier experimental single-file backup/restore dialog and
the `--enable-backup-restore` gate, which have been removed.

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
  (`ubuntu_dbtool_gui` in `.github/workflows/build.yml`) lands as the first
  step; the same `jurplel/install-qt-action@v4` recipe drops onto the other
  two platforms in a follow-up once the Linux job is stable.
- Postgres / MSSQL fan-out for the GUI test job (the existing
  `dbms_test_matrix` pattern reused with `LIGHTWEIGHT_BUILD_GUI=ON`).

## Building

```bash
# From the project root, with Qt 6.5+ installed in a standard location:
cmake -S . -B build -DLIGHTWEIGHT_BUILD_GUI=ON
cmake --build build --target dbtool-gui

# Or point CMake at a specific Qt install:
cmake -S . -B build -DLIGHTWEIGHT_BUILD_GUI=ON -DQt6_DIR=C:/Qt/6.11.0/msvc2022_64/lib/cmake/Qt6
```

## Running

```bash
# Launch with a profile from ~/.config/dbtool/dbtool.yml:
./build/target/dbtool-gui

# Headless smoke test (uses Qt's offscreen platform — no display needed):
QT_QPA_PLATFORM=offscreen ./build/target/dbtool-gui &
```

## Running the tests

When the project is configured with `LIGHTWEIGHT_BUILD_GUI=ON` **and**
`LIGHTWEIGHT_BUILD_TESTS=ON`, three CTest entries become available under the
shared `dbtool-gui` label, all of which run under
`QT_QPA_PLATFORM=offscreen`:

| CTest entry          | Layer              | Runner                                  |
|----------------------|--------------------|-----------------------------------------|
| `dbtool-gui-tests`   | C++ view-model     | Catch2 (`AppControllerTests.cpp`, `BackupStatusListModelTests.cpp`, `ManagedBackupCoreTests.cpp`, `ManagedBackupControllerTests.cpp`) |
| `dbtool-gui-qmltest` | QML components     | Qt `qmltestrunner` (`tests/qml/*.qml`, incl. `tst_backups_page.qml`) |
| `dbtool-gui-smoke`   | Process startup    | `src/tests/test_dbtool_gui.py` (Python) |

Run them all in one go:

```bash
ctest --preset clang-debug -L dbtool-gui --output-on-failure
# or, against the Windows build:
ctest --preset clangcl-debug -L dbtool-gui --output-on-failure
```

The same recipe runs on every push via the `dbtool-gui (Ubuntu, Qt 6.8,
headless)` GitHub Actions job — see `ubuntu_dbtool_gui` in
`.github/workflows/build.yml`.

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
                            LIGHTWEIGHT_BUILD_TESTS=ON; see "Running the
                            tests" above. Includes ManagedBackupCoreTests.cpp,
                            ManagedBackupControllerTests.cpp,
                            BackupStatusListModelTests.cpp, and
                            qml/tst_backups_page.qml.
```
