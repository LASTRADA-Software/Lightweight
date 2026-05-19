// SPDX-License-Identifier: Apache-2.0

#include "PluginDiscovery.hpp"

#include <algorithm>
#include <cwctype>
#include <format>
#include <iostream>
#include <print>
#include <ranges>
#include <string>
#include <unordered_map>

namespace Lightweight::Tools
{

namespace
{

#ifdef _WIN32
    using FilenameKey = std::wstring;

    /// Filenames on Windows are matched case-insensitively because both
    /// NTFS/refs and the dynamic loader (`LoadLibraryW`) treat them that
    /// way. Lower-casing once at insertion time keeps the lookup map
    /// itself a plain `unordered_map` (no custom hash/equal).
    [[nodiscard]] FilenameKey FilenameKeyFromPath(std::filesystem::path const& p)
    {
        FilenameKey key = p.filename().wstring();
        std::ranges::transform(
            key, key.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(static_cast<wint_t>(c))); });
        return key;
    }
#else
    using FilenameKey = std::string;

    [[nodiscard]] FilenameKey FilenameKeyFromPath(std::filesystem::path const& p)
    {
        // POSIX filesystems are case-sensitive; compare bytes verbatim.
        return p.filename().string();
    }
#endif

    [[nodiscard]] bool IsPluginExtension(std::filesystem::path const& p) noexcept
    {
        auto const ext = p.extension();
        return ext == ".so" || ext == ".dll" || ext == ".dylib";
    }

    struct Candidate
    {
        std::filesystem::path path;
        std::size_t dirIndex;
        std::filesystem::file_time_type mtime;

        /// True when this candidate has a real mtime that the rule
        /// "newer wins" can use. False when reading the time failed and
        /// we fell back to the directory-order tiebreaker only.
        bool hasMtime { true };

        /// Encounter order across all directories — used so that the
        /// output preserves the order in which files were first seen,
        /// even after later candidates replace earlier winners.
        std::size_t encounterOrder { 0 };
    };

    /// True when `incoming` should replace the currently-stored `existing`.
    /// Implements the documented order: newer mtime wins; on ties (or when
    /// either side is missing a real mtime), the smaller directory index
    /// wins. When both candidates compare equal on every axis the existing
    /// one is kept, which preserves stable behaviour across repeated runs.
    [[nodiscard]] bool ShouldReplace(Candidate const& existing, Candidate const& incoming) noexcept
    {
        if (incoming.hasMtime && existing.hasMtime)
        {
            if (incoming.mtime > existing.mtime)
                return true;
            if (incoming.mtime < existing.mtime)
                return false;
        }
        else if (incoming.hasMtime != existing.hasMtime)
        {
            // A candidate with a known mtime beats one without — it gives
            // the dedup decision a stronger basis than directory order.
            return incoming.hasMtime;
        }
        // Tie on mtime: earlier-listed directory wins.
        return incoming.dirIndex < existing.dirIndex;
    }

    /// Reads `last_write_time(path)` without propagating an exception. The
    /// `hasMtime` flag in the returned candidate is `false` when the
    /// timestamp could not be obtained — `ShouldReplace` then degrades
    /// gracefully to the directory-order tiebreaker.
    [[nodiscard]] Candidate MakeCandidate(std::filesystem::path path, std::size_t dirIndex, std::size_t encounter)
    {
        Candidate c {
            .path = std::move(path),
            .dirIndex = dirIndex,
            .mtime = {},
            .hasMtime = false,
            .encounterOrder = encounter,
        };
        std::error_code ec;
        auto const mtime = std::filesystem::last_write_time(c.path, ec);
        if (!ec)
        {
            c.mtime = mtime;
            c.hasMtime = true;
        }
        return c;
    }

    /// Inserts `incoming` into `winners` or resolves a collision against
    /// the existing entry by applying `ShouldReplace`. On collision the
    /// loser is reported through `emit`.
    void IngestCandidate(std::unordered_map<FilenameKey, Candidate>& winners,
                         Candidate incoming,
                         std::function<void(std::string_view)> const& emit)
    {
        auto const key = FilenameKeyFromPath(incoming.path);
        auto const [it, inserted] = winners.try_emplace(key, incoming);
        if (inserted)
            return;

        Candidate& existing = it->second;
        if (ShouldReplace(existing, incoming))
        {
            if (emit)
                emit(std::format("plugin '{}' from {} shadowed by {} (newer mtime)",
                                 incoming.path.filename().string(),
                                 existing.path.string(),
                                 incoming.path.string()));
            // Preserve the encounter order so the final result vector
            // keeps a stable, user-predictable layout.
            incoming.encounterOrder = existing.encounterOrder;
            existing = std::move(incoming);
        }
        else if (emit)
        {
            emit(std::format("plugin '{}' from {} shadowed by {} (newer mtime)",
                             incoming.path.filename().string(),
                             incoming.path.string(),
                             existing.path.string()));
        }
    }

    /// Walks one directory and feeds every plugin-shaped regular file into
    /// `IngestCandidate`. `encounter` is updated by reference so the
    /// per-file encounter order remains globally unique across all
    /// directories in one call to `DiscoverPlugins`.
    void ScanOneDirectory(std::filesystem::path const& dir,
                          std::size_t dirIndex,
                          std::size_t& encounter,
                          std::unordered_map<FilenameKey, Candidate>& winners,
                          std::function<void(std::string_view)> const& emit)
    {
        std::error_code ec;
        if (!std::filesystem::exists(dir, ec) || !std::filesystem::is_directory(dir, ec))
        {
            std::println(std::cerr, "Warning: Plugins directory '{}' does not exist.", dir.string());
            return;
        }

        for (auto const& entry: std::filesystem::directory_iterator(dir, ec))
        {
            if (ec)
                break;
            if (!entry.is_regular_file(ec) || !IsPluginExtension(entry.path()))
                continue;
            IngestCandidate(winners, MakeCandidate(entry.path(), dirIndex, encounter++), emit);
        }
    }

} // namespace

std::vector<ResolvedPlugin> DiscoverPlugins(std::span<std::filesystem::path const> dirs,
                                            std::function<void(std::string_view)> const& logShadowed)
{
    std::unordered_map<FilenameKey, Candidate> winners;
    std::size_t encounter = 0;

    // Plain index loop because `std::views::enumerate` on a
    // `std::span<T const>` does not compile on every libc++ version we
    // target (the C++26-reflection CI job uses an older libc++).
    for (std::size_t dirIndex = 0; dirIndex < dirs.size(); ++dirIndex)
        ScanOneDirectory(dirs[dirIndex], dirIndex, encounter, winners, logShadowed);

    std::vector<ResolvedPlugin> result;
    result.reserve(winners.size());
    for (auto const& [_, c]: winners)
        result.emplace_back(c.path, c.dirIndex);

    std::ranges::sort(result, [&](ResolvedPlugin const& a, ResolvedPlugin const& b) {
        // Re-emit in the order candidates were first encountered so the
        // result is deterministic and matches the user's directory listing
        // order.
        return winners.at(FilenameKeyFromPath(a.path)).encounterOrder
               < winners.at(FilenameKeyFromPath(b.path)).encounterOrder;
    });

    return result;
}

} // namespace Lightweight::Tools
