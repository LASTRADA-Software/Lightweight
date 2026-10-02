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

#include <Lightweight/SqlConnectInfo.hpp>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <variant>

#include <Config/ProfileStore.hpp>

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
        _path =
            std::filesystem::temp_directory_path() / ("lightweight-profilestore-test-" + std::to_string(++counter) + ".yml");
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
        .password = {},
    });
    store.Upsert(Profile {
        .name = "prod",
        .pluginsDir = "/srv/migrations",
        .schema = "dbo",
        .dsn = "ACME_PROD",
        .connectionString = {},
        .uid = "deploy",
        .secretRef = "env:ACME_PROD_PWD",
        .password = {},
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
    CHECK(result.error().contains("broken"));
}

TEST_CASE("Profile::ToConnectInfo — a resolved password is appended with connection-string quoting", "[ProfileStore]")
{
    using namespace Lightweight;
    auto const profile = Config::Profile {
        .name = "dev",
        .pluginsDir = {},
        .schema = {},
        .dsn = {},
        .connectionString = "DRIVER=SQLite3;Database=dev.db",
        .uid = {},
        .secretRef = {},
        .password = {},
    };

    // A plain secret keeps the spelling every existing profile has seen.
    auto const plain = std::get<SqlConnectionString>(profile.ToConnectInfo("s3cr3t"));
    CHECK(plain.value == "DRIVER=SQLite3;Database=dev.db;PWD=s3cr3t");

    // One carrying a `;` must not be spliced raw, or it becomes `PWD=p` plus a stray keyword (#635).
    auto const quoted = std::get<SqlConnectionString>(profile.ToConnectInfo("p;w"));
    CHECK(quoted.value == "DRIVER=SQLite3;Database=dev.db;PWD={p;w}");
    CHECK(ParseConnectionString(quoted).at("PWD") == "p;w");
}

TEST_CASE("Profile::ToConnectInfo — only a whole PWD/Password attribute counts as an existing password", "[ProfileStore]")
{
    using namespace Lightweight;
    auto profile = Config::Profile {
        .name = "dev",
        .pluginsDir = {},
        .schema = {},
        .dsn = {},
        .connectionString = "DRIVER=SQLite3;Database=mypwd=1.db",
        .uid = {},
        .secretRef = {},
        .password = {},
    };

    // `pwd=` inside another attribute's value is not a password, so the resolved secret is appended.
    auto const appended = std::get<SqlConnectionString>(profile.ToConnectInfo("s3cr3t"));
    CHECK(ParseConnectionString(appended).at("PWD") == "s3cr3t");

    // A real password attribute, whatever its spelling, wins over the resolved secret.
    profile.connectionString = "DRIVER=SQLite3;Database=dev.db;Password=typed";
    auto const kept = std::get<SqlConnectionString>(profile.ToConnectInfo("s3cr3t"));
    CHECK(kept.value == "DRIVER=SQLite3;Database=dev.db;Password=typed");
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
    CHECK(result.error().contains("dsn"));
    CHECK(result.error().contains("connectionString"));
}

TEST_CASE("ProfileStore — defaultPluginsDir is the fallback when a profile omits pluginsDir", "[ProfileStore]")
{
    using namespace Lightweight::Config;

    ScopedTempYaml const yaml(R"(defaultProfile: prod
defaultPluginsDir: /opt/migrations
profiles:
  prod:
    connectionString: "DRIVER=SQLite3;Database=prod.db"
  dev:
    pluginsDir: /opt/dev-migrations
    connectionString: "DRIVER=SQLite3;Database=dev.db"
)");

    auto const result = ProfileStore::LoadOrDefault(yaml.Path());
    REQUIRE(result.has_value());

    auto const& store = *result;
    REQUIRE(store.DefaultPluginsDir().size() == 1);
    CHECK(store.DefaultPluginsDir().front().string() == "/opt/migrations");

    auto const* prod = store.Find("prod");
    REQUIRE(prod != nullptr);
    CHECK(prod->pluginsDir.empty());
    auto const prodDirs = store.EffectivePluginsDir(*prod);
    REQUIRE(prodDirs.size() == 1);
    CHECK(prodDirs.front().string() == "/opt/migrations");

    auto const* dev = store.Find("dev");
    REQUIRE(dev != nullptr);
    auto const devDirs = store.EffectivePluginsDir(*dev);
    REQUIRE(devDirs.size() == 1);
    CHECK(devDirs.front().string() == "/opt/dev-migrations");
}

TEST_CASE("ProfileStore — defaultPluginsDir accepts a YAML sequence", "[ProfileStore]")
{
    using namespace Lightweight::Config;

    ScopedTempYaml const yaml(R"(defaultProfile: prod
defaultPluginsDir:
  - /opt/migrations
  - /opt/vendor-migrations
profiles:
  prod:
    connectionString: "DRIVER=SQLite3;Database=prod.db"
)");

    auto const result = ProfileStore::LoadOrDefault(yaml.Path());
    REQUIRE(result.has_value());

    auto const& dirs = result->DefaultPluginsDir();
    REQUIRE(dirs.size() == 2);
    CHECK(dirs[0].string() == "/opt/migrations");
    CHECK(dirs[1].string() == "/opt/vendor-migrations");

    auto const* prod = result->Find("prod");
    REQUIRE(prod != nullptr);
    auto const effective = result->EffectivePluginsDir(*prod);
    REQUIRE(effective.size() == 2);
    CHECK(effective[0].string() == "/opt/migrations");
    CHECK(effective[1].string() == "/opt/vendor-migrations");
}

TEST_CASE("ProfileStore — defaultPluginsDir rejects a non-string sequence element", "[ProfileStore]")
{
    using namespace Lightweight::Config;

    ScopedTempYaml const yaml(R"(defaultProfile: prod
defaultPluginsDir:
  - /opt/migrations
  - { not: a string }
profiles:
  prod:
    connectionString: "DRIVER=SQLite3;Database=prod.db"
)");

    auto const result = ProfileStore::LoadOrDefault(yaml.Path());
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().contains("defaultPluginsDir"));
}

TEST_CASE("ProfileStore — defaultPluginsDir round-trips through Save", "[ProfileStore]")
{
    using namespace Lightweight::Config;

    auto const path = std::filesystem::temp_directory_path() / "lightweight-profilestore-globaldir.yml";
    std::filesystem::remove(path);

    ProfileStore store;
    store.SetDefaultPluginsDir({ std::filesystem::path { "/srv/migrations" } });
    store.Upsert(Profile {
        .name = "solo",
        .pluginsDir = {},
        .schema = {},
        .dsn = {},
        .connectionString = "DRIVER=SQLite3;Database=:memory:",
        .uid = {},
        .secretRef = {},
        .password = {},
    });

    REQUIRE(store.Save(path).has_value());

    auto const reloaded = ProfileStore::LoadOrDefault(path);
    REQUIRE(reloaded.has_value());
    REQUIRE(reloaded->DefaultPluginsDir().size() == 1);
    CHECK(reloaded->DefaultPluginsDir().front().string() == "/srv/migrations");

    auto const* solo = reloaded->Find("solo");
    REQUIRE(solo != nullptr);
    auto const effective = reloaded->EffectivePluginsDir(*solo);
    REQUIRE(effective.size() == 1);
    CHECK(effective.front().string() == "/srv/migrations");

    std::filesystem::remove(path);
}

TEST_CASE("ProfileStore — defaultPluginsDir multi-entry round-trip preserves order", "[ProfileStore]")
{
    using namespace Lightweight::Config;

    auto const path = std::filesystem::temp_directory_path() / "lightweight-profilestore-globaldirs.yml";
    std::filesystem::remove(path);

    ProfileStore store;
    store.SetDefaultPluginsDir({
        std::filesystem::path { "/srv/migrations" },
        std::filesystem::path { "/opt/vendor-migrations" },
    });
    store.Upsert(Profile {
        .name = "solo",
        .pluginsDir = {},
        .schema = {},
        .dsn = {},
        .connectionString = "DRIVER=SQLite3;Database=:memory:",
        .uid = {},
        .secretRef = {},
        .password = {},
    });

    REQUIRE(store.Save(path).has_value());

    auto const reloaded = ProfileStore::LoadOrDefault(path);
    REQUIRE(reloaded.has_value());
    auto const& dirs = reloaded->DefaultPluginsDir();
    REQUIRE(dirs.size() == 2);
    CHECK(dirs[0].string() == "/srv/migrations");
    CHECK(dirs[1].string() == "/opt/vendor-migrations");

    std::filesystem::remove(path);
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
        .password = {},
    });
    store.SetDefault("solo");
    REQUIRE(store.Default() != nullptr);

    REQUIRE(store.Remove("solo"));
    CHECK(store.DefaultProfileName().empty());
    CHECK(store.Default() == nullptr);
}

TEST_CASE("ProfileStore — password is loaded verbatim", "[ProfileStore]")
{
    ScopedTempYaml const yaml(R"(profiles:
  enc:
    connectionString: "Driver=x"
    password: "enc:dev:AAAA"
  plain:
    connectionString: "Driver=x"
    password: hunter2
)");
    auto const store = Lightweight::Config::ProfileStore::LoadOrDefault(yaml.Path());
    REQUIRE(store.has_value());
    REQUIRE(store->Find("enc") != nullptr);
    CHECK(store->Find("enc")->password == "enc:dev:AAAA");
    CHECK(store->Find("plain")->password == "hunter2");
}

TEST_CASE("ProfileStore — legacy top-level Password is loaded", "[ProfileStore]")
{
    ScopedTempYaml const yaml("ConnectionString: \"Driver=x\"\nPassword: hunter2\n");
    auto const store = Lightweight::Config::ProfileStore::LoadOrDefault(yaml.Path());
    REQUIRE(store.has_value());
    REQUIRE(store->Default() != nullptr);
    CHECK(store->Default()->password == "hunter2");
}

TEST_CASE("ProfileStore — password and secretRef are mutually exclusive", "[ProfileStore]")
{
    ScopedTempYaml const yaml(R"(profiles:
  both:
    connectionString: "Driver=x"
    password: hunter2
    secretRef: env:X
)");
    auto const store = Lightweight::Config::ProfileStore::LoadOrDefault(yaml.Path());
    REQUIRE_FALSE(store.has_value());
    CHECK(store.error().contains("both 'password' and 'secretRef'"));
}

TEST_CASE("ProfileStore — password round-trips through Save", "[ProfileStore]")
{
    ScopedTempYaml const yaml("");
    Lightweight::Config::ProfileStore store;
    store.Upsert(Lightweight::Config::Profile {
        .name = "p",
        .pluginsDir = {},
        .schema = {},
        .dsn = {},
        .connectionString = "Driver=x",
        .uid = {},
        .secretRef = {},
        .password = "enc:dev:AAAA",
    });
    REQUIRE(store.Save(yaml.Path()).has_value());
    auto const loaded = Lightweight::Config::ProfileStore::LoadOrDefault(yaml.Path());
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->Find("p") != nullptr);
    CHECK(loaded->Find("p")->password == "enc:dev:AAAA");
}

TEST_CASE("ProfileStore — a blank password or secretRef means none, not the text 'null'", "[ProfileStore]")
{
    ScopedTempYaml const yaml(R"(profiles:
  blank:
    connectionString: "Driver=x"
    password:
  tilde:
    connectionString: "Driver=x"
    password: ~
    secretRef: env:X
  ref:
    connectionString: "Driver=x"
    secretRef:
    password: real
)");
    auto const store = Lightweight::Config::ProfileStore::LoadOrDefault(yaml.Path());
    REQUIRE(store.has_value());
    CHECK(store->Find("blank")->password.empty());
    CHECK(store->Find("tilde")->password.empty());
    CHECK(store->Find("tilde")->secretRef == "env:X");
    CHECK(store->Find("ref")->secretRef.empty());
    CHECK(store->Find("ref")->password == "real");
}

TEST_CASE("ProfileStore — a password field next to an inline PWD is ambiguous", "[ProfileStore]")
{
    ScopedTempYaml const yaml(R"(profiles:
  both:
    connectionString: "Driver=x;PWD=inline"
    password: field
)");
    auto const store = Lightweight::Config::ProfileStore::LoadOrDefault(yaml.Path());
    REQUIRE_FALSE(store.has_value());
    CHECK(store.error().contains("PWD"));
}
