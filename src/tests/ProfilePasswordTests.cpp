// SPDX-License-Identifier: Apache-2.0
//
// Unit tests for resolving a profile's password (encrypted / plaintext /
// secretRef) and for encrypting a plaintext password in place after it has
// been proven to work.

#include <Lightweight/SqlConnectInfo.hpp>
#include <Lightweight/SqlConnection.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <string>

#include <Config/ProfileFileEditor.hpp>
#include <Config/ProfilePassword.hpp>
#include <Config/ProfileStore.hpp>
#include <Secrets/ProfileCipher.hpp>
#include <Secrets/SecretResolver.hpp>

namespace fs = std::filesystem;
namespace Cfg = Lightweight::Config;
namespace Secrets = Lightweight::Secrets;

namespace
{

/// Dev-key cipher used by every test so results do not depend on the build's key ring.
Secrets::ProfileCipher const& DevCipher()
{
    static auto const cipher = Secrets::ProfileCipher { { Secrets::DevKey() }, Secrets::DevKeyPolicy::Allow };
    return cipher;
}

/// Profile with only a name, connection string and the given auth fields.
Cfg::Profile MakeProfile(std::string password, std::string secretRef = {})
{
    return Cfg::Profile {
        .name = "p",
        .pluginsDir = {},
        .schema = {},
        .dsn = {},
        .connectionString = "Driver=x",
        .uid = {},
        .secretRef = std::move(secretRef),
        .password = std::move(password),
    };
}

void SetEnv(char const* name, char const* value)
{
#ifdef _WIN32
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

/// A temp directory holding a dbtool.yml; removed on scope exit.
class TempConfig
{
  public:
    explicit TempConfig(std::string_view contents)
    {
        static int counter = 0;
        _dir = fs::temp_directory_path() / ("lightweight-password-" + std::to_string(++counter));
        fs::remove_all(_dir);
        fs::create_directories(_dir);
        _path = _dir / "dbtool.yml";
        std::ofstream(_path, std::ios::binary) << contents;
    }
    ~TempConfig()
    {
        std::error_code ec;
        fs::remove_all(_dir, ec);
    }
    TempConfig(TempConfig const&) = delete;
    TempConfig(TempConfig&&) = delete;
    TempConfig& operator=(TempConfig const&) = delete;
    TempConfig& operator=(TempConfig&&) = delete;

    [[nodiscard]] fs::path const& Path() const noexcept
    {
        return _path;
    }
    [[nodiscard]] fs::path const& Dir() const noexcept
    {
        return _dir;
    }
    [[nodiscard]] std::string Read() const
    {
        std::ifstream in(_path, std::ios::binary);
        return { std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };
    }
    [[nodiscard]] Cfg::Profile Load(std::string_view name) const
    {
        auto const store = Cfg::ProfileStore::LoadOrDefault(_path);
        REQUIRE(store.has_value());
        REQUIRE(store->Find(name) != nullptr);
        return *store->Find(name);
    }

  private:
    fs::path _dir;
    fs::path _path;
};

} // namespace

TEST_CASE("ProfilePassword — an encrypted password is decrypted", "[ProfilePassword]")
{
    auto const encrypted = DevCipher().Encrypt("s3cr3t");
    REQUIRE(encrypted.has_value());
    auto const resolver = Secrets::MakeDefaultResolver();
    auto const result = Cfg::ResolveProfilePassword(MakeProfile(*encrypted), DevCipher(), resolver);
    REQUIRE(result.has_value());
    CHECK(result->value == "s3cr3t");
    CHECK(result->origin == Cfg::PasswordOrigin::Encrypted);
}

TEST_CASE("ProfilePassword — a plaintext password is used as is", "[ProfilePassword]")
{
    auto const resolver = Secrets::MakeDefaultResolver();
    auto const result = Cfg::ResolveProfilePassword(MakeProfile("hunter2"), DevCipher(), resolver);
    REQUIRE(result.has_value());
    CHECK(result->value == "hunter2");
    CHECK(result->origin == Cfg::PasswordOrigin::Plaintext);
}

TEST_CASE("ProfilePassword — secretRef is resolved when there is no password", "[ProfilePassword]")
{
    SetEnv("LW_PROFILE_PASSWORD_TEST", "from-env");
    auto const resolver = Secrets::MakeDefaultResolver();
    auto const result =
        Cfg::ResolveProfilePassword(MakeProfile({}, "env:LW_PROFILE_PASSWORD_TEST"), DevCipher(), resolver);
    REQUIRE(result.has_value());
    CHECK(result->value == "from-env");
    CHECK(result->origin == Cfg::PasswordOrigin::SecretRef);
}

TEST_CASE("ProfilePassword — no password and no secretRef means none", "[ProfilePassword]")
{
    auto const resolver = Secrets::MakeDefaultResolver();
    auto const result = Cfg::ResolveProfilePassword(MakeProfile({}), DevCipher(), resolver);
    REQUIRE(result.has_value());
    CHECK(result->value.empty());
    CHECK(result->origin == Cfg::PasswordOrigin::None);
}

TEST_CASE("ProfilePassword — a corrupt encrypted password is an error naming the profile", "[ProfilePassword]")
{
    auto const resolver = Secrets::MakeDefaultResolver();
    auto const result = Cfg::ResolveProfilePassword(MakeProfile("enc:dev:AAAA"), DevCipher(), resolver);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().contains("'p'"));
}

TEST_CASE("ProfilePassword — upgrading encrypts the plaintext in place and keeps comments", "[ProfilePassword]")
{
    TempConfig const config("# keep me\nprofiles:\n  p:\n    connectionString: \"Driver=x\"\n    password: hunter2 # old\n");
    auto const result = Cfg::UpgradePlaintextPassword(config.Path(), config.Load("p"), DevCipher());
    REQUIRE(result.has_value());

    auto const text = config.Read();
    CHECK(text.starts_with("# keep me\n"));
    CHECK(text.contains("# old"));
    CHECK_FALSE(text.contains("hunter2"));
    auto const stored = config.Load("p").password;
    REQUIRE(Secrets::ProfileCipher::IsEncrypted(stored));
    CHECK(DevCipher().Decrypt(stored) == "hunter2");
}

TEST_CASE("ProfilePassword — upgrade reports whether the file is inside a git work tree", "[ProfilePassword]")
{
    TempConfig const config("profiles:\n  p:\n    password: hunter2\n");
    // A stray .git above the temp directory would make the "outside" expectation meaningless.
    if (Cfg::IsInsideGitWorkTree(config.Dir()))
        SKIP("the temp directory is inside a git work tree");

    auto const outside = Cfg::UpgradePlaintextPassword(config.Path(), config.Load("p"), DevCipher());
    REQUIRE(outside.has_value());
    CHECK_FALSE(outside->insideGitWorkTree);

    fs::create_directories(config.Dir() / ".git");
    std::ofstream(config.Path(), std::ios::binary) << "profiles:\n  p:\n    password: hunter2\n";
    auto const inside = Cfg::UpgradePlaintextPassword(config.Path(), config.Load("p"), DevCipher());
    REQUIRE(inside.has_value());
    CHECK(inside->insideGitWorkTree);
}

TEST_CASE("ProfilePassword — upgrading an already encrypted or empty password is refused", "[ProfilePassword]")
{
    TempConfig const config("profiles:\n  p:\n    password: \"enc:dev:AAAA\"\n");
    CHECK_FALSE(Cfg::UpgradePlaintextPassword(config.Path(), config.Load("p"), DevCipher()).has_value());
    CHECK_FALSE(Cfg::UpgradePlaintextPassword(config.Path(), MakeProfile({}), DevCipher()).has_value());
}

TEST_CASE("ProfilePassword — a profile that connects with its plaintext password is upgraded",
          "[ProfilePassword][db]")
{
    // Build a profile around the test environment's own connection string, with
    // any inline password moved into the profile's plaintext `password` field.
    auto attributes = Lightweight::ParseConnectionString(Lightweight::SqlConnection::DefaultConnectionString());
    auto password = std::string {};
    for (auto const* key: { "PWD", "PASSWORD" })
        if (auto const it = attributes.find(key); it != attributes.end())
        {
            password = it->second;
            attributes.erase(it);
        }
    if (password.empty())
        password = "unused-by-this-driver";
    auto const connectionString = Lightweight::BuildConnectionString(attributes).value;

    TempConfig const config(std::format("profiles:\n  db:\n    connectionString: {}\n    password: {}\n",
                                        Lightweight::Config::QuoteYamlScalar(connectionString),
                                        Lightweight::Config::QuoteYamlScalar(password)));
    auto const profile = config.Load("db");
    auto const resolver = Secrets::MakeDefaultResolver();
    auto const resolved = Cfg::ResolveProfilePassword(profile, DevCipher(), resolver);
    REQUIRE(resolved.has_value());
    REQUIRE(resolved->origin == Cfg::PasswordOrigin::Plaintext);

    auto connection = Lightweight::SqlConnection { std::nullopt };
    REQUIRE(connection.Connect(
        Lightweight::SqlConnectionString { std::format("{}", profile.ToConnectInfo(resolved->value)) }));

    REQUIRE(Cfg::UpgradePlaintextPassword(config.Path(), profile, DevCipher()).has_value());
    auto const upgraded = config.Load("db");
    REQUIRE(Secrets::ProfileCipher::IsEncrypted(upgraded.password));
    CHECK(DevCipher().Decrypt(upgraded.password) == password);
    CHECK(upgraded.connectionString == connectionString);
}
