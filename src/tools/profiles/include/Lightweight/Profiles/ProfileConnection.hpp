// SPDX-License-Identifier: Apache-2.0
//
// Public API of the `Lightweight::Profiles` library: read and write the connection profiles of
// dbtool.yml exactly as dbtool sees them. Only standard-library types cross the boundary, so
// consumers must use the same compiler, standard library and (on MSVC) runtime as the library.
// All functions are safe to call concurrently from multiple threads.

#pragma once

#include "Api.hpp"

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Lightweight::Profiles
{

/// A profile resolved for connecting, including its decrypted password.
struct ProfileConnection
{
    /// The profile actually used (after applying `defaultProfile`).
    std::string name;
    /// ODBC data source name; empty if the profile uses a connection string.
    std::string dsn;
    /// ODBC connection string, unsanitised; empty if the profile uses a DSN.
    std::string connectionString;
    /// User id; may be empty.
    std::string uid;
    /// The decrypted password; empty if the profile has none.
    std::string password;
    /// The config file layer the profile came from.
    std::filesystem::path configFile;
};

/// Loads the merged view of all dbtool.yml layers and resolves one profile non-interactively.
///
/// The password is taken as `Config::ResolveProfilePassword` does (`password`, decrypted if it is
/// `enc:`-encrypted, then `secretRef` through the environment and file backends). A plaintext
/// password in the file is returned as is and not upgraded in place.
///
/// An empty @p profileName selects the merged `defaultProfile`. If that is unset, or names a
/// profile that does not exist, an error is returned; this never falls back to the first profile.
/// A profile with neither a DSN nor a connection string is an error.
///
/// Errors name the file and profile involved but never contain a secret.
///
/// @param startDirectory Directory where the layered lookup starts; relative paths are made
///                       absolute against the working directory. `std::nullopt` consults only the
///                       per-user file.
/// @param profileName Name of the profile to resolve, or empty for the `defaultProfile`.
/// @return The resolved connection, or a secret-free error message.
[[nodiscard]] LIGHTWEIGHT_PROFILES_API std::expected<ProfileConnection, std::string> ResolveProfileConnection(
    std::optional<std::filesystem::path> const& startDirectory, std::string_view profileName);

/// One profile as listed by `ListProfiles`; never carries a password.
struct ProfileSummary
{
    /// Profile name.
    std::string name;
    /// ODBC data source name; empty if the profile uses a connection string.
    std::string dsn;
    /// Connection string with any `PWD=` / `Password=` value masked as `***`; connect via
    /// `ResolveProfileConnection`, not with this string.
    std::string connectionString;
    /// User id; may be empty.
    std::string uid;
    /// True if a password, a `secretRef` or a `PWD=` in the connection string is set; the value is
    /// never returned.
    bool hasPassword = false;
    /// The config file layer the profile came from.
    std::filesystem::path configFile;
};

/// Result of `ListProfiles`.
struct ProfileList
{
    /// Layers loaded, nearest first; may be empty.
    std::vector<std::filesystem::path> configFiles;
    /// The file `SaveProfile` writes a *new* profile to.
    std::filesystem::path newProfileTarget;
    /// The merged `defaultProfile`; may be empty.
    std::string defaultProfile;
    /// The merged view of all layers; a profile in a nearer layer shadows one of the same name in a
    /// farther layer.
    std::vector<ProfileSummary> profiles;
};

/// Lists the profiles of the merged view of all dbtool.yml layers.
///
/// Never returns a password: connection strings have `PWD=` / `Password=` values masked as `***` and `hasPassword` only
/// tells whether one is set. No config file at all yields an empty list, not an error. Errors never contain secrets.
///
/// @param startDirectory Directory where the layered lookup starts; relative paths are made
///                       absolute against the working directory. `std::nullopt` consults only the
///                       per-user file.
/// @return The profile list, or a secret-free error message.
[[nodiscard]] LIGHTWEIGHT_PROFILES_API std::expected<ProfileList, std::string> ListProfiles(
    std::optional<std::filesystem::path> const& startDirectory);

/// A profile to store with `SaveProfile`.
struct ProfileDefinition
{
    /// Profile name; an existing profile of that name is replaced.
    std::string name;
    /// ODBC data source name. Exactly one of `dsn` and `connectionString` must be set.
    std::string dsn;
    /// ODBC connection string. An inline `PWD=`/`Password=` is moved out and stored encrypted.
    std::string connectionString;
    /// User id; may be empty.
    std::string uid;
    /// Plaintext password, stored encrypted; wins over one in `connectionString`. Empty means no
    /// password.
    std::string password;
};

/// Adds or replaces a profile in dbtool.yml, like `dbtool add-profile --force`.
///
/// Replacing is wholesale: the old entry is removed and the new one written, so fields that
/// `ProfileDefinition` cannot express (`schema`, `pluginsDir`, `backupDir`, `secretRef`) are
/// dropped, and an empty `password` leaves the profile without one. The profile moves to the top
/// of its layer. The rest of the file is preserved byte for byte.
///
/// An existing profile is replaced in the layer it came from; a new one is written to the
/// `root: true` layer if any, else to the per-user file (see `ProfileList::newProfileTarget`).
/// The password is encrypted with the same key ring as dbtool. Saving into a legacy-format
/// (single-profile) layer fails. Errors never contain secrets.
///
/// @param startDirectory Directory where the layered lookup starts; relative paths are made
///                       absolute against the working directory. `std::nullopt` consults only the
///                       per-user file.
/// @param definition The profile to store.
/// @return The config file written, or a secret-free error message.
[[nodiscard]] LIGHTWEIGHT_PROFILES_API std::expected<std::filesystem::path, std::string> SaveProfile(
    std::optional<std::filesystem::path> const& startDirectory, ProfileDefinition const& definition);

/// Whether @p value is in the encrypted password format (`enc:<keyId>:<payload>`).
/// @param value A password as stored in dbtool.yml.
/// @return True if the value must be decrypted before use.
[[nodiscard]] LIGHTWEIGHT_PROFILES_API bool IsEncryptedPassword(std::string_view value) noexcept;

/// Encrypts a password with the newest key of dbtool's key ring.
/// @param plaintext The password.
/// @return The `enc:<keyId>:<payload>` value, or an error message that never contains the password.
[[nodiscard]] LIGHTWEIGHT_PROFILES_API std::expected<std::string, std::string> EncryptPassword(std::string_view plaintext);

/// Decrypts a value produced by `EncryptPassword` or by dbtool.
/// @param value An `enc:<keyId>:<payload>` value.
/// @return The password, or an error message that never contains the input or the password.
[[nodiscard]] LIGHTWEIGHT_PROFILES_API std::expected<std::string, std::string> DecryptPassword(std::string_view value);

} // namespace Lightweight::Profiles
