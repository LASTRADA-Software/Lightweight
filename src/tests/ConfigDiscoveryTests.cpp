// SPDX-License-Identifier: Apache-2.0
//
// Unit tests for `Lightweight::Config::FindConfigFile`: the dbtool.yml lookup
// order (explicit → walk up from cwd → walk up from the executable directory →
// user default). Every test builds its own directory tree under the system temp
// directory and injects all paths, so the real user config is never consulted.

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

#include <Config/ConfigDiscovery.hpp>

namespace fs = std::filesystem;
namespace Cfg = Lightweight::Config;

namespace
{

/// A fresh temp directory tree, removed on scope exit.
class TempTree
{
  public:
    TempTree()
    {
        static int counter = 0;
        _root = fs::temp_directory_path() / ("lightweight-discovery-" + std::to_string(++counter));
        fs::remove_all(_root);
        fs::create_directories(_root);
        _root = fs::canonical(_root);
    }
    ~TempTree()
    {
        std::error_code ec;
        fs::remove_all(_root, ec);
    }
    TempTree(TempTree const&) = delete;
    TempTree(TempTree&&) = delete;
    TempTree& operator=(TempTree const&) = delete;
    TempTree& operator=(TempTree&&) = delete;

    /// Creates (and returns) a directory below the root.
    [[nodiscard]] fs::path Dir(fs::path const& relative) const
    {
        auto const path = _root / relative;
        fs::create_directories(path);
        return path;
    }

    /// Writes a dbtool.yml into `relative` and returns its path.
    [[nodiscard]] fs::path Config(fs::path const& relative) const
    {
        auto const path = Dir(relative) / Cfg::ConfigFileName;
        std::ofstream { path } << "profiles: {}\n";
        return path;
    }

    [[nodiscard]] fs::path const& Root() const noexcept
    {
        return _root;
    }

  private:
    fs::path _root;
};

/// True if some ancestor of the temp directory already holds a dbtool.yml,
/// which would make the "nothing found" expectations meaningless.
bool StrayConfigAboveTemp()
{
    auto dir = fs::canonical(fs::temp_directory_path());
    while (true)
    {
        if (fs::exists(dir / Cfg::ConfigFileName))
            return true;
        if (dir == dir.parent_path())
            return false;
        dir = dir.parent_path();
    }
}

} // namespace

TEST_CASE("ConfigDiscovery — an explicit path wins", "[ConfigDiscovery]")
{
    TempTree const tree;
    auto const explicitFile = tree.Config("explicit");
    std::ignore = tree.Config("cwd");

    auto const result = Cfg::FindConfigFile(
        { .explicitPath = explicitFile, .workingDirectory = tree.Dir("cwd"), .executableDirectory = {}, .userDefault = {} });
    REQUIRE(result.has_value());
    CHECK(result->source == Cfg::ConfigSource::Explicit);
    CHECK(result->path == explicitFile);
}

TEST_CASE("ConfigDiscovery — a missing explicit path is an error naming it", "[ConfigDiscovery]")
{
    TempTree const tree;
    auto const missing = tree.Root() / "nope.yml";
    auto const result = Cfg::FindConfigFile(
        { .explicitPath = missing, .workingDirectory = tree.Root(), .executableDirectory = {}, .userDefault = {} });
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().contains(missing.string()));
}

TEST_CASE("ConfigDiscovery — walks up from the working directory to the nearest file", "[ConfigDiscovery]")
{
    TempTree const tree;
    std::ignore = tree.Config("a");
    auto const nearer = tree.Config("a/b");
    auto const result = Cfg::FindConfigFile(
        { .explicitPath = {}, .workingDirectory = tree.Dir("a/b/c"), .executableDirectory = {}, .userDefault = {} });
    REQUIRE(result.has_value());
    CHECK(result->source == Cfg::ConfigSource::WorkingDirectory);
    CHECK(result->path == nearer);
}

TEST_CASE("ConfigDiscovery — falls back to walking up from the executable directory", "[ConfigDiscovery]")
{
    if (StrayConfigAboveTemp())
        SKIP("a dbtool.yml exists above the temp directory");
    TempTree const tree;
    auto const exeConfig = tree.Config("exe");
    auto const result = Cfg::FindConfigFile({ .explicitPath = {},
                                              .workingDirectory = tree.Dir("cwd/deep"),
                                              .executableDirectory = tree.Dir("exe/bin"),
                                              .userDefault = tree.Root() / "user" / "dbtool.yml" });
    REQUIRE(result.has_value());
    CHECK(result->source == Cfg::ConfigSource::ExecutableDirectory);
    CHECK(result->path == exeConfig);
}

TEST_CASE("ConfigDiscovery — uses the user default when nothing is found upward", "[ConfigDiscovery]")
{
    if (StrayConfigAboveTemp())
        SKIP("a dbtool.yml exists above the temp directory");
    TempTree const tree;
    auto const userDefault = tree.Config("user");
    auto const result = Cfg::FindConfigFile({ .explicitPath = {},
                                              .workingDirectory = tree.Dir("cwd"),
                                              .executableDirectory = tree.Dir("exe"),
                                              .userDefault = userDefault });
    REQUIRE(result.has_value());
    CHECK(result->source == Cfg::ConfigSource::UserDefault);
    CHECK(result->path == userDefault);
}

TEST_CASE("ConfigDiscovery — reports None with the user default path when no file exists", "[ConfigDiscovery]")
{
    if (StrayConfigAboveTemp())
        SKIP("a dbtool.yml exists above the temp directory");
    TempTree const tree;
    auto const userDefault = tree.Root() / "user" / "dbtool.yml";
    auto const result = Cfg::FindConfigFile({ .explicitPath = {},
                                              .workingDirectory = tree.Dir("cwd"),
                                              .executableDirectory = tree.Dir("exe"),
                                              .userDefault = userDefault });
    REQUIRE(result.has_value());
    CHECK(result->source == Cfg::ConfigSource::None);
    CHECK(result->path == userDefault);
}

TEST_CASE("ConfigDiscovery — a directory named dbtool.yml is not a config file", "[ConfigDiscovery]")
{
    if (StrayConfigAboveTemp())
        SKIP("a dbtool.yml exists above the temp directory");
    TempTree const tree;
    std::ignore = tree.Dir(fs::path { "cwd" } / Cfg::ConfigFileName);
    auto const result = Cfg::FindConfigFile({ .explicitPath = {},
                                              .workingDirectory = tree.Dir("cwd"),
                                              .executableDirectory = {},
                                              .userDefault = tree.Root() / "none.yml" });
    REQUIRE(result.has_value());
    CHECK(result->source == Cfg::ConfigSource::None);
}

TEST_CASE("ConfigDiscovery — the executable directory is resolvable", "[ConfigDiscovery]")
{
    auto const dir = Cfg::ExecutableDirectory();
    REQUIRE_FALSE(dir.empty());
    CHECK(fs::is_directory(dir));
}

TEST_CASE("ConfigDiscovery — sources have human-readable names", "[ConfigDiscovery]")
{
    CHECK(Cfg::ToString(Cfg::ConfigSource::Explicit) == "--config");
    CHECK(Cfg::ToString(Cfg::ConfigSource::WorkingDirectory) == "current directory");
    CHECK(Cfg::ToString(Cfg::ConfigSource::ExecutableDirectory) == "executable directory");
    CHECK(Cfg::ToString(Cfg::ConfigSource::UserDefault) == "user default");
    CHECK(Cfg::ToString(Cfg::ConfigSource::None) == "none");
}

TEST_CASE("ConfigDiscovery — upward walks skip files owned by someone else", "[ConfigDiscovery]")
{
    TempTree const tree;
    auto const planted = tree.Config("a/b");
    auto const own = tree.Config("a");
    auto inputs = Cfg::DiscoveryInputs {
        .explicitPath = {}, .workingDirectory = tree.Dir("a/b/c"), .executableDirectory = {}, .userDefault = {}
    };
    inputs.isTrusted = [&](fs::path const& candidate) {
        return candidate != planted;
    };

    auto const result = Cfg::FindConfigFile(inputs);
    REQUIRE(result.has_value());
    CHECK(result->path == own);
    REQUIRE(result->skipped.size() == 1);
    CHECK(result->skipped.front() == planted);
}

TEST_CASE("ConfigDiscovery — files created by the current user are trusted by default", "[ConfigDiscovery]")
{
    TempTree const tree;
    auto const own = tree.Config("mine");
    CHECK(Cfg::IsTrustedConfigFile(own));
}
