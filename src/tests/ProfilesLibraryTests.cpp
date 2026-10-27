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
#include <format>
#include <fstream>
#include <iterator>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Config/ConfigDiscovery.hpp>
#include <Config/ProfileAdd.hpp>
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

/// A secret that cannot occur by chance in base64 ciphertext or in a path: long and containing
/// characters outside the base64 alphabet. Use it wherever a test asserts a secret is absent.
constexpr auto DistinctSecret = "pw-NOT-IN-FILE_7#"sv;
/// A second distinct secret, for tests needing two different ones.
constexpr auto OtherDistinctSecret = "cs-NOT-IN-FILE_8#"sv;

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
    auto const encrypted = Profiles::EncryptPassword("pw-NOT-IN-FILE_7#-\xC3\x84");
    REQUIRE(encrypted.has_value());
    REQUIRE(Profiles::IsEncryptedPassword(*encrypted));
    CHECK(encrypted->find("pw-NOT-IN-FILE_7#") == std::string::npos);

    auto const decrypted = Profiles::DecryptPassword(*encrypted);
    REQUIRE(decrypted.has_value());
    CHECK(*decrypted == "pw-NOT-IN-FILE_7#-\xC3\x84");
}

TEST_CASE("DecryptPassword of a plaintext or garbled value is an error without the input", "[profiles-lib]")
{
    for (auto const input: { DistinctSecret, "enc:dev:@@@"sv })
    {
        auto const result = Profiles::DecryptPassword(input);
        REQUIRE(!result.has_value());
        CHECK(result.error().find("@@@") == std::string::npos);
        CHECK(result.error().find(DistinctSecret) == std::string::npos);
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
              "  plain:\n    dsn: D\n    password: \"pw-NOT-IN-FILE_7#\"\n"
              "  inlined:\n    connectionString: \"DRIVER=x;UID=u;PWD=cs-NOT-IN-FILE_8#\"\n"
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
            CHECK(field.find(DistinctSecret) == std::string::npos);
            CHECK(field.find(OtherDistinctSecret) == std::string::npos);
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

namespace
{

/// Adds a DSN profile with a plaintext @p password (encrypted on write) to @p file.
void AddDsnProfile(fs::path const& file,
                   std::string name,
                   std::string password = {},
                   Config::MakeDefault makeDefault = Config::MakeDefault::No)
{
    auto request = Config::ProfileRequest {};
    request.name = std::move(name);
    request.dsn = "DSN1";
    request.uid = "u";
    request.password = std::move(password);
    auto const added =
        Config::AddProfileToFile(file, request, Config::ReplaceExisting::No, makeDefault, Secrets::ProfileCipher::Builtin());
    REQUIRE(added.has_value());
}

/// The raw bytes of @p path.
std::string ReadFileBytes(fs::path const& path)
{
    auto input = std::ifstream(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

} // namespace

TEST_CASE("ResolveProfileConnection decrypts an encrypted password", "[profiles-lib]")
{
    auto const fixture = ProfilesFixture {};
    AddDsnProfile(fixture.UserFile(), "p1", "pw-\xC3\x84");

    auto const result = Profiles::ResolveProfileConnection(fixture.Sub(), "p1");

    REQUIRE(result.has_value());
    CHECK(result->name == "p1");
    CHECK(result->dsn == "DSN1");
    CHECK(result->uid == "u");
    CHECK(result->password == "pw-\xC3\x84");
    CHECK(result->configFile == fixture.UserFile());
}

TEST_CASE("ResolveProfileConnection returns a plaintext password unchanged and leaves the file untouched", "[profiles-lib]")
{
    auto const fixture = ProfilesFixture {};
    WriteFile(fixture.UserFile(), "profiles:\n  p1:\n    dsn: D\n    password: \"plain-pw\"\n");
    auto const before = ReadFileBytes(fixture.UserFile());

    auto const result = Profiles::ResolveProfileConnection(fixture.Sub(), "p1");

    REQUIRE(result.has_value());
    CHECK(result->password == "plain-pw");
    CHECK(ReadFileBytes(fixture.UserFile()) == before);
}

TEST_CASE("ResolveProfileConnection resolves secretRef from the environment", "[profiles-lib]")
{
    auto const fixture = ProfilesFixture {};
    WriteFile(fixture.UserFile(), "profiles:\n  p1:\n    dsn: D\n    secretRef: \"env:LW_PROFILES_TEST_PWD\"\n");
    auto const password = ScopedEnvironmentVariable { "LW_PROFILES_TEST_PWD", "from-env" };

    auto const result = Profiles::ResolveProfileConnection(fixture.Sub(), "p1");

    REQUIRE(result.has_value());
    CHECK(result->password == "from-env");
}

TEST_CASE("ResolveProfileConnection uses defaultProfile for an empty name", "[profiles-lib]")
{
    auto const fixture = ProfilesFixture {};
    AddDsnProfile(fixture.UserFile(), "p1");
    AddDsnProfile(fixture.UserFile(), "p2", {}, Config::MakeDefault::Yes);

    auto const result = Profiles::ResolveProfileConnection(fixture.Sub(), "");

    REQUIRE(result.has_value());
    CHECK(result->name == "p2");
}

TEST_CASE("ResolveProfileConnection errors when no default is set", "[profiles-lib]")
{
    auto const fixture = ProfilesFixture {};
    AddDsnProfile(fixture.UserFile(), "p1");
    AddDsnProfile(fixture.UserFile(), "p2");

    auto const result = Profiles::ResolveProfileConnection(fixture.Sub(), "");

    REQUIRE(!result.has_value());
    CHECK(result.error().find("defaultProfile") != std::string::npos);
    CHECK(result.error().find("dbtool.yml") != std::string::npos);
}

TEST_CASE("ResolveProfileConnection errors when defaultProfile names a missing profile", "[profiles-lib]")
{
    auto const fixture = ProfilesFixture {};
    WriteFile(fixture.UserFile(), "defaultProfile: gone\nprofiles:\n  p1:\n    dsn: D\n  p2:\n    dsn: D\n");

    auto const result = Profiles::ResolveProfileConnection(fixture.Sub(), "");

    REQUIRE(!result.has_value());
    CHECK(result.error().find("gone") != std::string::npos);
}

TEST_CASE("ResolveProfileConnection resolves a legacy single-profile file by default", "[profiles-lib]")
{
    auto const fixture = ProfilesFixture {};
    WriteFile(fixture.UserFile(), "ConnectionString: \"DSN=Legacy\"\nPassword: \"legacy-pw\"\n");

    auto const result = Profiles::ResolveProfileConnection(fixture.Sub(), "");

    REQUIRE(result.has_value());
    CHECK(result->name == "default");
    CHECK(result->connectionString == "DSN=Legacy");
    CHECK(result->password == "legacy-pw");
}

TEST_CASE("ResolveProfileConnection errors for an unknown profile", "[profiles-lib]")
{
    auto const fixture = ProfilesFixture {};

    SECTION("with a file")
    {
        AddDsnProfile(fixture.UserFile(), "p1");
        auto const result = Profiles::ResolveProfileConnection(fixture.Sub(), "nope");
        REQUIRE(!result.has_value());
        CHECK(result.error().find("nope") != std::string::npos);
        CHECK(result.error().find("dbtool.yml") != std::string::npos);
    }

    SECTION("without any file the per-user path is named")
    {
        auto const result = Profiles::ResolveProfileConnection(fixture.Sub(), "nope");
        REQUIRE(!result.has_value());
        CHECK(result.error().find("nope") != std::string::npos);
        CHECK(result.error().find("(not found)") != std::string::npos);
        CHECK(result.error().find("dbtool.yml") != std::string::npos);
    }
}

TEST_CASE("ResolveProfileConnection errors for a profile without dsn or connection string", "[profiles-lib]")
{
    auto const fixture = ProfilesFixture {};
    WriteFile(fixture.UserFile(), "profiles:\n  p1:\n    uid: u\n");

    auto const result = Profiles::ResolveProfileConnection(fixture.Sub(), "p1");

    REQUIRE(!result.has_value());
    CHECK(result.error().find("p1") != std::string::npos);
}

TEST_CASE("ResolveProfileConnection reports an unknown key id without the secret", "[profiles-lib]")
{
    auto const fixture = ProfilesFixture {};
    constexpr auto Payload = "U0VDUkVUUEFZTE9BREJZVEVTMDEyMzQ1Njc4OWFiY2RlZg"sv;
    WriteFile(fixture.UserFile(), std::format("profiles:\n  p1:\n    dsn: D\n    password: \"enc:zz:{}\"\n", Payload));

    auto const result = Profiles::ResolveProfileConnection(fixture.Sub(), "p1");

    REQUIRE(!result.has_value());
    CHECK(result.error().find("p1") != std::string::npos);
    CHECK(result.error().find(Payload) == std::string::npos);
}

#if !defined(_WIN32)
TEST_CASE("ResolveProfileConnection returns an error instead of throwing for a world-readable secret file", "[profiles-lib]")
{
    auto const fixture = ProfilesFixture {};
    auto const credentials = fixture.Root() / "xdg" / "Lightweight" / "credentials";
    WriteFile(credentials, "p1:u:file-secret\n");
    fs::permissions(credentials,
                    fs::perms::owner_read | fs::perms::owner_write | fs::perms::group_read | fs::perms::others_read);
    auto const configHome = ScopedEnvironmentVariable { "XDG_CONFIG_HOME", (fixture.Root() / "xdg").string() };
    WriteFile(fixture.UserFile(), "profiles:\n  p1:\n    dsn: D\n    secretRef: \"file:p1\"\n");

    auto const result = Profiles::ResolveProfileConnection(fixture.Sub(), "p1");

    REQUIRE(!result.has_value());
    CHECK(result.error().find("file-secret") == std::string::npos);
}
#endif

TEST_CASE("ResolveProfileConnection returns the connection string unsanitised", "[profiles-lib]")
{
    auto const fixture = ProfilesFixture {};
    WriteFile(fixture.UserFile(), "profiles:\n  p1:\n    connectionString: \"DRIVER=x;UID=u;PWD=cs-NOT-IN-FILE_8#\"\n");

    auto const result = Profiles::ResolveProfileConnection(fixture.Sub(), "p1");

    REQUIRE(result.has_value());
    CHECK(result->connectionString == "DRIVER=x;UID=u;PWD=cs-NOT-IN-FILE_8#");
}

namespace
{

/// Reads the whole file at @p path.
std::string ReadFile(fs::path const& path)
{
    auto stream = std::ifstream(path, std::ios::binary);
    return { std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
}

/// Builds a ProfileDefinition with every field spelled out.
Profiles::ProfileDefinition Define(
    std::string name, std::string dsn, std::string connectionString = {}, std::string uid = {}, std::string password = {})
{
    return Profiles::ProfileDefinition { .name = std::move(name),
                                         .dsn = std::move(dsn),
                                         .connectionString = std::move(connectionString),
                                         .uid = std::move(uid),
                                         .password = std::move(password) };
}

/// Finds @p name in @p list.
Profiles::ProfileSummary const* FindProfile(Profiles::ProfileList const& list, std::string_view name)
{
    auto const found = std::ranges::find(list.profiles, name, &Profiles::ProfileSummary::name);
    return found != list.profiles.end() ? &*found : nullptr;
}

/// Restores write permission on a file when destroyed.
class WritePermissionRestorer
{
  public:
    explicit WritePermissionRestorer(fs::path path):
        _path { std::move(path) }
    {
    }
    WritePermissionRestorer(WritePermissionRestorer const&) = delete;
    WritePermissionRestorer(WritePermissionRestorer&&) = delete;
    WritePermissionRestorer& operator=(WritePermissionRestorer const&) = delete;
    WritePermissionRestorer& operator=(WritePermissionRestorer&&) = delete;
    ~WritePermissionRestorer()
    {
        auto errorCode = std::error_code {};
        fs::permissions(_path, fs::perms::owner_write, fs::perm_options::add, errorCode);
    }

  private:
    fs::path _path;
};

} // namespace

TEST_CASE("SaveProfile then ListProfiles and ResolveProfileConnection see the profile", "[profiles-lib]")
{
    auto const fixture = ProfilesFixture {};

    auto const saved = Profiles::SaveProfile(fixture.Sub(), Define("n", "D", "", "u", std::string(DistinctSecret)));

    REQUIRE(saved.has_value());
    CHECK(*saved == fixture.UserFile());

    auto const list = Profiles::ListProfiles(fixture.Sub());
    REQUIRE(list.has_value());
    auto const* const summary = FindProfile(*list, "n");
    REQUIRE(summary != nullptr);
    CHECK(summary->dsn == "D");
    CHECK(summary->hasPassword);

    auto const resolved = Profiles::ResolveProfileConnection(fixture.Sub(), "n");
    REQUIRE(resolved.has_value());
    CHECK(resolved->password == DistinctSecret);

    CHECK(ReadFile(fixture.UserFile()).find(DistinctSecret) == std::string::npos);
}

TEST_CASE("SaveProfile writes a new profile into the root layer", "[profiles-lib]")
{
    auto const fixture = ProfilesFixture {};
    WriteFile(fixture.ConfigIn(fixture.App()), "root: true\nprofiles:\n  other:\n    dsn: O\n");

    auto const saved = Profiles::SaveProfile(fixture.Sub(), Define("n", "D", "", "", ""));

    REQUIRE(saved.has_value());
    CHECK(*saved == fixture.ConfigIn(fixture.App()));
}

TEST_CASE("SaveProfile replaces a shadowing profile in its own layer only", "[profiles-lib]")
{
    auto const fixture = ProfilesFixture {};
    WriteFile(fixture.ConfigIn(fixture.App()), "profiles:\n  p1:\n    dsn: AppOld\n");
    WriteFile(fixture.UserFile(), "profiles:\n  p1:\n    dsn: UserOld\n");
    auto const userBefore = ReadFile(fixture.UserFile());

    auto const saved = Profiles::SaveProfile(fixture.Sub(), Define("p1", "New", "", "", ""));

    REQUIRE(saved.has_value());
    CHECK(*saved == fixture.ConfigIn(fixture.App()));
    CHECK(ReadFile(fixture.UserFile()) == userBefore);
    auto const resolved = Profiles::ResolveProfileConnection(fixture.Sub(), "p1");
    REQUIRE(resolved.has_value());
    CHECK(resolved->dsn == "New");
}

TEST_CASE("SaveProfile replaces wholesale like add-profile --force", "[profiles-lib]")
{
    auto const fixture = ProfilesFixture {};
    WriteFile(fixture.UserFile(),
              "profiles:\n  p1:\n    dsn: Old\n    schema: s\n    pluginsDir: /pl\n    backupDir: /bk\n"
              "    password: pw-NOT-IN-FILE_7#\n");

    auto const saved = Profiles::SaveProfile(fixture.Sub(), Define("p1", "New", "", "", ""));

    REQUIRE(saved.has_value());
    auto const text = ReadFile(fixture.UserFile());
    CHECK(text.find("schema") == std::string::npos);
    CHECK(text.find("pluginsDir") == std::string::npos);
    CHECK(text.find("backupDir") == std::string::npos);
    CHECK(text.find(DistinctSecret) == std::string::npos);
    auto const list = Profiles::ListProfiles(fixture.Sub());
    REQUIRE(list.has_value());
    auto const* const summary = FindProfile(*list, "p1");
    REQUIRE(summary != nullptr);
    CHECK(summary->dsn == "New");
    CHECK_FALSE(summary->hasPassword);
}

TEST_CASE("SaveProfile keeps comments and other profiles byte for byte", "[profiles-lib]")
{
    auto const fixture = ProfilesFixture {};
    constexpr auto Comment = "# keep this comment\n"sv;
    constexpr auto OtherBlock = "  p2:\n    dsn: Two   # trailing\n    uid: bob\n"sv;
    WriteFile(fixture.UserFile(), std::format("{}profiles:\n{}", Comment, OtherBlock));

    auto const saved = Profiles::SaveProfile(fixture.Sub(), Define("p1", "One", "", "", ""));

    REQUIRE(saved.has_value());
    auto const text = ReadFile(fixture.UserFile());
    CHECK(text.find(Comment) != std::string::npos);
    CHECK(text.find(OtherBlock) != std::string::npos);
}

TEST_CASE("SaveProfile moves a password out of the connection string", "[profiles-lib]")
{
    auto const fixture = ProfilesFixture {};

    SECTION("inline password")
    {
        auto const saved =
            Profiles::SaveProfile(fixture.Sub(), Define("n", "", std::format("DRIVER=x;PWD={}", OtherDistinctSecret)));
        REQUIRE(saved.has_value());

        auto const text = ReadFile(fixture.UserFile());
        CHECK(text.find(OtherDistinctSecret) == std::string::npos);
        CHECK(text.find("PWD=") == std::string::npos);
        auto const resolved = Profiles::ResolveProfileConnection(fixture.Sub(), "n");
        REQUIRE(resolved.has_value());
        CHECK(resolved->password == OtherDistinctSecret);
    }

    SECTION("explicit password wins")
    {
        auto const saved = Profiles::SaveProfile(
            fixture.Sub(),
            Define("n", "", std::format("DRIVER=x;PWD={}", OtherDistinctSecret), "", std::string(DistinctSecret)));
        REQUIRE(saved.has_value());

        auto const resolved = Profiles::ResolveProfileConnection(fixture.Sub(), "n");
        REQUIRE(resolved.has_value());
        CHECK(resolved->password == DistinctSecret);
    }
}

TEST_CASE("SaveProfile rejects both or neither of dsn and connection string, and an empty name", "[profiles-lib]")
{
    auto const fixture = ProfilesFixture {};

    for (auto const& definition: {
             Define("n", "D", "DRIVER=x", "", ""),
             Define("n", "", "", "", ""),
             Define("", "D", "", "", ""),
         })
    {
        auto const saved = Profiles::SaveProfile(fixture.Sub(), definition);
        CHECK_FALSE(saved.has_value());
        CHECK_FALSE(fs::exists(fixture.UserFile()));
    }
}

TEST_CASE("SaveProfile refuses a legacy-format target file", "[profiles-lib]")
{
    auto const fixture = ProfilesFixture {};
    constexpr auto Legacy = "ConnectionString: \"DRIVER=SQLite3;Database=legacy.db\"\n"sv;
    WriteFile(fixture.UserFile(), Legacy);

    auto const saved = Profiles::SaveProfile(fixture.Sub(), Define("n", "D", "", "", ""));

    CHECK_FALSE(saved.has_value());
    CHECK(ReadFile(fixture.UserFile()) == Legacy);
}

TEST_CASE("SaveProfile with no start directory writes the per-user file", "[profiles-lib]")
{
    auto const fixture = ProfilesFixture {};

    auto const saved = Profiles::SaveProfile(std::nullopt, Define("n", "D", "", "", ""));

    REQUIRE(saved.has_value());
    CHECK(*saved == fixture.UserFile());
    auto const list = Profiles::ListProfiles(std::nullopt);
    REQUIRE(list.has_value());
    CHECK(FindProfile(*list, "n") != nullptr);
}

TEST_CASE("SaveProfile round-trips a name with YAML-special characters or fails cleanly", "[profiles-lib]")
{
    auto const fixture = ProfilesFixture {};

    for (auto const name: { "a:b"sv, "q\"uote"sv, " lead"sv })
    {
        INFO("name: [" << name << "]");
        auto const saved = Profiles::SaveProfile(fixture.Sub(), Define(std::string(name), "D", "", "", ""));

        auto const list = Profiles::ListProfiles(fixture.Sub());
        REQUIRE(list.has_value());
        if (saved.has_value())
        {
            auto const* const summary = FindProfile(*list, name);
            REQUIRE(summary != nullptr);
            CHECK(summary->dsn == "D");
        }
        else
        {
            CHECK_FALSE(saved.error().empty());
            CHECK(FindProfile(*list, name) == nullptr);
        }
        WARN("outcome for [" << name << "]: " << (saved.has_value() ? "round-trips" : saved.error()));
    }
}

TEST_CASE("SaveProfile error does not contain the password", "[profiles-lib]")
{
    auto const fixture = ProfilesFixture {};
    WriteFile(fixture.UserFile(), "profiles:\n  p2:\n    dsn: Two\n");
    auto const restorer = WritePermissionRestorer { fixture.UserFile() };
    fs::permissions(fixture.UserFile(),
                    fs::perms::owner_write | fs::perms::group_write | fs::perms::others_write,
                    fs::perm_options::remove);

    auto const saved = Profiles::SaveProfile(fixture.Sub(), Define("n", "D", "", "", std::string(DistinctSecret)));

    if (saved.has_value())
        SKIP("the read-only target was still writable (e.g. running as root)");
    CHECK(saved.error().find(DistinctSecret) == std::string::npos);
}
