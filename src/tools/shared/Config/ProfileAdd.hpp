// SPDX-License-Identifier: Apache-2.0
//
// Adding a profile to dbtool.yml, shared by `dbtool add-profile` and dbtool-gui's
// "Save as profile…" / "Add profile…" so both behave identically: the password is
// encrypted before it touches the file, the rest of the file (comments included)
// is left alone, and the write is atomic.

#pragma once

#include "ProfileFileEditor.hpp"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>

#include <Secrets/ProfileCipher.hpp>

namespace Lightweight::Config
{

/// A profile to add. Unlike `NewProfile`, `password` is the plaintext secret (or empty):
/// `AddProfileToFile` encrypts it.
struct ProfileRequest
{
    /// Profile name (the key under `profiles:`).
    std::string name;

    /// Raw ODBC connection string; mutually exclusive with `dsn`. An inline `PWD=` /
    /// `Password=` attribute is moved out of it and stored encrypted instead.
    std::string connectionString;

    /// ODBC data source name; mutually exclusive with `connectionString`.
    std::string dsn;

    /// User name.
    std::string uid;

    /// Default schema.
    std::string schema;

    /// Plugin directory.
    std::string pluginsDir;

    /// Plaintext password, or empty for none. Wins over a password found inside `connectionString`.
    std::string password;
};

/// Whether the added profile also becomes `defaultProfile`.
enum class MakeDefault : std::uint8_t
{
    No,
    Yes,
};

/// A connection string split into the part that is safe to store and its password.
struct InlinePasswordSplit
{
    /// The connection string without any `PWD=` / `Password=` attribute.
    std::string connectionString;

    /// The password that was in it; empty when there was none.
    std::string password;
};

/// Splits an inline `PWD=`/`Password=` attribute out of a connection string, so the password
/// can be stored encrypted instead of in clear inside the connection string.
/// @param connectionString Connection string as typed by the user.
/// @return The connection string without the password, and the password (empty if none).
[[nodiscard]] InlinePasswordSplit SplitInlinePassword(std::string const& connectionString);

/// Writes `request` as a new profile into the dbtool.yml at `path` (created, with its parent
/// directories, when missing). The password, if any, is encrypted with `cipher`; the rest of an
/// existing file is preserved byte for byte.
/// @param path The dbtool.yml to edit.
/// @param request The profile to add.
/// @param replace Whether an existing profile of the same name may be replaced.
/// @param makeDefault Whether to also set it as the default profile.
/// @param cipher Cipher for the password (normally `ProfileCipher::Builtin()`).
/// @return Nothing on success, or a user-displayable error (missing name, both or neither of
///         dsn/connection string, duplicate name, read-only file, ...). The file is untouched on failure.
[[nodiscard]] std::expected<void, std::string> AddProfileToFile(std::filesystem::path const& path,
                                                                ProfileRequest const& request,
                                                                ReplaceExisting replace,
                                                                MakeDefault makeDefault,
                                                                Secrets::ProfileCipher const& cipher);

} // namespace Lightweight::Config
