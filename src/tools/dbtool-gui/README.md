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
MigrationRunner, BackupRunner, four QAbstractListModels, QmlProgressManager),
a QML UI mirroring the mockup in `docs/migrations-gui-mockup.html`, and a
basic backup/restore dialog. See `docs/migrations-gui-plan.md` for the full
plan and deferred work.

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
| `dbtool-gui-tests`   | C++ view-model     | Catch2 (`AppControllerTests.cpp`)       |
| `dbtool-gui-qmltest` | QML components     | Qt `qmltestrunner` (`tests/qml/*.qml`)  |
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
  BackupRunner.{hpp,cpp}    Async wrapper over SqlBackup::Backup/Restore.
  QmlProgressManager.{hpp,cpp}
                            Mirrors StandardProgressManager's callback shape.
  Models/                   QAbstractListModel subclasses (profiles, DSNs,
                            migrations, releases).
  qml/                      UI components (Main, MigrationView, ReleaseGroup,
                            MigrationRow, StatusCard, ReleasesSummary,
                            ActionsPanel, LogPanel, ConnectionPanel,
                            BackupRestoreDialog).
  tests/                    Headless test suites. Built when
                            LIGHTWEIGHT_BUILD_TESTS=ON; see "Running the
                            tests" above.
```
