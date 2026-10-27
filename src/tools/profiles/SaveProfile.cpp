// SPDX-License-Identifier: Apache-2.0

#include "Guarded.hpp"
#include "ProfileLookup.hpp"

#include <Lightweight/Profiles/ProfileConnection.hpp>

#include <expected>
#include <filesystem>
#include <optional>
#include <string>

#include <Config/ProfileAdd.hpp>
#include <Config/ProfileStore.hpp>
#include <Secrets/ProfileCipher.hpp>

namespace Lightweight::Profiles
{

std::expected<std::filesystem::path, std::string> SaveProfile(std::optional<std::filesystem::path> const& startDirectory,
                                                              ProfileDefinition const& definition)
{
    return detail::Guarded("cannot save profile", [&]() -> std::expected<std::filesystem::path, std::string> {
        return detail::LoadLayers(startDirectory).and_then([&](Config::LayeredProfileStore const& store) {
            auto const target = store.WriteTargetFor(definition.name, {}, Config::ProfileStore::DefaultPath());
            auto const request = Config::ProfileRequest {
                .name = definition.name,
                .connectionString = definition.connectionString,
                .dsn = definition.dsn,
                .uid = definition.uid,
                .schema = {},
                .pluginsDir = {},
                .password = definition.password,
                .backupDir = {},
            };
            return Config::AddProfileToFile(target,
                                            request,
                                            Config::ReplaceExisting::Yes,
                                            Config::MakeDefault::No,
                                            Secrets::ProfileCipher::Builtin())
                .transform([&] { return target; });
        });
    });
}

} // namespace Lightweight::Profiles
