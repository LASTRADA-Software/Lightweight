// SPDX-License-Identifier: Apache-2.0
//
// Interactive secret backend: reads a password from stdin, suppressing echo
// when stdin is a TTY. Intended as the "last resort" backend in the fallback
// chain for humans who forgot to set up `file` or `env`.
//
// In non-interactive contexts (stdin is a pipe or file) the backend reads
// one line verbatim and returns it. That keeps it usable from scripts that
// pipe a password in, without forcing them to add `env:` / `file:` plumbing.

#pragma once

#include "../ISecretBackend.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace Lightweight::Secrets
{

class LIGHTWEIGHT_API StdinBackend final: public ISecretBackend
{
  public:
    [[nodiscard]] std::string_view Name() const noexcept override
    {
        return "stdin";
    }

    /// Prompts for a password on stderr (so it survives stdout redirection)
    /// using `key` / `profileName` to shape the prompt text. Reads a single
    /// line from stdin. Returns `std::nullopt` only on EOF / read error.
    [[nodiscard]] std::optional<std::string> Read(std::string_view key) override;

    /// Stdin is read-only; these always throw.
    [[noreturn]] void Write(std::string_view key, std::string_view value) override;
    [[noreturn]] void Erase(std::string_view key) override;
};

} // namespace Lightweight::Secrets
