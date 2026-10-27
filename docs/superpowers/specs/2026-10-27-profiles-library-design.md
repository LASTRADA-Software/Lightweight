# Installed profiles library (`Lightweight::Profiles`) — design

Issue: [#675](https://github.com/LASTRADA-Software/Lightweight/issues/675)

## Intent

Any application other than dbtool / dbtool-gui must be able to use the database profiles that
`dbtool.yml` holds — list them, connect through one (with its encrypted password decrypted), and
save a new one — and to encrypt/decrypt passwords in the same `enc:` format, so a password never has
to sit in plaintext in that application's own configuration.

Today all of this (discovery, `LayeredProfileStore`, `ResolveProfilePassword`, `AddProfileToFile`,
`Secrets::ProfileCipher` and the compiled-in key ring) lives in `tools_shared`, a static,
tools-private, never-installed library. This work exposes a small, stable slice of it as an
installed shared library.

The issue names one consumer as an example; the library is written for **any arbitrary
application**. No product or customer names appear in code, comments or docs.

## Goals

- An application sees **exactly the profiles dbtool and dbtool-gui see** for a given start
  directory: same discovery, same layering, same precedence, same password resolution.
- A profile the application saves is the one its next list/resolve call sees.
- The application can encrypt/decrypt passwords compatible with what `dbtool add-profile` wrote.
- Secrets never appear in error messages or logs.
- Consumable via `find_package(Lightweight CONFIG)` → `Lightweight::Profiles`.

## Non-goals

- No make-default option on save (can be added later).
- No explicit single-file mode (the equivalent of `--config`).
- No interactive password prompting; no stdin secret backend.
- No change to how dbtool / dbtool-gui discover their files (they keep starting from the executable
  directory / `LIGHTWEIGHT_DBTOOL_CONFIG_SEARCH_DIR`).

## Anti-goals

- A library that sees a *different* set of profiles than dbtool for the same directory.
- Leaking yaml-cpp, `tools_shared` headers, or key material through the public header or the
  installed package.
- Any secret (plaintext password, key, decrypted value) in an error string.

## Constraints

- **Same build as dbtool.** The library is built only with `LIGHTWEIGHT_BUILD_TOOLS=ON`, from the
  same build tree, so it compiles in the same key ring (`DBTOOL_MASTER_KEYS` /
  `DBTOOL_MASTER_KEYS_FILE`). Dev-key policy unchanged: official (release-key) builds refuse
  `enc:dev:` values, exactly as dbtool does.
- Shared library `LightweightProfiles` (`LightweightProfiles.dll` on Windows). It links
  `tools_shared` **privately**, so yaml-cpp and the key ring stay inside it.
- Exported through the existing `Lightweight` CMake package; `Lightweight-config.cmake` must make
  `Lightweight::Profiles` available when it was installed, and must still work for an install built
  without tools (no `Lightweight::Profiles` then).
- Public header must be self-contained, Doxygen on everything, `[[nodiscard]]` on `std::expected`
  returns, project coding guidelines apply (see `AGENT.md`).

## Lookup (decided)

Every function takes `std::optional<std::filesystem::path> startDirectory`:

- **Given:** build `Config::DiscoveryInputs` with `searchStart = *startDirectory` and
  `userDefault = ProfileStore::DefaultPath()`, then `FindConfigCandidates` +
  `LayeredProfileStore::Load`. That is: every `dbtool.yml` from the start directory upward, nearest
  first, stopping after a `root: true` file; the per-user file appended when no `root: true` was
  found. Merged with the usual rules (nearest wins per profile name and per store-wide value;
  relative directories resolve against their own layer).
- **`std::nullopt`:** only the per-user file (`searchStart` empty — already supported by
  `DiscoveryInputs`).
- `LIGHTWEIGHT_DBTOOL_CONFIG_SEARCH_DIR` is **ignored** by the library: the caller's argument is the
  whole input. (*Claude's call*, confirmed in conversation.)
- No file at all is not an error: an empty list / "profile not found" for resolve.

## Public API (decided shape)

Header `Lightweight/Profiles/ProfileConnection.hpp`, namespace `Lightweight::Profiles`. Only
standard-library types cross the boundary.

```cpp
namespace Lightweight::Profiles
{
struct ProfileConnection
{
    std::string name;             // the profile actually used (after defaulting)
    std::string dsn;
    std::string connectionString;
    std::string uid;
    std::string password;         // decrypted
    std::filesystem::path configFile; // the layer the profile came from
};

// Loads the merged view and resolves profileName's password non-interactively.
// Empty profileName means the merged `defaultProfile`; if that is unset or names a missing
// profile, an error is returned — never a fallback to the first profile (decided).
// Errors name file/profile, never the secret.
std::expected<ProfileConnection, std::string> ResolveProfileConnection(
    std::optional<std::filesystem::path> const& startDirectory, std::string_view profileName);

struct ProfileSummary
{
    std::string name;
    std::string dsn;
    std::string connectionString;
    std::string uid;
    bool hasPassword = false;         // password or secretRef set; value never returned
    std::filesystem::path configFile; // the layer it came from
};

struct ProfileList
{
    std::vector<std::filesystem::path> configFiles; // layers loaded, nearest first (may be empty)
    std::filesystem::path newProfileTarget;         // where SaveProfile writes a *new* profile
    std::string defaultProfile;                     // merged value, may be empty
    std::vector<ProfileSummary> profiles;           // merged view, see ordering note below
};

// No file at all is an empty list, not an error.
std::expected<ProfileList, std::string> ListProfiles(std::optional<std::filesystem::path> const& startDirectory);

struct ProfileDefinition
{
    std::string name;             // existing profile of that name is replaced
    std::string dsn;              // exactly one of dsn / connectionString
    std::string connectionString; // inline PWD=/Password= is moved out and stored encrypted
    std::string uid;
    std::string password;         // plaintext, stored encrypted; wins over one in connectionString
};

// Adds or replaces a profile like `dbtool add-profile --force`. Returns the file written.
std::expected<std::filesystem::path, std::string> SaveProfile(
    std::optional<std::filesystem::path> const& startDirectory, ProfileDefinition const& definition);

bool IsEncryptedPassword(std::string_view value) noexcept;
std::expected<std::string, std::string> EncryptPassword(std::string_view plaintext);
std::expected<std::string, std::string> DecryptPassword(std::string_view value);
}
```

Changes from the issue text, all following from the layered lookup: `startDirectory` is optional;
`ProfileList::configFile` became `configFiles` + `newProfileTarget`; `ProfileSummary` gained
`configFile`; `ProfileDefinition` gained `connectionString`; `ProfileConnection` gained `name`.

### Behaviour

- **Resolve:** password precedence as `Config::ResolveProfilePassword` (`password`, decrypted if
  `enc:` → `secretRef` → none), using `ProfileCipher::Builtin()` and a `SecretResolver` with only the
  non-interactive backends (env, file) registered. A plaintext password in the file is returned as
  is; the library does **not** upgrade it in place (*Claude's call* — the library has not proven the
  password works by connecting).
- **Save:** delegates to `Config::AddProfileToFile` with `ReplaceExisting::Yes`, `MakeDefault::No`,
  `ProfileCipher::Builtin()`. Target file is `LayeredProfileStore::WriteTargetFor(name, {},
  ProfileStore::DefaultPath())`: an existing profile is replaced in the layer it came from, a new one
  goes to the `root: true` layer if any, else the per-user file. The rest of that file is preserved
  byte for byte. Validation (missing name, both/neither of dsn and connection string) comes from
  `AddProfileToFile`.
- **Replacing is wholesale, exactly like `add-profile --force` (decided).** The old entry is removed
  and the new one written; fields `ProfileDefinition` cannot express (`schema`, `pluginsDir`,
  `backupDir`, `secretRef`) are dropped, and an empty `password` leaves the profile without one. This
  must be stated in the Doxygen of `SaveProfile` and in the docs. A field-preserving merge is a
  possible later extension, not part of this work.
- **Empty profile name with no usable default:** `ResolveProfileConnection` returns an error naming
  the files consulted ("no profile name given and no defaultProfile set" or similar). This
  deliberately differs from `ProfileStore::Default()`, which falls back to the first profile — an
  arbitrary application must not silently connect to whichever database is listed first. A legacy
  single-profile file (loaded as profile `default`) is resolved only if `defaultProfile` names it or
  the caller passes its name. Note `ProfileStore::Parse` sets `defaultProfile = "default"` for a
  legacy file (`ProfileStore.cpp:284-292`), so an empty name *does* resolve it when that layer is the
  nearest one setting a default. `SaveProfile` targeting a legacy-format layer fails because
  `AddProfileText` refuses legacy files (`ProfileFileEditor.cpp:346`) — tested and documented.
- **Resolve, more edge cases:** a profile with neither `dsn` nor `connectionString`
  (`Profile::HasConnection()` false) is an error. A relative `startDirectory` is made absolute
  against the working directory; a non-existent one is not an error, it simply finds no layers there.
- **Secret resolver** registers env + file backends only — as dbtool-gui does (`AppController.cpp:164`),
  not dbtool, which also registers `stdin:`.
- **`ListProfiles` never returns a password, including one inside a connection string.** A
  hand-written `connectionString: "...;PWD=x"` is valid (`ProfileStore.cpp:204-218`). The summary's
  `connectionString` is passed through `SqlConnectionString::SanitizePwd` as dbtool's
  `list-profiles` does (`dbtool/main.cpp:1255-1261`), and `hasPassword` is true when a PWD was
  present. `ResolveProfileConnection` returns the connection string unsanitised (it is meant for
  connecting).
- **No exceptions cross the DLL boundary.** Every exported function catches everything and returns
  `std::unexpected` with a secret-free message (`FileBackend` throws for a group/world-readable
  secret file, `FileBackend.cpp:60-77`; `path.string()` can throw on Windows for non-ANSI paths).
  Prefer `u8string()`-based formatting of paths in messages. `IsEncryptedPassword` is `noexcept`.
- **Ordering:** `AddProfileText` inserts new *and replaced* profiles as the first entry of their
  layer, so a replaced profile moves to the top; mention in docs.
- **Encrypt/Decrypt/IsEncrypted:** thin wrappers over `ProfileCipher::Builtin()`; same `enc:`
  format and key-id handling as dbtool.

## In-place write fallback (decided)

`Config::EditConfigFile` currently writes a temporary sibling then renames. New behaviour: when it
**cannot create the temporary sibling** but the target file **exists**, it rewrites the target in
place (open existing file for writing, truncate, write). Rationale: an installation may grant users
modify rights on `dbtool.yml` only, not on its directory. This also fixes `dbtool add-profile` and
dbtool-gui saves in that situation. All other failure behaviour unchanged (file untouched when the
transform fails or the file itself is not writable). The fallback loses atomicity; that is accepted,
with these guards:

- Trigger only when creating the temporary file failed with a permission error (`EACCES`/`EPERM`,
  or `std::errc::permission_denied` from the filesystem API), not on any failure.
- The original bytes are already in memory (`ProfileFileEditor.cpp:514-526`): if the in-place write
  fails part-way, best-effort rewrite the original and report the error, so a disk-full or I/O error
  does not leave a truncated file that has lost every other profile's password.
- Document: the in-place path writes *through* a symlink while rename replaces it; on Windows the
  existing `owner_write` check reflects only the read-only attribute, not ACLs (the ACL case —
  file: Modify, directory: no write — is exactly what the fallback fixes).

## Build & packaging (decided)

- New target `LightweightProfiles` (alias / export name `Lightweight::Profiles`), `SHARED`, added only
  when `LIGHTWEIGHT_BUILD_TOOLS=ON`. Own export macro (`LIGHTWEIGHT_PROFILES_API` or similar) — it
  is a separate DLL from Lightweight.
- Links `tools_shared` PRIVATE. Nothing from Lightweight core appears in its public header, so it
  does not need `Lightweight::Lightweight` on its public interface (builder may still add it if the
  package config needs it).
- ABI: same contract as the core `Lightweight.dll` — standard-library types cross the boundary, so
  consumers must use the same compiler/standard library and, on MSVC, a matching Debug/Release
  runtime. Documented, not engineered around (no pimpl / C ABI) (*Claude's call*). Ship Debug and
  Release binaries the way the core library is shipped.
- **Only `Lightweight::Profiles::` symbols are exported.** Visibility is per object file, and neither
  `tools_shared` nor (with `LIGHTWEIGHT_BUILD_SHARED=OFF`, the non-Windows default) core
  `Lightweight` compiles hidden, so on Linux/macOS the key ring (`Secrets::BuiltinKeyRing()`),
  `ProfileCipher`, every `Config::*` symbol and a static yaml-cpp would land in the `.so`'s export
  table. Required: `CXX_VISIBILITY_PRESET hidden` + `VISIBILITY_INLINES_HIDDEN ON` on `tools_shared`
  as well as `LightweightProfiles`, `-Wl,--exclude-libs,ALL` for GNU ld/lld, and a test asserting the
  dynamic export list (`nm -D` / `dumpbin /exports`) contains only `Lightweight::Profiles::` symbols.
  Windows needs nothing extra (static libs export nothing without `__declspec`).
- **Runtime dependencies** (to document per platform): with `LIGHTWEIGHT_BUILD_SHARED=ON` the DLL
  imports `Lightweight.dll`; yaml-cpp is a DLL/so under a dynamic vcpkg triplet; Linux also needs
  `libcrypto`. With `LIGHTWEIGHT_BUILD_SHARED=OFF` the library embeds a private copy of static
  Lightweight — harmless once its symbols are hidden, but its statics (logger, pools) are separate
  from the consumer's. Bundle runtime deps the way dbtool does (`RUNTIME_DEPENDENCY_SET`,
  `src/tools/dbtool/CMakeLists.txt:31-50`).
- Installed into the `Lightweight-targets` export set (PRIVATE deps of a SHARED target are not
  exported, so `tools_shared` need not be), or a sibling export file pulled in by
  `Lightweight-config.cmake.in` via `include(... OPTIONAL)` guarded by `NOT TARGET
  Lightweight::Profiles`. Plus its header and, on Windows, PDB as the core library does. An install
  built without tools must still yield a working package without `Lightweight::Profiles`.
- Security note for the docs: the installed library decrypts any `enc:` value for anyone who can
  load it — the same model as the dbtool binary (`Secrets/KeyRing.hpp:13-15`). The encryption
  protects against reading the file, not against code running as the user.

## Tests (decided scope)

Catch2, in the existing suite (`src/tests/`), alongside `ProfileAddTests.cpp` etc.:

- Layered lookup from a start directory (two layers, `root: true` stops the walk, per-user appended).
- `std::nullopt` sees only the per-user file. Tests must inject / redirect the per-user location —
  never touch the real user's file.
- Save → list → resolve round trip, both new profile and replacement in its origin layer.
- Connection string with inline `PWD=`: stored without it, password stored encrypted, resolves back.
- Encrypt/decrypt round trip; `IsEncryptedPassword`; decrypt of garbage is an error.
- No secret in any error string (assert the plaintext does not occur in the error text).
- Empty profile name: resolves `defaultProfile` when set; errors when unset or dangling (no fallback).
- Replacing a profile drops `schema`/`pluginsDir`/`secretRef` (pins the documented `--force` behaviour).
- Per-user file redirected via the existing `LIGHTWEIGHT_DBTOOL_USER_CONFIG_DIR` (`ScopedEnvironmentVariable`).
- `ListProfiles` with a hand-written `connectionString` containing `PWD=`: summary is sanitised,
  `hasPassword` true.
- POSIX: `secretRef: file:` pointing at a 0644 file returns an error instead of throwing.
- Profile without dsn/connection string errors; legacy-format layer resolves via `default` and
  refuses `SaveProfile`.
- In-place fallback: triggers only on a permission error; a failed in-place write restores the
  original bytes.
- Export-list test (Linux/macOS): only `Lightweight::Profiles::` symbols exported.
- The consumer test builds against the *installed* package (CI already installs and runs from the
  install tree, `.github/workflows/build.yml:188, 303, 319`).
- `EditConfigFile` in-place fallback (temporary file cannot be created, target exists).
- A small consumer test target linking **only** `Lightweight::Profiles` (proves the public header is
  self-contained and nothing private leaks).

No database involvement — this is file/crypto logic; the multi-DBMS rule does not apply beyond the
normal full-suite run.

## Left to the builder

- Source location. Recommended: `src/tools/profiles/` with the header at
  `src/tools/profiles/include/Lightweight/Profiles/ProfileConnection.hpp`, so in-tree and installed
  include paths match, installed by an `install(DIRECTORY include/ …)` scoped to this target.
  Constraint: the header must **not** be installed when tools are off (`src/Lightweight/CMakeLists.txt`
  installs every `*.hpp` under `src/Lightweight/` unconditionally, so do not put it there).
- Ordering of `ProfileList::profiles` (follow whatever `list-profiles` in dbtool shows).
- Docs: a section in `docs/dbtool.md` (or a new page) on using the profiles from another
  application, and the in-place fallback note.
