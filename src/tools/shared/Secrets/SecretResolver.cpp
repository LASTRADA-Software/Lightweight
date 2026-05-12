// SPDX-License-Identifier: Apache-2.0

#include "SecretResolver.hpp"
#include "backends/EnvBackend.hpp"
#include "backends/FileBackend.hpp"
#include "backends/StdinBackend.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <format>

namespace Lightweight::Secrets
{

namespace
{

    /// Splits `secretRef` into `{prefix, key}` where the prefix is everything
    /// before the first colon. Bare references (no colon) yield an empty prefix
    /// and the whole string as the key.
    struct SecretRefParts
    {
        std::string_view prefix;
        std::string_view key;
    };

    SecretRefParts SplitSecretRef(std::string_view secretRef) noexcept
    {
        auto const colon = secretRef.find(':');
        if (colon == std::string_view::npos)
            return { .prefix = {}, .key = secretRef };
        return { .prefix = secretRef.substr(0, colon), .key = secretRef.substr(colon + 1) };
    }

    /// MSVC treats `std::getenv` as deprecated in favour of `_dupenv_s`; we wrap
    /// it once here so the suppression is centralised rather than scattered at
    /// every call site. The one-read-at-start-up use is safe enough — we never
    /// interleave reads with `putenv` on the same variable.
    char const* SafeGetenv(char const* name) noexcept
    {
#ifdef _WIN32
    #pragma warning(push)
    #pragma warning(disable : 4996)
#endif
        return std::getenv(name);
#ifdef _WIN32
    #pragma warning(pop)
#endif
    }

    /// Resolves `~` / `$HOME` in the default credentials path, matching the
    /// behaviour users expect from shells and pg_hba.conf-style configs.
    std::filesystem::path DefaultCredentialsPath()
    {
#ifdef _WIN32
        if (char const* appData = SafeGetenv("APPDATA"); appData && *appData)
            return std::filesystem::path(appData) / "dbtool" / "credentials";
        return "dbtool-credentials";
#else
        if (char const* xdg = SafeGetenv("XDG_CONFIG_HOME"); xdg && *xdg)
            return std::filesystem::path(xdg) / "Lightweight" / "credentials";
        if (char const* home = SafeGetenv("HOME"); home && *home)
            return std::filesystem::path(home) / ".config" / "Lightweight" / "credentials";
        return "Lightweight-credentials";
#endif
    }

    std::string JoinBackendNames(std::vector<std::string> const& names)
    {
        std::string joined;
        for (size_t i = 0; i < names.size(); ++i)
        {
            if (i != 0)
                joined += ", ";
            joined += names[i];
        }
        return joined;
    }

} // namespace

void SecretResolver::RegisterBackend(std::shared_ptr<ISecretBackend> backend)
{
    if (backend)
        _backends.push_back(std::move(backend));
}

ISecretBackend* SecretResolver::Lookup(std::string_view name) const noexcept
{
    auto const it = std::ranges::find_if(_backends, [&](auto const& b) { return b->Name() == name; });
    return it == _backends.end() ? nullptr : it->get();
}

std::expected<std::string, ResolveError> SecretResolver::ResolveExplicit(std::string_view prefix,
                                                                         std::string_view key,
                                                                         std::string_view profileName) const
{
    auto* backend = Lookup(prefix);
    if (!backend)
        return std::unexpected(ResolveError {
            .message = std::format(
                "unknown secret backend '{}:' (available: {})", prefix, JoinBackendNames(RegisteredBackendNames())),
        });

    auto const effectiveKey = key.empty() ? profileName : key;
    if (auto value = backend->Read(effectiveKey))
        return std::move(*value);

    return std::unexpected(ResolveError {
        .message = std::format("secret '{}' not found in backend '{}:'", effectiveKey, prefix),
    });
}

std::expected<std::string, ResolveError> SecretResolver::ResolveBare(std::string_view secretRef,
                                                                     std::string_view profileName) const
{
    // Bare ref: walk the registered chain in order. Each backend gets a key
    // shaped to its own conventions so callers don't have to remember whether
    // they registered an env: or file: backend first.
    for (auto const& backend: _backends)
    {
        std::string_view const backendName = backend->Name();
        if (backendName == "stdin")
            continue; // never prompt unexpectedly — caller must opt in with `stdin:`.

        std::string derivedKey;
        std::string_view effectiveKey = (profileName.empty() ? secretRef : profileName);
        if (backendName == "env")
        {
            derivedKey = EnvBackend::EnvVarForProfile(profileName);
            effectiveKey = derivedKey;
        }

        if (auto value = backend->Read(effectiveKey))
            return std::move(*value);
    }

    return std::unexpected(ResolveError {
        .message = std::format("no secret found for profile '{}' (ref '{}')", profileName, secretRef),
    });
}

std::expected<std::string, ResolveError> SecretResolver::Resolve(std::string_view secretRef,
                                                                 std::string_view profileName) const
{
    if (secretRef.empty())
        return std::unexpected(ResolveError {
            .message = std::format("empty secretRef for profile '{}'", profileName),
        });

    auto const [prefix, key] = SplitSecretRef(secretRef);
    if (!prefix.empty())
        return ResolveExplicit(prefix, key, profileName);
    return ResolveBare(secretRef, profileName);
}

std::vector<std::string> SecretResolver::RegisteredBackendNames() const
{
    std::vector<std::string> names;
    names.reserve(_backends.size());
    for (auto const& b: _backends)
        names.emplace_back(b->Name());
    return names;
}

SecretResolver MakeDefaultResolver()
{
    SecretResolver resolver;
    resolver.RegisterBackend(std::make_shared<EnvBackend>());
    resolver.RegisterBackend(std::make_shared<FileBackend>(DefaultCredentialsPath()));
    resolver.RegisterBackend(std::make_shared<StdinBackend>());
    return resolver;
}

} // namespace Lightweight::Secrets
