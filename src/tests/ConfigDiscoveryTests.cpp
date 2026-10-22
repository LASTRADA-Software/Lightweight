// SPDX-License-Identifier: Apache-2.0
//
// Unit tests for `Lightweight::Config::FindConfigCandidates`: the dbtool.yml files
// to merge (an explicit file on its own, else every file walking up from the
// executable directory, then the user default). Every test builds its own directory
// tree under the system temp directory and injects all paths, so the real user
// config is never consulted.

#include "ScopedEnvironmentVariable.hpp"
#include "UniqueTempPath.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <tuple>

#include <Config/ConfigDiscovery.hpp>
#include <Config/ProfileStore.hpp>

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
        _root = UniqueTempPath("lightweight-discovery-" + std::to_string(++counter));
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

TEST_CASE("ConfigDiscovery — a directory named dbtool.yml is not a config file", "[ConfigDiscovery]")
{
    if (StrayConfigAboveTemp())
        SKIP("a dbtool.yml exists above the temp directory");
    TempTree const tree;
    std::ignore = tree.Dir(fs::path { "exe" } / Cfg::ConfigFileName);
    auto const result = Cfg::FindConfigCandidates(
        { .explicitPath = {}, .searchStart = tree.Dir("exe"), .userDefault = tree.Root() / "none.yml" });
    REQUIRE(result.has_value());
    CHECK(result->empty());
}

TEST_CASE("ConfigDiscovery — the working directory takes no part in discovery", "[ConfigDiscovery]")
{
    if (StrayConfigAboveTemp())
        SKIP("a dbtool.yml exists above the temp directory");
    TempTree const tree;
    std::ignore = tree.Config("cwd");
    ScopedEnvironmentVariable const search { std::string { Cfg::ConfigSearchDirectoryVariable }.c_str(),
                                             tree.Dir("exe").string() };
    ScopedEnvironmentVariable const user { Cfg::ProfileStore::UserConfigDirectoryVariable, tree.Dir("user").string() };
    auto const previous = fs::current_path();
    fs::current_path(tree.Dir("cwd"));
    auto const result = Cfg::FindConfigCandidates(Cfg::DefaultDiscoveryInputs());
    fs::current_path(previous);
    REQUIRE(result.has_value());
    CHECK(result->empty());
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
    CHECK(Cfg::ToString(Cfg::ConfigSource::ExecutableDirectory) == "executable directory");
    CHECK(Cfg::ToString(Cfg::ConfigSource::UserDefault) == "user default");
    CHECK(Cfg::ToString(Cfg::ConfigSource::None) == "none");
}

TEST_CASE("ConfigDiscovery — candidates are every file above the executable directory, nearest first", "[ConfigDiscovery]")
{
    if (StrayConfigAboveTemp())
        SKIP("a dbtool.yml exists above the temp directory");
    TempTree const tree;
    auto const outer = tree.Config("a");
    auto const inner = tree.Config("a/b");
    auto const result =
        Cfg::FindConfigCandidates({ .explicitPath = {}, .searchStart = tree.Dir("a/b/c"), .userDefault = {} });
    REQUIRE(result.has_value());
    REQUIRE(result->size() == 2);
    CHECK((*result)[0].path == inner);
    CHECK((*result)[1].path == outer);
    CHECK((*result)[0].source == Cfg::ConfigSource::ExecutableDirectory);
    CHECK((*result)[1].source == Cfg::ConfigSource::ExecutableDirectory);
}

TEST_CASE("ConfigDiscovery — the user default is appended last", "[ConfigDiscovery]")
{
    if (StrayConfigAboveTemp())
        SKIP("a dbtool.yml exists above the temp directory");
    TempTree const tree;
    std::ignore = tree.Config("exe");
    auto const userDefault = tree.Config("user");
    auto const result =
        Cfg::FindConfigCandidates({ .explicitPath = {}, .searchStart = tree.Dir("exe"), .userDefault = userDefault });
    REQUIRE(result.has_value());
    REQUIRE(result->size() == 2);
    CHECK(result->back().path == userDefault);
    CHECK(result->back().source == Cfg::ConfigSource::UserDefault);
}

TEST_CASE("ConfigDiscovery — the user default is not listed twice", "[ConfigDiscovery]")
{
    if (StrayConfigAboveTemp())
        SKIP("a dbtool.yml exists above the temp directory");
    TempTree const tree;
    auto const shared = tree.Config("both");
    auto const result =
        Cfg::FindConfigCandidates({ .explicitPath = {}, .searchStart = tree.Dir("both"), .userDefault = shared });
    REQUIRE(result.has_value());
    REQUIRE(result->size() == 1);
    CHECK(result->front().source == Cfg::ConfigSource::ExecutableDirectory);
}

TEST_CASE("ConfigDiscovery — a dbtool.yml symlinked to the user default is not listed twice", "[ConfigDiscovery]")
{
    if (StrayConfigAboveTemp())
        SKIP("a dbtool.yml exists above the temp directory");
    TempTree const tree;
    auto const userDefault = tree.Config("user");
    auto const link = tree.Dir("exe") / Cfg::ConfigFileName;
    std::error_code ec;
    fs::create_symlink(userDefault, link, ec);
    if (ec)
        SKIP("cannot create a symlink here: " + ec.message());
    auto const result =
        Cfg::FindConfigCandidates({ .explicitPath = {}, .searchStart = tree.Dir("exe"), .userDefault = userDefault });
    REQUIRE(result.has_value());
    REQUIRE(result->size() == 1);
    CHECK(result->front().path == link);
}

TEST_CASE("ConfigDiscovery — an explicit path is the only candidate", "[ConfigDiscovery]")
{
    TempTree const tree;
    std::ignore = tree.Config("exe");
    auto const explicitFile = tree.Config("explicit");
    auto const result = Cfg::FindConfigCandidates(
        { .explicitPath = explicitFile, .searchStart = tree.Dir("exe/bin"), .userDefault = tree.Config("user") });
    REQUIRE(result.has_value());
    REQUIRE(result->size() == 1);
    CHECK(result->front().path == explicitFile);
    CHECK(result->front().source == Cfg::ConfigSource::Explicit);
}

TEST_CASE("ConfigDiscovery — a missing explicit path is a candidate error naming it", "[ConfigDiscovery]")
{
    TempTree const tree;
    auto const missing = tree.Root() / "nope.yml";
    auto const result = Cfg::FindConfigCandidates({ .explicitPath = missing, .searchStart = {}, .userDefault = {} });
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().contains(missing.string()));
}

TEST_CASE("ConfigDiscovery — no candidates when nothing exists", "[ConfigDiscovery]")
{
    if (StrayConfigAboveTemp())
        SKIP("a dbtool.yml exists above the temp directory");
    TempTree const tree;
    auto const result = Cfg::FindConfigCandidates(
        { .explicitPath = {}, .searchStart = tree.Dir("exe"), .userDefault = tree.Root() / "user" / "dbtool.yml" });
    REQUIRE(result.has_value());
    CHECK(result->empty());
}

TEST_CASE("ConfigDiscovery — the search directory variable replaces the executable directory", "[ConfigDiscovery]")
{
    TempTree const tree;
    auto const directory = tree.Dir("x");
    ScopedEnvironmentVariable const override { std::string { Cfg::ConfigSearchDirectoryVariable }.c_str(),
                                               directory.string() };
    CHECK(Cfg::DefaultDiscoveryInputs().searchStart == directory);
}
