// SPDX-License-Identifier: Apache-2.0
//
// Turns a profile's stored credentials into the password used to connect, and
// encrypts plaintext passwords in place once they have proven to work.
//
// Precedence: `password` (decrypted when encrypted) → `secretRef` → none.
// Shared by dbtool and dbtool-gui so both behave identically.

#pragma once

#include "ProfileStore.hpp"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>

#include <Secrets/ProfileCipher.hpp>
#include <Secrets/SecretResolver.hpp>

namespace Lightweight::Config
{

/// Where a resolved password came from.
enum class PasswordOrigin : std::uint8_t
{
    /// The profile carries no password.
    None,
    /// Decrypted from an `enc:` value in the file.
    Encrypted,
    /// Plaintext in the file — a candidate for `UpgradePlaintextPassword`.
    Plaintext,
    /// Resolved through `secretRef`.
    SecretRef,
};

/// A password ready to connect with, plus its origin.
struct ResolvedPassword
{
    /// The password; empty for `PasswordOrigin::None`.
    std::string value;

    /// Where it came from.
    PasswordOrigin origin = PasswordOrigin::None;
};

/// Resolves the password a profile should connect with.
/// @param profile Profile as loaded from dbtool.yml.
/// @param cipher Cipher for `enc:` values (normally `ProfileCipher::Builtin()`).
/// @param resolver Resolver for `secretRef`.
/// @return The password and its origin, or an error naming the profile (never containing the secret).
[[nodiscard]] std::expected<ResolvedPassword, std::string> ResolveProfilePassword(Profile const& profile,
                                                                                  Secrets::ProfileCipher const& cipher,
                                                                                  Secrets::SecretResolver const& resolver);

/// Outcome of a successful upgrade.
struct UpgradeResult
{
    /// True when the file lives in a git work tree: the old plaintext may remain in its history.
    bool insideGitWorkTree = false;
};

/// Encrypts `profile.password` (which must be plaintext) and rewrites it in
/// `configPath`, preserving the rest of the file. Call only after a connection
/// with that password succeeded.
/// @param configPath The dbtool.yml the profile was loaded from.
/// @param profile The profile whose plaintext password to encrypt.
/// @param cipher Cipher to encrypt with.
/// @return Upgrade details, or an error (file not writable, password not plaintext, ...).
[[nodiscard]] std::expected<UpgradeResult, std::string> UpgradePlaintextPassword(std::filesystem::path const& configPath,
                                                                                 Profile const& profile,
                                                                                 Secrets::ProfileCipher const& cipher);

/// Whether `path` or one of its ancestors contains a `.git` entry.
/// @param path File or directory to check.
/// @return True when inside a git work tree.
[[nodiscard]] bool IsInsideGitWorkTree(std::filesystem::path const& path);

} // namespace Lightweight::Config
