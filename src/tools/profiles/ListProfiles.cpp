// SPDX-License-Identifier: Apache-2.0

#include "Guarded.hpp"
#include "ProfileLookup.hpp"

#include <Lightweight/Profiles/ProfileConnection.hpp>
#include <Lightweight/SqlConnectInfo.hpp>

#include <expected>
#include <filesystem>
#include <optional>
#include <string>

namespace Lightweight::Profiles
{

std::expected<ProfileList, std::string> ListProfiles(std::optional<std::filesystem::path> const& startDirectory)
{
    return detail::Guarded("cannot list profiles", [&]() -> std::expected<ProfileList, std::string> {
        return detail::LoadLayers(startDirectory).transform([](Config::LayeredProfileStore const& store) {
            auto list = ProfileList {};
            for (auto const& layer: store.Layers())
                list.configFiles.push_back(layer.path);
            list.newProfileTarget = detail::SaveTarget(store);
            list.defaultProfile = store.Merged().DefaultProfileName();

            for (auto const& profile: store.Merged().Profiles())
            {
                auto sanitized = SqlConnectionString::SanitizePwd(profile.connectionString);
                auto const hasPassword =
                    !profile.password.empty() || !profile.secretRef.empty() || sanitized != profile.connectionString;
                auto const* const origin = store.OriginOf(profile.name);
                list.profiles.push_back(ProfileSummary {
                    .name = profile.name,
                    .dsn = profile.dsn,
                    .connectionString = std::move(sanitized),
                    .uid = profile.uid,
                    .hasPassword = hasPassword,
                    .configFile = origin != nullptr ? origin->path : std::filesystem::path {},
                });
            }
            return list;
        });
    });
}

} // namespace Lightweight::Profiles
