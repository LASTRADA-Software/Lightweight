# Lightweight::Profiles - reading and writing dbtool profiles from your application

`Lightweight::Profiles` is a small installed shared library (`LightweightProfiles`, a DLL on
Windows) that gives any application the connection profiles of `dbtool.yml` exactly as
[dbtool](dbtool.md) and `dbtool-gui` see them: the same discovery, the same layering, the same
precedence and the same password handling. A profile your application saves is the profile the next
`dbtool` run, or the next call into the library, sees.

Only standard-library types appear in its public header. yaml-cpp, the key ring and every other
internal stay inside the library; on Linux and macOS it exports nothing but its own
`Lightweight::Profiles` namespace.

## Using it from CMake

The library is part of the `Lightweight` package. It is installed when Lightweight was built with
`LIGHTWEIGHT_BUILD_TOOLS=ON` (the default); a package installed without tools is still valid but has
no `Lightweight::Profiles` target.

```cmake
find_package(Lightweight CONFIG REQUIRED)

add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE Lightweight::Profiles)
```

```cpp
#include <Lightweight/Profiles/ProfileConnection.hpp>
```

The header is self-contained and needs only C++23.

A Lightweight build that fetched `reflection-cpp` at configure time (rather than through a package
manager) does not install it, yet the package references it; in that case your project must provide
`reflection-cpp` itself (a pre-existing limitation of the core package).

## The six functions

All live in `Lightweight::Profiles`. The three that touch profiles take
`std::optional<std::filesystem::path> startDirectory` (see [Lookup rules](#lookup-rules)); the
fallible ones return `std::expected<T, std::string>` whose error text names the file and profile
involved but never contains a secret.

| Function | Purpose |
|----------|---------|
| `ResolveProfileConnection(startDirectory, profileName)` | Resolve one profile to DSN / connection string, user id and **decrypted** password. |
| `ListProfiles(startDirectory)` | List the merged profiles; never returns a password. |
| `SaveProfile(startDirectory, definition)` | Add or replace a profile, storing its password encrypted. |
| `IsEncryptedPassword(value)` | Whether a stored value is in the `enc:<keyId>:<payload>` format. |
| `EncryptPassword(plaintext)` | Encrypt with the newest key of dbtool's key ring. |
| `DecryptPassword(value)` | Decrypt a value written by `EncryptPassword` or by dbtool. |

```cpp
#include <Lightweight/Profiles/ProfileConnection.hpp>

#include <filesystem>
#include <iostream>
#include <optional>

namespace Profiles = Lightweight::Profiles;

int main()
{
    auto const projectDirectory = std::optional { std::filesystem::current_path() };

    // Store (or replace) a profile; the password is written encrypted.
    auto const saved = Profiles::SaveProfile(projectDirectory,
                                             Profiles::ProfileDefinition {
                                                 .name = "reporting",
                                                 .connectionString = "DRIVER={PostgreSQL Unicode};Server=db;Database=app",
                                                 .uid = "report",
                                                 .password = "s3cret",
                                             });
    if (!saved)
    {
        std::cerr << saved.error() << '\n';
        return 1;
    }

    // List what is configured. No password is ever part of the result.
    if (auto const list = Profiles::ListProfiles(projectDirectory))
        for (auto const& profile: list->profiles)
            std::cout << profile.name << " (from " << profile.configFile.string() << ")\n";

    // Resolve one profile, with its password, for connecting.
    auto const connection = Profiles::ResolveProfileConnection(projectDirectory, "reporting");
    if (!connection)
    {
        std::cerr << connection.error() << '\n';
        return 1;
    }
    // connection->connectionString, connection->uid and connection->password are ready to use.
}
```

`ResolveProfileConnection` with an empty profile name selects the merged `defaultProfile`. If that
is unset, or names a profile that does not exist, an error is returned; it never falls back to the
first profile. A profile with neither a DSN nor a connection string is an error. A plaintext
password found in the file is returned as is and is not upgraded in place. `secretRef` values are
resolved through the environment and file backends (there is no interactive prompt and no stdin
backend).

`ListProfiles` returns `configFiles` (the layers loaded, nearest first), `newProfileTarget` (the file
a *new* profile would be saved to), the merged `defaultProfile` and the profiles. Connection strings
have any `PWD=` value removed; `hasPassword` only says whether a password, a `secretRef` or a `PWD=`
is set. The paths in `configFiles` and `ProfileSummary::configFile` come back in the form discovery
produced them (the directories walked are canonical). Compare them with
`std::filesystem::equivalent`, not with string equality:

```cpp
auto const sameFile = [](std::filesystem::path const& a, std::filesystem::path const& b) {
    std::error_code error;
    return std::filesystem::equivalent(a, b, error);
};
```

## Lookup rules

- **`startDirectory` given:** every `dbtool.yml` from that directory upwards is loaded, nearest
  first. A file whose top level sets `root: true` ends the walk: it is loaded, nothing above it is,
  and the per-user file is not consulted. When no file set `root: true`, the per-user file is
  appended as the last layer. A relative path is made absolute against the working directory.
- **`std::nullopt`:** only the per-user file is consulted: `~/.config/dbtool/dbtool.yml`
  (honouring `$XDG_CONFIG_HOME`) or `%APPDATA%\dbtool\dbtool.yml`, or `dbtool.yml` inside
  `LIGHTWEIGHT_DBTOOL_USER_CONFIG_DIR` when that variable is set (a relative value resolves against
  the working directory).
- The layers are merged as dbtool merges them: the nearest file wins per profile name and per
  file-wide value, and a profile from a nearer file replaces a same-named one from a farther file as
  a whole. See [Where dbtool finds `dbtool.yml`](dbtool.md#where-dbtool-finds-dbtoolyml).
- `LIGHTWEIGHT_DBTOOL_CONFIG_SEARCH_DIR` is **ignored** by the library: the `startDirectory`
  argument is the whole input. (Unlike dbtool, the library does not start from an executable's
  directory; pass the directory you want.)
- No config file at all is not an error: `ListProfiles` returns an empty list.

## Saving profiles

`SaveProfile` behaves like `dbtool add-profile --force`:

- Exactly one of `dsn` and `connectionString` must be set. An inline `PWD=` / `Password=` in the
  connection string is moved out and stored encrypted; an explicit `password` wins over it.
- **Replacing is wholesale.** The old entry is removed and the new one written, so fields that
  `ProfileDefinition` cannot express are **dropped**: `schema`, `pluginsDir`, `backupDir` and
  `secretRef`. An empty `password` leaves the profile without one. The profile **moves to the top**
  of its layer; the rest of the file (comments, formatting, other profiles) is preserved byte for
  byte.
- An existing profile is replaced in the layer it came from. A new profile goes to the `root: true`
  layer if there is one, otherwise to the per-user file, created if missing
  (`ProfileList::newProfileTarget` tells you which in advance).
- Saving into a legacy-format (single-profile) layer fails.
- The profile name must not be empty.

### Writing when the directory is read-only

The file is normally edited by writing a temporary sibling and renaming it over the original. If
the temporary file cannot be created because of a permission error but the target file exists and is
writable (an installation may grant users modify rights on `dbtool.yml` only, not on its directory),
the target is rewritten **in place** instead. That fallback is not atomic, and it writes *through* a
symlink, where the rename replaces the link. If the in-place write fails part-way, the original
bytes are restored on a best-effort basis and the error is reported. On Windows the writability
check sees only the read-only attribute, not ACLs; the ACL case is exactly what the fallback covers.
`dbtool add-profile` and `dbtool-gui` use the same code and behave the same way.

## No secrets leak

- `ListProfiles` never returns a password, and connection strings are `PWD=`-sanitised.
- No error string contains a plaintext password, a key or a decrypted value.
- `DecryptPassword` errors do not echo their input.

## Same build as dbtool, same toolchain

- **Same build as dbtool.** The library compiles in the key ring of the build it comes from
  (`DBTOOL_MASTER_KEYS` / `DBTOOL_MASTER_KEYS_FILE`, see
  [Providing the master key at build time](dbtool.md#providing-the-master-key-at-build-time)), so it
  can only read passwords written with the same keys. To share profiles with `dbtool`, install the
  library and dbtool from the same build. Official (release-key) builds refuse `enc:dev:` values,
  exactly as dbtool does.
- **Same toolchain.** `std::string`, `std::filesystem::path`, `std::vector`, `std::optional` and
  `std::expected` cross the library boundary. Build your application with the same compiler and
  standard library as the library, and on MSVC with a matching Debug/Release runtime (use the
  Debug build of the library from a Debug application). Like the core `Lightweight` library, this is
  documented rather than engineered around.

## Runtime dependencies

The installed library needs, next to or on the loader search path of your application:

| Platform | Needed |
|----------|--------|
| Windows | `LightweightProfiles.dll`, plus `Lightweight.dll` (when built with `LIGHTWEIGHT_BUILD_SHARED=ON`), the yaml-cpp DLL under a dynamic vcpkg triplet, and the VC++ runtime. The install step bundles these next to the DLL unless `LIGHTWEIGHT_INSTALL_RUNTIME_DEPENDENCIES=OFF` (do that under a package manager that installs them itself). Put the install `bin` directory on `PATH` or next to your executable. |
| Linux | `libLightweightProfiles.so` (with its SONAME links) in the install `lib` directory, plus `libLightweight.so` when built with `LIGHTWEIGHT_BUILD_SHARED=ON`, and `libcrypto` and yaml-cpp when they are shared libraries. Use an RPATH or `LD_LIBRARY_PATH`. |
| macOS | `libLightweightProfiles.dylib`, plus yaml-cpp when shared. Use an RPATH or `DYLD_LIBRARY_PATH`. |

When Lightweight is built with `LIGHTWEIGHT_BUILD_SHARED=OFF` (the non-Windows default), the library
embeds a private copy of Lightweight. Its symbols are hidden, but its statics (logger, connection
pools) are separate from any Lightweight your application links itself.

## Security note

The installed library decrypts any `enc:` value for anyone who can load it, the same model as the
dbtool binary: the encryption protects the file against being read, not against code running as the
user. Do not treat an encrypted password in `dbtool.yml` as a secret from processes that can load
`LightweightProfiles`.
