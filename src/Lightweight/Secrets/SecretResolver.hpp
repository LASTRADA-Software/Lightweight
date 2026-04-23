// SPDX-License-Identifier: Apache-2.0
//
// `SecretResolver` is the front door for password / API-key lookups. Callers
// (dbtool, the migrations GUI, downstream apps) hand it a `secretRef` string
// like `env:ACME_PROD_PWD`, `file:~/.dbtool/acme.pwd` or a bare
// `lightweight/acme-prod`; the resolver dispatches to the matching backend or
// walks its backend chain in registration order.
//
// Platform-native backends (`keychain:` on macOS, `wincred:` on Windows,
// `secretservice:` on Linux) live in separate translation units so that pure
// CLI builds (e.g. `dbtool`) can ship with only `env:` / `file:` / `stdin:`
// without pulling Qt or D-Bus in.
//
// The resolver is thread-compatible, not thread-safe: register backends at
// start-up and only call `Resolve()` concurrently afterwards.

#pragma once

#include "../Api.hpp"
#include "ISecretBackend.hpp"

#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Lightweight::Secrets
{

/// Outcome of a single `SecretResolver::Resolve` call.
struct ResolveError
{
    /// Human-readable description of the failure. Safe to log or show to the
    /// user; does not include the secret value itself.
    std::string message;
};

/// Central lookup for opaque `secretRef` strings. Owns a list of registered
/// backends and dispatches based on either an explicit `prefix:` or
/// registration order for bare references.
class LIGHTWEIGHT_API SecretResolver
{
  public:
    SecretResolver() = default;
    ~SecretResolver() = default;
    SecretResolver(SecretResolver const&) = delete;
    SecretResolver(SecretResolver&&) = default;
    SecretResolver& operator=(SecretResolver const&) = delete;
    SecretResolver& operator=(SecretResolver&&) = default;

    /// Appends a backend to the chain. The backend's `Name()` is used both as
    /// the prefix (`<name>:`) and for matching the fallback chain. Backends
    /// registered later win when their prefix is explicitly requested, but
    /// earlier registrations are tried first for bare references — giving
    /// callers a clear way to shape the fallback order (register native OS
    /// vaults first, file / env fallbacks last).
    void RegisterBackend(std::shared_ptr<ISecretBackend> backend);

    /// Looks up a secret by reference.
    ///
    /// `secretRef` is one of:
    ///   - `prefix:key`  — route to the backend named `prefix`.
    ///   - `prefix:`     — route to `prefix`, with `key` derived from
    ///                     `profileName` (e.g. `stdin:` prompts interactively).
    ///   - bare key      — walk the registered backend chain in order.
    ///
    /// `profileName` supplies a fallback key for backends that support it
    /// (e.g. `env:` translates a bare ref into `LIGHTWEIGHT_<PROFILE>_PWD`).
    ///
    /// Returns `std::unexpected` when the prefix is unknown, the prefix is
    /// registered but declines to build at runtime (e.g. `secretservice:` on
    /// a headless box), or the bare-ref chain produced no hits.
    [[nodiscard]] std::expected<std::string, ResolveError> Resolve(std::string_view secretRef,
                                                                    std::string_view profileName) const;

    /// Returns the registered backend prefixes in registration order, for
    /// diagnostic / debugging output.
    [[nodiscard]] std::vector<std::string> RegisteredBackendNames() const;

  private:
    [[nodiscard]] ISecretBackend* Lookup(std::string_view name) const noexcept;

    [[nodiscard]] std::expected<std::string, ResolveError> ResolveExplicit(std::string_view prefix,
                                                                             std::string_view key,
                                                                             std::string_view profileName) const;

    [[nodiscard]] std::expected<std::string, ResolveError> ResolveBare(std::string_view secretRef,
                                                                        std::string_view profileName) const;

    std::vector<std::shared_ptr<ISecretBackend>> _backends;
};

/// Convenience: builds a default resolver wired with the platform-neutral
/// backends (`env:`, `file:`, `stdin:`) suitable for every build of
/// Lightweight, regardless of Qt / libsecret availability.
///
/// Native GUI-only backends (`keychain:`, `wincred:`, `secretservice:`,
/// `pass:`) are NOT registered here — consumers who need them must register
/// them explicitly after the call. This keeps pure-CLI binaries like `dbtool`
/// Qt-free.
[[nodiscard]] LIGHTWEIGHT_API SecretResolver MakeDefaultResolver();

} // namespace Lightweight::Secrets
