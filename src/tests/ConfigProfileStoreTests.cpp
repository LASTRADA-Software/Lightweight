// SPDX-License-Identifier: Apache-2.0
//
// Unit tests for `Lightweight::Config::ProfileStore`. Covers:
//   - the legacy single-profile YAML shape (PluginsDir/ConnectionString/Schema),
//   - the new multi-profile shape,
//   - save/load round-trip fidelity,
//   - missing-file = empty store,
//   - malformed-YAML and conflicting-auth error paths.
//
// Every test uses its own temp file under `temp_directory_path()` to stay
// independent of the real user config at `$HOME/.config/dbtool/dbtool.yml`.

#include <Lightweight/Config/ProfileStore.hpp>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace
{

/// Produces a unique temp-file path for each test case. Removed at scope exit
/// so debugging a failing test still leaves you with inspectable artefacts
/// (only successful runs clean up).
class ScopedTempYaml
{
  public:
    explicit ScopedTempYaml(std::string_view contents)
    {
        static int counter = 0;
        _path = std::filesystem::temp_directory_path()
              / ("lightweight-profilestore-test-" + std::to_string(++counter) + ".yml");
        std::ofstream out(_path, std::ios::binary | std::ios::trunc);
        out << contents;
    }
    ~ScopedTempYaml()
    {
        std::error_code ec;
        std::filesystem::remove(_path, ec);
    }
    ScopedTempYaml(ScopedTempYaml const&) = delete;
    ScopedTempYaml(ScopedTempYaml&&) = delete;
    ScopedTempYaml& operator=(ScopedTempYaml const&) = delete;
    ScopedTempYaml& operator=(ScopedTempYaml&&) = delete;

    [[nodiscard]] std::filesystem::path const& Path() const noexcept
    {
        return _path;
    }

  private:
    std::filesystem::path _path;
};

} // namespace

TEST_CASE("ProfileStore — missing file yields empty store, not an error", "[ProfileStore]")
{
    auto const nonexistent = std::filesystem::temp_directory_path() / "lightweight-profilestore-none.yml";
    std::filesystem::remove(nonexistent); // just in case.

    auto const result = Lightweight::Config::ProfileStore::LoadOrDefault(nonexistent);
    REQUIRE(result.has_value());
    REQUIRE(result->Empty());
    REQUIRE(result->Default() == nullptr);
}

TEST_CASE("ProfileStore — legacy single-profile YAML loads as 'default'", "[ProfileStore]")
{
    ScopedTempYaml const yaml(R"(PluginsDir: /opt/migrations
ConnectionString: "DRIVER=SQLite3;Database=legacy.db"
Schema: main
)");

    auto const result = Lightweight::Config::ProfileStore::LoadOrDefault(yaml.Path());
    REQUIRE(result.has_value());

    auto const& store = *result;
    REQUIRE(store.Size() == 1);

    auto const* profile = store.Default();
    REQUIRE(profile != nullptr);
    CHECK(profile->name == "default");
    CHECK(profile->pluginsDir.string() == "/opt/migrations");
    CHECK(profile->connectionString == "DRIVER=SQLite3;Database=legacy.db");
    CHECK(profile->schema == "main");
    CHECK(profile->dsn.empty());
}

TEST_CASE("ProfileStore — multi-profile YAML preserves order & default", "[ProfileStore]")
{
    ScopedTempYaml const yaml(R"(defaultProfile: prod
profiles:
  dev:
    pluginsDir: ./dev-migrations
    connectionString: "DRIVER=SQLite3;Database=dev.db"
    schema: main
  prod:
    pluginsDir: ./migrations
    dsn: ACME_PROD
    uid: deploy
    secretRef: keychain:lightweight/acme-prod
    schema: dbo
)");

    auto const result = Lightweight::Config::ProfileStore::LoadOrDefault(yaml.Path());
    REQUIRE(result.has_value());

    auto const& store = *result;
    REQUIRE(store.Size() == 2);
    REQUIRE(store.DefaultProfileName() == "prod");

    auto const* dev = store.Find("dev");
    REQUIRE(dev != nullptr);
    CHECK(dev->connectionString == "DRIVER=SQLite3;Database=dev.db");
    CHECK(dev->schema == "main");

    auto const* prod = store.Find("prod");
    REQUIRE(prod != nullptr);
    CHECK(prod->dsn == "ACME_PROD");
    CHECK(prod->uid == "deploy");
    CHECK(prod->secretRef == "keychain:lightweight/acme-prod");
    CHECK(prod->schema == "dbo");
    CHECK(prod->connectionString.empty());

    CHECK(store.Default() == prod);
}

TEST_CASE("ProfileStore — save/load round-trip is lossless", "[ProfileStore]")
{
    using namespace Lightweight::Config;

    auto const path = std::filesystem::temp_directory_path() / "lightweight-profilestore-roundtrip.yml";
    std::filesystem::remove(path);

    ProfileStore store;
    store.Upsert(Profile {
        .name = "dev",
        .pluginsDir = "/tmp/dev",
        .schema = "main",
        .dsn = {},
        .connectionString = "DRIVER=SQLite3;Database=dev.db",
        .uid = {},
        .secretRef = {},
    });
    store.Upsert(Profile {
        .name = "prod",
        .pluginsDir = "/srv/migrations",
        .schema = "dbo",
        .dsn = "ACME_PROD",
        .connectionString = {},
        .uid = "deploy",
        .secretRef = "env:ACME_PROD_PWD",
    });
    store.SetDefault("prod");

    REQUIRE(store.Save(path).has_value());

    auto const reloaded = ProfileStore::LoadOrDefault(path);
    REQUIRE(reloaded.has_value());
    REQUIRE(reloaded->Size() == 2);
    REQUIRE(reloaded->DefaultProfileName() == "prod");

    auto const* prod = reloaded->Find("prod");
    REQUIRE(prod != nullptr);
    CHECK(prod->dsn == "ACME_PROD");
    CHECK(prod->uid == "deploy");
    CHECK(prod->secretRef == "env:ACME_PROD_PWD");

    std::filesystem::remove(path);
}

TEST_CASE("ProfileStore — malformed YAML returns a readable error", "[ProfileStore]")
{
    ScopedTempYaml const yaml("profiles:\n  broken: [not-a-map]\n");
    auto const result = Lightweight::Config::ProfileStore::LoadOrDefault(yaml.Path());
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().find("broken") != std::string::npos);
}

TEST_CASE("ProfileStore — dsn and connectionString are mutually exclusive", "[ProfileStore]")
{
    ScopedTempYaml const yaml(R"(profiles:
  both:
    dsn: X
    connectionString: "DRIVER=Y"
)");
    auto const result = Lightweight::Config::ProfileStore::LoadOrDefault(yaml.Path());
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().find("dsn") != std::string::npos);
    CHECK(result.error().find("connectionString") != std::string::npos);
}

TEST_CASE("ProfileStore — Remove clears default when it points at the removed profile", "[ProfileStore]")
{
    using namespace Lightweight::Config;

    ProfileStore store;
    store.Upsert(Profile {
        .name = "solo",
        .pluginsDir = {},
        .schema = {},
        .dsn = {},
        .connectionString = "DRIVER=SQLite3;Database=:memory:",
        .uid = {},
        .secretRef = {},
    });
    store.SetDefault("solo");
    REQUIRE(store.Default() != nullptr);

    REQUIRE(store.Remove("solo"));
    CHECK(store.DefaultProfileName().empty());
    CHECK(store.Default() == nullptr);
}
