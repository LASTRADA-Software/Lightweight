# dbtool.yml layering — design

Date: 2026-10-22
Scope: `dbtool`, `dbtool-gui`, `src/tools/shared/Config/`

## Intent

Several `dbtool-gui` (and `dbtool`) installations must each use their **own plugin directories**
while **sharing one set of connection profiles**. Today discovery (`Config::FindConfigFile`) picks a
single `dbtool.yml` — first hit wins — so a file next to the binary that names the plugins hides the
shared profiles file above it, and vice versa. There is no way to compose the two.

Target layout:

```
/opt/acme/dbtool.yml               ← root: true; profiles shared by every installation
/opt/acme/app-a/dbtool.yml         ← defaultPluginsDir: ./plugins
/opt/acme/app-a/dbtool-gui.exe
/opt/acme/app-b/dbtool.yml         ← defaultPluginsDir: ./plugins
/opt/acme/app-b/dbtool-gui.exe
```

`app-a` sees `/opt/acme/app-a/plugins` and the shared profiles; `app-b` sees its own plugins and the
same profiles. The files are writable in this deployment.

## Goals

1. Discovery **loads and merges every `dbtool.yml`** found walking up from the executable's
   directory, instead of stopping at the first.
2. A file with top-level **`root: true`** ends discovery (as in `.editorconfig`): it is loaded,
   nothing above it is, and the per-user default file is **not** loaded either.
3. The per-user default file (`ProfileStore::DefaultPath()`, honouring
   `LIGHTWEIGHT_DBTOOL_USER_CONFIG_DIR`) is the last, lowest-precedence layer, loaded **only if no
   loaded file set `root: true`**.
4. Changes are written back to the file they belong to (rules below); a failed write warns and
   does not abort.

## Non-goals

- No include/import directive, no explicit list of files to merge.
- No field-by-field merging of profiles.
- No change to the profile file schema beyond the new `root` key.
- No change to how `--config <FILE>` or the GUI's saved profile-store path behaves.

## Anti-goals (would be a failure even if it "works")

- `app-a` picking up `app-b`'s plugins (plugin directory lists must not be concatenated across files).
- A user's hand-written file losing comments/formatting because the merged store was re-serialised
  over it.
- A write that fails on one file blocking a connect, a backup, or the UI.
- Writes landing in a different file than the one that supplied the value.

## Decided behaviour

### Discovery

- **The working-directory walk is removed.** Discovery never considers the current directory.
  This is a deliberate behaviour change (today rule 2 in `docs/dbtool.md` "Where dbtool finds
  `dbtool.yml`"); call it out in docs and the PR.
- Walk starts at the executable's directory and goes up to the filesystem root. Every regular file
  named `dbtool.yml` is a layer, ordered **nearest to the executable first** (highest precedence).
- On a layer whose top level has `root: true`, stop: that layer is included, no further ancestors,
  no user default.
- If no layer had `root: true`, append the user default file as the last layer (if it exists).
- **`--config <FILE>` / the GUI's saved path:** exactly that one file, no walk, no merging — as
  today.
- No file found at all: same outcome as today's `ConfigSource::None`.

### Merge (nearest wins)

- **Store-wide scalars** — `defaultPluginsDir`, `defaultBackupDir`, `defaultProfile`: the value from
  the nearest layer that sets the key replaces all others entirely. `defaultPluginsDir` lists are
  **not** concatenated.
- **Profiles** are keyed by name. A profile in a nearer layer replaces a same-named profile in a
  farther layer as a whole.
- **Relative paths** resolve against the file they are written in (the loader already does this via
  the `origin` argument of `ProfileStore::Parse`).
- Every merged profile and every store-wide value remembers its **origin file**.
- Existing precedence `profile.pluginsDir` > `defaultPluginsDir` is unchanged. Consequence to
  document (not enforce): a shared profile that sets its own `pluginsDir` pins every installation to
  it; shared profiles in this layout should leave `pluginsDir` unset.

### Writes

Today exactly two operations write `dbtool.yml`, in both tools: adding a profile
(`AddProfileToFile` — `dbtool add-profile`, `AppController::addProfile`) and in-place password
encryption (`UpgradePlaintextPassword` — `dbtool/main.cpp`, `AppController.cpp`). The GUI has no
profile editor and its Settings persist to QSettings, not YAML. **This work reroutes those two
writes; it adds no new write paths or UI.**

- **Encrypting a password in place / replacing an existing profile** (`add-profile` with replace)
  → the profile's origin file.
- **Adding a new profile** (decided):
  - if a loaded layer has `root: true` → that root file (the shared profiles file);
  - otherwise → the per-user file (`ProfileStore::DefaultPath()`, e.g.
    `~/.config/dbtool/dbtool.yml`), created if missing — even when layers were found above the
    executable.
- **Setting the default profile** while adding (`--set-default` / "make default") — *my call*:
  written to the **same file as the new profile**, in the same `EditConfigFile` call as today, so
  it stays one atomic edit. If a nearer layer also sets `defaultProfile`, it still wins at load;
  the CLI/GUI warns that the new default is shadowed by that file.
- Writes use the existing comment-preserving `ProfileFileEditor` (`EditConfigFile` and the `…Text`
  transforms) against that one file. Never re-serialise the merged store into any file.
- **Write failure** (read-only, permissions, I/O): the change stays in memory for the session and
  the triggering operation continues. `dbtool-gui` shows an **overlay warning** naming the file
  and the reason; `dbtool` prints a warning to stderr. For password encryption the warning states
  the password remains plaintext on disk.
- **A layer that fails to load** (unreadable, YAML parse error, non-boolean `root`) — *my call*:
  discovery/load fails with an error naming that file, exactly as a broken single file does today.
  Skipping it could silently skip the `root: true` cut-off and load the user's personal file.
- Existing safety rules still apply per file, e.g. a plaintext password in a merely *discovered*
  file is not auto-encrypted unless the profile was named via `--profile` (see `docs/dbtool.md`
  "Profile passwords"); the git-history warning still applies to the origin file.

### Visibility

- `dbtool list-profiles` shows, per profile, the file it came from, and lists the loaded layers in
  precedence order (marking the one with `root: true`).
- The GUI startup log line and Settings placeholder show the layer list instead of a single path.
- `dbtool-gui`'s live reload (`QFileSystemWatcher` in `AppController`) watches **every** loaded
  layer; a change to any of them reloads the merged store (re-running discovery). The existing "no
  echo reload" rule for the GUI's own writes applies per file. *My call:* a `dbtool.yml` newly
  created in an ancestor directory while the GUI runs is not detected until the next reload or
  restart; no directory watching.

## Constraints

- C++23 per `AGENT.md`: `std::expected` error channel, DI for filesystem inputs (keep
  `DiscoveryInputs`-style injection so tests build temp trees), Doxygen on new public API, no
  `NOLINT`, `clang-format`.
- The GUI must never block its UI thread (see `src/tools/dbtool-gui/README.md`).
- Unit tests in `src/tests/ConfigDiscoveryTests.cpp`, `ConfigProfileStoreTests.cpp`,
  `ProfileFileEditorTests.cpp`, the dbtool-gui tests, and `src/tests/test_dbtool.py` where CLI
  behaviour changes. Existing tests relying on working-directory discovery must be updated, not
  deleted silently.
- Docs: `docs/dbtool.md` ("Where dbtool finds `dbtool.yml`", "Profile passwords", `add-profile`,
  `list-profiles` output, `--config` default column) and `src/tools/dbtool-gui/README.md`
  ("Configuration file and passwords", "Running").

### Testing hook — *my call*

Without the working-directory walk, tests can only build layered trees by placing a binary in them,
and the build-tree binary would walk up through the repo and `$HOME`. Add an environment variable,
`LIGHTWEIGHT_DBTOOL_CONFIG_SEARCH_DIR`, alongside `LIGHTWEIGHT_DBTOOL_USER_CONFIG_DIR`: when set,
the upward walk starts there instead of at the executable's directory. Document it as for tests and
scripted runs. `test_dbtool.py` cases that used `cwd=` trees (e.g. 12c, `dbtool_in_tree`) switch to
it, and existing cwd-discovery tests are rewritten against the new rules. Unit tests keep injecting
paths via `DiscoveryInputs`.

## Left to the builder

- Data model: whether `ProfileStore` gains per-entry origins or a new `LayeredProfileStore` /
  `ConfigLayers` type wraps a list of per-file `ProfileStore`s; how `ConfigSource` /
  `DiscoveredConfig` evolve (e.g. a vector of layers) and how `ConnectionTarget` reports the
  source.
- How `root` is parsed (accept YAML booleans; non-boolean value → parse error naming the file).
- Whether `root: true` in an `--config` file is accepted silently (it should be harmless).
- How the legacy flat single-profile format participates as a layer (its implicit `default` profile
  takes part in the merge like any other).
- Exact wording/format of warnings and `list-profiles` output.
- How `ProfileStore::Save` (full re-serialise) is kept away from layered stores.
