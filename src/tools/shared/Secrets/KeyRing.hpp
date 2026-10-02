// SPDX-License-Identifier: Apache-2.0
//
// Master keys used to encrypt dbtool.yml profile passwords.
//
// Official builds receive their key ring from the `DBTOOL_MASTER_KEYS`
// environment variable at CMake configure time (a CI secret, never committed):
//
//     DBTOOL_MASTER_KEYS=v2=<64 hex>,v1=<64 hex>
//
// The first entry encrypts; every entry decrypts, which is how keys rotate.
// Builds without that variable fall back to the public development key below.
//
// Security model: the keys are embedded (XOR-masked) in the executable. They
// protect a dbtool.yml from people who only have the file, not from anyone who
// holds an official dbtool binary. See docs/dbtool.md.

#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace Lightweight::Secrets
{

/// Identifier of the public development key used by builds without a CI key ring.
inline constexpr std::string_view DevKeyId = "dev";

/// One master key and the identifier written into every value it encrypts.
struct KeyEntry
{
    /// Short identifier (`[a-z0-9]{1,16}`), e.g. "v1" or "dev".
    std::string id;

    /// 32-byte master key.
    std::array<std::byte, 32> key {};
};

/// The public development key: SHA-256("lightweight-dbtool-public-development-key").
/// It is published in the source on purpose and protects nothing.
/// @return The development key entry with id `DevKeyId`.
[[nodiscard]] KeyEntry DevKey();

/// The key ring compiled into this executable, newest (encrypting) key first.
/// @return Either the CI-provided ring or `{ DevKey() }`.
[[nodiscard]] std::vector<KeyEntry> BuiltinKeyRing();

/// Whether this executable was built with a CI-provided key ring.
/// @return True for official builds, false for development builds.
[[nodiscard]] bool BuiltinKeyRingIsRelease() noexcept;

} // namespace Lightweight::Secrets
