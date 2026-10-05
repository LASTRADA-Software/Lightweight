// SPDX-License-Identifier: Apache-2.0
//
// Locates the dbtool.yml that dbtool and dbtool-gui should use. Lookup order,
// first hit wins:
//
//   1. an explicit path (`--config`, or the GUI's saved setting);
//   2. dbtool.yml in the working directory or any of its ancestors;
//   3. dbtool.yml in the executable's directory or any of its ancestors;
//   4. the per-user default (`ProfileStore::DefaultPath()`).
//
// A project-local file therefore takes precedence over the user's own config.

#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>

namespace Lightweight::Config
{

/// File name searched for by discovery.
inline constexpr std::string_view ConfigFileName = "dbtool.yml";

/// Which lookup rule selected the configuration file.
enum class ConfigSource : std::uint8_t
{
    /// Given explicitly (`--config` or a saved GUI setting).
    Explicit,
    /// Found in the working directory or one of its ancestors.
    WorkingDirectory,
    /// Found in the executable's directory or one of its ancestors.
    ExecutableDirectory,
    /// The per-user default location.
    UserDefault,
    /// Nothing found; the path is the (non-existent) user default.
    None,
};

/// Short human-readable name of a source, for logs and `list-profiles`.
/// @param source The rule that matched.
/// @return E.g. "current directory".
[[nodiscard]] std::string_view ToString(ConfigSource source) noexcept;

/// Outcome of discovery.
struct DiscoveredConfig
{
    /// The chosen file; for `ConfigSource::None` the user default path, which does not exist.
    std::filesystem::path path;

    /// The rule that selected `path`.
    ConfigSource source = ConfigSource::None;
};

/// Everything discovery depends on, injected so tests control the file system layout.
struct DiscoveryInputs
{
    /// Explicit path; empty when not given.
    std::filesystem::path explicitPath;

    /// Start of the first upward walk; empty skips it.
    std::filesystem::path workingDirectory;

    /// Start of the second upward walk; empty skips it.
    std::filesystem::path executableDirectory;

    /// Per-user fallback location.
    std::filesystem::path userDefault;
};

/// Finds the configuration file according to the lookup order above.
/// @param inputs Paths to consider.
/// @return The chosen file and why, or an error when an explicit path does not exist.
[[nodiscard]] std::expected<DiscoveredConfig, std::string> FindConfigFile(DiscoveryInputs const& inputs);

/// Discovery inputs for the running process: current directory, executable
/// directory and `ProfileStore::DefaultPath()`.
/// @param explicitPath Explicit path, or empty.
/// @return Inputs ready for `FindConfigFile`.
[[nodiscard]] DiscoveryInputs DefaultDiscoveryInputs(std::filesystem::path explicitPath = {});

/// Directory containing the running executable.
/// @return The directory, or an empty path if the OS cannot report it.
[[nodiscard]] std::filesystem::path ExecutableDirectory();

} // namespace Lightweight::Config
