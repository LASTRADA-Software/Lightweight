// SPDX-License-Identifier: Apache-2.0

#include "Config/LayeredProfileStore.hpp"

#include <algorithm>
#include <format>

namespace Lightweight::Config
{

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

} // namespace

std::expected<LayeredProfileStore, std::string> LayeredProfileStore::Load(std::span<ConfigCandidate const> candidates,
                                                                          ConfigFileReader const& reader)
{
    LayeredProfileStore result;
    for (auto const& candidate: candidates)
    {
        auto const content = reader(candidate.path);
        if (!content)
            return std::unexpected { NamingFile(content.error(), candidate.path) };
        auto const parsed = ProfileStore::Parse(*content, candidate.path);
        if (!parsed)
            return std::unexpected { NamingFile(parsed.error(), candidate.path) };

        // Merge right away: whatever a nearer layer already set stays.
        auto const layerIndex = result._layers.size();
        auto const& file = candidate.path;
        result._layers.push_back(ConfigLayer { .path = file, .source = candidate.source, .isRoot = parsed->IsRoot() });
        for (auto const& profile: parsed->Profiles())
        {
            if (!result._profileOrigin.emplace(profile.name, layerIndex).second)
                continue;
            auto resolved = profile;
            resolved.pluginsDir = ResolveAgainst(profile.pluginsDir, file);
            resolved.backupDir = ResolveAgainst(profile.backupDir, file);
            result._merged.AppendUnique(std::move(resolved));
        }
        if (result._defaultProfileOrigin == NoLayer && !parsed->DefaultProfileName().empty())
        {
            result._defaultProfileOrigin = layerIndex;
            result._merged.SetDefault(parsed->DefaultProfileName());
        }
        if (result._merged.DefaultPluginsDir().empty() && !parsed->DefaultPluginsDir().empty())
        {
            auto dirs = parsed->DefaultPluginsDir();
            for (auto& dir: dirs)
                dir = ResolveAgainst(dir, file);
            result._merged.SetDefaultPluginsDir(std::move(dirs));
        }
        if (result._merged.DefaultBackupDir().empty())
            result._merged.SetDefaultBackupDir(ResolveAgainst(parsed->DefaultBackupDir(), file));
        if (parsed->IsRoot())
            break;
    }
    return result;
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

std::filesystem::path LayeredProfileStore::WriteTargetFor(std::string_view profileName,
                                                          std::filesystem::path const& explicitPath,
                                                          std::filesystem::path const& userDefault) const
{
    if (!explicitPath.empty())
        return explicitPath;
    if (auto const* origin = OriginOf(profileName); origin != nullptr)
        return origin->path;
    return NewProfileTarget(userDefault);
}

std::optional<std::string> LayeredProfileStore::ShadowedDefaultWarning(std::filesystem::path const& target,
                                                                       std::string_view newDefaultName) const
{
    auto const* nearer = NearerLayerSettingDefault(target);
    if (nearer == nullptr || _merged.DefaultProfileName() == newDefaultName)
        return std::nullopt;
    // UTF-8 on every platform: path::string() is the ANSI code page on Windows and can throw.
    auto const utf8 = [](std::filesystem::path const& path) {
        auto const text = path.u8string();
        return std::string { text.begin(), text.end() };
    };
    return std::format("{} sets defaultProfile '{}', which takes precedence over {}.",
                       utf8(nearer->path),
                       _merged.DefaultProfileName(),
                       utf8(target));
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
