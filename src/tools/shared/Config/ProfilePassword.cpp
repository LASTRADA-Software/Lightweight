// SPDX-License-Identifier: Apache-2.0

#include "ProfileFileEditor.hpp"
#include "ProfilePassword.hpp"

#include <format>
#include <system_error>

namespace Lightweight::Config
{

std::expected<ResolvedPassword, std::string> ResolveProfilePassword(Profile const& profile,
                                                                    Secrets::ProfileCipher const& cipher,
                                                                    Secrets::SecretResolver const& resolver)
{
    if (!profile.password.empty())
    {
        if (!Secrets::ProfileCipher::IsEncrypted(profile.password))
            return ResolvedPassword { .value = profile.password, .origin = PasswordOrigin::Plaintext };
        return cipher.Decrypt(profile.password)
            .transform([](std::string value) {
                return ResolvedPassword { .value = std::move(value), .origin = PasswordOrigin::Encrypted };
            })
            .transform_error([&](std::string const& error) {
                return std::format("cannot decrypt the password of profile '{}': {}", profile.name, error);
            });
    }

    if (!profile.secretRef.empty())
        return resolver.Resolve(profile.secretRef, profile.name)
            .transform([](std::string value) {
                return ResolvedPassword { .value = std::move(value), .origin = PasswordOrigin::SecretRef };
            })
            .transform_error([&](Secrets::ResolveError const& error) {
                return std::format("could not resolve the secret of profile '{}': {}", profile.name, error.message);
            });

    return ResolvedPassword {};
}

std::expected<UpgradeResult, std::string> UpgradePlaintextPassword(std::filesystem::path const& configPath,
                                                                   Profile const& profile,
                                                                   Secrets::ProfileCipher const& cipher)
{
    if (profile.password.empty() || Secrets::ProfileCipher::IsEncrypted(profile.password))
        return std::unexpected(std::format("profile '{}' has no plaintext password to encrypt", profile.name));

    auto const encrypted = cipher.Encrypt(profile.password);
    if (!encrypted)
        return std::unexpected(
            std::format("cannot encrypt the password of profile '{}': {}", profile.name, encrypted.error()));

    return EditConfigFile(configPath,
                          [&](std::string_view text) {
                              return SetProfilePasswordText(text, { .profileName = profile.name, .newValue = *encrypted });
                          })
        .transform([&] { return UpgradeResult { .insideGitWorkTree = IsInsideGitWorkTree(configPath) }; });
}

bool IsInsideGitWorkTree(std::filesystem::path const& path)
{
    std::error_code ec;
    auto dir = std::filesystem::weakly_canonical(std::filesystem::absolute(path, ec), ec);
    while (true)
    {
        if (std::filesystem::exists(dir / ".git", ec))
            return true;
        if (dir == dir.parent_path())
            return false;
        dir = dir.parent_path();
    }
}

} // namespace Lightweight::Config
