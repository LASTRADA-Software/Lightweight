// SPDX-License-Identifier: Apache-2.0
//
// Unit tests for the text-level dbtool.yml editor. Every edit must change only
// the bytes it targets: comments, ordering, quoting style and line endings of
// everything else are the user's and must survive untouched.

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>

#include <Config/ProfileFileEditor.hpp>
#include <Config/ProfileStore.hpp>

namespace fs = std::filesystem;
namespace Cfg = Lightweight::Config;
using namespace std::string_view_literals;

namespace
{

constexpr std::string_view Sample = R"(# Team config — keep this comment
defaultProfile: prod
profiles:
  # production database
  prod:
    connectionString: "Driver=x;Server=prod"
    password: hunter2  # rotate quarterly
  other:
    password: hunter2
)";

/// Writes `contents` to a fresh temp file and returns its path; removed on scope exit.
class TempFile
{
  public:
    explicit TempFile(std::string_view contents, bool create = true)
    {
        static int counter = 0;
        _dir = fs::temp_directory_path() / ("lightweight-editor-" + std::to_string(++counter));
        fs::remove_all(_dir);
        fs::create_directories(_dir);
        _path = _dir / "dbtool.yml";
        if (create)
            std::ofstream(_path, std::ios::binary) << contents;
    }
    ~TempFile()
    {
        std::error_code ec;
        fs::permissions(_path, fs::perms::owner_write, fs::perm_options::add, ec);
        fs::remove_all(_dir, ec);
    }
    TempFile(TempFile const&) = delete;
    TempFile(TempFile&&) = delete;
    TempFile& operator=(TempFile const&) = delete;
    TempFile& operator=(TempFile&&) = delete;

    [[nodiscard]] fs::path const& Path() const noexcept
    {
        return _path;
    }

    [[nodiscard]] std::string Read() const
    {
        std::ifstream in(_path, std::ios::binary);
        return { std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };
    }

  private:
    fs::path _dir;
    fs::path _path;
};

/// The edited text, or a readable marker for an error. Comparing `std::expected` directly
/// inside `CHECK` trips a recursive-constraint error in trunk libc++.
std::string Edited(std::expected<std::string, std::string> const& result)
{
    return result ? *result : "<error: " + result.error() + ">";
}

/// Replaces the first occurrence of `from` with `to` (test helper for expected texts).
std::string ReplaceOnce(std::string text, std::string_view from, std::string_view to)
{
    text.replace(text.find(from), from.size(), to);
    return text;
}

} // namespace

TEST_CASE("ProfileFileEditor — replaces a plain password and keeps its trailing comment", "[ProfileFileEditor]")
{
    auto const result = Cfg::SetProfilePasswordText(Sample, { .profileName = "prod", .newValue = "enc:dev:AAA" });
    REQUIRE(result.has_value());
    CHECK(*result
          == ReplaceOnce(std::string { Sample },
                         "password: hunter2  # rotate quarterly",
                         "password: \"enc:dev:AAA\"  # rotate quarterly"));
}

TEST_CASE("ProfileFileEditor — only the named profile's password changes", "[ProfileFileEditor]")
{
    auto const result = Cfg::SetProfilePasswordText(Sample, { .profileName = "other", .newValue = "enc:dev:BBB" });
    REQUIRE(result.has_value());
    CHECK(result->contains("password: hunter2  # rotate quarterly"));
    CHECK(result->ends_with("  other:\n    password: \"enc:dev:BBB\"\n"));
}

TEST_CASE("ProfileFileEditor — replaces quoted passwords whole, including '#' inside quotes", "[ProfileFileEditor]")
{
    auto const doubleQuoted = "profiles:\n  p:\n    password: \"pa#ss \\\" x\" # note\n"sv;
    CHECK(Edited(Cfg::SetProfilePasswordText(doubleQuoted, { .profileName = "p", .newValue = "enc:dev:A" }))
          == "profiles:\n  p:\n    password: \"enc:dev:A\" # note\n");

    auto const singleQuoted = "profiles:\n  p:\n    password: 'it''s #1'\n"sv;
    CHECK(Edited(Cfg::SetProfilePasswordText(singleQuoted, { .profileName = "p", .newValue = "enc:dev:A" }))
          == "profiles:\n  p:\n    password: \"enc:dev:A\"\n");
}

TEST_CASE("ProfileFileEditor — replaces a password inside a flow mapping", "[ProfileFileEditor]")
{
    auto const flow = "profiles:\n  p: {password: secret, uid: sa}\n"sv;
    CHECK(Edited(Cfg::SetProfilePasswordText(flow, { .profileName = "p", .newValue = "enc:dev:A" }))
          == "profiles:\n  p: {password: \"enc:dev:A\", uid: sa}\n");
}

TEST_CASE("ProfileFileEditor — does not touch a lookalike value elsewhere", "[ProfileFileEditor]")
{
    auto const text = "defaultProfile: password\nprofiles:\n  password:\n    uid: password\n    password: x\n"sv;
    CHECK(Edited(Cfg::SetProfilePasswordText(text, { .profileName = "password", .newValue = "enc:dev:A" }))
          == "defaultProfile: password\nprofiles:\n  password:\n    uid: password\n    password: \"enc:dev:A\"\n");
}

TEST_CASE("ProfileFileEditor — preserves CRLF line endings", "[ProfileFileEditor]")
{
    auto const text = "profiles:\r\n  p:\r\n    password: x\r\n    uid: sa\r\n"sv;
    CHECK(Edited(Cfg::SetProfilePasswordText(text, { .profileName = "p", .newValue = "enc:dev:A" }))
          == "profiles:\r\n  p:\r\n    password: \"enc:dev:A\"\r\n    uid: sa\r\n");
}

TEST_CASE("ProfileFileEditor — rewrites the legacy top-level Password", "[ProfileFileEditor]")
{
    auto const text = "ConnectionString: \"Driver=x\"\nPassword: x\n"sv;
    CHECK(Edited(Cfg::SetProfilePasswordText(text, { .profileName = "default", .newValue = "enc:dev:A" }))
          == "ConnectionString: \"Driver=x\"\nPassword: \"enc:dev:A\"\n");
}

TEST_CASE("ProfileFileEditor — password edits fail cleanly when they cannot be done exactly", "[ProfileFileEditor]")
{
    auto const block = Cfg::SetProfilePasswordText("profiles:\n  p:\n    password: |\n      x\n",
                                                   { .profileName = "p", .newValue = "enc:dev:A" });
    REQUIRE_FALSE(block.has_value());
    CHECK(block.error().contains("block"));

    auto const unknown = Cfg::SetProfilePasswordText(Sample, { .profileName = "nope", .newValue = "enc:dev:A" });
    REQUIRE_FALSE(unknown.has_value());
    CHECK(unknown.error().contains("'nope'"));

    auto const noPassword =
        Cfg::SetProfilePasswordText("profiles:\n  p:\n    uid: sa\n", { .profileName = "p", .newValue = "enc:dev:A" });
    REQUIRE_FALSE(noPassword.has_value());
    CHECK(noPassword.error().contains("no password"));
}

TEST_CASE("ProfileFileEditor — QuoteYamlScalar escapes what YAML needs", "[ProfileFileEditor]")
{
    CHECK(Cfg::QuoteYamlScalar("plain") == "\"plain\"");
    CHECK(Cfg::QuoteYamlScalar("a\"b\\c") == "\"a\\\"b\\\\c\"");
    CHECK(Cfg::QuoteYamlScalar("tab\there\nnl") == "\"tab\\there\\nnl\"");
}

TEST_CASE("ProfileFileEditor — adding a profile to an empty file creates the profiles map", "[ProfileFileEditor]")
{
    auto const profile = Cfg::NewProfile { .name = "new",
                                           .connectionString = "Driver=SQLite3;Database=x.db",
                                           .dsn = {},
                                           .uid = {},
                                           .schema = "main",
                                           .pluginsDir = {},
                                           .password = "enc:dev:A" };
    CHECK(Edited(Cfg::AddProfileText("", profile, Cfg::ReplaceExisting::No))
          == "profiles:\n  \"new\":\n    connectionString: \"Driver=SQLite3;Database=x.db\"\n    schema: \"main\"\n"
             "    password: \"enc:dev:A\"\n");
}

TEST_CASE("ProfileFileEditor — an added profile round-trips through ProfileStore", "[ProfileFileEditor]")
{
    auto const profile = Cfg::NewProfile { .name = "odd name: x",
                                           .connectionString = {},
                                           .dsn = "ACME",
                                           .uid = "sa \"admin\"",
                                           .schema = {},
                                           .pluginsDir = "C:\\plugins",
                                           .password = "enc:dev:A" };
    auto const text = Cfg::AddProfileText(Sample, profile, Cfg::ReplaceExisting::No);
    REQUIRE(text.has_value());
    TempFile const file(*text);
    auto const store = Cfg::ProfileStore::LoadOrDefault(file.Path());
    REQUIRE(store.has_value());
    auto const* loaded = store->Find("odd name: x");
    REQUIRE(loaded != nullptr);
    CHECK(loaded->dsn == "ACME");
    CHECK(loaded->uid == "sa \"admin\"");
    CHECK(loaded->pluginsDir == fs::path { "C:\\plugins" });
    CHECK(store->Find("prod") != nullptr);
    CHECK(store->Find("other") != nullptr);
}

TEST_CASE("ProfileFileEditor — adding keeps comments and uses the file's indentation", "[ProfileFileEditor]")
{
    auto const text = "# top\nprofiles:\n    a:\n        uid: x # keep\n"sv;
    auto const profile = Cfg::NewProfile {
        .name = "b", .connectionString = "Driver=x", .dsn = {}, .uid = {}, .schema = {}, .pluginsDir = {}, .password = {}
    };
    CHECK(Edited(Cfg::AddProfileText(text, profile, Cfg::ReplaceExisting::No))
          == "# top\nprofiles:\n    \"b\":\n        connectionString: \"Driver=x\"\n    a:\n        uid: x # keep\n");
}

TEST_CASE("ProfileFileEditor — adding to an empty flow map or a null profiles key", "[ProfileFileEditor]")
{
    auto const profile = Cfg::NewProfile {
        .name = "b", .connectionString = "Driver=x", .dsn = {}, .uid = {}, .schema = {}, .pluginsDir = {}, .password = {}
    };
    CHECK(Edited(Cfg::AddProfileText("profiles: {}\n", profile, Cfg::ReplaceExisting::No))
          == "profiles:\n  \"b\":\n    connectionString: \"Driver=x\"\n");
    CHECK(Edited(Cfg::AddProfileText("profiles:\ndefaultPluginsDir: x\n", profile, Cfg::ReplaceExisting::No))
          == "profiles:\n  \"b\":\n    connectionString: \"Driver=x\"\ndefaultPluginsDir: x\n");
    CHECK(Edited(Cfg::AddProfileText("defaultPluginsDir: x", profile, Cfg::ReplaceExisting::No))
          == "defaultPluginsDir: x\nprofiles:\n  \"b\":\n    connectionString: \"Driver=x\"\n");
}

TEST_CASE("ProfileFileEditor — duplicate names are refused unless replacing", "[ProfileFileEditor]")
{
    auto const profile = Cfg::NewProfile { .name = "prod",
                                           .connectionString = "Driver=new",
                                           .dsn = {},
                                           .uid = {},
                                           .schema = {},
                                           .pluginsDir = {},
                                           .password = {} };
    auto const refused = Cfg::AddProfileText(Sample, profile, Cfg::ReplaceExisting::No);
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().contains("already exists"));

    auto const replaced = Cfg::AddProfileText(Sample, profile, Cfg::ReplaceExisting::Yes);
    REQUIRE(replaced.has_value());
    CHECK(*replaced
          == "# Team config — keep this comment\ndefaultProfile: prod\nprofiles:\n  \"prod\":\n"
             "    connectionString: \"Driver=new\"\n  # production database\n  other:\n    password: hunter2\n");
}

TEST_CASE("ProfileFileEditor — legacy and flow-style files are not restructured", "[ProfileFileEditor]")
{
    auto const profile = Cfg::NewProfile {
        .name = "b", .connectionString = "Driver=x", .dsn = {}, .uid = {}, .schema = {}, .pluginsDir = {}, .password = {}
    };
    auto const legacy = Cfg::AddProfileText("ConnectionString: x\n", profile, Cfg::ReplaceExisting::No);
    REQUIRE_FALSE(legacy.has_value());
    CHECK(legacy.error().contains("legacy"));

    auto const flow = Cfg::AddProfileText("profiles: {a: {uid: x}}\n", profile, Cfg::ReplaceExisting::No);
    REQUIRE_FALSE(flow.has_value());
    CHECK(flow.error().contains("flow"));
}

TEST_CASE("ProfileFileEditor — sets or inserts defaultProfile", "[ProfileFileEditor]")
{
    CHECK(Edited(Cfg::SetDefaultProfileText(Sample, "other"))
          == ReplaceOnce(std::string { Sample }, "defaultProfile: prod", "defaultProfile: \"other\""));
    CHECK(Edited(Cfg::SetDefaultProfileText("profiles:\n  a:\n    uid: x\n", "a"))
          == "defaultProfile: \"a\"\nprofiles:\n  a:\n    uid: x\n");
}

TEST_CASE("ProfileFileEditor — EditConfigFile creates missing files and directories", "[ProfileFileEditor]")
{
    TempFile const file("", /*create=*/false);
    auto const nested = file.Path().parent_path() / "sub" / "dbtool.yml";
    auto const result = Cfg::EditConfigFile(nested, [](std::string_view text) -> std::expected<std::string, std::string> {
        return std::string { text } + "profiles: {}\n";
    });
    REQUIRE(result.has_value());
    std::ifstream in(nested, std::ios::binary);
    CHECK(std::string { std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() } == "profiles: {}\n");
}

TEST_CASE("ProfileFileEditor — EditConfigFile leaves the file untouched on failure", "[ProfileFileEditor]")
{
    TempFile const file(Sample);
    auto const transformError = Cfg::EditConfigFile(
        file.Path(), [](std::string_view) -> std::expected<std::string, std::string> { return std::unexpected("boom"); });
    REQUIRE_FALSE(transformError.has_value());
    CHECK(transformError.error() == "boom");
    CHECK(file.Read() == Sample);

    fs::permissions(
        file.Path(), fs::perms::owner_write | fs::perms::group_write | fs::perms::others_write, fs::perm_options::remove);
    auto const readOnly = Cfg::EditConfigFile(
        file.Path(), [](std::string_view) -> std::expected<std::string, std::string> { return std::string { "x" }; });
    REQUIRE_FALSE(readOnly.has_value());
    CHECK(readOnly.error().contains("read-only"));
    CHECK(file.Read() == Sample);
}

#ifdef _WIN32
TEST_CASE("ProfileFileEditor — EditConfigFile rides out a transient lock on the target", "[ProfileFileEditor]")
{
    // Virus scanners and the search indexer briefly open freshly written files
    // without FILE_SHARE_DELETE, which makes the replacing rename fail with
    // "Access is denied". Simulate that with a reader that lets go after a moment.
    TempFile const file(Sample);
    auto holder = std::make_unique<std::ifstream>(file.Path(), std::ios::binary);
    REQUIRE(holder->is_open());
    auto release = std::jthread { [&holder] {
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        holder.reset();
    } };

    auto const result = Cfg::EditConfigFile(
        file.Path(), [](std::string_view) -> std::expected<std::string, std::string> { return std::string { "new\n" }; });
    release.join();
    REQUIRE(result.has_value());
    CHECK(file.Read() == "new\n");
}
#endif

TEST_CASE("ProfileFileEditor — files with a UTF-8 BOM are edited at the right offsets", "[ProfileFileEditor]")
{
    auto const bom = std::string { "\xEF\xBB\xBF" };
    CHECK(Edited(Cfg::SetProfilePasswordText(bom + "profiles:\n  p:\n    password: secret\n",
                                             { .profileName = "p", .newValue = "enc:dev:A" }))
          == bom + "profiles:\n  p:\n    password: \"enc:dev:A\"\n");

    auto const profile = Cfg::NewProfile {
        .name = "b", .connectionString = "Driver=x", .dsn = {}, .uid = {}, .schema = {}, .pluginsDir = {}, .password = {}
    };
    CHECK(Edited(Cfg::AddProfileText(bom + "profiles:\n  a:\n    uid: x\n", profile, Cfg::ReplaceExisting::No))
          == bom + "profiles:\n  \"b\":\n    connectionString: \"Driver=x\"\n  a:\n    uid: x\n");

    CHECK(Edited(Cfg::SetDefaultProfileText(bom + "profiles:\n  a:\n    uid: x\n", "a"))
          == bom + "defaultProfile: \"a\"\nprofiles:\n  a:\n    uid: x\n");
}

TEST_CASE("ProfileFileEditor — anchored, aliased and tagged values are refused", "[ProfileFileEditor]")
{
    auto const text = "profiles:\n  p:\n    password: &pw secret\n  q:\n    password: *pw\n  r:\n    password: !!str x\n"sv;
    for (auto const name: { "p"sv, "q"sv, "r"sv })
    {
        auto const result = Cfg::SetProfilePasswordText(text, { .profileName = name, .newValue = "enc:dev:A" });
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().contains("anchor"));
    }
}

TEST_CASE("ProfileFileEditor — edits that would not round-trip exactly are refused", "[ProfileFileEditor]")
{
    // A multi-line plain scalar: replacing the first line alone would leave the rest behind.
    auto const multiLine = Cfg::SetProfilePasswordText("profiles:\n  p:\n    password: first\n      second\n",
                                                       { .profileName = "p", .newValue = "enc:dev:A" });
    REQUIRE_FALSE(multiLine.has_value());
    CHECK(multiLine.error().contains("left unchanged"));

    // A document marker: a line inserted above it would start a separate document.
    auto const documentMarker = Cfg::SetDefaultProfileText("---\nprofiles:\n  a:\n    uid: x\n", "a");
    REQUIRE_FALSE(documentMarker.has_value());
    CHECK(documentMarker.error().contains("left unchanged"));

    // --force across a comment at column 0: the old profile's tail must not leak into the new one.
    auto const profile = Cfg::NewProfile { .name = "prod",
                                           .connectionString = "Driver=new",
                                           .dsn = {},
                                           .uid = {},
                                           .schema = {},
                                           .pluginsDir = {},
                                           .password = {} };
    auto const forced =
        Cfg::AddProfileText("profiles:\n  prod:\n    uid: a\n# note\n    password: old\n  other:\n    uid: b\n",
                            profile,
                            Cfg::ReplaceExisting::Yes);
    REQUIRE_FALSE(forced.has_value());
    INFO(forced.error());
    CHECK(forced.error().contains("left unchanged"));
}
