// SPDX-License-Identifier: Apache-2.0
//
// Read-only secret backend that pulls values straight out of the process
// environment. Intended for CI pipelines, container orchestrators, and any
// other environment where secrets are injected as environment variables.
//
// `Read("MY_PWD")` returns the value of `$MY_PWD` or `std::nullopt` when
// unset. A bare reference (no key) falls back to
// `LIGHTWEIGHT_<UPPER_PROFILE>_PWD` — hyphens are converted to underscores
// and the whole name is uppercased, matching the convention documented in
// `docs/migrations-gui-plan.md` §6.3.

#pragma once

#include "../ISecretBackend.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace Lightweight::Secrets
{

class LIGHTWEIGHT_API EnvBackend final: public ISecretBackend
{
  public:
    [[nodiscard]] std::string_view Name() const noexcept override
    {
        return "env";
    }

    /// Reads `$key`. Empty-string values are treated as "present but empty"
    /// and returned as such; a truly missing variable yields `std::nullopt`.
    [[nodiscard]] std::optional<std::string> Read(std::string_view key) override;

    /// The environment is effectively read-only at runtime; calling this
    /// always throws `std::runtime_error`.
    [[noreturn]] void Write(std::string_view key, std::string_view value) override;

    /// See `Write`.
    [[noreturn]] void Erase(std::string_view key) override;

    /// Derives `LIGHTWEIGHT_<PROFILE>_PWD` from a profile name so the chain
    /// walker can ask `env` for a bare reference.
    [[nodiscard]] static std::string EnvVarForProfile(std::string_view profileName);
};

} // namespace Lightweight::Secrets
