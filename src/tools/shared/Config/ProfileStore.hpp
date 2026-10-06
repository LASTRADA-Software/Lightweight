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
//   defaultPluginsDir: ./migrations
//   defaultBackupDir: D:/backups
//   profiles:
//     acme-prod:
//       schema: dbo
//       dsn: ACME_PROD
//       uid: deploy
//       secretRef: lightweight/acme-prod
//       backupDir: D:/backups/prod     # overrides defaultBackupDir
//     acme-dev:
//       pluginsDir: ./dev-migrations   # overrides defaultPluginsDir
//       connectionString: "Driver=SQLite3;Database=dev.db"
//
// `defaultBackupDir` / `backupDir` name the folder backups are written to. They resolve like
// `pluginsDir`: a profile's own `backupDir`, else the top-level `defaultBackupDir`, else nothing.
//
// `defaultPluginsDir` is a top-level fallback used by any profile that does
// not set its own `pluginsDir`. It accepts either a single string or a
// sequence of strings:
//
//   defaultPluginsDir:
//     - ./migrations
//     - /opt/lightweight/plugins
//
// When the same plugin filename is present in more than one of these
// directories the plugin loader keeps the file with the newest modification
// time and discards the others (see `PluginDiscovery`).
//
// A profile authenticates either with a `password` stored in the file — kept
// encrypted (`enc:<keyId>:...`); dbtool encrypts plaintext values in place
// after a successful connect — or with a `secretRef` resolved by
// `Lightweight::Secrets::SecretResolver`. See `Config/ProfilePassword.hpp`.

#pragma once

#include <Lightweight/SqlConnectInfo.hpp>

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

    /// Password stored in the file: either encrypted (`enc:<keyId>:...`, see
    /// `Secrets::ProfileCipher`) or plaintext, which dbtool encrypts in place
    /// after the first successful connection. Mutually exclusive with `secretRef`.
    std::string password;

    /// Folder this profile's backups are written to; overrides the store's `defaultBackupDir`.
    /// Empty means "use the store-wide default". Relative paths are used as written.
    std::filesystem::path backupDir {};

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
    [[nodiscard]] SqlConnectInfo ToConnectInfo(std::string_view password = {}) const;
};

/// In-memory collection of named `Profile`s plus "which is the default".
class ProfileStore
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

    /// Store-wide fallback plugin search directories. Used when a profile
    /// does not declare its own `pluginsDir`. Empty when unset. May contain
    /// one or many directories: when multiple are listed and the same plugin
    /// filename appears in more than one, plugin-loading callers resolve
    /// the conflict by picking the file with the newest modification time
    /// (see `Lightweight::Tools::DiscoverPlugins`).
    [[nodiscard]] std::vector<std::filesystem::path> const& DefaultPluginsDir() const noexcept
    {
        return _defaultPluginsDir;
    }

    /// Sets the store-wide fallback plugin directories. Pass an empty vector
    /// to clear.
    void SetDefaultPluginsDir(std::vector<std::filesystem::path> paths)
    {
        _defaultPluginsDir = std::move(paths);
    }

    /// Effective plugin search directories for `profile`: a single-element
    /// vector containing the profile's own `pluginsDir` when set, otherwise
    /// the store-wide `defaultPluginsDir` list.
    [[nodiscard]] std::vector<std::filesystem::path> EffectivePluginsDir(Profile const& profile) const
    {
        if (!profile.pluginsDir.empty())
            return { profile.pluginsDir };
        return _defaultPluginsDir;
    }

    /// Store-wide fallback backup folder (`defaultBackupDir`). Empty when unset.
    [[nodiscard]] std::filesystem::path const& DefaultBackupDir() const noexcept
    {
        return _defaultBackupDir;
    }

    /// Sets the store-wide fallback backup folder. Pass an empty path to clear.
    void SetDefaultBackupDir(std::filesystem::path dir)
    {
        _defaultBackupDir = std::move(dir);
    }

    /// Backup folder for `profile`: its own `backupDir` when set, otherwise the store-wide
    /// `defaultBackupDir`. Empty when neither is set.
    [[nodiscard]] std::filesystem::path EffectiveBackupDir(Profile const& profile) const
    {
        return profile.backupDir.empty() ? _defaultBackupDir : profile.backupDir;
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
    std::vector<std::filesystem::path> _defaultPluginsDir;
    std::filesystem::path _defaultBackupDir;
};

} // namespace Lightweight::Config
