// SPDX-License-Identifier: Apache-2.0
//
// Lists the dbtool.yml files that dbtool and dbtool-gui merge (see `LayeredProfileStore`):
//
//   1. an explicit path (`--config`, or the GUI's saved setting) is used on its own; otherwise
//   2. every dbtool.yml in the executable's directory (or `LIGHTWEIGHT_DBTOOL_CONFIG_SEARCH_DIR`)
//      and each of its ancestors, nearest first; loading stops after one declaring `root: true`;
//   3. followed, when no file declared `root: true`, by the per-user default
//      (`ProfileStore::DefaultPath()`).
//
// The working directory takes no part in the lookup.

#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Lightweight::Config
{

/// File name searched for by discovery.
inline constexpr std::string_view ConfigFileName = "dbtool.yml";

/// Which lookup rule selected the configuration file.
enum class ConfigSource : std::uint8_t
{
    /// Given explicitly (`--config` or a saved GUI setting).
    Explicit,
    /// Found in the executable's directory or one of its ancestors.
    ExecutableDirectory,
    /// The per-user default location.
    UserDefault,
    /// Nothing found; the path is the (non-existent) user default.
    None,
};

/// Short human-readable name of a source, for logs and `list-profiles`.
/// @param source The rule that matched.
/// @return E.g. "executable directory".
[[nodiscard]] std::string_view ToString(ConfigSource source) noexcept;

/// Everything discovery depends on, injected so tests control the file system layout.
struct DiscoveryInputs
{
    /// Explicit path; empty when not given.
    std::filesystem::path explicitPath;

    /// Start of the upward walk: the executable's directory, or `ConfigSearchDirectoryVariable`
    /// when set; empty skips it.
    std::filesystem::path searchStart;

    /// Per-user fallback location.
    std::filesystem::path userDefault;
};

/// Environment variable that, when set and non-empty, replaces the executable directory as the
/// start of the upward search. Lets tests and scripted runs stay off the real installation layout.
/// A relative value resolves against the working directory.
inline constexpr std::string_view ConfigSearchDirectoryVariable = "LIGHTWEIGHT_DBTOOL_CONFIG_SEARCH_DIR";

/// One configuration file found by `FindConfigCandidates`.
struct ConfigCandidate
{
    /// The file.
    std::filesystem::path path;

    /// The rule that found it.
    ConfigSource source = ConfigSource::None;
};

/// Lists every configuration file that applies, for layered merging.
///
/// An explicit path yields exactly that one file. Otherwise every `dbtool.yml` from
/// `searchStart` up to the filesystem root is listed nearest first, followed by
/// `userDefault` when it exists and is not already listed.
/// @param inputs Paths to consider.
/// @return The candidates (empty when nothing exists), or an error when an explicit path does not exist.
[[nodiscard]] std::expected<std::vector<ConfigCandidate>, std::string> FindConfigCandidates(DiscoveryInputs const& inputs);

/// Discovery inputs for the running process: the executable directory (or
/// `ConfigSearchDirectoryVariable`) and `ProfileStore::DefaultPath()`.
/// @param explicitPath Explicit path, or empty.
/// @return Inputs ready for `FindConfigCandidates`.
[[nodiscard]] DiscoveryInputs DefaultDiscoveryInputs(std::filesystem::path explicitPath = {});

/// Directory containing the running executable.
/// @return The directory, or an empty path if the OS cannot report it.
[[nodiscard]] std::filesystem::path ExecutableDirectory();

} // namespace Lightweight::Config
