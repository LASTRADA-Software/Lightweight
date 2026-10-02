// SPDX-License-Identifier: Apache-2.0
//
// Encrypts and decrypts the `password` field of dbtool.yml profiles.
//
// Wire format:  enc:<keyId>:<base64( iv[16] || ciphertext || tag[32] )>
//
//   encKey = HMAC-SHA256(master, "dbtool-enc")
//   macKey = HMAC-SHA256(master, "dbtool-mac")
//   ciphertext = AES-256-CBC/PKCS#7(encKey, iv, password)
//   tag = HMAC-SHA256(macKey, "enc:" || keyId || ":" || iv || ciphertext)
//
// Encrypt-then-MAC with the key id inside the MAC, so neither the payload nor
// its key label can be altered undetected. AES-CBC + HMAC (rather than GCM) is
// used because it is available through public OS APIs on Windows, Linux and
// macOS, and the same file must decrypt on all three.

#pragma once

#include "KeyRing.hpp"

#include <cstdint>
#include <expected>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Lightweight::Secrets
{

/// Prefix that marks a profile password as encrypted.
inline constexpr std::string_view EncryptedPrefix = "enc:";

/// Whether a cipher may decrypt values made with the public development key.
enum class DevKeyPolicy : std::uint8_t
{
    /// Development builds: `enc:dev:` values are accepted.
    Allow,
    /// Official builds: `enc:dev:` values are rejected with an explanatory error.
    Refuse,
};

/// Source of random IV bytes; injectable so tests can produce fixed vectors.
using RandomSource = std::function<std::expected<void, std::string>(std::span<std::byte>)>;

/// Encrypts/decrypts profile passwords with a ring of master keys.
class ProfileCipher
{
  public:
    /// Constructs a cipher over an explicit key ring.
    /// @param ring Master keys, newest first; the first entry encrypts. Must not be empty.
    /// @param policy Whether `enc:dev:` values may be decrypted.
    /// @param random IV source; defaults to the OS CSPRNG.
    ProfileCipher(std::vector<KeyEntry> ring, DevKeyPolicy policy, RandomSource random = {});

    /// The cipher over the key ring compiled into this executable.
    /// @return A process-wide instance; official builds refuse the development key.
    [[nodiscard]] static ProfileCipher const& Builtin();

    /// Whether `value` is in the encrypted wire format (starts with `enc:`).
    /// @param value A profile password as stored in dbtool.yml.
    /// @return True if the value must be decrypted before use.
    [[nodiscard]] static bool IsEncrypted(std::string_view value) noexcept;

    /// Encrypts a password with the newest key of the ring.
    /// @param plaintext The password.
    /// @return The `enc:<keyId>:<base64>` value, or an error message.
    [[nodiscard]] std::expected<std::string, std::string> Encrypt(std::string_view plaintext) const;

    /// Decrypts a value produced by `Encrypt` with any key of the ring.
    /// @param value An `enc:<keyId>:<base64>` value.
    /// @return The password, or an error message that never contains secret material.
    [[nodiscard]] std::expected<std::string, std::string> Decrypt(std::string_view value) const;

  private:
    std::vector<KeyEntry> _ring;
    DevKeyPolicy _policy;
    RandomSource _random;
};

} // namespace Lightweight::Secrets
