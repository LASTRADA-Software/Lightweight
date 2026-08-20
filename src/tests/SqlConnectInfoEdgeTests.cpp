// SPDX-License-Identifier: Apache-2.0

#include <Lightweight/SqlConnectInfo.hpp>
#include <Lightweight/SqlConnection.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <string>

using namespace Lightweight;

// ================================================================================================
// SanitizePwd edge cases
// ================================================================================================

TEST_CASE("SanitizePwd: mixed-case match", "[SqlConnectInfo]")
{
    // The regex is icase, so PWD/Pwd/pwd should all be sanitized.
    auto const cases = {
        "DSN=test;PWD=secret;",
        "DSN=test;Pwd=secret;",
        "DSN=test;pwd=secret;",
    };
    for (auto const& s: cases)
    {
        INFO(std::string { "input: " } + s);
        auto const sanitized = SqlConnectionString::SanitizePwd(s);
        CHECK_FALSE(sanitized.contains("secret"));
        CHECK(sanitized.contains("Pwd=***;"));
    }
}

TEST_CASE("SanitizePwd: leaves a missing trailing semicolon untouched", "[SqlConnectInfo]")
{
    // The regex requires a trailing ';' — no replacement when missing.
    auto const sanitized = SqlConnectionString::SanitizePwd("DSN=test;PWD=secret");
    CHECK(sanitized == "DSN=test;PWD=secret");
}

TEST_CASE("SanitizePwd: replaces every PWD= occurrence", "[SqlConnectInfo]")
{
    auto const sanitized = SqlConnectionString::SanitizePwd("PWD=a;X=1;PWD=b;");
    CHECK_FALSE(sanitized.contains("a"));
    CHECK_FALSE(sanitized.contains("b"));
    // Both occurrences should now be masked.
    CHECK(sanitized.find("Pwd=***;") != sanitized.rfind("Pwd=***;"));
}

TEST_CASE("SanitizePwd: empty password value still gets masked", "[SqlConnectInfo]")
{
    auto const sanitized = SqlConnectionString::SanitizePwd("DSN=test;PWD=;X=1;");
    CHECK(sanitized.contains("Pwd=***;"));
    CHECK_FALSE(sanitized.contains("PWD=;"));
}

// ================================================================================================
// SqlConnectionDataSource <-> SqlConnectionString round-trip
// ================================================================================================

TEST_CASE("SqlConnectionDataSource::ToConnectionString preserves all fields", "[SqlConnectInfo]")
{
    SqlConnectionDataSource const ds {
        .datasource = "MyDSN",
        .username = "alice",
        .password = "shh",
        .timeout = std::chrono::seconds { 12 },
    };

    auto const cs = ds.ToConnectionString();
    auto const map = ParseConnectionString(cs);
    REQUIRE(map.contains("DSN"));
    CHECK(map.at("DSN") == "MyDSN");
    REQUIRE(map.contains("UID"));
    CHECK(map.at("UID") == "alice");
    REQUIRE(map.contains("PWD"));
    CHECK(map.at("PWD") == "shh");
    REQUIRE(map.contains("TIMEOUT"));
    CHECK(map.at("TIMEOUT") == "12");
}

TEST_CASE("SqlConnectionDataSource: round-trip via FromConnectionString -> ToConnectionString", "[SqlConnectInfo]")
{
    SqlConnectionDataSource const original {
        .datasource = "DS",
        .username = "u",
        .password = "p",
        .timeout = std::chrono::seconds { 5 },
    };

    auto const re = SqlConnectionDataSource::FromConnectionString(original.ToConnectionString());
    CHECK(re == original);
}

TEST_CASE("SqlConnectionDataSource: equality and ordering are well-defined", "[SqlConnectInfo]")
{
    SqlConnectionDataSource const a {
        .datasource = "A", .username = "u", .password = "p", .timeout = std::chrono::seconds { 1 }
    };
    SqlConnectionDataSource const b {
        .datasource = "A", .username = "u", .password = "p", .timeout = std::chrono::seconds { 1 }
    };
    SqlConnectionDataSource const c {
        .datasource = "B", .username = "u", .password = "p", .timeout = std::chrono::seconds { 1 }
    };
    CHECK(a == b);
    CHECK(a != c);
    CHECK(a < c);
}

// ================================================================================================
// SqlConnection::SetDefaultDataSource encodes the data source as the default connection string
// ================================================================================================

TEST_CASE("SqlConnection::SetDefaultDataSource updates the default connection string", "[SqlConnectInfo]")
{
    auto const previous = SqlConnectionString { SqlConnection::DefaultConnectionString() };

    SqlConnectionDataSource const probe {
        .datasource = "ProbeDSN",
        .username = "ProbeUser",
        .password = "ProbePass",
        .timeout = std::chrono::seconds { 7 },
    };

    SqlConnection::SetDefaultDataSource(probe);
    auto const& current = SqlConnection::DefaultConnectionString();
    CHECK(current.value.contains("ProbeDSN"));
    CHECK(current.value.contains("ProbeUser"));
    CHECK(current.value.contains("TIMEOUT=7"));

    // Restore the previous default so subsequent tests still find a working DSN.
    SqlConnection::SetDefaultConnectionString(previous);
}

// ================================================================================================
// SqlConnectionString equality + ordering
// ================================================================================================

TEST_CASE("SqlConnectionString: defaulted three-way comparison", "[SqlConnectInfo]")
{
    SqlConnectionString const a { .value = "Driver=A;DB=x;" };
    SqlConnectionString const b { .value = "Driver=A;DB=x;" };
    SqlConnectionString const c { .value = "Driver=B;DB=x;" };
    CHECK(a == b);
    CHECK(a != c);
    CHECK(a < c);
}

// ================================================================================================
// SqlEncryptionMode (SQL_COPT_SS_ENCRYPT) — parsing, rendering, and round-tripping
// ================================================================================================

TEST_CASE("ParseEncryptionMode: recognizes every documented spelling, case-insensitively", "[SqlConnectInfo]")
{
    for (auto const& value: { "yes", "YES", "Yes", "true", "TRUE", "1", "mandatory", "MANDATORY" })
    {
        INFO(std::string { "input: " } + value);
        CHECK(ParseEncryptionMode(value) == SqlEncryptionMode::Enabled);
    }

    for (auto const& value: { "no", "NO", "No", "false", "FALSE", "0", "optional", "Optional" })
    {
        INFO(std::string { "input: " } + value);
        CHECK(ParseEncryptionMode(value) == SqlEncryptionMode::Disabled);
    }
}

TEST_CASE("ParseEncryptionMode: tolerates surrounding whitespace", "[SqlConnectInfo]")
{
    CHECK(ParseEncryptionMode("  yes  ") == SqlEncryptionMode::Enabled);
    CHECK(ParseEncryptionMode("\tno\t") == SqlEncryptionMode::Disabled);
}

TEST_CASE("ParseEncryptionMode: unrecognized input falls back to DriverDefault", "[SqlConnectInfo]")
{
    for (auto const& value: { "", "strict", "maybe", "2", "yes!" })
    {
        INFO(std::string { "input: " } + value);
        CHECK(ParseEncryptionMode(value) == SqlEncryptionMode::DriverDefault);
    }
}

TEST_CASE("FormatEncryptionMode: renders the canonical keyword value", "[SqlConnectInfo]")
{
    CHECK(FormatEncryptionMode(SqlEncryptionMode::Enabled) == "yes");
    CHECK(FormatEncryptionMode(SqlEncryptionMode::Disabled) == "no");
    // DriverDefault is expressed by omitting the keyword entirely.
    CHECK(FormatEncryptionMode(SqlEncryptionMode::DriverDefault).empty());
}

TEST_CASE("FormatEncryptionMode / ParseEncryptionMode round-trip", "[SqlConnectInfo]")
{
    for (auto const mode: { SqlEncryptionMode::Enabled, SqlEncryptionMode::Disabled })
        CHECK(ParseEncryptionMode(FormatEncryptionMode(mode)) == mode);
}

TEST_CASE("SqlConnectionDataSource: defaults to DriverDefault and emits no Encrypt keyword", "[SqlConnectInfo]")
{
    SqlConnectionDataSource const ds {
        .datasource = "MyDSN",
        .username = "alice",
        .password = "shh",
        .timeout = std::chrono::seconds { 12 },
    };

    // Regression guard: opting out must leave the rendering byte-for-byte as it was before
    // SQL_COPT_SS_ENCRYPT support was added, so existing deployments are unaffected.
    CHECK(ds.encryption == SqlEncryptionMode::DriverDefault);
    CHECK(ds.ToConnectionString().value == "DSN=MyDSN;UID=alice;PWD=shh;TIMEOUT=12");
}

TEST_CASE("SqlConnectionDataSource::ToConnectionString emits Encrypt only when opted in", "[SqlConnectInfo]")
{
    auto ds = SqlConnectionDataSource {
        .datasource = "MyDSN",
        .username = "alice",
        .password = "shh",
        .timeout = std::chrono::seconds { 12 },
    };

    ds.encryption = SqlEncryptionMode::Enabled;
    CHECK(ds.ToConnectionString().value == "DSN=MyDSN;UID=alice;PWD=shh;TIMEOUT=12;Encrypt=yes");

    ds.encryption = SqlEncryptionMode::Disabled;
    CHECK(ds.ToConnectionString().value == "DSN=MyDSN;UID=alice;PWD=shh;TIMEOUT=12;Encrypt=no");
}

TEST_CASE("SqlConnectionDataSource::FromConnectionString picks up Encrypt", "[SqlConnectInfo]")
{
    auto const enabled = SqlConnectionDataSource::FromConnectionString(SqlConnectionString { .value = "DSN=d;Encrypt=yes" });
    CHECK(enabled.encryption == SqlEncryptionMode::Enabled);

    auto const disabled = SqlConnectionDataSource::FromConnectionString(SqlConnectionString { .value = "DSN=d;ENCRYPT=No" });
    CHECK(disabled.encryption == SqlEncryptionMode::Disabled);

    auto const absent = SqlConnectionDataSource::FromConnectionString(SqlConnectionString { .value = "DSN=d" });
    CHECK(absent.encryption == SqlEncryptionMode::DriverDefault);
}

TEST_CASE("SqlConnectionDataSource: encryption survives the connection-string round-trip", "[SqlConnectInfo]")
{
    for (auto const mode: { SqlEncryptionMode::DriverDefault, SqlEncryptionMode::Disabled, SqlEncryptionMode::Enabled })
    {
        SqlConnectionDataSource const original {
            .datasource = "DS",
            .username = "u",
            .password = "p",
            .timeout = std::chrono::seconds { 5 },
            .encryption = mode,
        };

        CHECK(SqlConnectionDataSource::FromConnectionString(original.ToConnectionString()) == original);
    }
}

TEST_CASE("SqlConnectionDataSource: encryption participates in comparison", "[SqlConnectInfo]")
{
    // `username` and `password` carry no default member initializer, so a designated-initializer
    // list that skips them is incomplete; spell them out as the round-trip test above does.
    SqlConnectionDataSource const plaintext {
        .datasource = "A", .username = "u", .password = "p", .encryption = SqlEncryptionMode::Disabled
    };
    SqlConnectionDataSource const encrypted {
        .datasource = "A", .username = "u", .password = "p", .encryption = SqlEncryptionMode::Enabled
    };

    CHECK(plaintext != encrypted);
    CHECK(plaintext < encrypted);
}

TEST_CASE("SqlConnection::SetDefaultDataSource carries the encryption setting over", "[SqlConnectInfo]")
{
    auto const previous = SqlConnectionString { SqlConnection::DefaultConnectionString() };

    SqlConnection::SetDefaultDataSource(SqlConnectionDataSource {
        .datasource = "ProbeDSN",
        .username = "ProbeUser",
        .password = "ProbePass",
        .timeout = std::chrono::seconds { 7 },
        .encryption = SqlEncryptionMode::Enabled,
    });
    CHECK(SqlConnection::DefaultConnectionString().value.contains("Encrypt=yes"));

    SqlConnection::SetDefaultConnectionString(previous);
}
