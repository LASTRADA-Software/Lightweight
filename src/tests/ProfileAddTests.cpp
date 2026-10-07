// SPDX-License-Identifier: Apache-2.0
//
// Unit tests for adding a profile to dbtool.yml with its password encrypted —
// the code behind `dbtool add-profile` and dbtool-gui's "Save as profile…".

#include <Lightweight/SqlConnectInfo.hpp>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include <Config/ProfileAdd.hpp>
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

/// The error of a failed result, or a marker when it succeeded. Comparing `std::expected` directly
/// inside `CHECK` trips a recursive-constraint error in trunk libc++.
std::string ErrorOf(std::expected<void, std::string> const& result)
{
    return result ? "<no error>" : result.error();
}

/// A temp directory with a dbtool.yml path in it (the file need not exist); removed on scope exit.
class TempDir
{
  public:
    TempDir()
    {
        static int counter = 0;
        _dir = fs::temp_directory_path() / ("lightweight-profile-add-" + std::to_string(++counter));
        fs::remove_all(_dir);
        fs::create_directories(_dir);
    }
    ~TempDir()
    {
        std::error_code ec;
        fs::remove_all(_dir, ec);
    }
    TempDir(TempDir const&) = delete;
    TempDir(TempDir&&) = delete;
    TempDir& operator=(TempDir const&) = delete;
    TempDir& operator=(TempDir&&) = delete;

    [[nodiscard]] fs::path File() const
    {
        return _dir / "dbtool.yml";
    }
    void Write(std::string_view contents) const
    {
        std::ofstream(File(), std::ios::binary) << contents;
    }
    [[nodiscard]] std::string Read() const
    {
        std::ifstream in(File(), std::ios::binary);
        return { std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };
    }
    [[nodiscard]] Cfg::ProfileStore Load() const
    {
        auto store = Cfg::ProfileStore::LoadOrDefault(File());
        REQUIRE(store.has_value());
        return std::move(*store);
    }

  private:
    fs::path _dir;
};

Cfg::ProfileRequest DsnRequest(std::string name = "warehouse")
{
    return Cfg::ProfileRequest { .name = std::move(name),
                                 .connectionString = {},
                                 .dsn = "WAREHOUSE",
                                 .uid = "deploy",
                                 .schema = "dbo",
                                 .pluginsDir = {},
                                 .password = "s3cret!",
                                 .backupDir = {} };
}

} // namespace

TEST_CASE("AddProfileToFile creates the file and stores a DSN profile with an encrypted password", "[profile-add]")
{
    TempDir dir;

    auto const added =
        Cfg::AddProfileToFile(dir.File(), DsnRequest(), Cfg::ReplaceExisting::No, Cfg::MakeDefault::No, DevCipher());

    INFO(ErrorOf(added));
    REQUIRE(added.has_value());
    auto const text = dir.Read();
    CHECK_FALSE(text.contains("s3cret!")); // never in the clear
    CHECK(text.contains("enc:"));

    auto const store = dir.Load();
    auto const* profile = store.Find("warehouse");
    REQUIRE(profile != nullptr);
    CHECK(profile->dsn == "WAREHOUSE");
    CHECK(profile->uid == "deploy");
    CHECK(profile->schema == "dbo");
    CHECK(profile->connectionString.empty());
    CHECK(Secrets::ProfileCipher::IsEncrypted(profile->password));

    auto const resolved = Cfg::ResolveProfilePassword(*profile, DevCipher(), Secrets::SecretResolver {});
    REQUIRE(resolved.has_value());
    CHECK(resolved->value == "s3cret!");
}

TEST_CASE("AddProfileToFile moves a password out of the connection string and stores it encrypted", "[profile-add]")
{
    TempDir dir;
    auto request = Cfg::ProfileRequest { .name = "dev",
                                         .connectionString = "DRIVER=SQL Server;SERVER=db1;DATABASE=dev;UID=sa;PWD=hunter2",
                                         .dsn = {},
                                         .uid = {},
                                         .schema = {},
                                         .pluginsDir = {},
                                         .password = {},
                                         .backupDir = {} };

    REQUIRE(
        Cfg::AddProfileToFile(dir.File(), request, Cfg::ReplaceExisting::No, Cfg::MakeDefault::No, DevCipher()).has_value());

    CHECK_FALSE(dir.Read().contains("hunter2"));
    auto const store = dir.Load();
    auto const* profile = store.Find("dev");
    REQUIRE(profile != nullptr);
    CHECK_FALSE(profile->connectionString.contains("PWD"));
    CHECK(profile->connectionString.contains("SERVER=db1"));
    auto const resolved = Cfg::ResolveProfilePassword(*profile, DevCipher(), Secrets::SecretResolver {});
    REQUIRE(resolved.has_value());
    CHECK(resolved->value == "hunter2");
}

TEST_CASE("AddProfileToFile prefers an explicit password over one inside the connection string", "[profile-add]")
{
    TempDir dir;
    auto request = Cfg::ProfileRequest { .name = "dev",
                                         .connectionString = "DRIVER=x;PWD=old",
                                         .dsn = {},
                                         .uid = {},
                                         .schema = {},
                                         .pluginsDir = {},
                                         .password = "new",
                                         .backupDir = {} };

    REQUIRE(
        Cfg::AddProfileToFile(dir.File(), request, Cfg::ReplaceExisting::No, Cfg::MakeDefault::No, DevCipher()).has_value());

    auto const store = dir.Load();
    auto const resolved = Cfg::ResolveProfilePassword(*store.Find("dev"), DevCipher(), Secrets::SecretResolver {});
    REQUIRE(resolved.has_value());
    CHECK(resolved->value == "new");
}

TEST_CASE("AddProfileToFile writes no password for a profile without one", "[profile-add]")
{
    TempDir dir;
    auto request = DsnRequest();
    request.password.clear();

    REQUIRE(
        Cfg::AddProfileToFile(dir.File(), request, Cfg::ReplaceExisting::No, Cfg::MakeDefault::No, DevCipher()).has_value());

    CHECK_FALSE(dir.Read().contains("enc:"));
    CHECK(dir.Load().Find("warehouse")->password.empty());
}

TEST_CASE("AddProfileToFile keeps the rest of an existing file, comments included", "[profile-add]")
{
    TempDir dir;
    dir.Write("# team profiles\nprofiles:\n  # the main one\n  prod:\n    dsn: \"PROD\"\n");

    REQUIRE(Cfg::AddProfileToFile(dir.File(), DsnRequest(), Cfg::ReplaceExisting::No, Cfg::MakeDefault::No, DevCipher())
                .has_value());

    auto const text = dir.Read();
    CHECK(text.contains("# team profiles"));
    CHECK(text.contains("# the main one"));
    auto const store = dir.Load();
    CHECK(store.Find("prod") != nullptr);
    CHECK(store.Find("warehouse") != nullptr);
    CHECK(store.Size() == 2);
}

TEST_CASE("AddProfileToFile refuses a duplicate name unless asked to replace it", "[profile-add]")
{
    TempDir dir;
    REQUIRE(Cfg::AddProfileToFile(dir.File(), DsnRequest(), Cfg::ReplaceExisting::No, Cfg::MakeDefault::No, DevCipher())
                .has_value());
    auto const before = dir.Read();

    auto const duplicate =
        Cfg::AddProfileToFile(dir.File(), DsnRequest(), Cfg::ReplaceExisting::No, Cfg::MakeDefault::No, DevCipher());
    CHECK_FALSE(duplicate.has_value());
    CHECK(dir.Read() == before); // untouched on failure

    auto changed = DsnRequest();
    changed.dsn = "OTHER";
    REQUIRE(Cfg::AddProfileToFile(dir.File(), changed, Cfg::ReplaceExisting::Yes, Cfg::MakeDefault::No, DevCipher())
                .has_value());
    auto const store = dir.Load();
    CHECK(store.Size() == 1);
    CHECK(store.Find("warehouse")->dsn == "OTHER");
}

TEST_CASE("AddProfileToFile can make the new profile the default", "[profile-add]")
{
    TempDir dir;

    REQUIRE(Cfg::AddProfileToFile(dir.File(), DsnRequest(), Cfg::ReplaceExisting::No, Cfg::MakeDefault::Yes, DevCipher())
                .has_value());

    CHECK(dir.Load().DefaultProfileName() == "warehouse");
}

TEST_CASE("AddProfileToFile rejects requests that cannot describe a connection", "[profile-add]")
{
    TempDir dir;

    auto unnamed = DsnRequest("");
    CHECK_FALSE(
        Cfg::AddProfileToFile(dir.File(), unnamed, Cfg::ReplaceExisting::No, Cfg::MakeDefault::No, DevCipher()).has_value());

    auto both = DsnRequest();
    both.connectionString = "DRIVER=x";
    CHECK_FALSE(
        Cfg::AddProfileToFile(dir.File(), both, Cfg::ReplaceExisting::No, Cfg::MakeDefault::No, DevCipher()).has_value());

    auto neither = DsnRequest();
    neither.dsn.clear();
    CHECK_FALSE(
        Cfg::AddProfileToFile(dir.File(), neither, Cfg::ReplaceExisting::No, Cfg::MakeDefault::No, DevCipher()).has_value());

    CHECK_FALSE(fs::exists(dir.File())); // nothing was written
}

TEST_CASE("SplitInlinePassword leaves a connection string without a password alone", "[profile-add]")
{
    auto const plain = Cfg::SplitInlinePassword("DRIVER=x;SERVER=y");
    CHECK(plain.connectionString == "DRIVER=x;SERVER=y");
    CHECK(plain.password.empty());

    auto const braced = Cfg::SplitInlinePassword("DRIVER=x;PWD={p;w};UID=u");
    CHECK(braced.password == "p;w");
    CHECK_FALSE(braced.connectionString.contains("PWD"));
    CHECK(braced.connectionString.contains("UID=u"));
}

TEST_CASE("AddProfileToFile stores awkward but plausible profile names faithfully", "[profile-add]")
{
    // The GUI lets people type a name; whatever they type must come back from the file unchanged.
    for (auto const* name: { "prod db", "acme: prod", "dev #2", "it's", "say \"hi\"", "Müller-DB", "a-b_c.d", "  padded  " })
    {
        INFO(name);
        TempDir dir;
        auto request = DsnRequest(name);

        auto const added =
            Cfg::AddProfileToFile(dir.File(), request, Cfg::ReplaceExisting::No, Cfg::MakeDefault::No, DevCipher());

        INFO(ErrorOf(added));
        REQUIRE(added.has_value());
        auto const store = dir.Load();
        REQUIRE(store.Size() == 1);
        CHECK(store.Profiles().front().name == name);
    }
}
