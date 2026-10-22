// SPDX-License-Identifier: Apache-2.0

#include "Config/LayeredProfileStore.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <ranges>
#include <sstream>
#include <system_error>

namespace Lightweight::Config
{

std::expected<std::string, std::string> ReadConfigFile(std::filesystem::path const& path)
{
    std::ifstream in { path, std::ios::binary };
    if (!in)
        return std::unexpected { "cannot read " + path.string() };
    std::ostringstream content;
    content << in.rdbuf();
    if (in.bad())
        return std::unexpected { "error reading " + path.string() };
    return std::move(content).str();
}

namespace
{

    /// Prefixes `error` with `file` unless it already names it.
    std::string NamingFile(std::string error, std::filesystem::path const& file)
    {
        if (error.contains(file.string()))
            return error;
        return file.string() + ": " + error;
    }

    /// Anchors a relative path at the directory of the file it was written in; empty and absolute
    /// paths are returned unchanged.
    std::filesystem::path ResolveAgainst(std::filesystem::path const& path, std::filesystem::path const& file)
    {
        if (path.empty() || path.is_absolute())
            return path;
        return (file.parent_path() / path).lexically_normal();
    }

    /// Rewrites every directory of `store` that was written relative into one relative to `file`.
    ProfileStore ResolvedAgainst(ProfileStore const& store, std::filesystem::path const& file)
    {
        ProfileStore result = store;
        auto dirs = store.DefaultPluginsDir();
        for (auto& dir: dirs)
            dir = ResolveAgainst(dir, file);
        result.SetDefaultPluginsDir(std::move(dirs));
        result.SetDefaultBackupDir(ResolveAgainst(store.DefaultBackupDir(), file));
        for (auto profile: store.Profiles())
        {
            profile.pluginsDir = ResolveAgainst(profile.pluginsDir, file);
            profile.backupDir = ResolveAgainst(profile.backupDir, file);
            result.Upsert(std::move(profile));
        }
        return result;
    }

} // namespace

std::expected<LayeredProfileStore, std::string> LayeredProfileStore::Load(std::span<ConfigCandidate const> candidates,
                                                                          ConfigFileReader const& reader)
{
    LayeredProfileStore result;
    std::vector<ProfileStore> stores;

    for (auto const& candidate: candidates)
    {
        auto const content = reader(candidate.path);
        if (!content)
            return std::unexpected { NamingFile(content.error(), candidate.path) };
        auto parsed = ProfileStore::Parse(*content, candidate.path);
        if (!parsed)
            return std::unexpected { NamingFile(std::move(parsed.error()), candidate.path) };

        auto const isRoot = parsed->IsRoot();
        result._layers.push_back(ConfigLayer { .path = candidate.path, .source = candidate.source, .isRoot = isRoot });
        stores.push_back(ResolvedAgainst(*parsed, candidate.path));
        if (isRoot)
            break;
    }

    bool pluginsSet = false;
    bool backupSet = false;
    for (auto const& [index, store]: std::views::enumerate(stores))
    {
        auto const layerIndex = static_cast<std::size_t>(index);
        for (auto const& profile: store.Profiles())
        {
            if (result._profileOrigin.contains(profile.name))
                continue;
            result._profileOrigin.emplace(profile.name, layerIndex);
            result._merged.Upsert(profile);
        }
        if (result._defaultProfileOrigin == NoLayer && !store.DefaultProfileName().empty())
        {
            result._defaultProfileOrigin = layerIndex;
            result._merged.SetDefault(store.DefaultProfileName());
        }
        if (!pluginsSet && !store.DefaultPluginsDir().empty())
        {
            pluginsSet = true;
            result._merged.SetDefaultPluginsDir(store.DefaultPluginsDir());
        }
        if (!backupSet && !store.DefaultBackupDir().empty())
        {
            backupSet = true;
            result._merged.SetDefaultBackupDir(store.DefaultBackupDir());
        }
    }
    return result;
}

bool LayeredProfileStore::RootFound() const noexcept
{
    return std::ranges::any_of(_layers, &ConfigLayer::isRoot);
}

ConfigLayer const* LayeredProfileStore::OriginOf(std::string_view profileName) const noexcept
{
    auto const it = _profileOrigin.find(profileName);
    return it == _profileOrigin.end() ? nullptr : &_layers[it->second];
}

ConfigLayer const* LayeredProfileStore::DefaultProfileOrigin() const noexcept
{
    return _defaultProfileOrigin == NoLayer ? nullptr : &_layers[_defaultProfileOrigin];
}

std::filesystem::path LayeredProfileStore::NewProfileTarget(std::filesystem::path const& userDefault) const
{
    if (_layers.size() == 1 && _layers.front().source == ConfigSource::Explicit)
        return _layers.front().path;
    auto const root = std::ranges::find_if(_layers, &ConfigLayer::isRoot);
    return root == _layers.end() ? userDefault : root->path;
}

ConfigLayer const* LayeredProfileStore::NearerLayerSettingDefault(std::filesystem::path const& target) const noexcept
{
    if (_defaultProfileOrigin == NoLayer)
        return nullptr;
    auto const targetIndex =
        static_cast<std::size_t>(std::ranges::find(_layers, target, &ConfigLayer::path) - _layers.begin());
    return _defaultProfileOrigin < targetIndex ? &_layers[_defaultProfileOrigin] : nullptr;
}

} // namespace Lightweight::Config
