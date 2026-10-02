// SPDX-License-Identifier: Apache-2.0
//
// Thin, OS-neutral facade over the platform's own cryptography: Windows CNG,
// OpenSSL libcrypto on Linux, CommonCrypto + Security.framework on macOS. One
// translation unit per OS implements these functions; CMake compiles exactly
// one of them. No cryptographic primitive is implemented in this project.
//
// Internal to `tools_shared` — consumers use `ProfileCipher`.

#pragma once

#include <array>
#include <cstddef>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace Lightweight::Secrets::Crypto
{

/// AES-256 key length in bytes.
inline constexpr std::size_t AesKeySize = 32;

/// AES block (and CBC IV) length in bytes.
inline constexpr std::size_t AesBlockSize = 16;

/// HMAC-SHA256 output length in bytes.
inline constexpr std::size_t HmacSize = 32;

/// Owned byte buffer returned by the cipher functions.
using Bytes = std::vector<std::byte>;

/// Encrypts with AES-256-CBC and PKCS#7 padding.
/// @param key 32-byte AES key.
/// @param iv 16-byte initialisation vector.
/// @param plaintext Data to encrypt (may be empty).
/// @return Ciphertext (a non-zero multiple of the block size), or an error message.
[[nodiscard]] std::expected<Bytes, std::string> AesCbcEncrypt(std::span<std::byte const, AesKeySize> key,
                                                              std::span<std::byte const, AesBlockSize> iv,
                                                              std::span<std::byte const> plaintext);

/// Decrypts AES-256-CBC with PKCS#7 padding.
/// @param key 32-byte AES key.
/// @param iv 16-byte initialisation vector.
/// @param ciphertext Data produced by `AesCbcEncrypt`.
/// @return Plaintext, or an error message (e.g. invalid padding).
[[nodiscard]] std::expected<Bytes, std::string> AesCbcDecrypt(std::span<std::byte const, AesKeySize> key,
                                                              std::span<std::byte const, AesBlockSize> iv,
                                                              std::span<std::byte const> ciphertext);

/// Computes HMAC-SHA256.
/// @param key MAC key of any length.
/// @param data Message to authenticate.
/// @return The 32-byte MAC, or an error message.
[[nodiscard]] std::expected<std::array<std::byte, HmacSize>, std::string> HmacSha256(std::span<std::byte const> key,
                                                                                    std::span<std::byte const> data);

/// Fills `out` with bytes from the OS cryptographically secure RNG.
/// @param out Buffer to fill.
/// @return Nothing on success, or an error message.
[[nodiscard]] std::expected<void, std::string> RandomBytes(std::span<std::byte> out);

} // namespace Lightweight::Secrets::Crypto
