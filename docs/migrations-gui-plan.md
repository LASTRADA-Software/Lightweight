# Migrations GUI — Implementation plan (issue #472)

Companion to `docs/migrations-gui-mockup.html`. Target: a Qt 6 / QML desktop
application that wraps the same `MigrationManager` logic `dbtool` already
drives, ships as a reusable QML component, and builds out of the box on
Windows / macOS / Linux with no manual Qt path configuration.

This document is organised so each section can be reviewed and signed off
independently before implementation starts.

---

## 1. Scope

In scope for the first cut:

- New Qt 6 desktop executable `dbtool-gui` that
  reproduces the mocked-up three-pane UI.
- A reusable QML module `Lightweight.Migrations 1.0` exporting
  `MigrationView`, so downstream apps can embed the whole migration panel
  in their own windows with a single `import`.
- ODBC data-source discovery (`SQLDataSources` / `SQLDrivers`) surfaced as
  a model in the connection card.
- Multi-profile config shared with `dbtool` (via `--profile <name>`).
- Async migration execution (dry-run, apply, rollback, rollback-to-release)
  with live progress and log streaming.
- Pluggable secret backends (Windows Credential Manager, macOS Keychain,
  libsecret on Linux, plus env-var / file / stdin fallbacks).
- CMake auto-detection of Qt in common install locations, so a fresh
  checkout builds on a developer machine with Qt 6 installed from the
  Qt Online Installer without any environment setup.

Also in scope for the first cut:

- Backup & restore UI: a fourth toolbar action that pops a small file-picker
  dialog and runs the existing `SqlBackup` API / dbtool's restore path in
  the async runner. Same progress + log wiring as migrations.

Explicitly *out* of scope for the first cut (called out again in §10):

- Installer packaging (MSIX, .dmg, AppImage). We will build a runnable app
  bundle (`windeployqt` / `macdeployqt`) but not a signed installer.
- Multi-connection concurrent editing (the app targets one profile at a
  time, matching `MigrationManager`'s singleton shape).

---

## 2. Repository layout

New or promoted paths (relative to repo root):

```
cmake/
  FindQt.cmake                         (NEW) Qt 6 probe helper.

src/Lightweight/
  Config/
    ProfileStore.hpp                       (NEW, promoted from dbtool)
    ProfileStore.cpp                       (NEW)
  Odbc/
    DataSourceEnumerator.hpp               (NEW)
    DataSourceEnumerator.cpp               (NEW)
  Secrets/
    ISecretBackend.hpp                     (NEW)
    SecretResolver.{hpp,cpp}               (NEW)
    backends/
      FileBackend.{hpp,cpp}                (NEW, cross-platform, used by dbtool+GUI)
      EnvBackend.{hpp,cpp}                 (NEW, cross-platform, used by dbtool+GUI)
      StdinBackend.{hpp,cpp}               (NEW, cross-platform, used by dbtool+GUI)
      QtKeychainBackend.{hpp,cpp}          (NEW, GUI-only; wraps Windows Credential
                                            Manager, macOS Keychain, Secret Service)

src/tools/                                  (existing; sibling of dbtool)
  dbtool-gui/                                (NEW)
    CMakeLists.txt
    main.cpp
    AppController.{hpp,cpp}                App-level QObject, registered in QML.
    Models/
      MigrationListModel.{hpp,cpp}
      ReleaseListModel.{hpp,cpp}
      ProfileListModel.{hpp,cpp}
      OdbcDataSourceListModel.{hpp,cpp}
    MigrationRunner.{hpp,cpp}              Async executor, emits progress.
    BackupRunner.{hpp,cpp}                 Async backup/restore over SqlBackup.
    QmlProgressManager.{hpp,cpp}           Adapter over dbtool's ProgressManager.
    qml/
      Main.qml
      ConnectionPanel.qml
      StatusCard.qml
      ReleasesSummary.qml
      MigrationView.qml                    Reusable component (the heart of the QML module).
      ReleaseGroup.qml                     Collapsible group + tri-state checkbox.
      MigrationRow.qml
      ActionsPanel.qml
      BackupRestoreDialog.qml              File picker + progress for backup/restore.
      LogPanel.qml
    resources/
      icons/...
      qml.qrc

src/tools/dbtool/
  main.cpp                                 Modified: use Lightweight::Config::ProfileStore,
                                           accept --profile.

src/tests/
  ConfigProfileStoreTests.cpp              (NEW)
  DataSourceEnumeratorTests.cpp            (NEW)
  SecretResolverTests.cpp                  (NEW)

docs/
  migrations-gui-mockup.html               (existing)
  migrations-gui-plan.md                   (this file)
```

The GUI lives in `src/tools/dbtool-gui/` alongside the other Lightweight
tooling (`dbtool`, `ddl2cpp`, `lup2dbtool`, `LupMigrationsPlugin`,
`large-db-generator`). `src/tools/CMakeLists.txt` guards the GUI subdir
behind `LIGHTWEIGHT_BUILD_GUI` so the Qt dependency only kicks in when
the option is on and the Qt probe succeeded.

---

## 3. CMake & Qt auto-detection

Two pieces: the probe helper, and the root-level wiring that consumes it.

### 3.1 `cmake/FindQt.cmake`

A helper module that probes common Qt 6 install paths and prepends the
best match to `CMAKE_PREFIX_PATH` so `find_package(Qt6 ...)` just works.
Respects any explicit configuration the developer already set.

```cmake
# cmake/FindQt.cmake
#
# Usage:
#   include(FindQt)
#   lightweight_probe_qt6()              # extends CMAKE_PREFIX_PATH if Qt is found
#   find_package(Qt6 6.5 COMPONENTS ...) # now succeeds on fresh machines
#
# The probe is a no-op when any of the following are set:
#   - Qt6_DIR (cache or env)
#   - CMAKE_PREFIX_PATH already contains a path ending in ".../Qt6"
# so developers who know what they are doing keep full control.

function(lightweight_probe_qt6)
    if(Qt6_DIR OR DEFINED ENV{Qt6_DIR})
        return()
    endif()
    foreach(_p IN LISTS CMAKE_PREFIX_PATH)
        if(_p MATCHES "Qt6$|Qt6/$")
            return()
        endif()
    endforeach()

    set(_candidates "")

    if(WIN32)
        # Qt Online Installer default roots, plus $SYSTEMDRIVE/Qt for corp images.
        foreach(_root IN ITEMS "C:/Qt" "D:/Qt" "$ENV{SYSTEMDRIVE}/Qt"
                               "$ENV{USERPROFILE}/Qt")
            if(EXISTS "${_root}")
                file(GLOB _versions RELATIVE "${_root}" "${_root}/6.*")
                foreach(_v IN LISTS _versions)
                    foreach(_c IN ITEMS
                        msvc2022_64 msvc2022_arm64
                        msvc2019_64 msvc2019_arm64
                        mingw_64 llvm-mingw_64)
                        set(_p "${_root}/${_v}/${_c}")
                        if(EXISTS "${_p}/lib/cmake/Qt6")
                            list(APPEND _candidates "${_p}")
                        endif()
                    endforeach()
                endforeach()
            endif()
        endforeach()
    elseif(APPLE)
        foreach(_root IN ITEMS
            "/opt/homebrew/opt/qt@6"     # Homebrew, Apple Silicon
            "/usr/local/opt/qt@6"        # Homebrew, Intel
            "/opt/homebrew/opt/qt"       # Homebrew (unversioned alias)
            "/usr/local/opt/qt")
            if(EXISTS "${_root}/lib/cmake/Qt6")
                list(APPEND _candidates "${_root}")
            endif()
        endforeach()
        file(GLOB _versions "$ENV{HOME}/Qt/6.*/macos")
        foreach(_p IN LISTS _versions)
            if(EXISTS "${_p}/lib/cmake/Qt6")
                list(APPEND _candidates "${_p}")
            endif()
        endforeach()
    else()
        # Linux: distro packages set Qt6_DIR via CMake config, so this
        # matters mostly for Qt Online Installer users.
        file(GLOB _versions "$ENV{HOME}/Qt/6.*/gcc_64")
        foreach(_p IN LISTS _versions)
            if(EXISTS "${_p}/lib/cmake/Qt6")
                list(APPEND _candidates "${_p}")
            endif()
        endforeach()
    endif()

    if(NOT _candidates)
        message(STATUS "FindQt: no Qt 6 install found in common locations.")
        return()
    endif()

    # Prefer the newest version that we saw (natural sort handles 6.5 < 6.10).
    list(REMOVE_DUPLICATES _candidates)
    list(SORT _candidates COMPARE NATURAL ORDER DESCENDING)
    list(GET _candidates 0 _best)
    message(STATUS "FindQt: probed Qt 6 at ${_best}")

    list(PREPEND CMAKE_PREFIX_PATH "${_best}")
    set(CMAKE_PREFIX_PATH "${CMAKE_PREFIX_PATH}" CACHE STRING
        "Search paths for find_package()" FORCE)
endfunction()
```

### 3.2 Root `CMakeLists.txt` additions

```cmake
option(LIGHTWEIGHT_BUILD_GUI "Build Qt-based Migrations GUI" ON)

if(LIGHTWEIGHT_BUILD_GUI)
    include(FindQt)
    lightweight_probe_qt6()

    find_package(Qt6 6.5 COMPONENTS Core Gui Qml Quick QuickControls2
                                     Concurrent QUIET)

    if(NOT Qt6_FOUND)
        message(STATUS
            "Qt 6 (>=6.5) not found; GUI will not be built.\n"
            "  Install Qt 6.5+ from the Qt Online Installer, your distro's\n"
            "  package manager, or Homebrew, then re-run CMake. You can also\n"
            "  point at it explicitly with -DQt6_DIR=/path/to/lib/cmake/Qt6.")
        set(LIGHTWEIGHT_BUILD_GUI OFF CACHE BOOL
            "Build Qt-based Migrations GUI" FORCE)
    else()
        qt_standard_project_setup(REQUIRES 6.5)
        message(STATUS "Qt ${Qt6_VERSION} found at ${Qt6_DIR}")
    endif()
endif()
```

The GUI is opt-out if Qt is absent: CI images or servers that never
install Qt are unaffected, but a developer with a standard Qt install
gets the GUI with `cmake ..` and nothing else.

### 3.3 `src/tools/dbtool-gui/CMakeLists.txt`

```cmake
qt_add_executable(dbtool-gui
    main.cpp
    AppController.cpp
    MigrationRunner.cpp
    QmlProgressManager.cpp
    Models/MigrationListModel.cpp
    Models/ReleaseListModel.cpp
    Models/ProfileListModel.cpp
    Models/OdbcDataSourceListModel.cpp
)

qt_add_qml_module(dbtool-gui
    URI Lightweight.Migrations
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/ConnectionPanel.qml
        qml/StatusCard.qml
        qml/ReleasesSummary.qml
        qml/MigrationView.qml
        qml/ReleaseGroup.qml
        qml/MigrationRow.qml
        qml/ActionsPanel.qml
        qml/LogPanel.qml
    RESOURCES
        resources/icons/app.svg
)

target_link_libraries(dbtool-gui PRIVATE
    Lightweight
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Concurrent
)

# windeployqt / macdeployqt hooks for a runnable bundle out of the build tree.
if(WIN32 AND TARGET Qt6::windeployqt)
    add_custom_command(TARGET dbtool-gui POST_BUILD
        COMMAND Qt6::windeployqt --qmldir ${CMAKE_CURRENT_SOURCE_DIR}/qml
                $<TARGET_FILE:dbtool-gui>
        COMMENT "Deploying Qt runtime next to the GUI executable")
elseif(APPLE AND TARGET Qt6::macdeployqt)
    # ...
endif()
```

---

## 4. Dependencies

Additions to `vcpkg.json`:

```json
{
  "dependencies": [
    { "name": "yaml-cpp", "version>=": "0.8.0" },
    { "name": "catch2",   "version>=": "3.5.2" },
    { "name": "libzip" }
    // No Qt entry — Qt is resolved via FindQt + find_package.
    // Pinning Qt via vcpkg is possible but makes CI builds multi-hour
    // and the GUI is opt-out, so we prefer the system/Qt-Installer path.
  ]
}
```

Secret-backend deps (resolved: use QtKeychain in the GUI, keep dbtool
Qt-free):

- New vcpkg dep: `qtkeychain` (pulled in only when `LIGHTWEIGHT_BUILD_GUI`
  is ON). QtKeychain wraps Windows Credential Manager, macOS Keychain
  Services, and the Linux Secret Service in one cross-platform API.
- `QtKeychainBackend.cpp` compiles into the GUI target only; `dbtool`
  keeps its pure `FileBackend` / `EnvBackend` / `StdinBackend` chain so
  the CLI stays Qt-free.
- Linux headless runtimes (no D-Bus session): QtKeychain's Secret Service
  backend fails gracefully and `SecretResolver` falls through to
  `file:` / `env:` / `stdin:` — same behaviour as the native path would
  have had.
- No direct link to `Advapi32.lib`, `-framework Security`, or
  `libsecret-1`: QtKeychain handles platform linkage internally.

---

## 5. Architecture

### 5.1 Layering

```
        ┌───────────────────────────────────────────────────────┐
        │  QML (Lightweight.Migrations 1.0)                     │
        │    Main.qml / MigrationView.qml / ReleaseGroup.qml    │
        └───────────────▲──────────────────────────▲────────────┘
                        │ contextProperties        │ signals
        ┌───────────────┴──────────────────────────┴────────────┐
        │  Qt view-model layer (src/tools/dbtool-gui)        │
        │    AppController                                      │
        │    MigrationListModel · ReleaseListModel              │
        │    ProfileListModel · OdbcDataSourceListModel         │
        │    MigrationRunner (async on QThreadPool)             │
        └───────────────▲───────────────────────────────────────┘
                        │ reuses only public APIs
        ┌───────────────┴───────────────────────────────────────┐
        │  Lightweight library (existing + promoted code)       │
        │    MigrationManager (unchanged)                       │
        │    ProfileStore · DataSourceEnumerator · SecretResolver│
        └───────────────▲───────────────────────────────────────┘
                        │ same library
        ┌───────────────┴───────────────────────────────────────┐
        │  dbtool CLI (uses ProfileStore, SecretResolver too)   │
        └───────────────────────────────────────────────────────┘
```

No migration logic lives in the GUI layer. Every action the GUI performs
resolves to a call on `MigrationManager` (plus the three new shared
helpers above). This guarantees dbtool and the GUI behave identically.

### 5.2 Key C++ types

**`AppController`** (QObject, singleton, registered as a QML singleton
via `QML_ELEMENT` + `QML_SINGLETON`). Exposes:

- `Q_PROPERTY(ProfileListModel* profiles READ profiles CONSTANT)`
- `Q_PROPERTY(QString currentProfile READ currentProfile WRITE setCurrentProfile NOTIFY currentProfileChanged)`
- `Q_PROPERTY(OdbcDataSourceListModel* odbcDataSources READ ...)` (populated on demand)
- `Q_PROPERTY(MigrationListModel* migrations READ ...)`
- `Q_PROPERTY(ReleaseListModel* releases READ ...)`
- `Q_PROPERTY(MigrationRunner* runner READ ...)`
- `Q_INVOKABLE bool connect(QString profileName)`
- `Q_INVOKABLE void refreshOdbcDataSources()`

**`MigrationRunner`** (QObject). Owns a `QThreadPool` and a
`std::unique_ptr<MigrationManager>` (or references the singleton —
decision in §10). Operations:

- `Q_INVOKABLE void dryRun(QString targetVersionOrEmpty)`
- `Q_INVOKABLE void applyTo(QString targetVersionOrEmpty)`
- `Q_INVOKABLE void rollbackToRelease(QString version)`
- `Q_INVOKABLE void cancel()` (best-effort; honoured between migrations)

Signals:

- `progress(MigrationProgressUpdate update)` — a POD with migration
  timestamp, title, phase (started/running/ok/error), percent, message.
- `logLine(QString line, LogLevel level)`
- `finished(RunResult result)` — summary counts, failed-at if any.

**`QmlProgressManager`** implements the same callback interface
`MigrationManager` already calls into (the one `StandardProgressManager`
uses for the CLI), but instead of printing to stdout it emits Qt signals
marshalled back to the GUI thread. This means the GUI and CLI see the
exact same callback surface and we do not fork behaviour.

**Models** — thin `QAbstractListModel` subclasses driven by the
corresponding core APIs. Roles map to struct fields so QML can just read
`model.version`, `model.status`, etc.

### 5.3 Threading & cancellation

- UI thread owns the `QGuiApplication` event loop, QML engine, and all
  `QAbstractListModel` instances.
- Long calls (`Migrate()`, `RevertToMigration()`, `VerifyChecksums()`,
  `SQLDataSources` enumeration) run in `QThreadPool` tasks.
- Progress is marshalled via `QMetaObject::invokeMethod(..., Qt::QueuedConnection)`
  or signal/slot with `Qt::AutoConnection` across threads (Qt's default
  becomes queued when sender/receiver live on different threads).
- Cancellation flag checked by `MigrationRunner` between per-migration
  callbacks. In-flight SQL is not interrupted (ODBC `SQLCancel` is
  unreliable across drivers; we simply do not start the next migration).

---

## 6. Shared code extraction

Three chunks move from `src/tools/dbtool/` into `src/Lightweight/`.

### 6.1 `ProfileStore`

Today `dbtool/main.cpp` has `GetDefaultConfigPath()` and `LoadConfig()`
reading a single YAML file with keys `PluginsDir`, `ConnectionString`,
`Schema`. We extend the schema backward-compatibly.

**API:**

```cpp
namespace Lightweight::Config {
    struct Profile {
        std::string name;
        std::string pluginsDir;
        std::string schema;
        // Exactly one of these is set:
        std::optional<std::string> dsn;
        std::optional<std::string> connectionString;
        std::optional<std::filesystem::path> sqliteFile;
        // Auth:
        std::string uid;            // optional
        std::string secretRef;      // parsed by SecretResolver
    };

    class LIGHTWEIGHT_API ProfileStore {
    public:
        static std::filesystem::path DefaultPath();  // unchanged logic
        static ProfileStore LoadOrDefault(std::filesystem::path path = {});

        [[nodiscard]] std::vector<Profile> const& Profiles() const noexcept;
        [[nodiscard]] Profile const* Find(std::string_view name) const noexcept;
        [[nodiscard]] std::string DefaultProfileName() const noexcept;

        void Upsert(Profile profile);
        void Remove(std::string_view name);
        void Save(std::filesystem::path path = {}) const;
    };
}
```

**File format (forward):**

```yaml
defaultProfile: acme-prod
profiles:
  acme-prod:
    source:
      dsn: ACME_PROD
      uid: deploy
      secretRef: lightweight/acme-prod        # resolver picks backend
    pluginsDir: ./migrations
    schema: dbo
  local-sqlite:
    source:
      sqliteFile: ./dev.db
    pluginsDir: ./migrations
```

**Backward-compat:** if the file has top-level `ConnectionString`,
`PluginsDir`, `Schema` (the current shape), `ProfileStore` synthesizes a
single implicit profile named `"default"` and `dbtool` with no
`--profile` flag picks it automatically. No existing user is forced to
migrate.

### 6.2 `DataSourceEnumerator`

Thin C++ wrapper around the ODBC driver manager. Uses the already-linked
ODBC (unixODBC on non-Windows, the Windows driver manager on Windows).

```cpp
namespace Lightweight::Odbc {
    struct DataSourceInfo {
        std::string name;
        std::string description;
        enum class Scope { User, System } scope;
    };
    struct DriverInfo {
        std::string name;
        std::vector<std::pair<std::string, std::string>> attributes;
    };

    [[nodiscard]] LIGHTWEIGHT_API std::vector<DataSourceInfo> EnumerateDataSources();
    [[nodiscard]] LIGHTWEIGHT_API std::vector<DriverInfo> EnumerateDrivers();
}
```

Implementation: `SQLAllocHandle(SQL_HANDLE_ENV)`,
`SQLSetEnvAttr(..., SQL_ATTR_ODBC_VERSION, SQL_OV_ODBC3)`, then
`SQLDataSources(env, SQL_FETCH_FIRST_USER, ...)` followed by
`SQL_FETCH_NEXT` until `SQL_NO_DATA`, then the same loop with
`SQL_FETCH_FIRST_SYSTEM`. `SQLDrivers` follows the same pattern.

Testable via Catch2 `[.integration]` tagged tests that require a local
ODBC install — the CI pipeline can either install unixODBC (cheap) or
skip the integration tag.

### 6.3 `SecretResolver`

```cpp
namespace Lightweight::Secrets {
    class ISecretBackend {
    public:
        virtual ~ISecretBackend() = default;
        [[nodiscard]] virtual std::optional<std::string> Read(std::string_view key) = 0;
        virtual void Write(std::string_view key, std::string_view value) = 0;
        virtual void Erase(std::string_view key) = 0;
        [[nodiscard]] virtual std::string_view Name() const noexcept = 0;
    };

    class LIGHTWEIGHT_API SecretResolver {
    public:
        // Parses `secretRef` and dispatches to the right backend.
        // Bare refs (no prefix) walk the fallback chain.
        [[nodiscard]] std::optional<std::string> Resolve(std::string_view secretRef,
                                                         std::string_view profileName);
        void RegisterBackend(std::unique_ptr<ISecretBackend> backend);
    };
}
```

Backend prefixes recognised: `secretservice:`, `keychain:`, `wincred:`,
`env:`, `file:`, `pass:`, `stdin`.

Behaviour on Linux (most nuanced case, see `docs/` discussion):

- Explicit `secretservice:lightweight/acme-prod` uses libsecret; error
  if libsecret was not compiled in or D-Bus is unavailable.
- Bare `lightweight/acme-prod` tries in order: libsecret →
  `env:LIGHTWEIGHT_<PROFILE>_PWD` (uppercase, hyphens → underscores) →
  `file:$XDG_CONFIG_HOME/Lightweight/credentials` → `stdin` (only if
  stdin is a TTY).

Windows & macOS only need one native backend each; fallback chain still
applies for headless use.

Credentials file format (`.pgpass`-style, mode 0600 enforced on load):

```
# profileName:uid:password
acme-prod:deploy:SuperSecret
acme-dev:admin:changeme
```

Reader rejects the file with a clear error if mode is wider than 0600 on
POSIX; write helper sets 0600.

---

## 7. Implementation phases

Sequenced so each phase leaves the repo in a shippable state and nothing
has to wait on the whole GUI landing to get CLI-side value.

### Phase 0 — CMake scaffolding (≈ 1 day)

- Add `cmake/FindQt.cmake`.
- Wire `LIGHTWEIGHT_BUILD_GUI` option into root CMakeLists.
- Create `src/tools/dbtool-gui/` with a "hello Qt" executable that
  boots an empty `ApplicationWindow` and prints Qt version + loaded
  plugin directory.
- CI: add a GUI-enabled job on each OS that installs Qt (via
  `jurplel/install-qt-action` on GitHub Actions) and runs the empty
  executable in offscreen mode (`-platform offscreen`) to confirm build +
  link.

**Exit criterion:** fresh clone on a Windows box with Qt 6.5+ installed
at `C:/Qt/...` builds the empty window app with `cmake -B build && cmake --build build`.

### Phase 1 — Shared core: ProfileStore + DataSourceEnumerator + SecretResolver skeleton (≈ 2–3 days)

- Promote current `dbtool` config reader into `Lightweight::Config::ProfileStore`.
- Migrate `dbtool` to use it — no UX change with the old file shape.
- Add `--profile <name>` to `dbtool` (implicit `"default"` when absent).
- Add `DataSourceEnumerator` + tests.
- Add `SecretResolver` with `FileBackend`, `EnvBackend`, `StdinBackend`
  only — enough for `dbtool` to stop embedding passwords in
  `ConnectionString`.
- Add `dbtool` `--resolve-secret <ref>` sub-subtool for scripting use.

**Exit criterion:** `dbtool --profile acme-prod migrate` works against a
multi-profile YAML; existing single-profile YAMLs still work
byte-identically.

### Phase 2 — Qt view-model layer (≈ 3–4 days)

- Build the four `QAbstractListModel` classes with unit tests.
- Build `MigrationRunner` with `QSignalSpy` tests that assert signals
  fire in the right order on dry-run/apply/rollback.
- Register everything with `qmlRegisterType` / `qmlRegisterSingletonType`.
- Wire `AppController` to a `ProfileListModel` backed by `ProfileStore`.

**Exit criterion:** a headless test `qmltestrunner` run instantiates
`MigrationView` against the `dummy_migration_plugin`s in-repo and the
list model exposes the right rows and release groupings.

### Phase 3 — Build the UI, screen by screen (≈ 5–7 days)

Bottom-up, mirroring the mockup:

1. `MigrationRow.qml` + `ReleaseGroup.qml` — static then interactive
   (collapse, tri-state select).
2. `MigrationView.qml` — wires to `MigrationListModel` + `ReleaseListModel`,
   implements filter tabs, search, global expand/collapse + select/deselect.
3. `StatusCard.qml`, `ReleasesSummary.qml`.
4. `ActionsPanel.qml` — target picker (Latest / Release version / Specific
   timestamp), options toggles, primary action button wired to
   `MigrationRunner.dryRun` / `applyTo` / `rollbackToRelease`.
5. `LogPanel.qml` — consumes `MigrationRunner.logLine`.
6. `ConnectionPanel.qml` — profile dropdown, source-type switcher,
   DSN picker bound to `OdbcDataSourceListModel`, "Manage data sources…"
   button (Windows only, launches `odbcad32.exe`).
7. `Main.qml` — composes the three panes with a `SplitView`.

**Exit criterion:** user can pick a profile, connect, see the migration
list grouped by release (with the collapse/tri-select behaviour from
the mockup), and run a dry-run whose output matches `dbtool migrate --dry-run`
byte-for-byte (modulo timestamps).

### Phase 4 — Secret backends (≈ 2 days)

- `QtKeychainBackend.cpp` in the GUI target: thin wrapper over
  `QtKeychain::ReadPasswordJob` / `WritePasswordJob`. Handles Windows
  Credential Manager, macOS Keychain, and Linux Secret Service uniformly.
  Tests behind `LIGHTWEIGHT_RUN_PLATFORM_SECRET_TESTS` opt-in so CI
  doesn't pollute the developer's credential store.
- `FileBackend.cpp` / `EnvBackend.cpp` / `StdinBackend.cpp` (shared with
  `dbtool`) using plain C++; no Qt, no third-party deps.
- Optional `pass:` backend for users of `pass(1)` — shells out to
  `pass show <path>` via `QProcess` (GUI) or `popen` (CLI). No library
  dep.
- Graceful-degrade: on a Linux runtime without a D-Bus session, the
  QtKeychain call fails and `SecretResolver` falls through the chain.

**Exit criterion:** `secretRef` styles `keychain:`, `secretservice:`,
`wincred:`, `file:`, `env:`, `stdin:`, `pass:` all resolve on their
native platform, with the fallback chain for bare refs behaving as
§6.3 describes.

### Phase 5 — Polish & packaging (≈ 2 days)

- `windeployqt` / `macdeployqt` integration (POST_BUILD custom commands,
  already sketched in §3.3).
- App icon, window title bound to profile name, "About" dialog showing
  `LIGHTWEIGHT_VERSION_STRING`.
- README entry for the GUI; screenshot from the mockup as placeholder
  until real UI screenshot exists.

**Exit criterion:** running `cmake --install build` on Windows produces
a self-contained folder the customer can zip and hand over.

---

## 8. Testing

Three layers:

- **Core library tests** (Catch2, run on CI today):
  `ConfigProfileStoreTests`, `DataSourceEnumeratorTests`
  (`[.integration]`), `SecretResolverTests` (file + env backends
  tested fully; native backends behind opt-in flag).
- **Qt model tests** (`QTest`): `MigrationListModelTest`,
  `ReleaseListModelTest`, `MigrationRunnerTest`. Models are driven by
  an in-memory `MigrationManager` seeded from the existing
  `dummy_migration_plugin` test plugins.
- **QML tests** (`qmltestrunner`): `tst_ReleaseGroup.qml`,
  `tst_MigrationView.qml`. Cover tri-state behaviour, collapse/expand,
  filter/search, and the global expand/select-all bulk actions.

CI matrix: each existing job remains; add
`ubuntu-latest + Qt 6.7`, `windows-latest + Qt 6.7`,
`macos-latest + Qt 6.7` jobs that build with `LIGHTWEIGHT_BUILD_GUI=ON`
and run the three test layers. All other jobs keep `=OFF`.

---

## 9. Packaging & deployment

- **Windows**: `windeployqt` produces a folder layout with `qt*.dll`,
  QML plugins, ODBC driver manager (system), and the executable. Ship
  as a .zip for customers; an MSIX installer is a later project.
- **macOS**: `macdeployqt` produces `dbtool-gui.app`.
  Notarisation + code signing require an Apple Developer ID — decide
  at release time.
- **Linux**: recommend distro packaging; in the meantime an AppImage
  via `linuxdeploy` is straightforward but not part of phase 5.

---

## 10. Open decisions

Decisions already made are marked **RESOLVED**; the remainder still need
an explicit call.

| # | Decision | Call | Notes |
|---|---|---|---|
| 1 | GUI location | **RESOLVED** — `src/tools/dbtool-gui/` | Sits alongside `dbtool` and the other Lightweight tools; gated by `LIGHTWEIGHT_BUILD_GUI`. |
| 2 | Multi-connection UI | **RESOLVED** — single profile at a time for v1 | Simplifies `MigrationManager` usage (keeps singleton). Multi-connection can be added later by lifting `AppController` per tab without UI churn. |
| 3 | Team-shared profile files | **RESOLVED** — out of scope | Profiles stay per-user in `~/.config/dbtool/dbtool.yml`; no committed-to-repo format. |
| 4 | Credentials-file encryption | cleartext, mode 0600 (parity with `.pgpass`) | Matches Linux/POSIX ecosystem. Opt-in encrypted mode is easy to add later. |
| 5 | Secret backend impl | **RESOLVED** — QtKeychain where it helps + native/env/file fallbacks | QtKeychain covers Windows Credential Manager, macOS Keychain, and Linux Secret Service in one dep; `env:` / `file:` / `stdin:` fallbacks cover headless/CI. Adds vcpkg dep `qtkeychain`. Note: this brings Qt into the secret path used by the GUI only — `dbtool` keeps a Qt-free fallback chain. |
| 6 | Qt minimum version | 6.5 LTS | Widely installed; `qt_add_qml_module` has been stable since 6.2, `qt_standard_project_setup` since 6.3. |
| 7 | Backup/restore UI in v1 | **RESOLVED** — in | A fourth toolbar action + a small dialog wired to `SqlBackup` and dbtool's restore path. Covered by a new backup/restore view-model in Phase 2 and UI work in Phase 3. |
| 8 | Installer packaging in v1 | out | First ship is a `windeployqt` zip / `macdeployqt` `.app` bundle. |

---

## Shipped state (end of phase 4)

- CMake scaffolding and Qt auto-probe: **landed**. `LIGHTWEIGHT_BUILD_GUI=ON`
  with Qt 6.5+ installed in a standard location builds out of the box; with
  Qt absent the option is silently downgraded.
- Shared library components (`Lightweight::Config::ProfileStore`,
  `Lightweight::Odbc::DataSourceEnumerator`,
  `Lightweight::Secrets::SecretResolver` + `env:` / `file:` / `stdin:`
  backends): **landed**. `dbtool --profile <name>` and
  `dbtool resolve-secret <ref>` both work; legacy single-profile YAML still
  loads byte-identically.
- Qt view-model layer (`AppController`, `MigrationRunner`, `BackupRunner`,
  `QmlProgressManager`, four `QAbstractListModel` subclasses): **landed**.
- QML UI (`Main`, `MigrationView`, `ReleaseGroup`, `MigrationRow`,
  `StatusCard`, `ReleasesSummary`, `ActionsPanel`, `LogPanel`,
  `ConnectionPanel`, `BackupRestoreDialog`): **landed** — the reusable
  module is published as `Lightweight.Migrations 1.0`.
- Catch2 unit tests for ProfileStore / SecretResolver (16 cases /
  128 assertions) + tagged-integration tests for DataSourceEnumerator: **landed**.
- Windows MSVC / clang-cl build verified; GUI launches headless via
  `QT_QPA_PLATFORM=offscreen` and the placeholder Phase-0 QML window is
  replaced by the full three-pane layout from the mockup.

### Deferred to a phase-4 follow-up

- `QtKeychainBackend` — requires adding `qtkeychain` to `vcpkg.json`. The
  placeholder header `src/tools/dbtool-gui/QtKeychainBackend.hpp`
  documents the intended wiring. `env:` / `file:` / `stdin:` cover CI and
  headless runners; no functional block.
- QML component tests (`qmltestrunner`) — the C++ view-model is covered by
  compilation plus offscreen smoke runs; a dedicated QML test suite lands
  with the CI matrix below.
- GitHub Actions matrix jobs (`ubuntu + Qt 6.7`, `windows + Qt 6.7`,
  `macos + Qt 6.7`) running `ctest -L gui-vm / gui-qml / gui-e2e`. The
  design in §8 is executable as soon as the team agrees on the
  `install-qt-action` caching strategy.

---

## 11. Risks & mitigations

- **Qt not installed on developer machine** → `FindQt` falls back
  to a friendly error; `LIGHTWEIGHT_BUILD_GUI` goes OFF automatically.
- **CI time budget** → adding three GUI-enabled jobs. Use
  `install-qt-action` caching; a cold install is ≈2 min, cached ≈20 s.
- **Singleton `MigrationManager`** → if a user wants to manage two
  databases in two windows they can't. Documented limitation; solvable
  by making `MigrationManager` constructible (currently a singleton
  accessor) — follow-up refactor.
- **ODBC DSN enumeration across platforms** → different driver managers
  return attributes in different strings. We test on Windows driver
  manager, unixODBC, and iODBC (macOS default) before Phase 3 ends.
- **libsecret on headless CI** → covered by fallback chain; no hard
  dependency on a running D-Bus.

---

## 12. Summary of deliverables (what "done" looks like)

- `cmake/FindQt.cmake` + root wiring so `cmake ..` on a machine with
  a standard Qt 6.5+ install builds the GUI with no flags.
- New library components under `src/Lightweight/Config`, `Odbc`,
  `Secrets`, used by both `dbtool` and the GUI.
- New executable `dbtool-gui` that reproduces the
  mocked-up UI end to end.
- New QML module `Lightweight.Migrations 1.0` exposing `MigrationView`,
  importable from downstream apps.
- `dbtool` accepts `--profile <name>` and reads the same
  multi-profile YAML the GUI writes.
- Per-platform secret backends with a documented fallback chain.
- CI covers all three OSes with the GUI enabled.
- README + this plan updated to reflect shipped state at end of Phase 5.
