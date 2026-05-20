// SPDX-License-Identifier: Apache-2.0
//
// Unit tests for `Lightweight::Tools::DiscoverPlugins`. Cover:
//   - the trivial "every filename unique" case,
//   - filename-based deduplication when the same plugin name appears in
//     multiple directories (newest mtime wins),
//   - mtime ties resolved by directory-listing order,
//   - case-sensitivity of the filename match (different rules per platform),
//   - the shadow-log callback firing once per discarded duplicate.
//
// The tests use plain regular files with one of the recognised plugin
// extensions (`.so` / `.dll` / `.dylib`); the helper does not try to load
// them, so empty files are sufficient.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <ranges>
#include <string>
#include <vector>

#include <PluginDiscovery.hpp>

namespace
{

/// Per-test scratch root that wipes its tree on destruction. Subdirectories
/// are created on demand by `Dir()`. The Catch2 fixture model would also
/// work but a small RAII helper keeps each test self-contained.
class ScopedTempTree
{
  public:
    ScopedTempTree():
        _root(std::filesystem::temp_directory_path()
              / std::format("lightweight-plugindisco-{}", std::chrono::steady_clock::now().time_since_epoch().count()))
    {
        std::filesystem::create_directories(_root);
    }

    ~ScopedTempTree()
    {
        std::error_code ec;
        std::filesystem::remove_all(_root, ec);
    }

    ScopedTempTree(ScopedTempTree const&) = delete;
    ScopedTempTree& operator=(ScopedTempTree const&) = delete;
    ScopedTempTree(ScopedTempTree&&) = delete;
    ScopedTempTree& operator=(ScopedTempTree&&) = delete;

    [[nodiscard]] std::filesystem::path Dir(std::string_view name) const
    {
        auto const p = _root / name;
        std::filesystem::create_directories(p);
        return p;
    }

  private:
    std::filesystem::path _root;
};

/// Touches `path` with the given mtime so test fixtures can pin the
/// "newer-wins" branch deterministically (the real wall-clock mtime of
/// two files created back-to-back is identical on coarse-grained filesystems).
void Touch(std::filesystem::path const& path, std::filesystem::file_time_type mtime)
{
    {
        std::ofstream os(path, std::ios::binary | std::ios::trunc);
        os << "fake plugin";
    }
    std::filesystem::last_write_time(path, mtime);
}

/// Convenience: `last_write_time` with an offset in seconds applied to a
/// stable base. Using a fixed base instead of `now()` avoids one test
/// modifying the clock another relies on.
[[nodiscard]] std::filesystem::file_time_type BaseTime()
{
    return std::filesystem::file_time_type::clock::now() - std::chrono::hours(1);
}

} // namespace

TEST_CASE("DiscoverPlugins — distinct filenames across directories all survive", "[PluginDiscovery]")
{
    ScopedTempTree const tree;
    auto const dirA = tree.Dir("a");
    auto const dirB = tree.Dir("b");

    Touch(dirA / "AlphaPlugin.dll", BaseTime());
    Touch(dirB / "BetaPlugin.dll", BaseTime());

    std::vector<std::filesystem::path> dirs { dirA, dirB };
    auto const resolved = Lightweight::Tools::DiscoverPlugins(dirs);

    REQUIRE(resolved.size() == 2);
    auto const names = resolved | std::views::transform([](auto const& r) { return r.path.filename().string(); });
    std::vector<std::string> sortedNames(names.begin(), names.end());
    std::ranges::sort(sortedNames);
    CHECK(sortedNames == std::vector<std::string> { "AlphaPlugin.dll", "BetaPlugin.dll" });
}

TEST_CASE("DiscoverPlugins — same filename: newer mtime wins", "[PluginDiscovery]")
{
    ScopedTempTree const tree;
    auto const dirOld = tree.Dir("old");
    auto const dirNew = tree.Dir("new");

    auto const base = BaseTime();
    Touch(dirOld / "MyPlugin.so", base);
    Touch(dirNew / "MyPlugin.so", base + std::chrono::seconds(30));

    std::vector<std::filesystem::path> dirs { dirOld, dirNew };
    std::vector<std::string> shadows;
    auto const resolved = Lightweight::Tools::DiscoverPlugins(dirs, [&](std::string_view m) { shadows.emplace_back(m); });

    REQUIRE(resolved.size() == 1);
    CHECK(resolved.front().path.parent_path() == dirNew);
    CHECK(resolved.front().path.filename() == std::filesystem::path("MyPlugin.so"));
    REQUIRE(shadows.size() == 1);
    CHECK(shadows.front().contains("MyPlugin.so"));
    CHECK(shadows.front().contains("newer mtime"));
}

TEST_CASE("DiscoverPlugins — mtime tie: earlier-listed dir wins", "[PluginDiscovery]")
{
    ScopedTempTree const tree;
    auto const dirFirst = tree.Dir("first");
    auto const dirSecond = tree.Dir("second");

    auto const same = BaseTime();
    Touch(dirFirst / "MyPlugin.dylib", same);
    Touch(dirSecond / "MyPlugin.dylib", same);

    std::vector<std::filesystem::path> dirs { dirFirst, dirSecond };
    auto const resolved = Lightweight::Tools::DiscoverPlugins(dirs);

    REQUIRE(resolved.size() == 1);
    CHECK(resolved.front().path.parent_path() == dirFirst);
    CHECK(resolved.front().dirIndex == 0);
}

TEST_CASE("DiscoverPlugins — non-plugin extensions are skipped", "[PluginDiscovery]")
{
    ScopedTempTree const tree;
    auto const dir = tree.Dir("mixed");

    Touch(dir / "RealPlugin.dll", BaseTime());
    Touch(dir / "notes.txt", BaseTime());
    Touch(dir / "data.json", BaseTime());

    std::vector<std::filesystem::path> dirs { dir };
    auto const resolved = Lightweight::Tools::DiscoverPlugins(dirs);

    REQUIRE(resolved.size() == 1);
    CHECK(resolved.front().path.filename() == std::filesystem::path("RealPlugin.dll"));
}

TEST_CASE("DiscoverPlugins — DLLs whose stem does not end in 'Plugin' are skipped", "[PluginDiscovery]")
{
    // Mirrors the real-world clutter in `out/build/.../plugins/` after vcpkg's
    // applocal step copies a plugin's dependency closure next to it: only the
    // actual plugin (stem ending in "Plugin") should be picked up by discovery.
    ScopedTempTree const tree;
    auto const dir = tree.Dir("applocal-like");

    Touch(dir / "SqlMigrationsPlugin.dll", BaseTime());
    Touch(dir / "Lightweight.dll", BaseTime());
    Touch(dir / "zlibd1.dll", BaseTime());
    Touch(dir / "bz2d.dll", BaseTime());

    std::vector<std::filesystem::path> dirs { dir };
    auto const resolved = Lightweight::Tools::DiscoverPlugins(dirs);

    REQUIRE(resolved.size() == 1);
    CHECK(resolved.front().path.filename() == std::filesystem::path("SqlMigrationsPlugin.dll"));
}

TEST_CASE("DiscoverPlugins — missing directories are skipped without crashing", "[PluginDiscovery]")
{
    ScopedTempTree const tree;
    auto const dir = tree.Dir("present");
    auto const ghost = tree.Dir("ghost");
    std::filesystem::remove_all(ghost);

    Touch(dir / "MyPlugin.dll", BaseTime());

    std::vector<std::filesystem::path> dirs { ghost, dir };
    auto const resolved = Lightweight::Tools::DiscoverPlugins(dirs);

    REQUIRE(resolved.size() == 1);
    CHECK(resolved.front().path.filename() == std::filesystem::path("MyPlugin.dll"));
}

#ifdef _WIN32
TEST_CASE("DiscoverPlugins — filename comparison is case-insensitive on Windows", "[PluginDiscovery]")
{
    // Both names pass the case-insensitive `*Plugin` suffix filter, and the
    // dedup key collapses `FooPlugin.dll` and `fooplugin.dll` to one entry.
    ScopedTempTree const tree;
    auto const dirA = tree.Dir("a");
    auto const dirB = tree.Dir("b");

    auto const base = BaseTime();
    Touch(dirA / "FooPlugin.dll", base);
    Touch(dirB / "fooplugin.dll", base + std::chrono::seconds(10));

    std::vector<std::filesystem::path> dirs { dirA, dirB };
    auto const resolved = Lightweight::Tools::DiscoverPlugins(dirs);

    REQUIRE(resolved.size() == 1);
    CHECK(resolved.front().path.parent_path() == dirB);
}
#else
TEST_CASE("DiscoverPlugins — filename comparison is case-sensitive on POSIX", "[PluginDiscovery]")
{
    // Both stems end in the exact token `Plugin` (case-sensitive), so both
    // pass the suffix filter; on a case-sensitive filesystem they're two
    // distinct plugins, not a duplicate.
    ScopedTempTree const tree;
    auto const dirA = tree.Dir("a");
    auto const dirB = tree.Dir("b");

    auto const base = BaseTime();
    Touch(dirA / "FooPlugin.so", base);
    Touch(dirB / "fooPlugin.so", base + std::chrono::seconds(10));

    std::vector<std::filesystem::path> dirs { dirA, dirB };
    auto const resolved = Lightweight::Tools::DiscoverPlugins(dirs);

    REQUIRE(resolved.size() == 2);
}
#endif
