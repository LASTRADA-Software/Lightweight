// SPDX-License-Identifier: Apache-2.0
//
// Tests for the Lightweight::Profiles shared library.

#include "ScopedEnvironmentVariable.hpp"
#include "UniqueTempPath.hpp"

#include <Lightweight/Profiles/ProfileConnection.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <expected>
#include <filesystem>
#include <fstream>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <Config/ConfigDiscovery.hpp>
#include <Config/ProfileStore.hpp>
#include <Secrets/ProfileCipher.hpp>
#include <profiles/Guarded.hpp>

namespace Profiles = Lightweight::Profiles;
namespace Secrets = Lightweight::Secrets;
namespace Config = Lightweight::Config;
namespace fs = std::filesystem;

using namespace std::string_view_literals;

namespace
{

/// Writes @p text to @p path, creating missing parent directories.
void WriteFile(fs::path const& path, std::string_view text)
{
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}

/// A temporary directory tree `root/app/sub` plus a per-user config directory `root/user`, with the
/// per-user lookup redirected into it for the lifetime of the fixture.
class ProfilesFixture
{
  public:
    ProfilesFixture():
        _root { CreateRoot() },
        _userRedirect { Config::ProfileStore::UserConfigDirectoryVariable, (_root / "user").string() }
    {
        fs::create_directories(_root / "app" / "sub");
        fs::create_directories(_root / "user");
    }

    ProfilesFixture(ProfilesFixture const&) = delete;
    ProfilesFixture(ProfilesFixture&&) = delete;
    ProfilesFixture& operator=(ProfilesFixture const&) = delete;
    ProfilesFixture& operator=(ProfilesFixture&&) = delete;

    ~ProfilesFixture()
    {
        auto errorCode = std::error_code {};
        fs::remove_all(_root, errorCode);
    }

    /// The temporary root directory.
    [[nodiscard]] fs::path const& Root() const noexcept
    {
        return _root;
    }

    /// The application directory `root/app`.
    [[nodiscard]] fs::path App() const
    {
        return _root / "app";
    }

    /// The directory `root/app/sub`, where lookups usually start.
    [[nodiscard]] fs::path Sub() const
    {
        return _root / "app" / "sub";
    }

    /// The per-user config file `root/user/dbtool.yml`.
    [[nodiscard]] fs::path UserFile() const
    {
        return _root / "user" / "dbtool.yml";
    }

    /// The `dbtool.yml` inside @p directory.
    [[nodiscard]] static fs::path ConfigIn(fs::path const& directory)
    {
        return directory / "dbtool.yml";
    }

  private:
    /// Creates the unique root directory and returns its canonical form, so paths reported by the
    /// library (which are canonical) compare equal even when the temp directory is a short 8.3 alias.
    static fs::path CreateRoot()
    {
        auto const root = UniqueTempPath("profiles-lib");
        fs::create_directories(root);
        return fs::canonical(root);
    }

    fs::path _root;
    ScopedEnvironmentVariable _userRedirect;
};

} // namespace

TEST_CASE("EncryptPassword and DecryptPassword round-trip", "[profiles-lib]")
{
    auto const encrypted = Profiles::EncryptPassword("s3cr3t-\xC3\x84");
    REQUIRE(encrypted.has_value());
    REQUIRE(Profiles::IsEncryptedPassword(*encrypted));
    CHECK(encrypted->find("s3cr3t") == std::string::npos);

    auto const decrypted = Profiles::DecryptPassword(*encrypted);
    REQUIRE(decrypted.has_value());
    CHECK(*decrypted == "s3cr3t-\xC3\x84");
}

TEST_CASE("DecryptPassword of a plaintext or garbled value is an error without the input", "[profiles-lib]")
{
    for (auto const input: { "plain"sv, "enc:dev:@@@"sv })
    {
        auto const result = Profiles::DecryptPassword(input);
        REQUIRE(!result.has_value());
        CHECK(result.error().find("@@@") == std::string::npos);
        CHECK(result.error().find("plain") == std::string::npos);
    }
}

TEST_CASE("IsEncryptedPassword is false for values without the enc: prefix", "[profiles-lib]")
{
    CHECK_FALSE(Profiles::IsEncryptedPassword(""));
    CHECK_FALSE(Profiles::IsEncryptedPassword("enc"));
    CHECK_FALSE(Profiles::IsEncryptedPassword("hunter2"));
}

TEST_CASE("Library output matches ProfileCipher::Builtin()", "[profiles-lib]")
{
    auto const encrypted = Profiles::EncryptPassword("x");
    REQUIRE(encrypted.has_value());

    auto const decrypted = Secrets::ProfileCipher::Builtin().Decrypt(*encrypted);
    REQUIRE(decrypted.has_value());
    CHECK(*decrypted == "x");
}

namespace
{
struct NotAStdException
{
};
} // namespace

TEST_CASE("Guarded converts exceptions into prefixed errors", "[profiles-lib]")
{
    using Result = std::expected<int, std::string>;

    SECTION("success passes through")
    {
        auto const result = Profiles::detail::Guarded("ctx", [] { return Result(42); });
        REQUIRE(result.has_value());
        CHECK(*result == 42);
    }

    SECTION("std::exception keeps its message")
    {
        auto const result = Profiles::detail::Guarded("ctx", []() -> Result { throw std::runtime_error("boom"); });
        REQUIRE(!result.has_value());
        CHECK(result.error() == "ctx: boom");
    }

    SECTION("other exceptions become a generic message")
    {
        auto const result = Profiles::detail::Guarded("ctx", []() -> Result { throw NotAStdException {}; });
        REQUIRE(!result.has_value());
        CHECK(result.error() == "ctx: unexpected error");
    }
}

TEST_CASE("ListProfiles returns an empty list when no file exists", "[profiles-lib]")
{
    auto const fixture = ProfilesFixture {};

    auto const list = Profiles::ListProfiles(fixture.Sub());

    REQUIRE(list.has_value());
    CHECK(list->profiles.empty());
    CHECK(list->configFiles.empty());
    CHECK(list->newProfileTarget == fixture.UserFile());
}

TEST_CASE("ListProfiles merges layers nearest first and appends the per-user file", "[profiles-lib]")
{
    auto const fixture = ProfilesFixture {};
    WriteFile(fixture.ConfigIn(fixture.App()), "profiles:\n  p1:\n    dsn: D1\n");
    WriteFile(fixture.UserFile(), "profiles:\n  p1:\n    dsn: U1\n  p2:\n    dsn: U2\n");

    auto const list = Profiles::ListProfiles(fixture.Sub());

    REQUIRE(list.has_value());
    CHECK(list->configFiles == std::vector<fs::path> { fixture.ConfigIn(fixture.App()), fixture.UserFile() });
    REQUIRE(list->profiles.size() == 2);
    auto const byName = [&](std::string_view name) -> Profiles::ProfileSummary const& {
        return *std::ranges::find(list->profiles, name, &Profiles::ProfileSummary::name);
    };
    CHECK(byName("p1").dsn == "D1");
    CHECK(byName("p1").configFile == fixture.ConfigIn(fixture.App()));
    CHECK(byName("p2").configFile == fixture.UserFile());
}

TEST_CASE("ListProfiles never reads past a root layer", "[profiles-lib]")
{
    auto const fixture = ProfilesFixture {};
    WriteFile(fixture.ConfigIn(fixture.App()), "root: true\nprofiles:\n  p1:\n    dsn: D1\n");
    WriteFile(fixture.UserFile(), "{{{ not yaml");

    auto const list = Profiles::ListProfiles(fixture.Sub());

    REQUIRE(list.has_value());
    CHECK(list->configFiles == std::vector<fs::path> { fixture.ConfigIn(fixture.App()) });
    CHECK(list->newProfileTarget == fixture.ConfigIn(fixture.App()));
}

TEST_CASE("ListProfiles with no start directory reads only the per-user file", "[profiles-lib]")
{
    auto const fixture = ProfilesFixture {};
    WriteFile(fixture.ConfigIn(fixture.App()), "profiles:\n  appOnly:\n    dsn: D1\n");
    WriteFile(fixture.UserFile(), "profiles:\n  userOnly:\n    dsn: U1\n");

    auto const list = Profiles::ListProfiles(std::nullopt);

    REQUIRE(list.has_value());
    REQUIRE(list->profiles.size() == 1);
    CHECK(list->profiles.front().name == "userOnly");
}

TEST_CASE("ListProfiles ignores LIGHTWEIGHT_DBTOOL_CONFIG_SEARCH_DIR", "[profiles-lib]")
{
    auto const fixture = ProfilesFixture {};
    WriteFile(fixture.ConfigIn(fixture.App()), "profiles:\n  appOnly:\n    dsn: D1\n");
    WriteFile(fixture.UserFile(), "profiles:\n  userOnly:\n    dsn: U1\n");
    auto const searchDirectory =
        ScopedEnvironmentVariable { "LIGHTWEIGHT_DBTOOL_CONFIG_SEARCH_DIR", fixture.App().string() };

    auto const list = Profiles::ListProfiles(std::nullopt);

    REQUIRE(list.has_value());
    REQUIRE(list->profiles.size() == 1);
    CHECK(list->profiles.front().name == "userOnly");
}

TEST_CASE("ListProfiles never returns a password", "[profiles-lib]")
{
    auto const fixture = ProfilesFixture {};
    WriteFile(fixture.UserFile(),
              "profiles:\n"
              "  plain:\n    dsn: D\n    password: \"plain-secret\"\n"
              "  inlined:\n    connectionString: \"DRIVER=x;UID=u;PWD=cs-secret\"\n"
              "  referenced:\n    dsn: D\n    secretRef: \"env:X\"\n"
              "  none:\n    dsn: D\n");

    auto const list = Profiles::ListProfiles(std::nullopt);

    REQUIRE(list.has_value());
    REQUIRE(list->profiles.size() == 4);
    auto const hasPassword =
        list->profiles | std::views::transform(&Profiles::ProfileSummary::hasPassword) | std::ranges::to<std::vector>();
    CHECK(hasPassword == std::vector<bool> { true, true, true, false });
    for (auto const& summary: list->profiles)
    {
        for (auto const& field: { summary.name, summary.dsn, summary.connectionString, summary.uid })
        {
            CHECK(field.find("plain-secret") == std::string::npos);
            CHECK(field.find("cs-secret") == std::string::npos);
        }
    }
    auto const inlined = std::ranges::find(list->profiles, "inlined"sv, &Profiles::ProfileSummary::name);
    REQUIRE(inlined != list->profiles.end());
    CHECK(inlined->connectionString.find("DRIVER=x") != std::string::npos);
}

TEST_CASE("ListProfiles reports defaultProfile", "[profiles-lib]")
{
    auto const fixture = ProfilesFixture {};
    WriteFile(fixture.ConfigIn(fixture.App()), "defaultProfile: p2\nprofiles:\n  p1:\n    dsn: D1\n");
    WriteFile(fixture.UserFile(), "defaultProfile: p1\nprofiles:\n  p2:\n    dsn: U2\n");

    auto const list = Profiles::ListProfiles(fixture.Sub());

    REQUIRE(list.has_value());
    CHECK(list->defaultProfile == "p2");
}

TEST_CASE("ListProfiles reports an unparsable file by name", "[profiles-lib]")
{
    auto const fixture = ProfilesFixture {};
    WriteFile(fixture.ConfigIn(fixture.App()), "{{{");

    auto const list = Profiles::ListProfiles(fixture.Sub());

    REQUIRE(!list.has_value());
    CHECK(list.error().find("dbtool.yml") != std::string::npos);
}

TEST_CASE("ListProfiles handles a non-ASCII start directory", "[profiles-lib]")
{
    auto const fixture = ProfilesFixture {};
    auto const startDirectory = fixture.Root() / fs::path(u8"Ämter-数据");
    WriteFile(fixture.ConfigIn(startDirectory), "profiles:\n  p1:\n    dsn: D1\n");

    auto const list = Profiles::ListProfiles(startDirectory);

    REQUIRE(list.has_value());
    REQUIRE(list->profiles.size() == 1);
    CHECK(list->profiles.front().name == "p1");
    CHECK(list->profiles.front().configFile == fixture.ConfigIn(startDirectory));
}

TEST_CASE("ListProfiles accepts a relative or missing start directory", "[profiles-lib]")
{
    auto const fixture = ProfilesFixture {};
    WriteFile(fixture.UserFile(), "profiles:\n  userOnly:\n    dsn: U1\n");

    SECTION("missing")
    {
        auto const list = Profiles::ListProfiles(fixture.Root() / "nope");
        REQUIRE(list.has_value());
        REQUIRE(list->profiles.size() == 1);
        CHECK(list->profiles.front().name == "userOnly");
    }

    SECTION("relative")
    {
        auto const list = Profiles::ListProfiles(fs::path("no-such-relative-directory"));
        REQUIRE(list.has_value());
        CHECK(!list->profiles.empty());
    }
}
