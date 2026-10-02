# dbtool Config Discovery & Encrypted Profile Passwords — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `dbtool` / `dbtool-gui` find `dbtool.yml` by walking up from the cwd and exe dir, and profiles can
carry a password encrypted with a CI-baked master key; plaintext passwords are encrypted in place after a
successful connect, and `dbtool add-profile` writes encrypted profiles.

**Architecture:** All logic lives in the tools-private `tools_shared` static library
(`src/tools/shared/`): `Config/ConfigDiscovery`, `Config/ProfileFileEditor` (comment-preserving YAML text
edits), `Config/ProfilePassword` (resolve + upgrade), `Secrets/ProfileCipher` (format) over
`Secrets/Crypto/CryptoPrimitives` (one TU per OS: CNG / OpenSSL libcrypto / CommonCrypto). `dbtool` and
`dbtool-gui` only call these.

**Tech Stack:** C++23, yaml-cpp, Catch2, Windows CNG (`bcrypt`), OpenSSL `libcrypto` (Linux), CommonCrypto +
Security.framework (macOS), Qt 6 (GUI only), Python (`test_dbtool.py`).

**Spec:** `docs/superpowers/specs/2026-10-02-dbtool-config-discovery-encrypted-passwords-design.md`

## Global Constraints

- Config file name: `dbtool.yml` (single `inline constexpr` constant).
- Discovery order: explicit → walk up from cwd → walk up from exe dir → `ProfileStore::DefaultPath()`.
- Wire format: `enc:<keyId>:<base64(iv[16] || ciphertext || tag[32])>`, keyId `[a-z0-9]{1,16}`.
- Cipher: AES-256-CBC/PKCS#7 + HMAC-SHA256 encrypt-then-MAC; `encKey = HMAC(master,"dbtool-enc")`,
  `macKey = HMAC(master,"dbtool-mac")`, `tag = HMAC(macKey, "enc:" + keyId + ":" + iv + ct)`; constant-time compare.
- Key ring env var: `DBTOOL_MASTER_KEYS=id=<64 hex>[,id=<64 hex>...]` (first encrypts). Read from the environment
  at configure time, never stored in `CMakeCache.txt`. Absent → public `dev` key; present → release ring, `dev` refused.
- Only OS-provided crypto. No new vcpkg/CPM dependencies.
- Passwords never accepted on argv; never logged.
- File rewrites preserve every byte except the edited region; atomic (temp + rename); preserve CRLF/LF.
- Tools layer error convention: `std::expected<T, std::string>`. Doxygen on every new declaration. No C-style
  loops, no `NOLINT`, `clang-format` all touched files.
- Namespace-scope constants in headers are `inline constexpr` (modules build).

## Review Focus

1. **`password:` value already quoted with single quotes or containing `#`** — the in-place rewrite must replace
   exactly the scalar, not the trailing comment. Test in Task 4.
2. **Same profile name appears under a nested key elsewhere** (e.g. `defaultProfile: prod` and `profiles.prod`) —
   rewrite must target `profiles.<name>.password`, never a lookalike line. Test in Task 4.
3. **Discovered config is the user default and doesn't exist yet when `add-profile` runs** — parent directories
   are created, file written. Test in Task 8 (`test_dbtool.py`) and Task 4 (`AddProfile` on empty text).
4. **Plaintext password + `connectionString` that already has `PWD=`** — `ToConnectInfo` keeps the inline `PWD`;
   upgrade must still only run when the profile's `password` field was used. Test in Task 6.
5. **Profile `password` is an empty string** — treated as "no password", never encrypted, never rewritten. Test in Task 6.

---

## File Map

| File | Responsibility |
|---|---|
| `src/tools/shared/Secrets/Crypto/CryptoPrimitives.hpp` | Internal OS-neutral interface: AES-256-CBC, HMAC-SHA256, CSPRNG |
| `src/tools/shared/Secrets/Crypto/CryptoPrimitives_win.cpp` | CNG implementation |
| `src/tools/shared/Secrets/Crypto/CryptoPrimitives_openssl.cpp` | libcrypto implementation (Linux) |
| `src/tools/shared/Secrets/Crypto/CryptoPrimitives_apple.cpp` | CommonCrypto implementation (macOS) |
| `src/tools/shared/Secrets/KeyRing.hpp` / `.cpp` | Compile-time masked key ring from generated `DbtoolKeyRing.inc` |
| `src/tools/shared/Secrets/DbtoolKeyRing.inc.in` | CMake template |
| `src/tools/shared/Secrets/ProfileCipher.hpp` / `.cpp` | `enc:` format, encrypt/decrypt, key policy |
| `src/tools/shared/Config/ConfigDiscovery.hpp` / `.cpp` | `FindConfigFile`, `ExecutableDirectory` |
| `src/tools/shared/Config/ProfileFileEditor.hpp` / `.cpp` | Text-level YAML edits: set password, add/replace profile, set default |
| `src/tools/shared/Config/ProfilePassword.hpp` / `.cpp` | `ResolveProfilePassword`, `UpgradePlaintextPassword` |
| `src/tools/shared/Config/ProfileStore.{hpp,cpp}` | + `password` field |
| `src/tools/dbtool/main.cpp` | discovery, password resolution, upgrade probe, `list-profiles` AUTH column, `add-profile` |
| `src/tools/dbtool-gui/AppController.cpp` | discovery, password resolution, upgrade |
| `src/tools/dbtool-gui/ManagedBackupCore.cpp` | decrypt `password` (no upgrade — runs on worker threads) |
| `src/tests/ProfileCipherTests.cpp`, `ConfigDiscoveryTests.cpp`, `ProfileFileEditorTests.cpp`, `ProfilePasswordTests.cpp` | Catch2 |
| `src/tests/ConfigProfileStoreTests.cpp` | + password cases |
| `src/tests/test_dbtool.py` | add-profile, discovery, auto-upgrade end-to-end |
| `.github/workflows/build.yml` | `libssl-dev`; secret in release jobs |
| `docs/dbtool.md`, `src/tools/dbtool-gui/README.md` | user docs + security model |

---

### Task 1: OS crypto primitives

**Files:**
- Create: `src/tools/shared/Secrets/Crypto/CryptoPrimitives.hpp`, `CryptoPrimitives_win.cpp`, `CryptoPrimitives_openssl.cpp`, `CryptoPrimitives_apple.cpp`
- Modify: `src/tools/shared/CMakeLists.txt`
- Test: `src/tests/ProfileCipherTests.cpp` (new; add to `src/tests/CMakeLists.txt` `SOURCE_FILES`)

**Interfaces — Produces:**
```cpp
namespace Lightweight::Secrets::Crypto
{
inline constexpr std::size_t AesKeySize = 32;
inline constexpr std::size_t AesBlockSize = 16;
inline constexpr std::size_t HmacSize = 32;
using Bytes = std::vector<std::byte>;
[[nodiscard]] std::expected<Bytes, std::string> AesCbcEncrypt(std::span<std::byte const, AesKeySize> key, std::span<std::byte const, AesBlockSize> iv, std::span<std::byte const> plaintext);
[[nodiscard]] std::expected<Bytes, std::string> AesCbcDecrypt(std::span<std::byte const, AesKeySize> key, std::span<std::byte const, AesBlockSize> iv, std::span<std::byte const> ciphertext);
[[nodiscard]] std::expected<std::array<std::byte, HmacSize>, std::string> HmacSha256(std::span<std::byte const> key, std::span<std::byte const> data);
[[nodiscard]] std::expected<void, std::string> RandomBytes(std::span<std::byte> out);
}
```
PKCS#7 padding is done by the backend (CNG `BCRYPT_BLOCK_PADDING`, OpenSSL default, `kCCOptionPKCS7Padding`).

- [ ] **Step 1: Failing tests** (`[Crypto]`), using vectors computed with the OpenSSL CLI:
  - RFC 4231 test case 2: `HmacSha256("Jefe", "what do ya want for nothing?")` ==
    `5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843`.
  - `AesCbcEncrypt(key=020f8e05d0bb8a19855f720b700e723e3d786948c1f08c5f7936bfed5903ef4a, iv=000102…0f, "s3cr3t-P@ss")`
    == `ab12d6816ed64c081c8e9ad7e118e157`; decrypt round-trips.
  - Decrypt with wrong key → `!has_value()` (bad padding) or different plaintext — assert not equal to original.
  - `RandomBytes` of 32 bytes twice → different.
  - Small hex helper `FromHex(std::string_view) -> std::vector<std::byte>` local to the test file.
- [ ] **Step 2:** Build `LightweightTest`, run `LightweightTest "[Crypto]"` → link errors / FAIL.
- [ ] **Step 3: Implement**
  - `_win.cpp`: `BCryptOpenAlgorithmProvider(BCRYPT_AES_ALGORITHM)`, `BCryptSetProperty(BCRYPT_CHAINING_MODE, BCRYPT_CHAIN_MODE_CBC)`,
    `BCryptGenerateSymmetricKey`, `BCryptEncrypt/Decrypt(..., BCRYPT_BLOCK_PADDING)` (copy IV — CNG mutates it);
    HMAC via `BCRYPT_SHA256_ALGORITHM` + `BCRYPT_ALG_HANDLE_HMAC_FLAG` and `BCryptCreateHash/HashData/FinishHash`;
    `BCryptGenRandom(nullptr, …, BCRYPT_USE_SYSTEM_PREFERRED_RNG)`. RAII `unique_ptr` deleters for
    `BCRYPT_ALG_HANDLE`, `BCRYPT_KEY_HANDLE`, `BCRYPT_HASH_HANDLE`. Errors formatted with the `NTSTATUS` hex.
  - `_openssl.cpp`: `EVP_CIPHER_CTX` (unique_ptr with `EVP_CIPHER_CTX_free`) + `EVP_aes_256_cbc`;
    `HMAC(EVP_sha256(), …)`; `RAND_bytes`.
  - `_apple.cpp`: `CCCrypt(kCCEncrypt/kCCDecrypt, kCCAlgorithmAES, kCCOptionPKCS7Padding, …)`;
    `CCHmac(kCCHmacAlgSHA256, …)`; `SecRandomCopyBytes(kSecRandomDefault, …)`.
  - CMake (`src/tools/shared/CMakeLists.txt`):
    ```cmake
    if(WIN32)
        target_sources(tools_shared PRIVATE Secrets/Crypto/CryptoPrimitives_win.cpp)
        target_link_libraries(tools_shared PRIVATE bcrypt)
    elseif(APPLE)
        target_sources(tools_shared PRIVATE Secrets/Crypto/CryptoPrimitives_apple.cpp)
        target_link_libraries(tools_shared PRIVATE "-framework Security")
    else()
        find_package(OpenSSL REQUIRED COMPONENTS Crypto)
        target_sources(tools_shared PRIVATE Secrets/Crypto/CryptoPrimitives_openssl.cpp)
        target_link_libraries(tools_shared PRIVATE OpenSSL::Crypto)
    endif()
    ```
- [ ] **Step 4:** Run `[Crypto]` → PASS (Windows). Compile the OpenSSL TU in WSL with `g++-14 -std=c++23 -fsyntax-only`
  (requires `libssl-dev` in WSL); Apple TU is CI-verified only.
- [ ] **Step 5:** Commit `feat(tools): OS-native AES-CBC/HMAC-SHA256 primitives for dbtool`.

### Task 2: Key ring + ProfileCipher

**Files:**
- Create: `src/tools/shared/Secrets/DbtoolKeyRing.inc.in`, `KeyRing.hpp`, `KeyRing.cpp`, `ProfileCipher.hpp`, `ProfileCipher.cpp`
- Modify: `src/tools/shared/CMakeLists.txt`
- Test: `src/tests/ProfileCipherTests.cpp`

**Interfaces — Produces:**
```cpp
namespace Lightweight::Secrets
{
/// One master key with its identifier.
struct KeyEntry { std::string id; std::array<std::byte, 32> key; };
/// Whether the public development key may decrypt.
enum class DevKeyPolicy : std::uint8_t { Allow, Refuse };
/// Source of IVs; injectable for deterministic tests.
using RandomSource = std::function<std::expected<void, std::string>(std::span<std::byte>)>;

inline constexpr std::string_view EncryptedPrefix = "enc:";
inline constexpr std::string_view DevKeyId = "dev";

[[nodiscard]] std::vector<KeyEntry> BuiltinKeyRing();      // KeyRing.cpp
[[nodiscard]] bool BuiltinKeyRingIsRelease() noexcept;      // KeyRing.cpp
[[nodiscard]] KeyEntry DevKey();                            // public constant

class ProfileCipher
{
  public:
    ProfileCipher(std::vector<KeyEntry> ring, DevKeyPolicy policy, RandomSource random = {});
    [[nodiscard]] static ProfileCipher const& Builtin();
    [[nodiscard]] static bool IsEncrypted(std::string_view value) noexcept;
    [[nodiscard]] std::expected<std::string, std::string> Encrypt(std::string_view plaintext) const;
    [[nodiscard]] std::expected<std::string, std::string> Decrypt(std::string_view value) const;
};
}
```

Dev key = SHA-256("lightweight-dbtool-public-development-key") =
`ba456ca44382fdc8bfd9c84ac989779fa0da3754722e2691f1e0d7a735a3b232`.

Generated `DbtoolKeyRing.inc` (from `DbtoolKeyRing.inc.in` via `configure_file(@ONLY)` into
`${CMAKE_CURRENT_BINARY_DIR}/generated/`, added PRIVATE include dir):
```cpp
inline constexpr bool DbtoolKeyRingIsRelease = @DBTOOL_KEYRING_IS_RELEASE@;
inline constexpr std::string_view DbtoolKeyRingSpec = "@DBTOOL_KEYRING_SPEC@";
inline constexpr std::string_view DbtoolKeyRingMask = "@DBTOOL_KEYRING_MASK@";
```
`KeyRing.cpp` parses the spec in a `consteval` function into `std::array<MaskedEntry, N>` (bytes XOR mask), so only
masked bytes are emitted; unmasked at runtime in `BuiltinKeyRing()`. CMake:
```cmake
if(DEFINED ENV{DBTOOL_MASTER_KEYS} AND NOT "$ENV{DBTOOL_MASTER_KEYS}" STREQUAL "")
    set(DBTOOL_KEYRING_SPEC "$ENV{DBTOOL_MASTER_KEYS}")
    set(DBTOOL_KEYRING_IS_RELEASE true)
    message(STATUS "dbtool: using master key ring from DBTOOL_MASTER_KEYS")
else()
    set(DBTOOL_KEYRING_SPEC "dev=ba456ca44382fdc8bfd9c84ac989779fa0da3754722e2691f1e0d7a735a3b232")
    set(DBTOOL_KEYRING_IS_RELEASE false)
    message(STATUS "dbtool: no master key provided - using the public development key")
endif()
string(RANDOM LENGTH 64 ALPHABET "0123456789abcdef" DBTOOL_KEYRING_MASK)
```
The spec is validated at configure time with `string(REGEX MATCH "^[a-z0-9]+=[0-9a-fA-F]{64}(,[a-z0-9]+=[0-9a-fA-F]{64})*$")`
→ `FATAL_ERROR` on mismatch (message must not echo the value).

- [ ] **Step 1: Failing tests** (`[ProfileCipher]`), cipher built with `{DevKey()}` and a fixed IV source
  (`000102…0f`):
  - `Encrypt("s3cr3t-P@ss")` == `"enc:dev:AAECAwQFBgcICQoLDA0OD6sS1oFu1kwIHI6a1+EY4VevOb7jEx+oT8+0ceuOpyZ0qgglTS0t8m638s+GnkICcg=="`.
  - `Encrypt("")` == `"enc:dev:AAECAwQFBgcICQoLDA0OD4e/nfkrNXAncseigUrvkuQv1B2I90y+b80luPRR2XRt/W1musybB6QYMi2ImOzsLw=="`.
  - `Decrypt` of both vectors round-trips (this is the cross-OS compatibility check).
  - Default random source: two encrypts of same plaintext differ; both decrypt.
  - Tamper: flip one byte in iv / ct / tag (decode base64, flip, re-encode) → error containing `"integrity"`.
  - Relabel `enc:dev:` → `enc:v1:` with ring `{dev, v1}` → error (tag binds key id).
  - Unknown key id → error containing `"unknown key id 'zz'"`.
  - `DevKeyPolicy::Refuse` + `enc:dev:…` → error containing `"development build"`.
  - Ring `{v2, v1}`: `Encrypt` uses `v2`; value made with ring `{v1}` decrypts with `{v2, v1}` (rotation).
  - Malformed: `"enc:"`, `"enc:dev"`, `"enc:dev:!!!"`, too-short payload → errors, no crash.
  - `IsEncrypted("enc:dev:x")` true, `IsEncrypted("hunter2")` false.
  - `BuiltinKeyRing()` non-empty; first entry id is `dev` iff `!BuiltinKeyRingIsRelease()`.
- [ ] **Step 2:** Run `[ProfileCipher]` → FAIL.
- [ ] **Step 3:** Implement `ProfileCipher` (base64 encode/decode locally in `ProfileCipher.cpp`; constant-time
  compare via `std::ranges::fold_left` of XORs); `Builtin()` is a function-local static constructed from
  `BuiltinKeyRing()` with policy `Refuse` when release else `Allow`.
- [ ] **Step 4:** Run `[ProfileCipher]` and `[Crypto]` → PASS.
- [ ] **Step 5:** Commit `feat(tools): ProfileCipher with CI-injected master key ring`.

### Task 3: Config discovery

**Files:**
- Create: `src/tools/shared/Config/ConfigDiscovery.hpp`, `.cpp`
- Test: `src/tests/ConfigDiscoveryTests.cpp`

**Interfaces — Produces:**
```cpp
namespace Lightweight::Config
{
inline constexpr std::string_view ConfigFileName = "dbtool.yml";
enum class ConfigSource : std::uint8_t { Explicit, WorkingDirectory, ExecutableDirectory, UserDefault, None };
[[nodiscard]] std::string_view ToString(ConfigSource source) noexcept; // "--config", "current directory", "executable directory", "user default", "none"
struct DiscoveredConfig { std::filesystem::path path; ConfigSource source; };
struct DiscoveryInputs { std::filesystem::path explicitPath, workingDirectory, executableDirectory, userDefault; };
[[nodiscard]] std::expected<DiscoveredConfig, std::string> FindConfigFile(DiscoveryInputs const& inputs);
[[nodiscard]] DiscoveryInputs DefaultDiscoveryInputs(std::filesystem::path explicitPath = {});
[[nodiscard]] std::filesystem::path ExecutableDirectory();
}
```

- [ ] **Step 1: Failing tests** (`[ConfigDiscovery]`) on a temp tree `root/a/b/c` and `root/exe/bin`:
  - explicit existing → `Explicit`; explicit missing → error containing the path.
  - file in `root/a` with cwd `root/a/b/c` → `WorkingDirectory`, path `root/a/dbtool.yml`.
  - files in `root/a/b` and `root/a` → nearest (`root/a/b`) wins.
  - nothing in cwd chain, file in `root/exe` with exeDir `root/exe/bin` → `ExecutableDirectory`.
  - only user default exists → `UserDefault`. Nothing → `None` with `path == userDefault`.
  - a *directory* named `dbtool.yml` is ignored (must be a regular file).
  - `ExecutableDirectory()` is non-empty and exists.
  - Note: tests use a temp root but the walk continues to the real FS root — ensure no `dbtool.yml` sits above
    `temp_directory_path()` by asserting precondition (`SKIP` if one exists, with message).
- [ ] **Step 2:** Run → FAIL.
- [ ] **Step 3:** Implement `FindUpwards(start)`: `while` loop over `std::filesystem::path` (absolute,
  `weakly_canonical`) stopping when `dir == dir.parent_path()`; hit = `is_regular_file(dir / ConfigFileName, ec)`.
  Exe dir: `GetModuleFileNameW` / `read_symlink("/proc/self/exe")` / `_NSGetExecutablePath`, then
  `weakly_canonical(...).parent_path()`. The exe walk runs only when the cwd walk found nothing, so an overlapping
  chain cannot produce a different result.
- [ ] **Step 4:** Run → PASS.
- [ ] **Step 5:** Commit `feat(tools): dbtool.yml discovery walking up from cwd and exe dir`.

### Task 4: Comment-preserving YAML editor

**Files:**
- Create: `src/tools/shared/Config/ProfileFileEditor.hpp`, `.cpp`
- Test: `src/tests/ProfileFileEditorTests.cpp`

**Interfaces — Produces:**
```cpp
namespace Lightweight::Config
{
/// Fields written by AddProfile. Values are emitted as double-quoted YAML scalars.
struct NewProfile { std::string name, connectionString, dsn, uid, schema, pluginsDir, password; };
enum class ReplaceExisting : std::uint8_t { No, Yes };

// Pure text transforms (unit-tested directly)
[[nodiscard]] std::expected<std::string, std::string> SetProfilePasswordText(std::string_view yaml, std::string_view profileName, std::string_view newValue);
[[nodiscard]] std::expected<std::string, std::string> AddProfileText(std::string_view yaml, NewProfile const& profile, ReplaceExisting replace);
[[nodiscard]] std::expected<std::string, std::string> SetDefaultProfileText(std::string_view yaml, std::string_view profileName);

// File wrappers: read, transform, atomic write (temp in same dir + rename), create parent dirs
[[nodiscard]] std::expected<void, std::string> EditConfigFile(std::filesystem::path const& path, std::function<std::expected<std::string, std::string>(std::string_view)> const& transform);
[[nodiscard]] std::string QuoteYamlScalar(std::string_view value); // "..." with \\ \" \n \t \r escaped
}
```

Algorithm notes:
- Locate nodes with yaml-cpp (`YAML::Load`), take `node.Mark().pos` of the target scalar value (for
  `profiles.<name>.password`, or legacy top-level `Password`/`password` when the file is legacy-shaped).
- Scalar extent from `pos`: `"`→ to next unescaped `"`; `'` → to next `'` not followed by `'`; `|`/`>` → error
  "block scalars are not supported for password"; plain → up to EOL or first ` #`, right-trimmed.
- Line ending detection: if text contains `"\r\n"`, emit inserted lines with `\r\n`.
- `AddProfileText`: legacy-shaped file → error "convert to multi-profile format first"; flow-style `profiles` → error;
  `profiles` missing → append `profiles:` block at end (ensure preceding newline); else insert the new block on the
  line after the `profiles:` key line using the indentation of the first existing child (default 2). Existing name +
  `No` → error "profile '<n>' already exists (use --force to replace)"; `Yes` → remove old block (the child key line
  through the line before the next line with indentation ≤ child indent that is not blank/comment) then insert.
- Password set via `AddProfileText` must already be encrypted by the caller (editor is crypto-agnostic).
- `SetDefaultProfileText`: replace scalar if `defaultProfile` exists, else prepend `defaultProfile: "<n>"` line.

- [ ] **Step 1: Failing tests** (`[ProfileFileEditor]`):
  - Replace plain `password: hunter2  # dev only` → `password: "enc:dev:AAA"  # dev only`; all other bytes equal.
  - Replace double-quoted and single-quoted (`'it''s'`) values.
  - Review-focus #1: value `"pa#ss"` (quoted, contains `#`) replaced whole.
  - Review-focus #2: `defaultProfile: prod` + `profiles: {…}` block form with `prod: {password: x}` → only the
    nested one changes; also an unrelated profile `other` with `password: x` stays.
  - CRLF input stays CRLF everywhere.
  - Legacy file with top-level `Password: x` → replaced.
  - Block scalar `password: |` → error.
  - Unknown profile → error. Profile without `password` key → error "has no password".
  - `AddProfileText` on `""` → `profiles:\n  "new":\n    connectionString: "…"\n    password: "enc:…"\n` parses
    with `ProfileStore` to the same fields (round-trip through a temp file).
  - `AddProfileText` keeps comments of an existing file; duplicate name errors; `ReplaceExisting::Yes` replaces and
    leaves sibling profiles + comments intact.
  - `QuoteYamlScalar("a\"b\\c")` == `"\"a\\\"b\\\\c\""`; value with `;{}` round-trips through yaml-cpp.
  - `SetDefaultProfileText` both cases.
  - `EditConfigFile` on read-only file → error (Windows: set `readonly` perms via `std::filesystem::permissions`).
- [ ] **Step 2:** Run → FAIL. **Step 3:** Implement. **Step 4:** Run → PASS.
- [ ] **Step 5:** Commit `feat(tools): comment-preserving dbtool.yml editor`.

### Task 5: `password` in ProfileStore

**Files:** Modify `src/tools/shared/Config/ProfileStore.{hpp,cpp}`; Test `src/tests/ConfigProfileStoreTests.cpp`.

- [ ] **Step 1: Failing tests:** `password: "enc:dev:x"` loads verbatim; `password` + `secretRef` → error containing
  `"both 'password' and 'secretRef'"`; legacy `Password:` loads; `Save` round-trips `password`.
- [ ] **Step 2:** Run `[ProfileStore]` → FAIL.
- [ ] **Step 3:** Add `std::string password;` (doxygen: "Encrypted (`enc:…`) or plaintext password; see ProfileCipher"),
  parse `password`/`Password`, conflict check next to the dsn/connectionString check (also for legacy shape), emit in
  `Save`. Update header comment ("Secret material … never written" → "only encrypted passwords are written by
  dbtool; plaintext ones are upgraded after a successful connect").
- [ ] **Step 4:** PASS. **Step 5:** Commit `feat(tools): ProfileStore password field`.

### Task 6: Password resolution & plaintext upgrade

**Files:** Create `src/tools/shared/Config/ProfilePassword.{hpp,cpp}`; Test `src/tests/ProfilePasswordTests.cpp`.

**Interfaces — Consumes:** Tasks 2, 4, 5. **Produces:**
```cpp
namespace Lightweight::Config
{
enum class PasswordOrigin : std::uint8_t { None, Encrypted, Plaintext, SecretRef };
struct ResolvedPassword { std::string value; PasswordOrigin origin = PasswordOrigin::None; };
[[nodiscard]] std::expected<ResolvedPassword, std::string> ResolveProfilePassword(Profile const& profile, Secrets::ProfileCipher const& cipher, Secrets::SecretResolver const& resolver);
struct UpgradeResult { bool insideGitWorkTree = false; };
[[nodiscard]] std::expected<UpgradeResult, std::string> UpgradePlaintextPassword(std::filesystem::path const& configPath, Profile const& profile, Secrets::ProfileCipher const& cipher);
[[nodiscard]] bool IsInsideGitWorkTree(std::filesystem::path const& path);
}
```

- [ ] **Step 1: Failing tests** (`[ProfilePassword]`):
  - encrypted → `Encrypted` with decrypted value; plaintext → `Plaintext`; `secretRef: env:LW_TEST_PWD` (set env) →
    `SecretRef`; nothing → `None`; review-focus #5: `password: ""` → `None`.
  - bad ciphertext → error containing profile name.
  - `UpgradePlaintextPassword` on temp file → file now has `enc:` value that decrypts to the original; comments kept;
    `insideGitWorkTree` true when a `.git` dir is created in the temp root, false otherwise.
  - DB-backed (`[ProfilePassword][db]`): SQLite profile from the test env's connection string with
    `password: "irrelevant"` → `ToConnectInfo(pw)` connects (SQLite ignores PWD), then upgrade succeeds. Review-focus #4:
    connection string already containing `PWD=inline` + `password: other` → `ToConnectInfo` keeps `PWD=inline`
    (existing behaviour), and the upgrade still rewrites `password` (it was the field used).
- [ ] **Step 2:** FAIL. **Step 3:** Implement; upgrade = `cipher.Encrypt(profile.password)` →
  `EditConfigFile(path, SetProfilePasswordText(...))`. **Step 4:** PASS.
- [ ] **Step 5:** Commit `feat(tools): resolve profile passwords and upgrade plaintext ones`.

### Task 7: dbtool integration (discovery, resolution, upgrade, list-profiles)

**Files:** Modify `src/tools/dbtool/main.cpp`.

- [ ] **Step 1:** Add to `Options`: `std::filesystem::path resolvedConfigPath; Config::ConfigSource configSource;`
  and `std::optional<Config::Profile> pendingPasswordUpgrade;`.
- [ ] **Step 2:** `ApplyProfileToOptions`: replace path logic with `FindConfigFile(DefaultDiscoveryInputs(options.configFile))`
  (error → `Error: …`, exit). `--verbose` prints `Using config: <path> (<source>)` to stderr. Replace
  `ProfileToConnectionString` with:
  `ResolveProfilePassword(*profile, ProfileCipher::Builtin(), resolver)` → `std::format("{}", profile->ToConnectInfo(pw.value))`;
  if origin `Plaintext`, set `pendingPasswordUpgrade = *profile`. Delete `ProfileToConnectionString` (no other users).
- [ ] **Step 3:** In `main`, after `SetupConnectionString`, if `pendingPasswordUpgrade`: probe
  `SqlConnection conn { std::nullopt }; if (conn.Connect(options.connectionString))` → `UpgradePlaintextPassword`;
  print `Encrypted the plaintext password of profile '<n>' in <path>.` (stderr), plus git-history warning when
  `insideGitWorkTree`; on upgrade error print `Warning: could not encrypt … : <err>` and continue. Probe failure:
  no message (the command reports the connection error itself).
- [ ] **Step 4:** `ListProfiles`: use discovery; header `Profiles (from <path>, found via <source>):`; add `AUTH`
  column: `encrypted` / `plaintext` / `secretRef` / `-`. "No profile configuration found" message lists the search.
- [ ] **Step 5:** Update `PrintUsage` `--config` help: "Path to configuration file (default: nearest dbtool.yml
  above the current or executable directory, then the user config)"; fix the `~/.config/dbtool/dbtool.yml` hint in
  `SetupConnectionString`.
- [ ] **Step 6:** Build `dbtool`; manual smoke: `dbtool list-profiles` in a temp dir with a `dbtool.yml`.
- [ ] **Step 7:** Commit `feat(dbtool): config discovery and encrypted profile passwords`.

### Task 8: `dbtool add-profile`

**Files:** Modify `src/tools/dbtool/main.cpp`.

- [ ] **Step 1:** Options: `profileNameArg` via `--name`, `dsn` (`--dsn`), `uid` (`--uid`), `noPassword`
  (`--no-password`), `setDefault` (`--set-default`), `force` (`--force`). Reuse `--connection-string`, `--schema`,
  `--plugins-dir`. `--password*` on argv is rejected: `Error: passwords are never accepted on the command line`.
- [ ] **Step 2:** Dispatch `add-profile` in the early (no-DB) block **before** `ApplyProfileToOptions` side effects
  matter: `ApplyProfileToOptions` must skip profile application when `command == "add-profile"`.
- [ ] **Step 3:** `AddProfileCommand(options)`:
  1. validate `--name` and exactly one of `--connection-string`/`--dsn`;
  2. target = discovery result path (`None` → user default);
  3. if connection string has `PWD`/`PASSWORD` attribute (via `ParseConnectionString`), remove it from the stored
     string (rebuild from parsed attributes) and use it as the password, printing a notice;
  4. else unless `--no-password`: `StdinBackend{}.Read(name)` (prompt on TTY, one line from pipe);
  5. encrypt with `ProfileCipher::Builtin()`;
  6. `EditConfigFile(target, AddProfileText(...))`, then `SetDefaultProfileText` if `--set-default`;
  7. print `Added profile '<n>' to <path>.`
- [ ] **Step 4:** Help text + `PrintExamples` entry.
- [ ] **Step 5:** Smoke: `echo secret | dbtool --config tmp.yml add-profile --name x --connection-string "Driver=SQLite3;Database=x.db"`
  then `dbtool --config tmp.yml list-profiles` shows `encrypted`.
- [ ] **Step 6:** Commit `feat(dbtool): add-profile command writing encrypted passwords`.

### Task 9: dbtool-gui integration

**Files:** Modify `src/tools/dbtool-gui/AppController.{hpp,cpp}`, `ManagedBackupCore.cpp`; Test
`src/tools/dbtool-gui/tests/AppControllerTests.cpp`, `ManagedBackupCoreTests.cpp`.

- [ ] **Step 1: Failing tests:**
  - `ResolveConnectionString` with `password: <enc of "pw">` and a connection string → result contains `PWD=pw`.
  - `AppController` with an explicit store path pointing at a fixture whose profile has plaintext `password` and a
    SQLite connection string → `connectToProfile()` true and the fixture file (copied to temp) now holds `enc:`.
- [ ] **Step 2:** FAIL.
- [ ] **Step 3:** Implement:
  - startup + `loadProfiles({})` + `defaultProfileStorePath()`: when `_profileStorePath` is empty use
    `FindConfigFile(DefaultDiscoveryInputs())` (exe dir = `QCoreApplication::applicationDirPath()` overriding the
    default when available); log `Profile store: <path> (found via <source>)`.
  - `connectToProfile` profile mode: `ResolveProfilePassword` → `format("{}", profile->ToConnectInfo(pw))`; on
    resolution error `ReportError`. After successful `GetDataMapper()`, if origin `Plaintext`: upgrade, `LogInfo`
    success, `LogWarning` git note, `LogWarning` on failure. Copy the profile before upgrade (the file watcher reload
    replaces `_store`).
  - `ManagedBackupCore::ResolveConnectionString`: use `ResolveProfilePassword(profile, ProfileCipher::Builtin(), resolver)`.
- [ ] **Step 4:** Build `dbtool-gui` + `dbtool-gui-tests`; run → PASS.
- [ ] **Step 5:** Commit `feat(dbtool-gui): config discovery and encrypted profile passwords`.

### Task 10: End-to-end `test_dbtool.py`

**Files:** Modify `src/tests/test_dbtool.py` (new numbered sections after 12a).

- [ ] **Step 1:** Add sections:
  - **12b add-profile:** pipe password via stdin into `add-profile --name e2e --connection-string <test-env CS without PWD>`;
    assert file contains `enc:` and not the password; `list-profiles` shows `encrypted`; duplicate fails; `--force` ok;
    `--password hunter2` rejected.
  - **12c discovery:** write `dbtool.yml` in `tmp/proj`, run `dbtool list-profiles` with `cwd=tmp/proj/sub/dir` →
    output mentions `tmp/proj/dbtool.yml` and `current directory`.
  - **12d auto-upgrade:** profile with plaintext `password: <real test-env PWD>` (or dummy for SQLite) and the
    test-env CS stripped of PWD; run `dbtool --config f --profile p status` (or `migrations`) → file now contains
    `enc:`; second run still succeeds. With a wrong plaintext password on MSSQL/Postgres → command fails, file unchanged.
- [ ] **Step 2:** Run against sqlite3, mssql2022, postgres (Docker harness). **Step 3:** Commit `test(dbtool): e2e for add-profile, discovery, password upgrade`.

### Task 11: CI + docs

**Files:** `.github/workflows/build.yml`, `docs/dbtool.md`, `src/tools/dbtool-gui/README.md`, spec (backup note).

- [ ] **Step 1:** Add `libssl-dev` to every Linux `apt install` line that configures the project (and to any
  Dockerfile/script used by those jobs; grep `libzip-dev`). Export `DBTOOL_MASTER_KEYS: ${{ secrets.DBTOOL_MASTER_KEYS }}`
  only in jobs that produce release artifacts (identify by `upload-artifact` of packages / `release` triggers).
- [ ] **Step 2:** `docs/dbtool.md`: "Configuration file discovery", "Profile passwords" (`password`, `add-profile`,
  auto-encrypt, security model table, key rotation, dev-key note).
- [ ] **Step 3:** GUI README: discovery + where the active file is shown.
- [ ] **Step 4:** Commit `docs(dbtool): config discovery and encrypted passwords`; `ci: libssl-dev and master key secret`.

### Task 12: Verification

- [ ] `clang-format -i` on all touched C++ files.
- [ ] Build `cl-release` (Windows, user preference: release builds) — warnings are errors.
- [ ] `LightweightTest --test-env=sqlite3|mssql2022|postgres` full suite.
- [ ] `test_dbtool.py` × 3 DBs; dbtool-gui tests.
- [ ] WSL `g++-14`: compile `CryptoPrimitives_openssl.cpp` and all new shared TUs (`-fsyntax-only`, with `libssl-dev`).
- [ ] Record results for the PR summary (DBs, compilers, macOS backend = CI-only).
