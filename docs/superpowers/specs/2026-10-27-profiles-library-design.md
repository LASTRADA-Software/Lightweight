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
  the caller passes its name (*Claude's call*, follows from the rule).
- **Encrypt/Decrypt/IsEncrypted:** thin wrappers over `ProfileCipher::Builtin()`; same `enc:`
  format and key-id handling as dbtool.

## In-place write fallback (decided)

`Config::EditConfigFile` currently writes a temporary sibling then renames. New behaviour: when it
**cannot create the temporary sibling** but the target file **exists**, it rewrites the target in
place (open existing file for writing, truncate, write). Rationale: an installation may grant users
modify rights on `dbtool.yml` only, not on its directory. This also fixes `dbtool add-profile` and
dbtool-gui saves in that situation. All other failure behaviour unchanged (file untouched when the
transform fails or the file itself is not writable). The fallback loses atomicity; that is accepted.

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
- Installed into the `Lightweight-targets` export set (or a sibling export file included
  conditionally by `Lightweight-config.cmake.in`), plus its header and, on Windows, PDB as the core
  library does.

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
- Per-user file redirected via the existing `LIGHTWEIGHT_DBTOOL_USER_CONFIG_DIR`.
- `EditConfigFile` in-place fallback (temporary file cannot be created, target exists).
- A small consumer test target linking **only** `Lightweight::Profiles` (proves the public header is
  self-contained and nothing private leaks).

No database involvement — this is file/crypto logic; the multi-DBMS rule does not apply beyond the
normal full-suite run.

## Left to the builder

- Source location (e.g. `src/tools/profiles/` vs `src/Lightweight/Profiles/`). Constraint: the header
  must **not** be installed when tools are off (note `src/Lightweight/CMakeLists.txt` installs every
  `*.hpp` under `src/Lightweight/` unconditionally).
- Ordering of `ProfileList::profiles` (follow whatever `list-profiles` in dbtool shows).
- Docs: a section in `docs/dbtool.md` (or a new page) on using the profiles from another
  application, and the in-place fallback note.
