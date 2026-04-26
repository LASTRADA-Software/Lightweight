# dbtool-gui

Qt 6 graphical companion to the `dbtool` CLI. Runs the same migration, ad-hoc
SQL query, and backup/restore workflows declared via the Lightweight
library's `LIGHTWEIGHT_SQL_RELEASE` macro and `dbtool` profile system.

This subdirectory is **opt-in**. It only builds when both of the following are
true:

- `LIGHTWEIGHT_BUILD_GUI=ON` is passed at configure time, and
- A Qt 6 (>= 6.5) install is found by `cmake/FindQt.cmake` — either via an
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

- `QtKeychainBackend` (native `keychain:` / `wincred:` / `secretservice:`
  prefixes) — requires adding `qtkeychain` to `vcpkg.json`. Tracking as a
  phase-4 follow-up; the CLI-grade fallback chain (`env:` / `file:` /
  `stdin:`) is wired today and covers every headless / CI scenario.
- Three new GitHub Actions matrix jobs (`ubuntu + Qt 6.7`, `windows + Qt 6.7`,
  `macos + Qt 6.7`) — the plan's §8 CI section. Ready to add once the team
  confirms the `jurplel/install-qt-action` caching strategy.
- QML test-runner integration (`qmltestrunner`) — the C++ view-model is
  covered by compilation + offscreen smoke runs; dedicated QML component
  tests come in the CI follow-up.

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
  QtKeychainBackend.hpp     Placeholder — see "What's deferred" above.
```
