// SPDX-License-Identifier: Apache-2.0
//
// Unit tests for `Lightweight::Secrets::SecretResolver` and its file / env
// backends. `StdinBackend` is intentionally not exercised automatically
// (prompting from inside a test run would hang CI) — we only verify it is
// registered by `MakeDefaultResolver`.

#include <Lightweight/Secrets/SecretResolver.hpp>
#include <Lightweight/Secrets/backends/EnvBackend.hpp>
#include <Lightweight/Secrets/backends/FileBackend.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{

/// Cross-platform setenv helper. Windows `_putenv_s` is the portable alternative.
void PortableSetEnv(char const* name, char const* value) noexcept
{
#ifdef _WIN32
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

void PortableUnsetEnv(char const* name) noexcept
{
#ifdef _WIN32
    _putenv_s(name, "");
#else
    unsetenv(name);
#endif
}

class ScopedCredentialsFile
{
  public:
    explicit ScopedCredentialsFile(std::string_view body)
    {
        static int counter = 0;
        _path = std::filesystem::temp_directory_path() / ("lightweight-secrets-test-" + std::to_string(++counter) + ".pwd");
        {
            std::ofstream out(_path, std::ios::binary | std::ios::trunc);
            out << body;
        }
#ifndef _WIN32
        // FileBackend enforces 0600 on POSIX (pgpass-style); the default umask
        // on most distros (0022) yields 0644, which the backend rightly rejects.
        std::error_code ec;
        std::filesystem::permissions(_path,
                                     std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                                     std::filesystem::perm_options::replace,
                                     ec);
#endif
    }
    ~ScopedCredentialsFile()
    {
        std::error_code ec;
        std::filesystem::remove(_path, ec);
    }
    ScopedCredentialsFile(ScopedCredentialsFile const&) = delete;
    ScopedCredentialsFile(ScopedCredentialsFile&&) = delete;
    ScopedCredentialsFile& operator=(ScopedCredentialsFile const&) = delete;
    ScopedCredentialsFile& operator=(ScopedCredentialsFile&&) = delete;

    [[nodiscard]] std::filesystem::path const& Path() const noexcept
    {
        return _path;
    }

  private:
    std::filesystem::path _path;
};

} // namespace

using namespace Lightweight::Secrets;

TEST_CASE("SecretResolver — unknown prefix yields a descriptive error", "[SecretResolver]")
{
    SecretResolver resolver;
    resolver.RegisterBackend(std::make_shared<EnvBackend>());

    auto const result = resolver.Resolve("no-such-backend:foo", "prof");
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message.contains("no-such-backend"));
    CHECK(result.error().message.contains("available"));
}

TEST_CASE("SecretResolver — env: prefix resolves to process environment", "[SecretResolver]")
{
    PortableSetEnv("LIGHTWEIGHT_TEST_SECRET", "sEcReT!value");

    SecretResolver resolver;
    resolver.RegisterBackend(std::make_shared<EnvBackend>());

    auto const result = resolver.Resolve("env:LIGHTWEIGHT_TEST_SECRET", "prof");
    REQUIRE(result.has_value());
    CHECK(*result == "sEcReT!value");

    PortableUnsetEnv("LIGHTWEIGHT_TEST_SECRET");
}

TEST_CASE("SecretResolver — bare ref walks the chain and lands on env when present", "[SecretResolver]")
{
    // The EnvBackend's bare-ref convention is LIGHTWEIGHT_<PROFILE>_PWD.
    PortableSetEnv("LIGHTWEIGHT_ACME_PWD", "chain-win");

    SecretResolver resolver;
    resolver.RegisterBackend(std::make_shared<EnvBackend>());

    auto const result = resolver.Resolve("lightweight/acme", "acme");
    REQUIRE(result.has_value());
    CHECK(*result == "chain-win");

    PortableUnsetEnv("LIGHTWEIGHT_ACME_PWD");
}

TEST_CASE("SecretResolver — file: prefix reads from a .pgpass-style file", "[SecretResolver]")
{
    ScopedCredentialsFile const creds("# comment line\nacme:deploy:topsecret\nother:root:p4ss\n");

    SecretResolver resolver;
    resolver.RegisterBackend(std::make_shared<FileBackend>(creds.Path()));

    auto const result = resolver.Resolve("file:acme", "acme");
    REQUIRE(result.has_value());
    CHECK(*result == "topsecret");

    // Missing key is reported as an error, not a silent empty.
    auto const missing = resolver.Resolve("file:nosuch", "nosuch");
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().message.contains("nosuch"));
}

TEST_CASE("FileBackend — Write / Erase round-trip", "[SecretResolver][FileBackend]")
{
    auto const path = std::filesystem::temp_directory_path() / "lightweight-secrets-roundtrip.pwd";
    std::filesystem::remove(path);

    FileBackend backend(path);
    backend.Write("foo", "bar");
    REQUIRE(backend.Read("foo") == std::optional<std::string> { "bar" });
    backend.Write("foo", "baz"); // overwrite
    REQUIRE(backend.Read("foo") == std::optional<std::string> { "baz" });
    backend.Erase("foo");
    REQUIRE_FALSE(backend.Read("foo").has_value());

    std::filesystem::remove(path);
}

TEST_CASE("EnvBackend::EnvVarForProfile — normalises separators & uppercases", "[SecretResolver]")
{
    CHECK(EnvBackend::EnvVarForProfile("acme-prod") == "LIGHTWEIGHT_ACME_PROD_PWD");
    CHECK(EnvBackend::EnvVarForProfile("foo.bar/baz") == "LIGHTWEIGHT_FOO_BAR_BAZ_PWD");
    CHECK(EnvBackend::EnvVarForProfile("") == "LIGHTWEIGHT__PWD");
}

TEST_CASE("MakeDefaultResolver — registers env, file, stdin in that order", "[SecretResolver]")
{
    auto const resolver = MakeDefaultResolver();
    auto const names = resolver.RegisteredBackendNames();
    REQUIRE(names.size() == 3);
    CHECK(names[0] == "env");
    CHECK(names[1] == "file");
    CHECK(names[2] == "stdin");
}
