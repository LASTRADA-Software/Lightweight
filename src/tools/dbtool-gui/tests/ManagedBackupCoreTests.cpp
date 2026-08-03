// SPDX-License-Identifier: Apache-2.0
//
// Unit tests for the pure (non-Qt) decision logic behind the managed
// backup feature. No event loop, no QObject — plain data in, data out,
// which is what keeps this layer's line coverage near 100%.

#include "../ManagedBackupCore.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

#include <Config/ProfileStore.hpp>
#include <Secrets/SecretResolver.hpp>

#if defined(_WIN32)
    #include <process.h>
#else
    #include <unistd.h>
#endif

using namespace DbtoolGui::ManagedBackup;
namespace fs = std::filesystem;

namespace
{

/// Returns the current process ID, portable across POSIX (`getpid`) and
/// Windows/clang-cl, where only `_getpid()` (declared in <process.h>) is
/// available — `::getpid` does not exist there. Used solely to make the
/// per-run temp directory name unique.
int CurrentProcessId() noexcept
{
#if defined(_WIN32)
    return _getpid();
#else
    return ::getpid();
#endif
}

/// RAII temp directory unique to one test run.
struct TempDir
{
    fs::path path;
    TempDir():
        path(fs::temp_directory_path() / ("mbcore-" + std::to_string(CurrentProcessId()) + "-" + std::to_string(counter++)))
    {
        fs::create_directories(path);
    }
    ~TempDir()
    {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    static inline int counter = 0;
};

void WriteFile(fs::path const& p, std::string_view content)
{
    std::ofstream(p, std::ios::binary) << content;
}

} // namespace

TEST_CASE("SanitizeArchiveName keeps safe characters and maps the rest to underscore", "[dbtool-gui][managed-backup]")
{
    CHECK(SanitizeArchiveName("prod") == "prod");
    CHECK(SanitizeArchiveName("Prod-DB_2") == "Prod-DB_2");
    CHECK(SanitizeArchiveName("acme prod") == "acme_prod");
    CHECK(SanitizeArchiveName("a/b\\c:d") == "a_b_c_d");
    CHECK(SanitizeArchiveName("umlaut-äöü") == "umlaut-___");
    // Exercises the 3-byte (u20AC "€") and 4-byte (U1F600 "😀") UTF-8 lead-byte
    // branches of the continuation-byte counter; 2-byte leads are already
    // covered by the umlaut case above.
    CHECK(SanitizeArchiveName("price-€100") == "price-_100");
    CHECK(SanitizeArchiveName("emoji-😀-x") == "emoji-_-x");
    CHECK(SanitizeArchiveName("") == "_");
}

TEST_CASE("PlanArchiveNames appends .zip and flags sanitized collisions", "[dbtool-gui][managed-backup]")
{
    auto const plan = PlanArchiveNames({ "prod", "acme prod", "acme_prod" });
    REQUIRE(plan.size() == 3);
    CHECK(plan[0].fileName == "prod.zip");
    CHECK_FALSE(plan[0].collision);
    CHECK(plan[1].fileName == "acme_prod.zip");
    CHECK_FALSE(plan[1].collision);
    // Third profile sanitizes to the same file name as the second: the
    // later entry is the collision (config smell, never silently merged).
    CHECK(plan[2].fileName == "acme_prod.zip");
    CHECK(plan[2].collision);
}

TEST_CASE("ScanArchive reports a missing file as not existing", "[dbtool-gui][managed-backup]")
{
    TempDir const dir;
    auto const status = ScanArchive(dir.path / "nope.zip");
    CHECK_FALSE(status.exists);
    CHECK(status.sizeBytes == 0);
}

TEST_CASE("ScanArchive reports size and mtime for an existing file", "[dbtool-gui][managed-backup]")
{
    TempDir const dir;
    auto const file = dir.path / "prod.zip";
    WriteFile(file, "0123456789");
    auto const status = ScanArchive(file);
    CHECK(status.exists);
    CHECK(status.sizeBytes == 10);
    CHECK(status.mtime == fs::last_write_time(file));
}

TEST_CASE("TempArchivePath appends .tmp to the final path", "[dbtool-gui][managed-backup]")
{
    CHECK(TempArchivePath("/x/prod.zip") == fs::path("/x/prod.zip.tmp"));
}

TEST_CASE("CommitArchive atomically replaces the previous archive", "[dbtool-gui][managed-backup]")
{
    TempDir const dir;
    auto const finalPath = dir.path / "prod.zip";
    auto const tmpPath = TempArchivePath(finalPath);
    WriteFile(finalPath, "old");
    WriteFile(tmpPath, "new-content");

    auto const result = CommitArchive(tmpPath, finalPath);

    REQUIRE(result.has_value());
    CHECK_FALSE(fs::exists(tmpPath));
    CHECK(fs::file_size(finalPath) == 11);
}

TEST_CASE("CommitArchive reports an error when the tmp file is missing", "[dbtool-gui][managed-backup]")
{
    TempDir const dir;
    auto const result = CommitArchive(dir.path / "ghost.zip.tmp", dir.path / "ghost.zip");
    REQUIRE_FALSE(result.has_value());
    CHECK_FALSE(result.error().empty());
}

TEST_CASE("CheckWritableFolder accepts an existing writable directory", "[dbtool-gui][managed-backup]")
{
    TempDir const dir;
    CHECK(CheckWritableFolder(dir.path).has_value());
}

TEST_CASE("CheckWritableFolder creates a missing directory", "[dbtool-gui][managed-backup]")
{
    TempDir const dir;
    auto const target = dir.path / "sub" / "backups";
    CHECK(CheckWritableFolder(target).has_value());
    CHECK(fs::is_directory(target));
}

TEST_CASE("CheckWritableFolder rejects a path that is a file", "[dbtool-gui][managed-backup]")
{
    TempDir const dir;
    auto const file = dir.path / "not-a-dir";
    WriteFile(file, "x");
    auto const result = CheckWritableFolder(file);
    REQUIRE_FALSE(result.has_value());
    CHECK_FALSE(result.error().empty());
}

TEST_CASE("CheckWritableFolder reports an error when create_directories fails", "[dbtool-gui][managed-backup]")
{
    // The target itself doesn't exist (so the "is a file" early-out above
    // doesn't fire), but a path component above it is a plain file, so
    // fs::create_directories() can't materialize the requested directory.
    TempDir const dir;
    auto const blocker = dir.path / "blocker";
    WriteFile(blocker, "x");
    auto const target = blocker / "sub";
    auto const result = CheckWritableFolder(target);
    REQUIRE_FALSE(result.has_value());
    CHECK_FALSE(result.error().empty());
}

#if !defined(_WIN32)
TEST_CASE("CheckWritableFolder reports an error when the directory isn't writable", "[dbtool-gui][managed-backup]")
{
    // Root and CAP_DAC_OVERRIDE ignore directory permissions, so this guard
    // (mirroring SecretResolverTests.cpp's ScopedCredentialsFile) keeps the
    // case skippable instead of flaky under those environments.
    if (::geteuid() == 0)
        return;
    TempDir const dir;
    std::error_code ec;
    fs::permissions(dir.path, fs::perms::owner_read | fs::perms::owner_exec, fs::perm_options::replace, ec);
    REQUIRE_FALSE(ec);
    auto const result = CheckWritableFolder(dir.path);
    fs::permissions(dir.path, fs::perms::owner_all, fs::perm_options::replace, ec);
    REQUIRE_FALSE(result.has_value());
    CHECK_FALSE(result.error().empty());
}
#endif

TEST_CASE("FormatRunSummary covers all-ok, mixed, and all-failed", "[dbtool-gui][managed-backup]")
{
    CHECK(FormatRunSummary(3, 0) == "Backed up 3 profile(s).");
    CHECK(FormatRunSummary(2, 1) == "Backed up 2 profile(s), 1 failed.");
    CHECK(FormatRunSummary(0, 2) == "Backed up 0 profile(s), 2 failed.");
}

TEST_CASE("ResolveConnectionString uses a raw connection string as-is", "[dbtool-gui][managed-backup]")
{
    Lightweight::Config::Profile profile;
    profile.name = "dev";
    profile.connectionString = "DRIVER=SQLite3;Database=dev.db";
    auto const resolver = Lightweight::Secrets::SecretResolver {};

    auto const cs = ResolveConnectionString(profile, resolver);

    REQUIRE(cs.has_value());
    CHECK(*cs == "DRIVER=SQLite3;Database=dev.db");
}

TEST_CASE("ResolveConnectionString builds a DSN descriptor with uid", "[dbtool-gui][managed-backup]")
{
    Lightweight::Config::Profile profile;
    profile.name = "prod";
    profile.dsn = "ACME_PROD";
    profile.uid = "deploy";
    auto const resolver = Lightweight::Secrets::SecretResolver {};

    auto const cs = ResolveConnectionString(profile, resolver);

    REQUIRE(cs.has_value());
    CHECK(cs->contains("DSN=ACME_PROD"));
    CHECK(cs->contains("UID=deploy"));
}

#if !defined(_WIN32)
TEST_CASE("ResolveConnectionString resolves secretRef via the resolver chain", "[dbtool-gui][managed-backup]")
{
    Lightweight::Config::Profile profile;
    profile.name = "prod";
    profile.dsn = "ACME_PROD";
    profile.uid = "deploy";
    profile.secretRef = "env:MBCORE_TEST_PWD";
    ::setenv("MBCORE_TEST_PWD", "s3cr3t", 1);
    auto const resolver = Lightweight::Secrets::MakeDefaultResolver();

    auto const cs = ResolveConnectionString(profile, resolver);

    REQUIRE(cs.has_value());
    CHECK(cs->contains("PWD=s3cr3t"));
    ::unsetenv("MBCORE_TEST_PWD");
}
#endif

#if !defined(_WIN32)
TEST_CASE("ResolveConnectionString appends the resolved secret to a raw connection string", "[dbtool-gui][managed-backup]")
{
    Lightweight::Config::Profile profile;
    profile.name = "dev";
    profile.connectionString = "DRIVER=SQLite3;Database=x.db";
    profile.secretRef = "env:MBCORE_RAWCS_PWD";
    ::setenv("MBCORE_RAWCS_PWD", "s3cr3t", 1);
    auto const resolver = Lightweight::Secrets::MakeDefaultResolver();

    auto const cs = ResolveConnectionString(profile, resolver);

    REQUIRE(cs.has_value());
    CHECK(cs->contains("Database=x.db"));
    CHECK(cs->contains("PWD=s3cr3t"));
    ::unsetenv("MBCORE_RAWCS_PWD");
}
#endif

TEST_CASE("ResolveConnectionString fails on unresolvable secretRef", "[dbtool-gui][managed-backup]")
{
    Lightweight::Config::Profile profile;
    profile.name = "prod";
    profile.dsn = "ACME_PROD";
    profile.secretRef = "env:MBCORE_MISSING_PWD";
    auto const resolver = Lightweight::Secrets::MakeDefaultResolver();

    auto const cs = ResolveConnectionString(profile, resolver);

    REQUIRE_FALSE(cs.has_value());
    CHECK_FALSE(cs.error().empty());
}

TEST_CASE("ResolveConnectionString fails on a profile without connection info", "[dbtool-gui][managed-backup]")
{
    Lightweight::Config::Profile profile;
    profile.name = "empty";
    auto const resolver = Lightweight::Secrets::SecretResolver {};

    auto const cs = ResolveConnectionString(profile, resolver);

    REQUIRE_FALSE(cs.has_value());
}

TEST_CASE("PathToUtf8 round-trips a non-ASCII path", "[dbtool-gui][managed-backup]")
{
    // The GUI hands paths around as UTF-8 std::strings. `path::string()` would
    // re-encode into the active code page on Windows and mangle a folder such
    // as "Müller/Sicherungen"; the UTF-8 form must survive byte-exact so error
    // messages name the folder the user actually picked.
    auto const path = fs::path { std::u8string { u8"Müller" } } / std::u8string { u8"Sicherungen" };

    auto const utf8 = PathToUtf8(path);

    CHECK(utf8.find("M\xc3\xbcller") != std::string::npos);
    CHECK(utf8.find("Sicherungen") != std::string::npos);
    // And feeding it back to fs::path yields the same path again.
    CHECK(fs::path { std::u8string { utf8.begin(), utf8.end() } } == path);
}

TEST_CASE("CheckWritableFolder names a non-ASCII folder without mangling it", "[dbtool-gui][managed-backup]")
{
    TempDir const dir;
    auto const blocker = dir.path / std::u8string { u8"Müller" };
    WriteFile(blocker, "x"); // a file where a directory is expected

    auto const result = CheckWritableFolder(blocker);

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().find("M\xc3\xbcller") != std::string::npos);
}
