// SPDX-License-Identifier: Apache-2.0

#include "Guarded.hpp"
#include "ProfileLookup.hpp"

#include <Lightweight/Profiles/ProfileConnection.hpp>

#include <expected>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <string_view>

#include <Config/ProfilePassword.hpp>
#include <Config/ProfileStore.hpp>
#include <Secrets/ProfileCipher.hpp>
#include <Secrets/SecretResolver.hpp>

namespace Lightweight::Profiles
{

namespace
{

    /// Describes the files a lookup consulted, for error messages.
    /// @param store The loaded layers.
    /// @param startDirectory Where the lookup started, if the caller gave one.
    /// @return The layer paths, comma separated, or the per-user path marked "(not found)" if none was
    ///         loaded (naming the start directory that was searched, when there was one).
    std::string DescribeConsultedFiles(Config::LayeredProfileStore const& store,
                                       std::optional<std::filesystem::path> const& startDirectory)
    {
        auto const layers = store.Layers();
        if (layers.empty())
        {
            auto const userPath = detail::DisplayPath(Config::ProfileStore::DefaultPath());
            if (startDirectory)
                return std::format(
                    "{} (not found; no dbtool.yml from {} upwards)", userPath, detail::DisplayPath(*startDirectory));
            return std::format("{} (not found)", userPath);
        }

        auto description = std::string {};
        for (auto const& layer: layers)
        {
            if (!description.empty())
                description += ", ";
            description += detail::DisplayPath(layer.path);
        }
        return description;
    }

    /// Picks the profile to resolve and checks it can be connected with.
    /// @param store The loaded layers.
    /// @param startDirectory Where the lookup started, for error messages.
    /// @param profileName The requested name, or empty for the merged `defaultProfile`.
    /// @return The profile, or a secret-free error.
    std::expected<Config::Profile const*, std::string> SelectProfile(
        Config::LayeredProfileStore const& store,
        std::optional<std::filesystem::path> const& startDirectory,
        std::string_view profileName)
    {
        auto const name = profileName.empty() ? std::string_view { store.Merged().DefaultProfileName() } : profileName;
        if (name.empty())
            return std::unexpected(std::format("no profile name given and no defaultProfile set in {}",
                                               DescribeConsultedFiles(store, startDirectory)));

        auto const* const profile = store.Merged().Find(name);
        if (profile == nullptr)
            return std::unexpected(
                std::format("profile '{}' not found in {}", name, DescribeConsultedFiles(store, startDirectory)));

        if (!profile->HasConnection())
            return std::unexpected(std::format("profile '{}' has neither a dsn nor a connectionString", name));
        return profile;
    }

} // namespace

std::expected<ProfileConnection, std::string> ResolveProfileConnection(
    std::optional<std::filesystem::path> const& startDirectory, std::string_view profileName)
{
    return detail::Guarded("cannot resolve profile", [&]() -> std::expected<ProfileConnection, std::string> {
        auto const store = detail::LoadLayers(startDirectory);
        if (!store)
            return std::unexpected(store.error());

        auto const selected = SelectProfile(*store, startDirectory, profileName);
        if (!selected)
            return std::unexpected(selected.error());
        auto const& profile = **selected;

        return Config::ResolveProfilePassword(
                   profile, Secrets::ProfileCipher::Builtin(), Secrets::MakeNonInteractiveResolver())
            .transform([&](Config::ResolvedPassword const& resolved) {
                auto const* const origin = store->OriginOf(profile.name);
                return ProfileConnection {
                    .name = profile.name,
                    .dsn = profile.dsn,
                    .connectionString = profile.connectionString,
                    .uid = profile.uid,
                    .password = resolved.value,
                    .configFile = origin != nullptr ? origin->path : std::filesystem::path {},
                };
            });
    });
}

} // namespace Lightweight::Profiles
