// SPDX-License-Identifier: Apache-2.0
//
// Multi-profile connection store, shared by dbtool and the migrations GUI.
//
// Extends the single-profile YAML format that `dbtool` used previously:
//   PluginsDir: ./migrations
//   ConnectionString: "DSN=acme-prod;UID=deploy"
//   Schema: dbo
// backward-compatibly. Such a file loads as one anonymous profile called
// "default". The new multi-profile format looks like:
//
//   defaultProfile: acme-prod
//   profiles:
//     acme-prod:
//       pluginsDir: ./migrations
//       schema: dbo
//       dsn: ACME_PROD
//       uid: deploy
//       secretRef: lightweight/acme-prod
//     acme-dev:
//       connectionString: "Driver=SQLite3;Database=dev.db"
//
// Secret material (`password`) is never written to the YAML file. Callers
// resolve `secretRef` via `Lightweight::Secrets::SecretResolver` before
// attempting to open a connection.

#pragma once

#include "../Api.hpp"
#include "../SqlConnectInfo.hpp"

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace Lightweight::Config
{

/// A single named connection profile.
///
/// Exactly one of `dsn` or `connectionString` is populated; the remaining
/// field is left empty. `uid` and `secretRef` provide auth metadata that
/// applies to both forms.
struct Profile
{
    /// Profile name, unique within a ProfileStore.
    std::string name;

    /// Plugin search directory. Relative paths are resolved relative to
    /// the current working directory at load time.
    std::filesystem::path pluginsDir;

    /// Optional database schema (e.g. "dbo").
    std::string schema;

    /// ODBC data source name. Mutually exclusive with `connectionString`.
    std::string dsn;

    /// Raw ODBC connection string. Mutually exclusive with `dsn`.
    std::string connectionString;

    /// Username used when connecting. Empty means "ask the driver manager".
    std::string uid;

    /// Opaque reference resolved by SecretResolver at connect time. May be
    /// empty (no stored secret) or a prefixed ref like
    /// `env:ACME_PROD_PWD`, `file:~/.dbtool/acme-prod.pwd`,
    /// `keychain:lightweight/acme-prod`, `stdin:`.
    std::string secretRef;

    /// True when the profile carries enough info to attempt a connection.
    [[nodiscard]] bool HasConnection() const noexcept
    {
        return !dsn.empty() || !connectionString.empty();
    }

    /// Builds an ODBC-level connect descriptor from this profile.
    ///
    /// `password` is the secret value previously resolved by the caller;
    /// passing an empty string is valid (driver may prompt or use
    /// integrated auth).
    [[nodiscard]] LIGHTWEIGHT_API SqlConnectInfo ToConnectInfo(std::string_view password = {}) const;
};

/// In-memory collection of named `Profile`s plus "which is the default".
class LIGHTWEIGHT_API ProfileStore
{
  public:
    ProfileStore() = default;

    /// Path where `LoadOrDefault()` looks when given no explicit path.
    ///
    /// - Windows: `%APPDATA%\dbtool\dbtool.yml`
    /// - POSIX:   `$XDG_CONFIG_HOME/dbtool/dbtool.yml` or
    ///            `$HOME/.config/dbtool/dbtool.yml`
    [[nodiscard]] static std::filesystem::path DefaultPath();

    /// Reads the store from `path`. Empty path means `DefaultPath()`.
    /// A missing file yields an empty store (not an error); any other IO
    /// or parse failure is reported via the error channel.
    [[nodiscard]] static std::expected<ProfileStore, std::string> LoadOrDefault(std::filesystem::path path = {});

    /// Writes the store back to `path`. Empty path means `DefaultPath()`.
    /// Creates parent directories as needed. Never writes secret values.
    [[nodiscard]] std::expected<void, std::string> Save(std::filesystem::path path = {}) const;

    /// All profiles in insertion order.
    [[nodiscard]] std::vector<Profile> const& Profiles() const noexcept
    {
        return _profiles;
    }

    /// Looks up a profile by name. Returns nullptr if none matches.
    [[nodiscard]] Profile const* Find(std::string_view name) const noexcept;

    /// Returns the default profile (by `DefaultProfileName()`), or the
    /// first profile if no default is set, or nullptr if empty.
    [[nodiscard]] Profile const* Default() const noexcept;

    /// Name of the default profile. Empty when unset.
    [[nodiscard]] std::string const& DefaultProfileName() const noexcept
    {
        return _defaultProfile;
    }

    /// Inserts or replaces a profile with the given name.
    void Upsert(Profile profile);

    /// Removes a profile by name. Returns true if removed.
    bool Remove(std::string_view name);

    /// Sets the default profile name. Pass "" to clear.
    void SetDefault(std::string name)
    {
        _defaultProfile = std::move(name);
    }

    /// Number of profiles.
    [[nodiscard]] std::size_t Size() const noexcept
    {
        return _profiles.size();
    }

    /// True when there are no profiles.
    [[nodiscard]] bool Empty() const noexcept
    {
        return _profiles.empty();
    }

  private:
    std::vector<Profile> _profiles;
    std::string _defaultProfile;
};

} // namespace Lightweight::Config
