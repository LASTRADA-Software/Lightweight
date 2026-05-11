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
