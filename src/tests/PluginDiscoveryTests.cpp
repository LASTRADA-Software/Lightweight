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
              / std::format("lightweight-plugindisco-{}",
                            std::chrono::steady_clock::now().time_since_epoch().count()))
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

    Touch(dirA / "alpha.dll", BaseTime());
    Touch(dirB / "beta.dll", BaseTime());

    std::vector<std::filesystem::path> dirs { dirA, dirB };
    auto const resolved = Lightweight::Tools::DiscoverPlugins(dirs);

    REQUIRE(resolved.size() == 2);
    auto const names = resolved | std::views::transform([](auto const& r) {
                           return r.path.filename().string();
                       });
    std::vector<std::string> sortedNames(names.begin(), names.end());
    std::ranges::sort(sortedNames);
    CHECK(sortedNames == std::vector<std::string> { "alpha.dll", "beta.dll" });
}

TEST_CASE("DiscoverPlugins — same filename: newer mtime wins", "[PluginDiscovery]")
{
    ScopedTempTree const tree;
    auto const dirOld = tree.Dir("old");
    auto const dirNew = tree.Dir("new");

    auto const base = BaseTime();
    Touch(dirOld / "plugin.so", base);
    Touch(dirNew / "plugin.so", base + std::chrono::seconds(30));

    std::vector<std::filesystem::path> dirs { dirOld, dirNew };
    std::vector<std::string> shadows;
    auto const resolved =
        Lightweight::Tools::DiscoverPlugins(dirs, [&](std::string_view m) { shadows.emplace_back(m); });

    REQUIRE(resolved.size() == 1);
    CHECK(resolved.front().path.parent_path() == dirNew);
    CHECK(resolved.front().path.filename() == std::filesystem::path("plugin.so"));
    REQUIRE(shadows.size() == 1);
    CHECK(shadows.front().find("plugin.so") != std::string::npos);
    CHECK(shadows.front().find("newer mtime") != std::string::npos);
}

TEST_CASE("DiscoverPlugins — mtime tie: earlier-listed dir wins", "[PluginDiscovery]")
{
    ScopedTempTree const tree;
    auto const dirFirst = tree.Dir("first");
    auto const dirSecond = tree.Dir("second");

    auto const same = BaseTime();
    Touch(dirFirst / "plugin.dylib", same);
    Touch(dirSecond / "plugin.dylib", same);

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

    Touch(dir / "real.dll", BaseTime());
    Touch(dir / "notes.txt", BaseTime());
    Touch(dir / "data.json", BaseTime());

    std::vector<std::filesystem::path> dirs { dir };
    auto const resolved = Lightweight::Tools::DiscoverPlugins(dirs);

    REQUIRE(resolved.size() == 1);
    CHECK(resolved.front().path.filename() == std::filesystem::path("real.dll"));
}

TEST_CASE("DiscoverPlugins — missing directories are skipped without crashing", "[PluginDiscovery]")
{
    ScopedTempTree const tree;
    auto const dir = tree.Dir("present");
    auto const ghost = tree.Dir("ghost");
    std::filesystem::remove_all(ghost);

    Touch(dir / "p.dll", BaseTime());

    std::vector<std::filesystem::path> dirs { ghost, dir };
    auto const resolved = Lightweight::Tools::DiscoverPlugins(dirs);

    REQUIRE(resolved.size() == 1);
    CHECK(resolved.front().path.filename() == std::filesystem::path("p.dll"));
}

#ifdef _WIN32
TEST_CASE("DiscoverPlugins — filename comparison is case-insensitive on Windows", "[PluginDiscovery]")
{
    ScopedTempTree const tree;
    auto const dirA = tree.Dir("a");
    auto const dirB = tree.Dir("b");

    auto const base = BaseTime();
    Touch(dirA / "Plugin.dll", base);
    Touch(dirB / "plugin.dll", base + std::chrono::seconds(10));

    std::vector<std::filesystem::path> dirs { dirA, dirB };
    auto const resolved = Lightweight::Tools::DiscoverPlugins(dirs);

    REQUIRE(resolved.size() == 1);
    CHECK(resolved.front().path.parent_path() == dirB);
}
#else
TEST_CASE("DiscoverPlugins — filename comparison is case-sensitive on POSIX", "[PluginDiscovery]")
{
    ScopedTempTree const tree;
    auto const dirA = tree.Dir("a");
    auto const dirB = tree.Dir("b");

    auto const base = BaseTime();
    Touch(dirA / "Plugin.so", base);
    Touch(dirB / "plugin.so", base + std::chrono::seconds(10));

    std::vector<std::filesystem::path> dirs { dirA, dirB };
    auto const resolved = Lightweight::Tools::DiscoverPlugins(dirs);

    // On case-sensitive filesystems these are two distinct plugins, not a
    // duplicate — both should survive.
    REQUIRE(resolved.size() == 2);
}
#endif
