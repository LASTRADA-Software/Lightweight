// SPDX-License-Identifier: Apache-2.0
//
// Merges several `dbtool.yml` files (nearest first) into one read view, remembering which file
// each profile and store-wide value came from, and where new profiles should be written.

#pragma once

#include "Config/ConfigDiscovery.hpp"
#include "Config/ProfileStore.hpp"

#include <cstddef>
#include <expected>
#include <filesystem>
#include <functional>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Lightweight::Config
{

/// One file that took part in the merge.
struct ConfigLayer
{
    /// Path of the file.
    std::filesystem::path path;

    /// How the file was found.
    ConfigSource source;

    /// True when the file declared `root: true`.
    bool isRoot = false;
};

/// Reads a whole file; injected so dbtool-gui can fingerprint the bytes it parsed.
using ConfigFileReader = std::function<std::expected<std::string, std::string>(std::filesystem::path const&)>;

/// Reads files from disk (the default reader).
/// @param path The file to read.
/// @return The file's content, or an error naming `path`.
[[nodiscard]] std::expected<std::string, std::string> ReadConfigFile(std::filesystem::path const& path);

/// Read-only merge of layered profile files. The nearest layer wins per profile name and per
/// store-wide value; profiles are never merged field by field. Relative directories in a layer
/// (`pluginsDir`, `backupDir`, `defaultPluginsDir`, `defaultBackupDir`) are resolved against that
/// layer's own directory.
class LayeredProfileStore
{
  public:
    /// Loads the candidates in order (nearest first), stopping after the first `root: true` layer.
    /// @param candidates The files to load, nearest first.
    /// @param reader Reads a file's content.
    /// @return The merged store, or an error naming the file that failed to read or parse.
    [[nodiscard]] static std::expected<LayeredProfileStore, std::string> Load(
        std::span<ConfigCandidate const> candidates, ConfigFileReader const& reader = ReadConfigFile);

    /// The merged profiles and store-wide values.
    [[nodiscard]] ProfileStore const& Merged() const noexcept
    {
        return _merged;
    }

    /// The layers that were loaded, nearest first. Pointers returned by the origin queries point
    /// into this storage and stay valid for the lifetime of this object, including after moves.
    [[nodiscard]] std::span<ConfigLayer const> Layers() const noexcept
    {
        return _layers;
    }

    /// True when no layer was loaded.
    [[nodiscard]] bool Empty() const noexcept
    {
        return _layers.empty();
    }

    /// True when a loaded layer declared `root: true`.
    [[nodiscard]] bool RootFound() const noexcept;

    /// The layer a merged profile came from.
    /// @param profileName Name of the profile.
    /// @return The layer, or nullptr when no such profile exists.
    [[nodiscard]] ConfigLayer const* OriginOf(std::string_view profileName) const noexcept;

    /// The layer that set `defaultProfile`, or nullptr when no layer did.
    [[nodiscard]] ConfigLayer const* DefaultProfileOrigin() const noexcept;

    /// The file a newly added profile should be written to.
    /// @param userDefault The per-user file, used when no root layer was found.
    /// @return The explicit file when one was loaded (regardless of its `root`), else the root
    ///         layer's path when one was found, else `userDefault`.
    [[nodiscard]] std::filesystem::path NewProfileTarget(std::filesystem::path const& userDefault) const;

    /// The layer that sets `defaultProfile` if it is nearer than `target`, i.e. would shadow a
    /// default written to `target`.
    /// @param target The file being written; one that is not a layer counts as the farthest.
    /// @return The shadowing layer, or nullptr.
    [[nodiscard]] ConfigLayer const* NearerLayerSettingDefault(std::filesystem::path const& target) const noexcept;

  private:
    static constexpr std::size_t NoLayer = static_cast<std::size_t>(-1);

    std::vector<ConfigLayer> _layers;
    ProfileStore _merged;
    std::map<std::string, std::size_t, std::less<>> _profileOrigin;
    std::size_t _defaultProfileOrigin = NoLayer;
};

} // namespace Lightweight::Config
