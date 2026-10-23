# dbtool.yml Layering Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** dbtool and dbtool-gui load and merge every `dbtool.yml` from the executable's directory upward
(nearest wins), stop at `root: true`, add the per-user file only when no root was found, and route each
write to the file that owns the value.

**Architecture:** `ConfigDiscovery` stops choosing a file and instead lists candidate files (explicit
file, or every ancestor of the executable directory nearest-first, then the user default).
A new `LayeredProfileStore` parses candidates in order, stops after a `root: true` layer, merges them into
one `ProfileStore`, and remembers each profile's and each store-wide value's origin file. Both tools load
through it and ask it where a write goes; all writes keep using the comment-preserving `ProfileFileEditor`
helpers (`AddProfileToFile`, `UpgradePlaintextPassword`) against a single file.

**Tech Stack:** C++23, yaml-cpp, Catch2 (`LightweightTest`), Qt 6 (dbtool-gui + its tests), Python
(`src/tests/test_dbtool.py`, `src/tests/test_dbtool_gui.py`).

**Spec:** `docs/superpowers/specs/2026-10-22-dbtool-config-layering-design.md` — read it before any task.

## Global Constraints

- Work in a git worktree on branch `feature/dbtool-config-layering`; one branch, one PR, CI green.
- Commits end with `Signed-off-by: Christian Parpart <christian@parpart.family>`.
- C++23; fallible API returns `std::expected<T, std::string>` (the Config module's existing error type).
- Doxygen (`///`, `@param`, `@return`) on every new public function, class, struct and member.
- `[[nodiscard]]` on queries and `std::expected`-returning functions. No C-style loops. No `NOLINT`.
- `clang-format` every touched `.hpp`/`.cpp`; build `clang-debug` (clang-tidy, ASan, UBSan) warning-free.
- New environment variable name, exactly: `LIGHTWEIGHT_DBTOOL_CONFIG_SEARCH_DIR`.
- New top-level YAML key, exactly: `root` (YAML boolean; any non-boolean value is a load error naming the file).
- No working-directory discovery anywhere after Task 4; `ConfigSource::WorkingDirectory` is deleted.
- Never call `ProfileStore::Save` on a merged store.
- Before the PR: `LightweightTest` on `sqlite3`, `mssql2022`, `postgres` (Docker harness), plus one run
  under `gcc-release`; `test_dbtool.py` per DB; `ctest -L dbtool-gui` (headless Qt).

## Review Focus

1. **A stray `dbtool.yml` higher up** (e.g. in `/opt` or `$HOME`) is now merged in silently → `list-profiles`
   must list every loaded file, nearest first, with the file each profile came from (Task 3).
2. **`add-profile --name X` without `--force` when `X` exists in another layer** → error naming that layer's
   file; never write a second, shadowed `X` into the target (Tasks 3, 4).
3. **Relative `defaultPluginsDir: ./plugins` in the file next to the binary** → resolves against that file's
   directory, not the root file's and not the CWD (Task 2).
4. **The origin file is read-only during password encryption** → CLI warns on stderr and the command
   continues; GUI shows the `lastWarning` banner and stays connected (Tasks 3, 4).
5. **A legacy single-profile layer** (top-level `ConnectionString`) merged with modern layers → its synthetic
   `default` profile and `defaultProfile: default` take part in nearest-wins like any other values (Task 2).

---

### Task 1: Candidate discovery and the `root` key

**Files:**
- Modify: `src/tools/shared/Config/ConfigDiscovery.hpp`, `ConfigDiscovery.cpp`
- Modify: `src/tools/shared/Config/ProfileStore.hpp`, `ProfileStore.cpp`
- Test: `src/tests/ConfigDiscoveryTests.cpp`, `src/tests/ConfigProfileStoreTests.cpp`

**Interfaces:**
- Produces (ConfigDiscovery.hpp, namespace `Lightweight::Config`):
  - `inline constexpr std::string_view ConfigSearchDirectoryVariable = "LIGHTWEIGHT_DBTOOL_CONFIG_SEARCH_DIR";`
  - `struct ConfigCandidate { std::filesystem::path path; ConfigSource source; };`
  - `[[nodiscard]] std::expected<std::vector<ConfigCandidate>, std::string> FindConfigCandidates(DiscoveryInputs const& inputs);`
    - explicit path set → exactly `{ {explicitPath, Explicit} }`, or the existing "Config file not found: …" error;
    - otherwise every regular file `dbtool.yml` from `executableDirectory` up to the filesystem root,
      nearest first, source `ExecutableDirectory`; then `userDefault` with source `UserDefault` if it is a
      regular file **and** not already in the list (compare `weakly_canonical`);
    - empty vector = nothing found.
  - `DefaultDiscoveryInputs` sets `executableDirectory` from `$LIGHTWEIGHT_DBTOOL_CONFIG_SEARCH_DIR` when it is
    set and non-empty (relative → `std::filesystem::absolute`), else `ExecutableDirectory()`.
  - `FindConfigFile`, `DiscoveryInputs::workingDirectory` and `ConfigSource::WorkingDirectory` stay **unchanged**
    in this task (callers migrate in Tasks 3–4; Task 4 deletes them).
- Produces (ProfileStore.hpp): `[[nodiscard]] bool IsRoot() const noexcept;` — true when the parsed file had
  `root: true`. Parsed before the legacy-shape branch, so legacy files may carry it too. `Save` does not emit it.

- [ ] **Step 1: Write failing tests** in `ConfigDiscoveryTests.cpp` (reuse `TempTree`, `StrayConfigAboveTemp`):
  - `"ConfigDiscovery — candidates are every file above the executable directory, nearest first"`:
    files in `a` and `a/b`, exe dir `a/b/c` → candidates `== { a/b/dbtool.yml, a/dbtool.yml }`, both `ExecutableDirectory`
    (skip when `StrayConfigAboveTemp()`).
  - `"ConfigDiscovery — the user default is appended last"`: one file in `exe`, user default in `user` → 2 candidates,
    last is `{userDefault, UserDefault}`.
  - `"ConfigDiscovery — the user default is not listed twice"`: exe dir == user default's directory → 1 candidate.
  - `"ConfigDiscovery — an explicit path is the only candidate"`: files above exe dir too → exactly `{explicit, Explicit}`.
  - `"ConfigDiscovery — no candidates when nothing exists"` → `REQUIRE(result); CHECK(result->empty())`.
  - `"ConfigDiscovery — the search directory variable replaces the executable directory"`: set the variable (use
    `ScopedEnvironmentVariable` as in `ConfigProfileStoreTests.cpp:736`) to `tree.Dir("x")` →
    `DefaultDiscoveryInputs().executableDirectory == tree.Dir("x")`.
  In `ConfigProfileStoreTests.cpp`:
  - `"ProfileStore — root: true is recognised"` → `Parse("root: true\nprofiles: {}\n", p)->IsRoot()`; absent → false;
    `"root: false"` → false; legacy `"root: true\nConnectionString: \"DSN=x\"\n"` → `IsRoot()` and has profile `default`.
  - `"ProfileStore — a non-boolean root is an error naming the file"` → `Parse("root: maybe\n", p)` fails and
    `error().contains(p.string())`.

- [ ] **Step 2: Run, expect compile failure / FAIL**
  Run: `cmake --build --preset clang-debug --target LightweightTest && ./out/build/clang-debug/src/tests/LightweightTest "[ConfigDiscovery],[ProfileStore]"`

- [ ] **Step 3: Implement** `FindConfigCandidates`, the variable handling in `DefaultDiscoveryInputs`, and `root`
  parsing (`node.as<bool>()` inside try/catch → error `"invalid 'root' in <file>: expected true or false"`).

- [ ] **Step 4: Run the same command, expect PASS** (whole `LightweightTest --test-env=sqlite3` still green).

- [ ] **Step 5: Commit** `feat(dbtool): list dbtool.yml candidates above the executable; parse root: true`

---

### Task 2: `LayeredProfileStore` — load, merge, origins, write targets

**Files:**
- Create: `src/tools/shared/Config/LayeredProfileStore.hpp`, `LayeredProfileStore.cpp`
- Modify: `src/tools/shared/CMakeLists.txt` (add both files next to `ProfileStore.*`)
- Create: `src/tests/ConfigLayeredStoreTests.cpp`; Modify: `src/tests/CMakeLists.txt` (register it)

**Interfaces:**
- Consumes: `ConfigCandidate`, `ConfigSource` (Task 1); `ProfileStore::Parse`, `IsRoot()`.
- Produces (namespace `Lightweight::Config`):
  ```cpp
  /// One file that took part in the merge.
  struct ConfigLayer { std::filesystem::path path; ConfigSource source; bool isRoot = false; };

  /// Reads a whole file; injected so dbtool-gui can fingerprint the bytes it parsed.
  using ConfigFileReader = std::function<std::expected<std::string, std::string>(std::filesystem::path const&)>;

  /// Reads files from disk (the default reader).
  [[nodiscard]] std::expected<std::string, std::string> ReadConfigFile(std::filesystem::path const& path);

  class LayeredProfileStore {
  public:
      [[nodiscard]] static std::expected<LayeredProfileStore, std::string>
          Load(std::span<ConfigCandidate const> candidates, ConfigFileReader const& reader = ReadConfigFile);
      [[nodiscard]] ProfileStore const& Merged() const noexcept;
      [[nodiscard]] std::span<ConfigLayer const> Layers() const noexcept;      // loaded only, nearest first
      [[nodiscard]] bool Empty() const noexcept;                                 // no layer loaded
      [[nodiscard]] bool RootFound() const noexcept;
      [[nodiscard]] ConfigLayer const* OriginOf(std::string_view profileName) const noexcept;  // nullptr if unknown
      [[nodiscard]] ConfigLayer const* DefaultProfileOrigin() const noexcept;    // layer that set defaultProfile
      [[nodiscard]] std::filesystem::path NewProfileTarget(std::filesystem::path const& userDefault) const;
      [[nodiscard]] ConfigLayer const* NearerLayerSettingDefault(std::filesystem::path const& target) const noexcept;
  };
  ```
- Semantics (from the spec — exact):
  - `Load` reads candidates in order; a read or parse error fails the whole load with the error naming the file;
    after a layer with `IsRoot()` stop (that layer is included). An `Explicit` candidate is loaded alone; its
    `root` is accepted and has no effect.
  - Merge, nearest first: a profile name is taken from the first (nearest) layer that defines it and later
    same-named profiles are ignored; merged profile order = layer order, then file order. `defaultProfile`,
    `defaultPluginsDir`, `defaultBackupDir` each come from the nearest layer that sets them (non-empty), whole —
    `defaultPluginsDir` lists are never concatenated. Relative paths are already resolved by `Parse(…, origin)`.
  - `NewProfileTarget(userDefault)`: the root layer's path if `RootFound()`, else `userDefault`.
  - `NearerLayerSettingDefault(target)`: the `DefaultProfileOrigin()` if it is a layer nearer than `target`
    (lower index; a target not among the layers counts as farthest), else `nullptr`.

- [ ] **Step 1: Write failing tests** in `ConfigLayeredStoreTests.cpp`, driving `Load` with an in-memory reader
  (a `std::map<path, std::string>`) so no filesystem is needed. Paths below are illustrative absolute paths
  (`/opt/acme/app-a/dbtool.yml` = `near`, `/opt/acme/dbtool.yml` = `far`, `/home/u/dbtool.yml` = `user`):
  - `"LayeredProfileStore — the deployment layout merges plugins and shared profiles"`: near
    `defaultPluginsDir: ./plugins`; far `root: true` + profiles `p1`,`p2` + `defaultPluginsDir: /shared`; user has
    profile `u` → `Merged().DefaultPluginsDir() == { "/opt/acme/app-a/plugins" }` (Review Focus 3), profiles
    `{p1,p2}`, `Layers().size() == 2`, `RootFound()`, `OriginOf("p1")->path == far`, user `u` absent.
  - `"… without root the user file is the last layer"`: same minus `root` → 3 layers, `u` present, `OriginOf("u")->source == UserDefault`.
  - `"… the nearest same-named profile wins whole"`: near `p1: {dsn: A}`, far `p1: {dsn: B, schema: s}` →
    `Find("p1")->dsn == "A"` and `schema.empty()`; `OriginOf("p1")->path == near`.
  - `"… store-wide values come from the nearest layer setting them"`: far sets `defaultProfile: p2` and
    `defaultBackupDir`, near sets only `defaultBackupDir` → default `p2` from far, backup dir from near;
    `DefaultProfileOrigin()->path == far`.
  - `"… a legacy layer takes part"` (Review Focus 5): near legacy `ConnectionString: "DSN=x"`, far `profiles: {p1: …}`
    → profiles `{default, p1}`, `DefaultProfileName() == "default"`, origin of `default` is near.
  - `"… a broken layer fails the load naming it"`: far is `"profiles: [unclosed"` → `!Load(...)`, error contains far.
  - `"… an explicit file is loaded alone"`: single `Explicit` candidate with `root: true` → 1 layer, `Merged()` has its profiles.
  - `"… NewProfileTarget is the root file, else the user file"`: with root → far; without → the passed `userDefault`
    even when other layers exist; no layers → `userDefault`.
  - `"… NearerLayerSettingDefault"`: near sets `defaultProfile`, target far → returns near; target near → nullptr;
    target not a layer → near.
  - `"… ReadConfigFile reports a missing file"` (filesystem, temp dir) → error contains the path.

- [ ] **Step 2: Build and run, expect FAIL:** `./out/build/clang-debug/src/tests/LightweightTest "[LayeredProfileStore]"`

- [ ] **Step 3: Implement** `LayeredProfileStore.{hpp,cpp}`. Store per-profile origin as a layer index
  (`std::vector<std::size_t>` parallel to merged profiles, or a name→index map); `ConfigLayer const*` results point
  into the layer vector. Build `Merged()` with the existing `ProfileStore` setters/`Upsert`.

- [ ] **Step 4: Run, expect PASS.**

- [ ] **Step 5: Commit** `feat(dbtool): merge layered dbtool.yml files with nearest-wins and origins`

---

### Task 3: dbtool CLI on layers (load, list, writes, docs, integration tests)

**Files:**
- Modify: `src/tools/dbtool/main.cpp` — `LoadedConfig`/`LoadConfig` (~L529-563), `SelectProfile`, `ResolveConnectionTargetOptions`,
  `EncryptPlaintextPasswordIfConnectable` (~L781), `ListProfiles` (~L1255-1380), `AddProfileCommand` (~L1388-1450), help text (~L260-266)
- Modify: `src/tests/test_dbtool.py` — section 12b (the `dbtool_in_tree`/`cwd=` discovered-target suite, ~L540-680), 12c (~L1520)
- Modify: `docs/dbtool.md` — "Where dbtool finds `dbtool.yml`" (~L124-140), "Profile passwords" (~L236-250),
  `add-profile` (~L721), `list-profiles` sample (~L305), `--config` default column (~L951), L86-112 wording

**Interfaces:**
- Consumes: `FindConfigCandidates`, `DefaultDiscoveryInputs`, `LayeredProfileStore` (Tasks 1–2).
- Produces: `LoadedConfig { LayeredProfileStore layers; }`; `options.resolvedConfigPath` = `OriginOf(selected)->path`;
  `options.configSource` = that layer's `source` (so `ConnectionTarget` and refusals behave as before).

- [ ] **Step 1: Update `test_dbtool.py` first (failing):**
  - 12b and every other case relying on `cwd=` discovery: pass `env=dict(env, LIGHTWEIGHT_DBTOOL_CONFIG_SEARCH_DIR=nested)`
    (or the directory the case used as `cwd`) instead; keep all assertions (refusal must still name `cfg_path`).
  - Replace 12c with `"--- 12c. layered dbtool.yml discovery ---"`, using a temp `LIGHTWEIGHT_DBTOOL_USER_CONFIG_DIR`
    and `LIGHTWEIGHT_DBTOOL_CONFIG_SEARCH_DIR=<tmp>/acme/app-a`:
    - `<tmp>/acme/dbtool.yml` = `root: true` + profile `shared`; `<tmp>/acme/app-a/dbtool.yml` = `defaultPluginsDir: ./plugins`;
      user file = profile `personal`.
    - `list-profiles` stdout contains `shared`, both layer paths in nearest-first order, `(root)`, and not `personal`.
    - Remove `root: true` → `personal` appears and the user file is listed last.
    - Running from a cwd that holds its own `dbtool.yml` with profile `cwdonly` → `cwdonly` absent (CWD is ignored).
  - New `"--- 12e. writes go to the owning dbtool.yml ---"` (SQLite target, `--no-password`/`input=` as 12d does):
    - with root: `add-profile --name n1 …` → `n1` written into `<tmp>/acme/dbtool.yml`; app-a file byte-identical.
    - without root: `add-profile --name n2 …` → written into the user file.
    - `add-profile --name shared …` (no `--force`) → non-zero exit, stderr names `<tmp>/acme/dbtool.yml` (Review Focus 2).
    - `add-profile --name n3 --set-default …` while app-a sets `defaultProfile: x` → stderr warns that the default is
      shadowed by the app-a file.
    - profile `pw` with plaintext `password:` in the root file; run `dbtool --profile pw status` → root file now has `enc:`.
      Then `chmod` the root file read-only (skip on Windows and when `os.geteuid() == 0`), reset plaintext → command
      still exits 0 and stderr contains `Warning: could not encrypt` and the root file path (Review Focus 4).

- [ ] **Step 2: Run, expect FAIL:**
  `python3 src/tests/test_dbtool.py --dbtool out/build/clang-debug/target/dbtool --plugins-dir out/build/clang-debug/tests/plugins --test-env sqlite3`
  (adjust paths to the build tree; `find out/build/clang-debug -name dbtool -type f`).

- [ ] **Step 3: Implement in `main.cpp`:**
  - `LoadConfig`: `FindConfigCandidates(DefaultDiscoveryInputs(options.configFile))` → `LayeredProfileStore::Load`;
    `std::nullopt` when `Empty()`; `--verbose` prints `Using config: <path> (<source>[, root])` once per layer.
  - `SelectProfile`: look up in `Merged()`; the not-found error names all layer paths (comma-separated) or
    `"any dbtool.yml"`. Fill `resolvedConfigPath`/`configSource` from `OriginOf`.
  - `EncryptPlaintextPasswordIfConnectable`: unchanged logic, path = origin (already in `resolvedConfigPath`).
  - `ListProfiles`: header `Profiles (merged from N file(s)):` followed by one line per layer
    `  <path> (<ToString(source)>)` + ` (root)` when `isRoot`, then the table with a final `FILE` column holding each
    profile's origin path. The "nothing found" text drops "current and": `searched for dbtool.yml above the executable
    directory, then <userDefault>`.
  - `AddProfileCommand` (no `--config`): load layers; if `Merged().Find(name)` and not `--force` → error
    `profile '<name>' already exists in <origin>; use --force to replace it`; target = origin when it exists (and
    `--force`), else `NewProfileTarget(DefaultDiscoveryInputs().userDefault)`. After success with `--set-default`, if
    `NearerLayerSettingDefault(target)` → `Warning: <nearer> sets defaultProfile '<name>', which takes precedence over <target>.`
    With `--config` behaviour is unchanged.
  - Help text (~L265): describe the new lookup and `LIGHTWEIGHT_DBTOOL_CONFIG_SEARCH_DIR`.

- [ ] **Step 4: Update `docs/dbtool.md`** per the Files list: new lookup (executable directory upward, merge,
  nearest wins, `root: true`, user file last only without root, CWD no longer searched — flagged as a change),
  the deployment layout example from the spec, the `pluginsDir`-on-shared-profiles gotcha, where writes go, the
  search-dir variable (for tests/scripted runs), new `list-profiles` sample output.

- [ ] **Step 5: Run `LightweightTest --test-env=sqlite3` and the `test_dbtool.py` command, expect PASS.**

- [ ] **Step 6: Commit** `feat(dbtool): load layered dbtool.yml; route profile writes to their owning file`

---

### Task 4: dbtool-gui on layers; remove the single-file API

**Files:**
- Modify: `src/tools/dbtool-gui/AppController.hpp`, `AppController.cpp` — `DiscoverProfileStore` (~L173), ctor discovery
  (~L412-416), `BeginStartup` log (~L419-430), `EncryptPlaintextPassword` (~L711), `loadProfiles` (~L823-866),
  `addProfile` (~L1740-1790), `ProfileTargetPath` (~L1664), `LoadProfileStore`/fingerprints (~L1810-1840),
  `HandleProfileFileChange` (~L1850)
- Modify: `src/tools/dbtool-gui/qml/SettingsPage.qml` (placeholder → layer list)
- Modify: `src/tools/dbtool-gui/tests/TestIsolation.hpp` (also set `LIGHTWEIGHT_DBTOOL_CONFIG_SEARCH_DIR` to the sandbox)
- Modify: `src/tests/test_dbtool_gui.py` (~L166-201: it relies on `cwd=sandbox`; set the search-dir variable instead)
- Test: `src/tools/dbtool-gui/tests/AppControllerTests.cpp`
- Modify: `src/tools/shared/Config/ConfigDiscovery.{hpp,cpp}` — delete `FindConfigFile`, `DiscoveredConfig`,
  `DiscoveryInputs::workingDirectory`, `ConfigSource::WorkingDirectory` (+ its `ToString` case); update the file comment
- Modify: `src/tests/ConfigDiscoveryTests.cpp`, `src/tests/ConfigConnectionTargetTests.cpp` (drop tests of deleted API)
- Modify: `src/tools/dbtool-gui/README.md` ("Configuration file and passwords" ~L302, "Running" ~L319)

**Interfaces:**
- Consumes: Tasks 1–2.
- Produces (AppController): `_layers` (`Lightweight::Config::LayeredProfileStore`) replaces `_store` as the source;
  `_store` stays as `_layers.Merged()` copy or accessor (builder's choice). `_knownFingerprint` becomes
  `std::vector<ProfileFileFingerprint> _knownFingerprints` (one per layer, filled by the reader passed to `Load`).
  New `Q_PROPERTY(QStringList profileLayers READ profileLayers NOTIFY profilePathChanged)` — layer paths nearest first,
  root suffixed ` (root)`. `profilePath` = nearest layer's path (or the Settings path).
- Mode: a Settings path → one `Explicit` candidate; empty → discovered candidates, re-run on every reload.

- [ ] **Step 1: Write failing tests** in `AppControllerTests.cpp` (use the existing sandbox/`TestIsolation` helpers):
  - `"layered profile files are merged"`: sandbox `acme/dbtool.yml` (root, profiles `p1`) + `acme/app/dbtool.yml`
    (`defaultBackupDir`) with search dir `acme/app` → profile list has `p1`, `profileLayers` has 2 entries, last ends ` (root)`.
  - `"a change to any layer reloads"`: rewrite the root file adding `p2` → `profileFileChangeHandled(Reloaded)`, `p2` listed.
  - `"own write to a non-nearest layer is ignored"`: after `EncryptPlaintextPassword` on a `p1` with plaintext →
    root file contains `enc:`, app file unchanged, reaction `IgnoredOwnWrite`.
  - `"encrypting into a read-only origin warns"` (skip on Windows / root) → `lastWarning` contains the root file path;
    controller not in error state (Review Focus 4).
  - `"addProfile writes to the root file"` → returns empty, profile appears in root file; app file byte-identical.
  - `"addProfile refuses a name another layer defines"` → returned message names that layer (Review Focus 2).
  - `"addProfile with makeDefault warns when a nearer layer sets the default"` → `lastWarning` names the nearer file.
  - `"without root, addProfile writes to the user file"` → file under `TestIsolation`'s user config dir.

- [ ] **Step 2: Build and run, expect FAIL:**
  `cmake --build out/build/<gui-build> --target dbtool-gui-tests && ctest --test-dir out/build/<gui-build> -L dbtool-gui -R AppController --output-on-failure`
  (configure a Qt build as CI's `ci-gui` job does: see `.github/workflows/build.yml` ~L697-765).

- [ ] **Step 3: Implement** in `AppController`: load via `LayeredProfileStore::Load` with a reader that records each
  file's fingerprint; watch every layer path (clear and re-add on each load, as today); `HandleProfileFileChange` accepts
  any watched path and compares against that file's fingerprint; `EncryptPlaintextPassword` writes to
  `OriginOf(profile.name)->path` and on failure sets `_lastWarning` (+ `emit lastWarningChanged()`) in addition to the
  log line; `addProfile` mirrors Task 3's CLI rules (duplicate → returned error, target via origin /
  `NewProfileTarget(ProfileStore::DefaultPath())`, shadowed default → `_lastWarning`). `ProfileTargetPath` is replaced by
  that rule. Startup log: `Profile store: <n> file(s): <p1>, <p2> (root) — <k> profile(s)`. Settings placeholder shows
  `profileLayers` joined by newlines.

- [ ] **Step 4: Delete the single-file API** listed under Files; fix the compile fallout; update both test files and
  `TestIsolation.hpp` and `test_dbtool_gui.py`. `grep -rn "FindConfigFile\|WorkingDirectory\|workingDirectory" src` → no hits
  in Config code.

- [ ] **Step 5: Update `src/tools/dbtool-gui/README.md`** to the layered lookup (same wording as `docs/dbtool.md`).

- [ ] **Step 6: Run** `ctest -L dbtool-gui` (headless: `QT_QPA_PLATFORM=offscreen`), `LightweightTest --test-env=sqlite3`,
  and `python3 src/tests/test_dbtool_gui.py …` as CI does → PASS.

- [ ] **Step 7: Commit** `feat(dbtool-gui): load layered dbtool.yml; drop working-directory discovery`

---

### Task 5: Full verification, PR, CI to green (controller)

- [ ] `clang-format` check over changed files; `cmake --build --preset clang-debug` with zero warnings / clang-tidy findings.
- [ ] `python3 scripts/tests/docker-databases.py --start --wait mssql2022 postgres`; run `LightweightTest` and
  `test_dbtool.py` for `sqlite3`, `mssql2022`, `postgres`.
- [ ] `cmake --preset gcc-release && cmake --build --preset gcc-release -j` and `LightweightTest --test-env=sqlite3` + `mssql2022`.
- [ ] Run `/simplify` over the branch; ask before out-of-scope fixes.
- [ ] Push `feature/dbtool-config-layering`, open one PR (body: behaviour change — CWD no longer searched; risk; DBs and
  compilers tested), then drive CI to fully green with fixes committed on the same branch.
