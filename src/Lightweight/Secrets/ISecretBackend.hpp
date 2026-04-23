// SPDX-License-Identifier: Apache-2.0
//
// `ISecretBackend` is the storage-plugin interface behind `SecretResolver`.
// A backend is typically backed by an OS facility (Windows Credential Manager,
// macOS Keychain, Linux Secret Service) or a plain-file / env-var / stdin
// source, and is addressed by a short `Name()` prefix (e.g. `"env"`,
// `"file"`, `"keychain"`).
//
// Backends are expected to be cheap to construct; `SecretResolver` instantiates
// them once at start-up and holds them for the process lifetime.

#pragma once

#include "../Api.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace Lightweight::Secrets
{

/// Plugin interface implemented by each secret storage backend.
///
/// Backends are addressed by the prefix returned from `Name()`. A reference
/// like `env:MY_PWD` routes to the backend whose `Name()` equals `"env"`.
/// Bare references (no prefix) walk the backend chain registered with
/// `SecretResolver::RegisterBackend()`; the first backend that returns a
/// non-empty result wins.
class ISecretBackend
{
  public:
    ISecretBackend() = default;
    ISecretBackend(ISecretBackend const&) = delete;
    ISecretBackend(ISecretBackend&&) = delete;
    ISecretBackend& operator=(ISecretBackend const&) = delete;
    ISecretBackend& operator=(ISecretBackend&&) = delete;
    virtual ~ISecretBackend() = default;

    /// Short, stable name used both as the prefix (`<name>:`) and as the
    /// identifier displayed in error messages.
    [[nodiscard]] virtual std::string_view Name() const noexcept = 0;

    /// Reads the secret identified by `key`. Returns `std::nullopt` when the
    /// backend does not know `key`. Returns an empty string only if the stored
    /// value is literally empty.
    ///
    /// A backend that is fundamentally unavailable (e.g. the Linux Secret
    /// Service backend without a running D-Bus session) should also return
    /// `std::nullopt` so `SecretResolver` can transparently fall through to
    /// the next backend.
    [[nodiscard]] virtual std::optional<std::string> Read(std::string_view key) = 0;

    /// Stores `value` under `key`. Not every backend supports writing
    /// (`env:` is read-only at runtime, for example); those should throw
    /// `std::runtime_error` with a descriptive message.
    virtual void Write(std::string_view key, std::string_view value) = 0;

    /// Removes the secret at `key`. Silently succeeds if the key is absent;
    /// read-only backends should throw just like in `Write`.
    virtual void Erase(std::string_view key) = 0;
};

} // namespace Lightweight::Secrets
