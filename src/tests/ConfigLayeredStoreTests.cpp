// SPDX-License-Identifier: Apache-2.0
//
// Unit tests for `Lightweight::Config::LayeredProfileStore`: nearest-wins merging of several
// dbtool.yml layers, origin tracking and write targets. Files are served from memory.

#include "UniqueTempPath.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <map>
#include <ranges>
#include <string>
#include <vector>

#include <Config/LayeredProfileStore.hpp>

namespace fs = std::filesystem;
namespace Cfg = Lightweight::Config;

namespace
{

// Platform-absolute fixture paths (a drive letter is prepended on Windows).
fs::path const Acme = fs::absolute("/opt/acme");
fs::path const Near = Acme / "app-a" / "dbtool.yml";
fs::path const Far = Acme / "dbtool.yml";
fs::path const User = fs::absolute("/home/u/dbtool.yml");
fs::path const Elsewhere = fs::absolute("/elsewhere/dbtool.yml");

using Files = std::map<fs::path, std::string>;

Cfg::ConfigFileReader ReaderFor(Files const& files)
{
    return [&files](fs::path const& path) -> std::expected<std::string, std::string> {
        auto const it = files.find(path);
        if (it == files.end())
            return std::unexpected { "missing " + path.string() };
        return it->second;
    };
}

std::vector<Cfg::ConfigCandidate> Candidates(std::vector<fs::path> const& paths)
{
    auto const toCandidate = [](fs::path const& path) {
        return Cfg::ConfigCandidate { .path = path,
                                      .source = path == User ? Cfg::ConfigSource::UserDefault
                                                             : Cfg::ConfigSource::ExecutableDirectory };
    };
    return std::ranges::to<std::vector>(paths | std::views::transform(toCandidate));
}

std::expected<Cfg::LayeredProfileStore, std::string> LoadFiles(Files const& files, std::vector<fs::path> const& paths)
{
    auto const candidates = Candidates(paths);
    return Cfg::LayeredProfileStore::Load(candidates, ReaderFor(files));
}

std::vector<std::string> ProfileNames(Cfg::ProfileStore const& store)
{
    std::vector<std::string> names;
    for (auto const& profile: store.Profiles())
        names.push_back(profile.name);
    return names;
}

Files DeploymentFiles(bool withRoot)
{
    return {
        { Near, "defaultPluginsDir: ./plugins\nprofiles: {}\n" },
        { Far,
          std::string(withRoot ? "root: true\n" : "") + "defaultPluginsDir: " + (Acme / "shared").string()
              + "\nprofiles:\n  p1:\n    dsn: A\n  p2:\n    dsn: B\n" },
        { User, "profiles:\n  u:\n    dsn: U\n" },
    };
}

} // namespace

TEST_CASE("LayeredProfileStore — the deployment layout merges plugins and shared profiles", "[LayeredProfileStore]")
{
    auto const store = LoadFiles(DeploymentFiles(true), { Near, Far, User });
    REQUIRE(store);
    CHECK(store->Merged().DefaultPluginsDir() == std::vector<fs::path> { Acme / "app-a" / "plugins" });
    CHECK(ProfileNames(store->Merged()) == std::vector<std::string> { "p1", "p2" });
    CHECK(store->Layers().size() == 2);
    CHECK(store->RootFound());
    CHECK_FALSE(store->Empty());
    REQUIRE(store->OriginOf("p1") != nullptr);
    CHECK(store->OriginOf("p1")->path == Far);
    CHECK(store->Merged().Find("u") == nullptr);
}

TEST_CASE("LayeredProfileStore — without root the user file is the last layer", "[LayeredProfileStore]")
{
    auto const store = LoadFiles(DeploymentFiles(false), { Near, Far, User });
    REQUIRE(store);
    CHECK(store->Layers().size() == 3);
    CHECK_FALSE(store->RootFound());
    CHECK(store->Merged().Find("u") != nullptr);
    REQUIRE(store->OriginOf("u") != nullptr);
    CHECK(store->OriginOf("u")->source == Cfg::ConfigSource::UserDefault);
    CHECK(store->OriginOf("nope") == nullptr);
}

TEST_CASE("LayeredProfileStore — the nearest same-named profile wins whole", "[LayeredProfileStore]")
{
    Files const files {
        { Near, "profiles:\n  p1:\n    dsn: A\n" },
        { Far, "profiles:\n  p1:\n    dsn: B\n    schema: s\n" },
    };
    auto const store = LoadFiles(files, { Near, Far });
    REQUIRE(store);
    auto const* p1 = store->Merged().Find("p1");
    REQUIRE(p1 != nullptr);
    CHECK(p1->dsn == "A");
    CHECK(p1->schema.empty());
    CHECK(store->OriginOf("p1")->path == Near);
    CHECK(store->Merged().Size() == 1);
}

TEST_CASE("LayeredProfileStore — store-wide values come from the nearest layer setting them", "[LayeredProfileStore]")
{
    Files const files {
        { Near, "defaultBackupDir: " + (Acme / "near-bak").string() + "\nprofiles:\n  p1:\n    dsn: A\n" },
        { Far, "defaultProfile: p2\ndefaultBackupDir: " + (Acme / "far-bak").string() + "\nprofiles:\n  p2:\n    dsn: B\n" },
    };
    auto const store = LoadFiles(files, { Near, Far });
    REQUIRE(store);
    CHECK(store->Merged().DefaultProfileName() == "p2");
    CHECK(store->Merged().DefaultBackupDir() == Acme / "near-bak");
    REQUIRE(store->DefaultProfileOrigin() != nullptr);
    CHECK(store->DefaultProfileOrigin()->path == Far);
}

TEST_CASE("LayeredProfileStore — a legacy layer takes part", "[LayeredProfileStore]")
{
    Files const files {
        { Near, "ConnectionString: \"DSN=x\"\n" },
        { Far, "profiles:\n  p1:\n    dsn: B\n" },
    };
    auto const store = LoadFiles(files, { Near, Far });
    REQUIRE(store);
    CHECK(ProfileNames(store->Merged()) == std::vector<std::string> { "default", "p1" });
    CHECK(store->Merged().DefaultProfileName() == "default");
    CHECK(store->OriginOf("default")->path == Near);
}

TEST_CASE("LayeredProfileStore — a broken layer fails the load naming it", "[LayeredProfileStore]")
{
    Files const files {
        { Near, "profiles: {}\n" },
        { Far, "profiles: [unclosed" },
    };
    auto const store = LoadFiles(files, { Near, Far });
    REQUIRE_FALSE(store);
    CHECK(store.error().contains(Far.string()));
}

TEST_CASE("LayeredProfileStore — an unreadable layer fails the load", "[LayeredProfileStore]")
{
    auto const store = LoadFiles({}, { Near });
    REQUIRE_FALSE(store);
    CHECK(store.error().contains(Near.string()));
}

TEST_CASE("LayeredProfileStore — an explicit file is loaded alone", "[LayeredProfileStore]")
{
    Files const files {
        { Near, "root: true\nprofiles:\n  p1:\n    dsn: A\n" },
        { Far, "profiles:\n  p2:\n    dsn: B\n" },
    };
    std::vector<Cfg::ConfigCandidate> const candidates { { .path = Near, .source = Cfg::ConfigSource::Explicit } };
    auto const store = Cfg::LayeredProfileStore::Load(candidates, ReaderFor(files));
    REQUIRE(store);
    CHECK(store->Layers().size() == 1);
    CHECK(store->Merged().Find("p1") != nullptr);
    CHECK(store->NewProfileTarget(User) == Near);

    Files const unrooted { { Near, "profiles:\n  p1:\n    dsn: A\n" } };
    auto const other = Cfg::LayeredProfileStore::Load(candidates, ReaderFor(unrooted));
    REQUIRE(other);
    CHECK(other->NewProfileTarget(User) == Near);
}

TEST_CASE("LayeredProfileStore — no candidates yields an empty store", "[LayeredProfileStore]")
{
    auto const store = LoadFiles({}, {});
    REQUIRE(store);
    CHECK(store->Empty());
    CHECK_FALSE(store->RootFound());
    CHECK(store->DefaultProfileOrigin() == nullptr);
}

TEST_CASE("LayeredProfileStore — NewProfileTarget is the root file, else the user file", "[LayeredProfileStore]")
{
    auto const rooted = LoadFiles(DeploymentFiles(true), { Near, Far, User });
    REQUIRE(rooted);
    CHECK(rooted->NewProfileTarget(User) == Far);

    auto const unrooted = LoadFiles(DeploymentFiles(false), { Near, Far, User });
    REQUIRE(unrooted);
    CHECK(unrooted->NewProfileTarget(Elsewhere) == Elsewhere);

    auto const none = LoadFiles({}, {});
    REQUIRE(none);
    CHECK(none->NewProfileTarget(User) == User);
}

TEST_CASE("LayeredProfileStore — NearerLayerSettingDefault", "[LayeredProfileStore]")
{
    Files const files {
        { Near, "defaultProfile: p1\nprofiles:\n  p1:\n    dsn: A\n" },
        { Far, "profiles:\n  p2:\n    dsn: B\n" },
    };
    auto const store = LoadFiles(files, { Near, Far });
    REQUIRE(store);
    REQUIRE(store->NearerLayerSettingDefault(Far) != nullptr);
    CHECK(store->NearerLayerSettingDefault(Far)->path == Near);
    CHECK(store->NearerLayerSettingDefault(Near) == nullptr);
    REQUIRE(store->NearerLayerSettingDefault(User) != nullptr);
    CHECK(store->NearerLayerSettingDefault(User)->path == Near);
}

TEST_CASE("LayeredProfileStore — ReadConfigFile reports a missing file", "[LayeredProfileStore]")
{
    auto const path = UniqueTempPath("lightweight-layered-missing") / "dbtool.yml";
    auto const content = Cfg::ReadConfigFile(path);
    REQUIRE_FALSE(content);
    CHECK(content.error().contains(path.string()));
}

TEST_CASE("LayeredProfileStore — relative directories resolve against their own file", "[LayeredProfileStore]")
{
    auto const AbsBak = fs::absolute("/abs/bak");
    Files const files {
        { Near,
          "defaultBackupDir: ../bak\nprofiles:\n  p1:\n    dsn: A\n    pluginsDir: ./pl\n    backupDir: " + AbsBak.string()
              + "\n" },
        { Far, "profiles:\n  p2:\n    dsn: B\n    backupDir: bak2\n" },
    };
    auto const store = LoadFiles(files, { Near, Far });
    REQUIRE(store);
    CHECK(store->Merged().DefaultBackupDir() == Acme / "bak");
    CHECK(store->Merged().Find("p1")->pluginsDir == Acme / "app-a" / "pl");
    CHECK(store->Merged().Find("p1")->backupDir == AbsBak);
    CHECK(store->Merged().Find("p2")->backupDir == Acme / "bak2");
}
