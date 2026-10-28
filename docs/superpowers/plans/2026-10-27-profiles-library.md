# Lightweight::Profiles Library Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship an installed shared library `Lightweight::Profiles` that lets any application list, resolve and save dbtool.yml profiles exactly as dbtool / dbtool-gui see them, and encrypt/decrypt profile passwords (issue #675).

**Architecture:** A thin SHARED library in `src/tools/profiles/` whose public header uses only standard-library types. It links the existing static `tools_shared` PRIVATE and delegates to `FindConfigCandidates` + `LayeredProfileStore`, `ResolveProfilePassword`, `AddProfileToFile` and `ProfileCipher::Builtin()`. Every exported function is wrapped so no exception crosses the boundary. Separately, `Config::EditConfigFile` gains a guarded in-place write fallback.

**Tech Stack:** C++23, CMake (install/export), Catch2, yaml-cpp (private), OS crypto (private).

**Spec:** `docs/superpowers/specs/2026-10-27-profiles-library-design.md` — read it; this plan argues from it.

## Global Constraints

- Built only when `LIGHTWEIGHT_BUILD_TOOLS=ON`, in the same build tree as dbtool (same key ring). Dev-key policy unchanged: release-key builds refuse `enc:dev:`.
- Target `LightweightProfiles`, `SHARED` always (independent of `LIGHTWEIGHT_BUILD_SHARED`), export name `Profiles` under namespace `Lightweight::` → `Lightweight::Profiles`; in-tree alias `Lightweight::Profiles`.
- Public header `Lightweight/Profiles/ProfileConnection.hpp`, namespace `Lightweight::Profiles`; only standard-library types cross the boundary; must compile standalone.
- `tools_shared` linked PRIVATE; no yaml-cpp / `tools_shared` header reachable from the public header.
- Only `Lightweight::Profiles::` symbols in the dynamic export table (Linux/macOS).
- No secret (plaintext password, decrypted value, key) in any error string or log line.
- No exception crosses an exported function; `IsEncryptedPassword` is `noexcept`.
- Lookup: `std::optional<std::filesystem::path> startDirectory`; given → walk up from it (+ per-user file unless a `root: true` layer stops it); `std::nullopt` → per-user file only. `LIGHTWEIGHT_DBTOOL_CONFIG_SEARCH_DIR` is ignored.
- Product/customer names never appear in code, comments or docs.
- Project rules (AGENT.md): Doxygen on every public entity, `[[nodiscard]]` on `std::expected` returns, no C-style loops, no `NOLINT`, `clang-format` every changed file, warnings are errors, descriptive local names, no `k`-prefixed names.
- Commits end with `Signed-off-by: Christian Parpart <c.parpart@lastrada.net>`.

## Review Focus

1. A profile directory or start directory with non-ASCII characters on Windows — expected: works, and any error message is readable (no throw from `path.string()`). → Task 3 test `ListProfiles handles a non-ASCII start directory`.
2. Two layers defining the same profile name — expected: list/resolve show the nearer one; `SaveProfile` replaces it in the nearer file and leaves the farther file byte-identical. → Task 5 test `SaveProfile replaces a shadowing profile in its own layer only`.
3. A per-user file that exists but is unparsable while the start directory has a valid `root: true` layer — expected: the per-user file is never read, so no error. → Task 3 test `ListProfiles never reads past a root layer`.
4. Encrypted password in the file but written by a different key ring (unknown key id) — expected: error naming the profile and the key id, no secret. → Task 4 test `ResolveProfileConnection reports an unknown key id without the secret`.
5. `SaveProfile` with a name containing YAML-special characters (`:`, `"`, leading space) — expected: saved and read back identically or a clean error, never a corrupted file. → Task 5 test `SaveProfile round-trips a name with YAML-special characters`.

---

## File Structure

| Path | Responsibility |
|------|----------------|
| `src/tools/shared/Config/ProfileFileEditor.{hpp,cpp}` (modify) | `EditConfigFile` in-place fallback via injectable `ConfigFileOps` |
| `src/tools/shared/CMakeLists.txt` (modify) | hidden visibility for `tools_shared` |
| `src/tools/profiles/CMakeLists.txt` (new) | `LightweightProfiles` target, visibility, install |
| `src/tools/profiles/include/Lightweight/Profiles/Api.hpp` (new) | `LIGHTWEIGHT_PROFILES_API` export macro |
| `src/tools/profiles/include/Lightweight/Profiles/ProfileConnection.hpp` (new) | the public API (spec §Public API) |
| `src/tools/profiles/Guarded.hpp` (new, private) | exception → `std::unexpected` wrapper |
| `src/tools/profiles/ProfileLookup.{hpp,cpp}` (new, private) | optional start dir → `LayeredProfileStore` |
| `src/tools/profiles/Passwords.cpp` (new) | `IsEncryptedPassword` / `EncryptPassword` / `DecryptPassword` |
| `src/tools/profiles/ListProfiles.cpp` (new) | `ListProfiles` |
| `src/tools/profiles/ResolveProfileConnection.cpp` (new) | `ResolveProfileConnection` |
| `src/tools/profiles/SaveProfile.cpp` (new) | `SaveProfile` |
| `src/tools/CMakeLists.txt` (modify) | `add_subdirectory(profiles)` after `shared` |
| `src/tests/ProfilesLibraryTests.cpp` (new) | Catch2 tests for the public API, `[profiles-lib]` |
| `src/tests/ProfileFileEditorTests.cpp` (modify) | in-place fallback tests |
| `cmake/tests/ProfilesExportsTest.cmake` (new) | export-table check (POSIX) |
| `src/tests/profiles_consumer/{CMakeLists.txt,main.cpp}` (new) | out-of-tree consumer against the installed package |
| `cmake/Lightweight-config.cmake.in` (modify) | optional include of the Profiles export file |
| `docs/profiles-library.md` (new), `docs/dbtool.md` (modify) | user docs |

---

### Task 1: `EditConfigFile` in-place fallback

**Files:**
- Modify: `src/tools/shared/Config/ProfileFileEditor.hpp` (near `EditConfigFile`, ~line 104-111)
- Modify: `src/tools/shared/Config/ProfileFileEditor.cpp` (`EditConfigFile`, ~line 507 onward)
- Test: `src/tests/ProfileFileEditorTests.cpp`

**Interfaces:**
- Produces (in `Lightweight::Config`):

```cpp
/// File writes `EditConfigFile` performs, injected so tests can simulate failures.
struct ConfigFileOps
{
    /// Writes `bytes` to a new file `path` (the temporary sibling). Fails with the OS error.
    std::function<std::expected<void, std::error_code>(std::filesystem::path const& path, std::string_view bytes)> writeNewFile;
    /// Truncates the existing `path` and writes `bytes` into it.
    std::function<std::expected<void, std::error_code>(std::filesystem::path const& path, std::string_view bytes)> overwriteInPlace;
};
[[nodiscard]] ConfigFileOps DefaultConfigFileOps();
[[nodiscard]] std::expected<void, std::string> EditConfigFile(std::filesystem::path const& path,
                                                              ConfigTextTransform const& transform,
                                                              ConfigFileOps const& ops = DefaultConfigFileOps());
```

- [ ] **Step 1: Write the failing tests** (tag `[profile-file-editor]`)

  - `EditConfigFile falls back to an in-place write when the temporary file is not permitted`: existing file `a: 1\n`; `ops.writeNewFile` returns `std::make_error_code(std::errc::permission_denied)`; default `overwriteInPlace`; transform appends `b: 2\n`. REQUIRE success; file content == `"a: 1\nb: 2\n"`.
  - `EditConfigFile does not fall back on a non-permission error`: `writeNewFile` returns `std::errc::io_error`; `overwriteInPlace` records calls. REQUIRE error; zero in-place calls; file content unchanged.
  - `EditConfigFile does not fall back when the file does not exist yet`: target absent, `writeNewFile` → `permission_denied`. REQUIRE error; file still absent.
  - `EditConfigFile restores the original bytes when the in-place write fails`: `writeNewFile` → `permission_denied`; `overwriteInPlace` records every `bytes` argument and returns `std::errc::no_space_on_device` on the first call, success afterwards. REQUIRE error; recorded calls == `{edited, original}`.
  - POSIX only (`#if !defined(_WIN32)`, `SKIP` when `geteuid() == 0`): real directory chmod `0555`, file `0644`; `EditConfigFile` with default ops succeeds and content is edited. Restore directory mode in a scope guard.

- [ ] **Step 2: Run to verify failure**

  Run: `cmake --build --preset clangcl-debug --target LightweightTest && out/build/clangcl-debug/target/LightweightTest.exe "[profile-file-editor]"`
  Expected: compile error (`ConfigFileOps` undefined).

- [ ] **Step 3: Implement**

  Move the temp-file write and the in-place write into `DefaultConfigFileOps()` (binary, truncating `std::ofstream`; map a failed open to `std::error_code(errno, std::generic_category())` on POSIX, `std::errc::permission_denied` when `GetLastError()==ERROR_ACCESS_DENIED` on Windows, else `std::errc::io_error`). In `EditConfigFile`: on `writeNewFile` failure, fall back iff `exists && ec == std::errc::permission_denied || ec == std::errc::operation_not_permitted`; on in-place failure call `overwriteInPlace(path, original)` once (ignore its result) and return `"cannot write <path>: <message>; the original content was restored"`. Rename-retry path unchanged. Replace `path.string()` in this function's messages with a UTF-8 rendering (`reinterpret_cast<char const*>(path.u8string().c_str())` or an existing helper if one exists in `ProfileFileEditor.cpp`). Doxygen on `EditConfigFile` states the fallback, its loss of atomicity, and that it writes through symlinks.

- [ ] **Step 4: Run tests**

  Run: `out/build/clangcl-debug/target/LightweightTest.exe "[profile-file-editor],[profile-add],[profile-password]"`
  Expected: all pass.

- [ ] **Step 5: Commit** — `feat(dbtool): write dbtool.yml in place when its directory is not writable`

---

### Task 2: Library skeleton, password helpers, hidden symbols

**Files:**
- Create: `src/tools/profiles/CMakeLists.txt`, `include/Lightweight/Profiles/Api.hpp`, `include/Lightweight/Profiles/ProfileConnection.hpp`, `Guarded.hpp`, `Passwords.cpp`
- Create: `src/tests/ProfilesLibraryTests.cpp`, `cmake/tests/ProfilesExportsTest.cmake`
- Modify: `src/tools/CMakeLists.txt` (add `add_subdirectory(profiles)` right after `add_subdirectory(shared)`), `src/tools/shared/CMakeLists.txt`, `src/tests/CMakeLists.txt`

**Interfaces:**
- Produces: the whole public header exactly as spec §Public API (all structs and all six functions declared, each `LIGHTWEIGHT_PROFILES_API`, `[[nodiscard]]` on the `std::expected` ones, full Doxygen including the `SaveProfile` wholesale-replace warning and the empty-name rule). Only the three password functions are defined in this task; the other three are defined in Tasks 3–5 (declare them now so the header is final).
- `Api.hpp`: `LIGHTWEIGHT_PROFILES_API` = `__declspec(dllexport)` when `BUILD_LIGHTWEIGHT_PROFILES` is defined, else `__declspec(dllimport)` on MSVC; `__attribute__((visibility("default")))` on GCC/Clang.
- `Guarded.hpp` (private, `Lightweight::Profiles::detail`):

```cpp
/// Runs `body`; converts any exception into `std::unexpected` with `what()` prefixed by `context`.
template <typename Body>
auto Guarded(std::string_view context, Body&& body) noexcept -> std::invoke_result_t<Body>;
```

  (`catch (std::exception const&)` → `"<context>: <what>"`; `catch (...)` → `"<context>: unexpected error"`. Callers must never put a secret into an exception message — existing `tools_shared` code already does not.)

- [ ] **Step 1: Write the failing tests** (`[profiles-lib]`)

  - `EncryptPassword and DecryptPassword round-trip`: `auto enc = EncryptPassword("s3cr3t-Ä")`; REQUIRE `IsEncryptedPassword(*enc)`; `enc->find("s3cr3t") == npos`; `*DecryptPassword(*enc) == "s3cr3t-Ä"`.
  - `DecryptPassword of a plaintext or garbled value is an error without the input`: inputs `"plain"`, `"enc:dev:@@@"`; REQUIRE `!result`; error text does not contain `"@@@"`.
  - `IsEncryptedPassword` false for `""`, `"enc"`, `"hunter2"`.
  - `Library output matches ProfileCipher::Builtin()`: `Secrets::ProfileCipher::Builtin().Decrypt(*EncryptPassword("x")) == "x"` (proves same key ring as dbtool).

  `cmake/tests/ProfilesExportsTest.cmake` (script mode, args `-DLIB=<file>`): on Linux run `nm -D --defined-only -C`, on macOS `nm -gU` + `c++filt`; FAIL if any defined text/data symbol line does not contain `Lightweight::Profiles::` (ignore `_init`, `_fini`, `__bss_start`, `_edata`, `_end`). Register in `src/tests/CMakeLists.txt` as `add_test(NAME ProfilesExportsTest …)` only `if(UNIX)`.

- [ ] **Step 2: Run to verify failure** — build fails (`LightweightProfiles` / header missing).

- [ ] **Step 3: Implement**

  - `src/tools/profiles/CMakeLists.txt`: `add_library(LightweightProfiles SHARED …)`, `add_library(Lightweight::Profiles ALIAS LightweightProfiles)`, `EXPORT_NAME Profiles`, `OUTPUT_NAME LightweightProfiles`, `target_include_directories(PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)`, `target_compile_definitions(PRIVATE BUILD_LIGHTWEIGHT_PROFILES=1)`, `target_link_libraries(PRIVATE tools_shared)`, `cxx_std_23` PUBLIC, `CXX_VISIBILITY_PRESET hidden`, `VISIBILITY_INLINES_HIDDEN ON`; on non-Apple UNIX with GNU/LLVM linker `target_link_options(PRIVATE "LINKER:--exclude-libs,ALL")`; clang-tidy and `enable_coverage_for_target` like `tools_shared`. No install rules yet (Task 6).
  - `src/tools/shared/CMakeLists.txt`: set `CXX_VISIBILITY_PRESET hidden` and `VISIBILITY_INLINES_HIDDEN ON` on `tools_shared`.
  - `Passwords.cpp`: thin `Guarded` wrappers over `Secrets::ProfileCipher::Builtin()`; `IsEncryptedPassword` → `ProfileCipher::IsEncrypted`.
  - `src/tests/CMakeLists.txt`: add `ProfilesLibraryTests.cpp` to `LightweightTest`; link `Lightweight::Profiles`.

- [ ] **Step 4: Verify**

  Run: `LightweightTest "[profiles-lib]"` → pass. On Linux (WSL or CI) `ctest --preset clang-debug -R ProfilesExportsTest` → pass. Run the full `LightweightTest --test-env=sqlite3` and `ctest -R DbToolIntegrationTest` to prove hiding `tools_shared` symbols broke no plugin / dbtool-gui link.

- [ ] **Step 5: Commit** — `feat(profiles): add the Lightweight::Profiles shared library with password helpers`

---

### Task 3: Layer lookup and `ListProfiles`

**Files:**
- Create: `src/tools/profiles/ProfileLookup.hpp`, `src/tools/profiles/ProfileLookup.cpp` (private)
- Create: `src/tools/profiles/ListProfiles.cpp`
- Test: `src/tests/ProfilesLibraryTests.cpp`

**Interfaces:**
- Consumes: `detail::Guarded` (Task 2).
- Produces: `ListProfiles` as declared in Task 2, and (`Lightweight::Profiles::detail`, used by Tasks 4–5):

```cpp
/// The merged view an application sees for `startDirectory` (spec §Lookup).
/// @return The store (possibly empty), or an error naming the file that failed to read/parse.
[[nodiscard]] std::expected<Config::LayeredProfileStore, std::string> LoadLayers(
    std::optional<std::filesystem::path> const& startDirectory);

/// UTF-8 rendering of a path for error messages; never throws.
[[nodiscard]] std::string DisplayPath(std::filesystem::path const& path);
```

`LoadLayers` builds `Config::DiscoveryInputs{ .explicitPath = {}, .searchStart = startDirectory.value_or({}), .userDefault = Config::ProfileStore::DefaultPath() }` directly — do **not** call `DefaultDiscoveryInputs`, which reads `LIGHTWEIGHT_DBTOOL_CONFIG_SEARCH_DIR` — then `FindConfigCandidates` → `LayeredProfileStore::Load`.

Test fixture (shared by Tasks 3–5, define once at the top of the test file): a `UniqueTempPath` root with `root/app/sub/` and `root/user/`; `ScopedEnvironmentVariable(Config::ProfileStore::UserConfigDirectoryVariable, root/"user")`; helper `WriteFile(path, text)`.

- [ ] **Step 1: Write the failing tests**

  - `ListProfiles returns an empty list when no file exists`: `ListProfiles(root/"app"/"sub")` → success, `profiles.empty()`, `configFiles.empty()`, `newProfileTarget == root/"user"/"dbtool.yml"`.
  - `ListProfiles merges layers nearest first and appends the per-user file`: `app/dbtool.yml` has `p1` (dsn `D1`), `user/dbtool.yml` has `p1` (dsn `U1`) and `p2`; from `app/sub`: `configFiles == {app/dbtool.yml, user/dbtool.yml}`, `p1.dsn == "D1"`, `p1.configFile == app/dbtool.yml`, `p2.configFile == user/dbtool.yml`.
  - `ListProfiles never reads past a root layer` (Review Focus 3): `app/dbtool.yml` has `root: true`; `user/dbtool.yml` is `"{{{ not yaml"`; success, `configFiles == {app/dbtool.yml}`, `newProfileTarget == app/dbtool.yml`.
  - `ListProfiles with no start directory reads only the per-user file`: `app/dbtool.yml` and `user/dbtool.yml` both exist; `ListProfiles(std::nullopt)` lists only the user file's profiles.
  - `ListProfiles ignores LIGHTWEIGHT_DBTOOL_CONFIG_SEARCH_DIR`: set it to `app`; `ListProfiles(std::nullopt)` still lists only user profiles.
  - `ListProfiles never returns a password`: profile with `password: "plain-secret"`, another with `connectionString: "DRIVER=x;UID=u;PWD=cs-secret"`, another with `secretRef: "env:X"`, another with none. `hasPassword` == true, true, true, false; no field of any summary contains `plain-secret` or `cs-secret`; `connectionString` still contains `DRIVER=x`.
  - `ListProfiles reports defaultProfile`: nearer layer sets `defaultProfile: p2` → `defaultProfile == "p2"`.
  - `ListProfiles reports an unparsable file by name`: `app/dbtool.yml` = `"{{{"` → error contains `dbtool.yml`.
  - `ListProfiles handles a non-ASCII start directory` (Review Focus 1): start dir `root/"Ämter-数据"` with a profile; success and the profile is listed.
  - `ListProfiles accepts a relative or missing start directory`: a non-existent `root/"nope"` → success (per-user profiles only).

- [ ] **Step 2: Run** `LightweightTest "[profiles-lib]"` → new tests fail (unresolved `ListProfiles`).
- [ ] **Step 3: Implement** `ListProfiles(std::optional<std::filesystem::path> const&) -> std::expected<ProfileList, std::string>`: `Guarded`; `LoadLayers`; `configFiles` from `Layers()`; `newProfileTarget = store.NewProfileTarget(ProfileStore::DefaultPath())`; `defaultProfile = Merged().DefaultProfileName()` (the raw value — **not** `Default()`, which falls back to the first profile); profiles in `Merged().Profiles()` order (the order `dbtool list-profiles` prints); `connectionString` via `SqlConnectionString::SanitizePwd`; `hasPassword = !password.empty() || !secretRef.empty() || sanitized != original`; `configFile = OriginOf(name)->path`.
- [ ] **Step 4: Run** `LightweightTest "[profiles-lib]"` → pass.
- [ ] **Step 5: Commit** — `feat(profiles): list the layered dbtool.yml profiles`

---

### Task 4: `ResolveProfileConnection`

**Files:**
- Create: `src/tools/profiles/ResolveProfileConnection.cpp`
- Test: `src/tests/ProfilesLibraryTests.cpp`

**Interfaces:**
- Consumes: `detail::LoadLayers`, `detail::Guarded`, `Config::ResolveProfilePassword`, `Secrets::MakeNonInteractiveResolver()`, `Secrets::ProfileCipher::Builtin()`.
- Produces: `ResolveProfileConnection` as declared in Task 2.

- [ ] **Step 1: Write the failing tests**

  - `ResolveProfileConnection decrypts an encrypted password`: write the profile via `Config::AddProfileToFile` (dsn `DSN1`, uid `u`, password `pw-Ä`); result `.name == "p1"`, `.dsn == "DSN1"`, `.uid == "u"`, `.password == "pw-Ä"`, `.configFile` == the file.
  - `ResolveProfileConnection returns a plaintext password unchanged and leaves the file untouched`: file bytes identical before/after.
  - `ResolveProfileConnection resolves secretRef from the environment`: `secretRef: "env:LW_PROFILES_TEST_PWD"` + `ScopedEnvironmentVariable` → password matches.
  - `ResolveProfileConnection uses defaultProfile for an empty name`: `defaultProfile: p2` → `.name == "p2"`.
  - `ResolveProfileConnection errors when no default is set` and `… when defaultProfile names a missing profile`: two profiles, no/dangling `defaultProfile`, empty name → error; error does not equal success for the first profile.
  - `ResolveProfileConnection resolves a legacy single-profile file by default`: legacy-format file (top-level `DSN:`/`UID:`/`Password:`), empty name → `.name == "default"`.
  - `ResolveProfileConnection errors for an unknown profile` → error contains the name and `dbtool.yml`.
  - `ResolveProfileConnection errors for a profile without dsn or connection string`.
  - `ResolveProfileConnection reports an unknown key id without the secret` (Review Focus 4): `password: "enc:zz:<valid-looking payload>"` → error contains `p1` and does not contain the payload.
  - `ResolveProfileConnection returns an error instead of throwing for a world-readable secret file` (POSIX only): `secretRef: "file:<0644 file>"` → `!result` (no exception escapes).
  - `ResolveProfileConnection returns the connection string unsanitised`: `connectionString` with `PWD=x` round-trips exactly.

- [ ] **Step 2: Run** → new tests fail.
- [ ] **Step 3: Implement** `ResolveProfileConnection(std::optional<std::filesystem::path> const&, std::string_view profileName) -> std::expected<ProfileConnection, std::string>`: empty name → `Merged().DefaultProfileName()` (never `Default()`); empty or not found → `"no profile name given and no defaultProfile set in <files>"` / `"profile '<name>' not found in <files>"` (files via `DisplayPath`, or `"<per-user path> (not found)"` when no layer). Reject `!HasConnection()`. Password via `ResolveProfilePassword(profile, ProfileCipher::Builtin(), MakeNonInteractiveResolver())`; map its error unchanged (already secret-free). No `UpgradePlaintextPassword`.
- [ ] **Step 4: Run** `LightweightTest "[profiles-lib]"` → pass.
- [ ] **Step 5: Commit** — `feat(profiles): resolve a profile's connection with its password`

---

### Task 5: `SaveProfile`

**Files:**
- Create: `src/tools/profiles/SaveProfile.cpp`
- Test: `src/tests/ProfilesLibraryTests.cpp`

**Interfaces:**
- Consumes: `detail::LoadLayers`, `detail::Guarded`, `Config::AddProfileToFile`, `LayeredProfileStore::WriteTargetFor`.
- Produces: `SaveProfile` as declared in Task 2.

- [ ] **Step 1: Write the failing tests**

  - `SaveProfile then ListProfiles and ResolveProfileConnection see the profile`: no files; `SaveProfile(app/sub, {name "n", dsn "D", uid "u", password "pw"})` returns `root/"user"/"dbtool.yml"`; list contains `n`; resolve gives `pw`; raw file text does not contain `pw`.
  - `SaveProfile writes a new profile into the root layer`: `app/dbtool.yml` has `root: true` → returned path == `app/dbtool.yml`.
  - `SaveProfile replaces a shadowing profile in its own layer only` (Review Focus 2): `p1` in both `app` and `user`; save `p1` from `app/sub` → returns `app/dbtool.yml`; `user/dbtool.yml` bytes unchanged; resolve shows new dsn.
  - `SaveProfile replaces wholesale like add-profile --force`: existing `p1` with `schema`, `pluginsDir`, `backupDir`, `password`; save `p1` with only dsn → those fields are gone and `hasPassword == false`.
  - `SaveProfile keeps comments and other profiles byte for byte`: file with a comment line and `p2`; after saving `p1`, the comment line and `p2`'s block are present verbatim.
  - `SaveProfile moves a password out of the connection string`: `connectionString "DRIVER=x;PWD=cs"` → stored connection string has no `PWD`, resolve returns `cs`; with an explicit `password "ex"` as well → resolve returns `ex`.
  - `SaveProfile rejects both or neither of dsn and connection string`, and an empty name — errors, no file created.
  - `SaveProfile refuses a legacy-format target file` → error, file unchanged.
  - `SaveProfile with no start directory writes the per-user file`.
  - `SaveProfile round-trips a name with YAML-special characters` (Review Focus 5): names `"a:b"`, `"q\"uote"`, `" lead"` → each either listed back with the identical name or a clean error, and the file still parses (`ListProfiles` succeeds).
  - `SaveProfile error does not contain the password`: force a failure (read-only target via `fs::permissions` removing write, skip if it still writes) with password `"leak-me"` → error lacks `leak-me`.

- [ ] **Step 2: Run** → new tests fail.
- [ ] **Step 3: Implement** `SaveProfile(std::optional<std::filesystem::path> const&, ProfileDefinition const&) -> std::expected<std::filesystem::path, std::string>`: `LoadLayers`; `target = store.WriteTargetFor(definition.name, {}, ProfileStore::DefaultPath())`; `AddProfileToFile(target, ProfileRequest{name, connectionString, dsn, uid, {}, {}, password, {}}, ReplaceExisting::Yes, MakeDefault::No, ProfileCipher::Builtin())`; return `target`.
- [ ] **Step 4: Run** `LightweightTest "[profiles-lib]"` and `"[profile-add],[profile-file-editor]"` → pass.
- [ ] **Step 5: Commit** — `feat(profiles): save a profile with its password encrypted`

---

### Task 6: Install, package export, consumer test, docs

**Files:**
- Modify: `src/tools/profiles/CMakeLists.txt`, `cmake/Lightweight-config.cmake.in`, `src/tests/CMakeLists.txt`
- Create: `src/tests/profiles_consumer/CMakeLists.txt`, `src/tests/profiles_consumer/main.cpp`
- Create: `docs/profiles-library.md`; Modify: `docs/dbtool.md` (link + in-place write note), `AGENT.md` component map (one row for `src/tools/profiles/`)

**Interfaces:**
- Consumes: the installed `Lightweight::Profiles`.
- Produces: `find_package(Lightweight CONFIG)` → `Lightweight::Profiles` when built with tools; package still valid without it.

- [ ] **Step 1: Write the failing test** — `src/tests/profiles_consumer/`: standalone project `cmake_minimum_required`, `project(ProfilesConsumer CXX)`, `find_package(Lightweight CONFIG REQUIRED)`, `add_executable(profiles_consumer main.cpp)` linking **only** `Lightweight::Profiles`. `main.cpp` includes only `<Lightweight/Profiles/ProfileConnection.hpp>`, round-trips `EncryptPassword`/`DecryptPassword`, calls `ListProfiles(std::nullopt)` and returns non-zero on failure. Register `add_test(NAME ProfilesPackageTest …)` running a `cmake -P` script that (1) `cmake --install <build> --prefix <build>/profiles-pkg-test --component Development` and `--component Runtime`, (2) `ctest --build-and-test` the consumer with `-DCMAKE_PREFIX_PATH=<prefix>` (+ the build's toolchain file / vcpkg prefix so transitive `find_dependency` resolve), (3) runs it with `LIGHTWEIGHT_DBTOOL_USER_CONFIG_DIR` pointed at a temp dir and the install `bin` on `PATH`.
- [ ] **Step 2: Run** `ctest --preset clangcl-debug -R ProfilesPackageTest` → fails (`Lightweight::Profiles` not found).
- [ ] **Step 3: Implement**
  - `install(TARGETS LightweightProfiles EXPORT LightweightProfiles-targets RUNTIME_DEPENDENCY_SET profiles_deps RUNTIME/LIBRARY/ARCHIVE …)` with the same component split as core `Lightweight`; `install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} COMPONENT Development)`; `install(EXPORT LightweightProfiles-targets NAMESPACE Lightweight:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Lightweight COMPONENT Development)`; Windows PDB for Debug/RelWithDebInfo; runtime-dependency bundling guarded by `WIN32 AND LIGHTWEIGHT_INSTALL_RUNTIME_DEPENDENCIES` exactly like `src/tools/dbtool/CMakeLists.txt:35-50`. Non-Windows: `VERSION`/`SOVERSION` like core.
  - `Lightweight-config.cmake.in`: after the core include, `if(NOT TARGET Lightweight::Profiles) include("${CMAKE_CURRENT_LIST_DIR}/LightweightProfiles-targets.cmake" OPTIONAL) endif()`.
  - Docs `docs/profiles-library.md`: purpose; CMake usage; the six functions with a short example; lookup rules (optional start dir, layering, `root: true`, per-user file, env var ignored); `SaveProfile` replaces wholesale (dropped fields listed) and moves the profile to the top of its layer; empty-name rule; no-secret guarantees; in-place write fallback (non-atomic, writes through symlinks); same-toolchain / matching Debug-Release requirement; runtime dependencies per platform; must be built in the same build as dbtool to share keys; security note that the library decrypts for anyone who can load it. No product/customer names.
- [ ] **Step 4: Verify**
  - `ctest --preset clangcl-debug -R "ProfilesPackageTest|ProfilesExportsTest"` → pass.
  - Configure once with `-D LIGHTWEIGHT_BUILD_TOOLS=OFF -D LIGHTWEIGHT_BUILD_TESTS=OFF`, install, and `find_package(Lightweight CONFIG REQUIRED)` from a scratch project → succeeds, no `Lightweight::Profiles`.
  - `python3 scripts/check-doc-snippets.py` → pass.
- [ ] **Step 5: Commit** — `feat(profiles): install and export Lightweight::Profiles`

---

### Task 7: Whole-branch verification

- [ ] `clang-format` on every changed `.hpp/.cpp`.
- [ ] `clangcl-debug` and `cl-debug`: build warning-free; `LightweightTest --test-env=sqlite3`; `ctest` (includes `DbToolIntegrationTest`, `ProfilesPackageTest`).
- [ ] `gcc-release` (Linux/WSL): build; `LightweightTest --test-env=sqlite3`; `ctest -R "Profiles|DbTool"`.
- [ ] `clang-debug` (Linux/WSL, clang-tidy + ASan/UBSan): build clean, `ctest -R "Profiles"`.
- [ ] `gcc-release -D LIGHTWEIGHT_BUILD_MODULES=ON` building `chinook` (sanity: no public-header change in core, should be unaffected).
- [ ] The change has no SQL/DBMS surface; run `LightweightTest --test-env=mssql2022` and `--test-env=postgres` once anyway as the regression gate AGENT.md requires, or record why a DB was skipped.
- [ ] `/simplify`, then record databases and compilers tested for the PR summary.
