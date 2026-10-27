// SPDX-License-Identifier: Apache-2.0

#include "ProfileLookup.hpp"

#include <system_error>

#include <Config/ConfigDiscovery.hpp>
#include <Config/ProfileStore.hpp>

namespace Lightweight::Profiles::detail
{

std::expected<Config::LayeredProfileStore, std::string> LoadLayers(
    std::optional<std::filesystem::path> const& startDirectory)
{
    auto searchStart = std::filesystem::path {};
    if (startDirectory.has_value())
    {
        auto errorCode = std::error_code {};
        searchStart = std::filesystem::absolute(*startDirectory, errorCode);
        if (errorCode)
            searchStart = *startDirectory;
    }

    auto const inputs = Config::DiscoveryInputs {
        .explicitPath = {},
        .searchStart = std::move(searchStart),
        .userDefault = Config::ProfileStore::DefaultPath(),
    };

    return Config::FindConfigCandidates(inputs).and_then(
        [](auto const& candidates) { return Config::LayeredProfileStore::Load(candidates); });
}

std::string DisplayPath(std::filesystem::path const& path)
{
    try
    {
        auto const utf8 = path.u8string();
        return std::string(reinterpret_cast<char const*>(utf8.data()), utf8.size());
    }
    catch (...)
    {
        return "<unprintable path>";
    }
}

} // namespace Lightweight::Profiles::detail
