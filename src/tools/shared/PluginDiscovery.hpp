// SPDX-License-Identifier: Apache-2.0
//
// Shared plugin-discovery helper for dbtool, dbtool-gui, and any future
// tool that needs to enumerate migration plugins across one or more
// directories.
//
// Why this lives in `tools_shared`: both `dbtool` (CLI) and `dbtool-gui`
// (Qt frontend) need to scan the same set of directories with the same
// dedup rules. Keeping that logic in one place avoids the two binaries
// drifting apart when a user reports "the GUI shows different migrations
// than the CLI for the same profile".

#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <span>
#include <string_view>
#include <vector>

namespace Lightweight::Tools
{

/// One plugin file that survived the deduplication pass and is ready to be
/// handed to `PluginLoader`.
struct ResolvedPlugin
{
    /// Absolute or as-supplied path to the plugin file.
    std::filesystem::path path;

    /// Index into the original `dirs` span — i.e. which directory this
    /// winning file came from. Exposed so callers can correlate the result
    /// with the user-visible directory list (e.g. for diagnostic output).
    std::size_t dirIndex;
};

/// Scans every directory in `dirs` for shared libraries (`.so` / `.dll` /
/// `.dylib`), deduplicates by filename, and returns the surviving entries
/// in directory-listing order.
///
/// Deduplication rules:
///   - The filename (basename, including extension) is the dedup key. Two
///     files with the same filename in different directories are treated
///     as the same plugin.
///   - Filename comparison is **case-insensitive on Windows**, where the
///     filesystem and the dynamic loader both treat filenames that way,
///     and **case-sensitive on every other platform**.
///   - When the same filename is seen more than once, the file with the
///     newest `std::filesystem::last_write_time` wins.
///   - When two candidates share the same modification time (or a time
///     cannot be read), the directory that appears earlier in `dirs` wins.
///     This makes the user's directory ordering an effective tiebreaker.
///
/// Directories that don't exist or that aren't directories are skipped
/// with a single warning written to `std::cerr` (matching the behaviour
/// of dbtool's previous single-directory loader).
///
/// @param dirs          Ordered list of directories to scan.
/// @param logShadowed   Optional callback invoked once per discarded
///                      duplicate with a single-line human-readable
///                      message. Pass an empty `std::function` (the
///                      default) to discard the messages — `dbtool`
///                      typically passes a sink that writes to `stderr`
///                      only when `--verbose` is set.
[[nodiscard]] std::vector<ResolvedPlugin> DiscoverPlugins(std::span<std::filesystem::path const> dirs,
                                                          std::function<void(std::string_view)> const& logShadowed = {});

} // namespace Lightweight::Tools
