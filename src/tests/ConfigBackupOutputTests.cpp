// SPDX-License-Identifier: Apache-2.0
//
// Unit tests for where `dbtool backup` writes its archive when dbtool.yml names a backup folder.

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <regex>
#include <string>
#include <string_view>

#include <Config/BackupOutput.hpp>

namespace fs = std::filesystem;
namespace Cfg = Lightweight::Config;

namespace
{

Cfg::BackupOutputRequest Request(std::string_view requested, std::string_view folder, std::string_view profile = "prod")
{
    return Cfg::BackupOutputRequest { .requested = fs::path { requested },
                                      .folder = fs::path { folder },
                                      .profileName = profile,
                                      .now = std::chrono::system_clock::now() };
}

} // namespace

TEST_CASE("ResolveBackupOutput puts a bare file name into the configured folder", "[backup-dir]")
{
    auto const output = Cfg::ResolveBackupOutput(Request("nightly.zip", "/srv/backups"));

    if (!output)
    {
        FAIL("no backup output");
        return;
    }
    CHECK(output->path == fs::path { "/srv/backups" } / "nightly.zip");
    CHECK(output->inConfiguredFolder);
}

TEST_CASE("ResolveBackupOutput keeps a path that has a directory part", "[backup-dir]")
{
    for (auto const* given:
         { "sub/nightly.zip", "./nightly.zip", "../nightly.zip", "/abs/nightly.zip", "D:/abs/nightly.zip" })
    {
        INFO(given);
        auto const output = Cfg::ResolveBackupOutput(Request(given, "/srv/backups"));

        if (!output)
        {
            FAIL_CHECK("no backup output");
            continue;
        }
        CHECK(output->path == fs::path { given });
        CHECK_FALSE(output->inConfiguredFolder);
    }
}

TEST_CASE("ResolveBackupOutput generates a name when --output is omitted", "[backup-dir]")
{
    auto const output = Cfg::ResolveBackupOutput(Request("", "/srv/backups", "prod"));

    if (!output)
    {
        FAIL("no backup output");
        return;
    }
    CHECK(output->inConfiguredFolder);
    CHECK(output->path.parent_path() == fs::path { "/srv/backups" });
    auto const name = output->path.filename().string();
    CHECK(std::regex_match(name, std::regex { R"(prod-\d{8}-\d{6}\.zip)" }));
}

TEST_CASE("ResolveBackupOutput keeps a non-ASCII profile name in a generated name", "[backup-dir]")
{
    auto const output = Cfg::ResolveBackupOutput(Request("", "/srv/backups", "M\xC3\xBCller"));

    if (!output)
    {
        FAIL("no backup output");
        return;
    }
    // Compared as UTF-8, independent of the platform's narrow code page.
    auto const name = output->path.filename().u8string();
    CHECK(name.starts_with(u8"M\u00fcller-"));
    CHECK(output->path.parent_path() == fs::path { "/srv/backups" });
}

TEST_CASE("ResolveBackupOutput without a configured folder changes nothing", "[backup-dir]")
{
    auto const named = Cfg::ResolveBackupOutput(Request("nightly.zip", ""));
    if (!named)
    {
        FAIL("no backup output");
        return;
    }
    CHECK(named->path == fs::path { "nightly.zip" });
    CHECK_FALSE(named->inConfiguredFolder);

    // Nothing requested and nowhere configured: there is no file to write.
    CHECK_FALSE(Cfg::ResolveBackupOutput(Request("", "")).has_value());
}

TEST_CASE("GeneratedBackupFileName is safe in a file name", "[backup-dir]")
{
    auto const now = std::chrono::system_clock::now();

    auto const unsafe = Cfg::GeneratedBackupFileName("acme/prod: eu*", now);
    CHECK(std::regex_match(unsafe, std::regex { R"(acme_prod__eu_-\d{8}-\d{6}\.zip)" }));

    auto const anonymous = Cfg::GeneratedBackupFileName("", now);
    CHECK(std::regex_match(anonymous, std::regex { R"(backup-\d{8}-\d{6}\.zip)" }));

    // Non-ASCII letters are kept; they are fine in a file name.
    CHECK(Cfg::GeneratedBackupFileName("M\xC3\xBCller", now).starts_with("M\xC3\xBCller-"));
}
