// SPDX-License-Identifier: Apache-2.0
//
// Placeholder for the `QtKeychainBackend` from the dbtool-gui plan
// (`docs/migrations-gui-plan.md` §6.3 and phase 4). The real implementation
// wraps `qtkeychain` — the cross-platform Qt binding for Windows Credential
// Manager, macOS Keychain Services, and the Linux Secret Service.
//
// STATUS: not built. Adding `qtkeychain` to `vcpkg.json` is a deliberate
// follow-up so CI images without D-Bus (headless Linux runners) keep working
// unmodified. When the vcpkg entry lands, `#include <qt6keychain/keychain.h>`
// and register this backend in `main.cpp` before `MakeDefaultResolver()` so
// it claims the `keychain:` / `wincred:` / `secretservice:` prefixes from
// the platform-neutral chain.
//
// The GUI still works without it — `env:` / `file:` / `stdin:` in
// `SecretResolver` cover the CLI-grade flows. Users who want native OS
// vaults currently set `secretRef: file:...` and point `file:` at a mode-0600
// credentials file, which is the same story as every other Lightweight-based
// tool that predates this PR.

#pragma once

#include <Lightweight/Secrets/ISecretBackend.hpp>

namespace DbtoolGui
{

// Intentionally not defined — see comment at top of file. The header exists
// so that CMake, code-review tooling, and IDE auto-complete all know the
// class is coming.
class QtKeychainBackend; // IWYU pragma: export

} // namespace DbtoolGui
