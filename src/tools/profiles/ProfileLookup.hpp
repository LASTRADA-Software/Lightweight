// SPDX-License-Identifier: Apache-2.0
//
// Private helpers shared by the exported functions: the layered lookup and path rendering.

#pragma once

#include <expected>
#include <filesystem>
#include <optional>
#include <string>

#include <Config/LayeredProfileStore.hpp>

namespace Lightweight::Profiles::detail
{

/// The merged view an application sees for @p startDirectory.
///
/// The lookup starts at @p startDirectory (made absolute against the working directory) and walks
/// up through its ancestors, followed by the per-user file. `LIGHTWEIGHT_DBTOOL_CONFIG_SEARCH_DIR`
/// is deliberately not consulted.
///
/// @param startDirectory Directory where the layered lookup starts; `std::nullopt` consults only
///                       the per-user file.
/// @return The store (possibly empty), or an error naming the file that failed to read or parse.
[[nodiscard]] std::expected<Config::LayeredProfileStore, std::string> LoadLayers(
    std::optional<std::filesystem::path> const& startDirectory);

/// UTF-8 rendering of a path for error messages; never throws.
/// @param path The path to render.
/// @return The path as UTF-8 text, or a placeholder if it cannot be rendered.
[[nodiscard]] std::string DisplayPath(std::filesystem::path const& path);

} // namespace Lightweight::Profiles::detail
