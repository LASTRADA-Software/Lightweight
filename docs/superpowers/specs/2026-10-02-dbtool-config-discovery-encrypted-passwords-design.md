# dbtool config discovery & encrypted profile passwords — design

Date: 2026-10-02 · Scope: `dbtool`, `dbtool-gui`, `src/tools/shared/`

## Goals

1. `dbtool` and `dbtool-gui` locate `dbtool.yml` automatically by searching upward from the
   working directory (and the executable's directory), so a project-local config is the first
   choice and the per-user config is the fallback.
2. Profiles may carry a `password` inside `dbtool.yml`, stored encrypted with a master key that
   is baked into the executable by CI. Nothing on the user's machine (DPAPI, keychain, key files)
   is needed to decrypt — the same `dbtool.yml` works on any machine running an official build.
3. Users never have to produce ciphertext by hand: `dbtool add-profile` encrypts on write, and a
   plaintext password that proves valid on connect is rewritten encrypted automatically.

## Non-goals

- Protecting passwords from anyone holding an official `dbtool` binary (see Security model).
- Per-user / per-machine secret isolation — that remains the job of `secretRef` (`env:`, `file:`,
  `stdin:`, future OS vault backends).
- A profile *editor* in the GUI. The GUI decrypts and auto-upgrades; creation is via the CLI.
- Bundling or maintaining any cryptographic library. Only OS-provided crypto is used.

## Security model (to be reproduced in `docs/dbtool.md`)

The master key is injected from a CI secret at build time; it is not in the repository.

| Threat | Protected? |
|---|---|
| Someone obtains only `dbtool.yml` (repo, share, ticket, screenshot) | Yes |
| Someone reads the open-source code | Yes — the key is not in git |
| Tampering with an encrypted value | Detected (HMAC), connect fails with a clear error |
| Someone holding an official `dbtool` / `dbtool-gui` binary | **No** — the key is extractable; every holder of that release can decrypt every file produced by it |

The key bytes are XOR-masked in the binary to defeat trivial `strings` extraction. This is
documented as a speed bump only.

## 1. Config file discovery

New free function in `src/tools/shared/Config/ConfigDiscovery.{hpp,cpp}`:

```cpp
namespace Lightweight::Config
{
/// Which rule selected the config file.
enum class ConfigSource : std::uint8_t { Explicit, WorkingDirectory, ExecutableDirectory, UserDefault, None };

/// Result of config discovery.
struct DiscoveredConfig
{
    std::filesystem::path path; ///< Chosen file; for `None`, the user-default path (may not exist).
    ConfigSource source;        ///< Rule that selected it.
};

/// Inputs to discovery; injected so tests can drive it without touching the real FS layout.
struct DiscoveryInputs
{
    std::filesystem::path explicitPath;     ///< `--config` / GUI setting; empty when unset.
    std::filesystem::path workingDirectory; ///< Normally std::filesystem::current_path().
    std::filesystem::path executableDirectory;
    std::filesystem::path userDefault;      ///< Normally ProfileStore::DefaultPath().
};

[[nodiscard]] std::expected<DiscoveredConfig, std::string> FindConfigFile(DiscoveryInputs const& inputs);
}
```

Order — first hit wins:

1. `explicitPath` if non-empty. Missing file → error (preserves today's `--config` behaviour).
2. Walk from `workingDirectory` up to the filesystem root, checking `<dir>/dbtool.yml`.
3. Walk from `executableDirectory` up to the root, same check. Skipped if it is the same chain
   already walked in step 2.
4. `userDefault` if it exists.
5. Otherwise `{userDefault, None}` — callers treat this as "no config" exactly as today.

The file name is a single constant (`inline constexpr std::string_view ConfigFileName = "dbtool.yml";`).
Executable directory is obtained via `GetModuleFileNameW` (Windows), `/proc/self/exe` (Linux),
`_NSGetExecutablePath` (macOS) in a small helper, `Tools::ExecutableDirectory()`.

Consumers:

- `dbtool` `ApplyProfileToOptions` replaces its `DefaultPath()` logic with `FindConfigFile`.
  `list-profiles` and `--verbose` print `Using config: <path> (<source>)`.
- `dbtool-gui` `AppController`: the persisted Settings path (when set) is `explicitPath`;
  otherwise discovery runs. Settings page displays the resolved path and source.
- `ProfileStore::LoadOrDefault` / `Save` are unchanged — they take the resolved path.

## 2. Profile cipher

`src/tools/shared/Secrets/ProfileCipher.{hpp,cpp}` + per-OS backend TUs.

### Wire format

```
password: "enc:<keyId>:<base64( iv[16] || ciphertext || tag[32] )>"
```

- `keyId`: `[a-z0-9]{1,16}`, e.g. `v1`, `dev`.
- Algorithm: AES-256-CBC with PKCS#7 padding, then HMAC-SHA256 (encrypt-then-MAC).
- Sub-keys derived from the 32-byte master key: `encKey = HMAC-SHA256(master, "dbtool-enc")`,
  `macKey = HMAC-SHA256(master, "dbtool-mac")`.
- `tag = HMAC-SHA256(macKey, "enc:" || keyId || ":" || iv || ciphertext)` — the key id is
  authenticated, so a value cannot be relabelled to another key.
- `iv`: 16 bytes from the OS CSPRNG.
- Tag compared in constant time.
- A value not starting with `enc:` is plaintext.

Rationale for CBC+HMAC instead of GCM: the format must be portable across all three OSes and
macOS CommonCrypto exposes no public AES-GCM API. CBC + HMAC-SHA256 is available through public
APIs on every platform.

### OS backends (one TU each, selected in CMake)

| OS | AES-256-CBC | HMAC-SHA256 | CSPRNG | Link |
|---|---|---|---|---|
| Windows | CNG `BCryptEncrypt` (`BCRYPT_CHAIN_MODE_CBC`) | CNG `BCRYPT_ALG_HANDLE_HMAC_FLAG` | `BCryptGenRandom` | `bcrypt` |
| Linux | OpenSSL `EVP_aes_256_cbc` | `HMAC()` / `EVP_MAC` | `RAND_bytes` | `OpenSSL::Crypto` (system `libssl-dev`) |
| macOS | CommonCrypto `CCCrypt` | `CCHmac` | `SecRandomCopyBytes` | `Security` framework |

Backend interface (internal, not public API):

```cpp
struct CryptoPrimitives
{
    static std::expected<std::vector<std::byte>, std::string> AesCbcEncrypt(std::span<std::byte const, 32> key, std::span<std::byte const, 16> iv, std::span<std::byte const> plain);
    static std::expected<std::vector<std::byte>, std::string> AesCbcDecrypt(...);
    static std::array<std::byte, 32> HmacSha256(std::span<std::byte const> key, std::span<std::byte const> data);
    static std::expected<void, std::string> RandomBytes(std::span<std::byte> out);
};
```

The format logic (`ProfileCipher`) is OS-independent and tested once; backend TUs are thin.
Cross-backend compatibility is enforced by fixed test vectors (known key + IV → known ciphertext)
checked in and run on every OS in CI.

### Public API

```cpp
namespace Lightweight::Secrets
{
class ProfileCipher
{
  public:
    /// Uses the key ring compiled into this build.
    [[nodiscard]] static ProfileCipher Builtin();
    /// For tests: explicit key ring; first entry is the encryption key.
    explicit ProfileCipher(std::vector<KeyEntry> ring, DevKeyPolicy policy);

    [[nodiscard]] static bool IsEncrypted(std::string_view value) noexcept;
    [[nodiscard]] std::expected<std::string, std::string> Encrypt(std::string_view plaintext) const;
    [[nodiscard]] std::expected<std::string, std::string> Decrypt(std::string_view value) const;
};
}
```

### Key ring injection

- CMake reads `DBTOOL_MASTER_KEYS` (cache var, defaulting from the environment variable of the
  same name): `id=<64 hex>[,id=<64 hex>...]`. First entry encrypts; all decrypt (rotation).
- Configure step generates `${CMAKE_BINARY_DIR}/generated/DbtoolKeyRing.inc` containing the
  XOR-masked bytes. The generated file is never installed and lives only in the build tree.
- If unset: ring = `{ dev = <public constant in source> }` and a CMake `STATUS` message says
  "dbtool: no master key provided — using the public development key".
- `DBTOOL_RELEASE_KEYS=ON` is set automatically when `DBTOOL_MASTER_KEYS` is provided. In such
  builds, decrypting an `enc:dev:` value fails with: "this password was encrypted by a development
  build; re-run `dbtool add-profile` or replace it with plaintext to re-encrypt". Dev builds can
  encrypt/decrypt `dev` values only (no release key present).
- CI: the GitHub secret `DBTOOL_MASTER_KEYS` is exported in the release/packaging jobs only.
  PR jobs (including forks) build with the dev key. Test vectors for the dev key run everywhere.

## 3. ProfileStore changes

- `Profile` gains `std::string password;` — raw YAML value, either `enc:…` or plaintext.
- Load error if a profile has both `password` and `secretRef`.
- `Save` writes `password` back verbatim (it is already ciphertext after the flows below). The
  header comment "never writes secret values" is updated to "writes only encrypted values".
- Legacy single-profile format: a top-level `Password:` key is accepted the same way.

## 4. Password resolution & plaintext auto-upgrade

New in `src/tools/shared/Config/ProfilePassword.{hpp,cpp}`:

```cpp
/// Where a resolved password came from.
enum class PasswordOrigin : std::uint8_t { None, Encrypted, Plaintext, SecretRef };

struct ResolvedPassword { std::string value; PasswordOrigin origin; };

[[nodiscard]] std::expected<ResolvedPassword, std::string> ResolveProfilePassword(
    Profile const& profile, Secrets::ProfileCipher const& cipher, Secrets::SecretResolver const& resolver);

/// Rewrites the `password` scalar of `profileName` in `configPath` to `encryptedValue`,
/// preserving all other bytes of the file (comments, ordering, formatting).
[[nodiscard]] std::expected<void, std::string> RewritePasswordInPlace(
    std::filesystem::path const& configPath, std::string_view profileName, std::string_view encryptedValue);
```

Precedence: `password` → `secretRef` → none.

Connect flow, shared by `dbtool` (all connecting commands) and `dbtool-gui` (`connectToProfile`
and `ManagedBackupCore`, which already resolves `secretRef`):

1. `ResolveProfilePassword`. Decrypt failure → hard error naming the profile; no connection attempt.
2. Connect with the resolved password.
3. If origin was `Plaintext` **and** the connection succeeded:
   `cipher.Encrypt` → `RewritePasswordInPlace`. On success print/emit
   "Encrypted the plaintext password of profile '<name>' in <path>". If the file is inside a git
   work tree (a `.git` entry in any ancestor), additionally warn that the plaintext remains in
   git history and the database password should be rotated.
   On write failure (read-only, permissions): warn and continue; the connection is unaffected.
4. If the connection failed: no rewrite; the normal connection error is reported.

This also fixes the existing gap where `dbtool` (`main.cpp`, "Secret resolution lands in a
follow-up") and `AppController::connectToProfile` ignore `secretRef`.

### In-place rewrite

`RewritePasswordInPlace` uses yaml-cpp only to *locate* the node (`Node::Mark()` gives line/column
of the `password` scalar under `profiles.<name>` or the legacy top-level `Password`), then replaces
that scalar's byte range in the original text with a double-quoted `"enc:…"` (base64 and the
`enc:` alphabet need no escaping). Write is atomic: temp file in the same directory + rename.
Line endings of the original file are preserved. If the scalar spans multiple lines (block
scalar), the rewrite is refused with a warning — no partial edits.

## 5. `dbtool add-profile`

```
dbtool add-profile --name <NAME>
                   (--connection-string <CS> | --dsn <DSN> [--uid <UID>])
                   [--schema <S>] [--plugins-dir <DIR>]
                   [--password-stdin] [--no-password] [--set-default] [--force]
                   [--config <FILE>]
```

- Target file: `--config`, else the discovered file, else the user default (created with parent
  directories).
- Password: interactive no-echo prompt by default (`StdinBackend`'s prompt helper);
  `--password-stdin` reads one line from stdin for scripting; `--no-password` skips it. Never
  accepted on argv. If `--connection-string` contains `PWD=`, it is stripped from the stored
  string and treated as the password (so it gets encrypted), with a notice.
- The profile is appended as a text block under `profiles:` (creating `profiles:` if absent) —
  no full re-serialisation, so existing comments survive. Existing name → error unless `--force`,
  which replaces that profile's block via the same locate-by-mark technique.
- Option parsing is table-driven, consistent with the existing command table in `main.cpp`.
- `--set-default` updates `defaultProfile` in place.

## 6. Error handling

All new APIs return `std::expected<…, std::string>` (tools layer convention, matching
`ProfileStore`). Messages name the profile and file, never include the secret.

## 7. Testing

Catch2 (`src/tests/` for shared tools code; `src/tools/dbtool-gui/tests/` for GUI wiring):

- Discovery: temp directory trees covering each rule, precedence, cwd == exe dir dedupe,
  explicit-missing error, nothing-found.
- Cipher: round-trip; fixed dev-key test vectors (cross-OS compatibility); tamper of
  iv/ciphertext/tag/keyId → error; unknown keyId; release policy refuses `dev`; non-`enc:`
  passthrough; empty password.
- ProfileStore: `password` load/save; `password`+`secretRef` conflict.
- RewritePasswordInPlace: comments and ordering preserved byte-for-byte except the scalar; CRLF
  file; legacy top-level form; block scalar refusal; read-only file → error.
- Auto-upgrade: SQLite profile with plaintext password connects → file rewritten; failing
  connection → file untouched.
- `src/tests/test_dbtool.py`: `add-profile` (prompt via `--password-stdin`, duplicate, `--force`,
  `--set-default`), discovery from a nested cwd, `list-profiles` shows source — against sqlite3,
  mssql2022, postgres. MSSQL/Postgres cases exercise a real password round-trip.

## 8. Build & CI changes

- `src/tools/shared/CMakeLists.txt`: select backend TU per OS; link `bcrypt` / `OpenSSL::Crypto` /
  `-framework Security`; generate `DbtoolKeyRing.inc`.
- `.github/workflows/build.yml`: add `libssl-dev` to every Linux apt line that builds tools;
  export `DBTOOL_MASTER_KEYS` secret only in release/packaging jobs.
- No new vcpkg dependencies (Windows uses CNG from the SDK; macOS uses system frameworks).

## 9. Documentation

`docs/dbtool.md`: discovery order, `add-profile`, the `password` field, auto-upgrade behaviour,
security model table, key rotation procedure (prepend new key to `DBTOOL_MASTER_KEYS`, ship,
re-encrypt via `add-profile --force` or by replacing with plaintext). `src/tools/dbtool-gui/README.md`:
discovery and where the GUI shows the active config.

## Open risks

- Distributions building `dbtool` themselves get the dev key; their users' encrypted values are
  incompatible with official builds. Documented; the error message explains it.
- Key leak requires a rotation release; old ciphertext stays readable by anyone with an old binary.
