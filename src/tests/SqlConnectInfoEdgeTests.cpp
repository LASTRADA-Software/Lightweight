// SPDX-License-Identifier: Apache-2.0

#include <Lightweight/SqlConnectInfo.hpp>
#include <Lightweight/SqlConnection.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <string>
#include <string_view>

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
        .password = "Probe;Pass", // the `;` must not become an attribute separator (#635)
        .timeout = std::chrono::seconds { 7 },
    };

    SqlConnection::SetDefaultDataSource(probe);
    auto const& current = SqlConnection::DefaultConnectionString();
    CHECK(current.value.contains("ProbeDSN"));
    CHECK(current.value.contains("ProbeUser"));
    CHECK(current.value.contains("TIMEOUT=7"));
    CHECK(ParseConnectionString(current).at("PWD") == "Probe;Pass");

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

TEST_CASE("FormatEncryptionMode: a mode outside the enumeration renders as no keyword", "[SqlConnectInfo]")
{
    // The enumeration has a fixed underlying type, so a value outside the named enumerators is a
    // representable value rather than undefined behaviour — it reaches Lightweight from an ABI
    // mismatch against a differently-versioned build, or from a plain cast. The keyword table lookup
    // then finds nothing, and the caller must get an empty spelling (which BuildConnectionString
    // omits) rather than a garbage `Encrypt=` value going to the driver.
    CHECK(FormatEncryptionMode(static_cast<SqlEncryptionMode>(99)).empty());
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

// ================================================================================================
// ODBC attribute-value quoting (issue #635)
//
// A connection string is `KEY=VALUE;...`, so a value carrying `;`, `{`, `}` or `=` has to be wrapped
// in braces, with every embedded `}` doubled — that is what the driver managers parse
// (unixODBC's __get_attr, and the Microsoft driver manager). Nothing here needs a database.
// ================================================================================================

TEST_CASE("FormatConnectionStringValue quotes only what the connection-string syntax would misread", "[SqlConnectInfo]")
{
    // Plain values keep their spelling, so existing renderings stay byte-for-byte unchanged.
    CHECK(FormatConnectionStringValue("plain") == "plain");
    CHECK(FormatConnectionStringValue("").empty());
    CHECK(FormatConnectionStringValue("with space inside") == "with space inside");

    // Metacharacters force braces, and an embedded `}` is doubled.
    CHECK(FormatConnectionStringValue("p;w") == "{p;w}");
    CHECK(FormatConnectionStringValue("a=b") == "{a=b}");
    CHECK(FormatConnectionStringValue("a}b") == "{a}}b}");
    CHECK(FormatConnectionStringValue("a{b") == "{a{b}");
    CHECK(FormatConnectionStringValue("}") == "{}}}");
    CHECK(FormatConnectionStringValue("{x}") == "{{x}}}");

    // Leading/trailing whitespace is trimmed by every parser, so it survives only inside braces.
    CHECK(FormatConnectionStringValue(" padded") == "{ padded}");
    CHECK(FormatConnectionStringValue("padded ") == "{padded }");
}

TEST_CASE("ParseConnectionString honours brace quoting", "[SqlConnectInfo]")
{
    struct Row
    {
        std::string_view rationale;
        std::string_view input;
        SqlConnectionStringMap expected;
    };
    auto const rows = std::array {
        Row { .rationale = "a `;` inside braces is part of the value, not a separator",
              .input = "DSN=d;UID={p;w};PWD={a=b;c};X=1",
              .expected = { { "DSN", "d" }, { "UID", "p;w" }, { "PWD", "a=b;c" }, { "X", "1" } } },
        Row { .rationale = "`}}` is one literal `}`",
              .input = "PWD={a}}b};UID={}}};DSN=d",
              .expected = { { "PWD", "a}b" }, { "UID", "}" }, { "DSN", "d" } } },
        Row { .rationale = "an unterminated brace swallows the rest of the string, as the driver managers do",
              .input = "DSN=d;PWD={a;b",
              .expected = { { "DSN", "d" }, { "PWD", "a;b" } } },
        Row { .rationale =
                  "`{` quotes only in the first position; a braced value followed by trailing text is taken verbatim",
              .input = "A=x{y;B={q}z;C={q}",
              .expected = { { "A", "x{y" }, { "B", "{q}z" }, { "C", "q" } } },
        Row { .rationale = "whitespace around a quoted value is trimmed, whitespace inside it is kept",
              .input = "A = { a b } ; B=c",
              .expected = { { "A", " a b " }, { "B", "c" } } },
    };
    for (auto const& [rationale, input, expected]: rows)
    {
        INFO(rationale);
        INFO(input);
        CHECK(ParseConnectionString(SqlConnectionString { .value = std::string(input) }) == expected);
    }
}

TEST_CASE("BuildConnectionString renders values so the map round-trips", "[SqlConnectInfo]")
{
    SqlConnectionStringMap input;
    input["DRIVER"] = "SQLite3";
    input["PWD"] = "a}b;c";
    input["UID"] = "}";

    auto const built = BuildConnectionString(input);
    // Plain values are not braced: the SQLite ODBC driver would keep braces as part of a file name.
    CHECK(built.value == "DRIVER=SQLite3;PWD={a}}b;c};UID={}}}");
    CHECK(ParseConnectionString(built) == input);
}

TEST_CASE("SqlConnectionDataSource::ToConnectionString quotes credentials that carry separators", "[SqlConnectInfo]")
{
    SqlConnectionDataSource const ds {
        .datasource = "My;DSN",
        .username = "us=er",
        .password = "p;w}",
        .timeout = std::chrono::seconds { 5 },
    };
    CHECK(ds.ToConnectionString().value == "DSN={My;DSN};UID={us=er};PWD={p;w}}};TIMEOUT=5");
}

TEST_CASE("SqlConnectionDataSource::ToConnectionString leaves plain credentials unquoted", "[SqlConnectInfo]")
{
    // The common case must stay byte-for-byte as before, so a DSN that never needed quoting is
    // rendered exactly as every existing deployment has seen it.
    SqlConnectionDataSource const ds {
        .datasource = "MyDSN",
        .username = "alice",
        .password = "pa ss.word!",
        .timeout = std::chrono::seconds { 5 },
    };
    CHECK(ds.ToConnectionString().value == "DSN=MyDSN;UID=alice;PWD=pa ss.word!;TIMEOUT=5");
}

TEST_CASE("SqlConnectionDataSource: credentials with separators survive the round-trip", "[SqlConnectInfo]")
{
    auto const passwords = { "p;w", "a}b", "{braced}", "x=y", "}", " lead", "trail ", ";{}=;" };
    for (auto const* password: passwords)
    {
        INFO(std::string { "password: " } + password);
        SqlConnectionDataSource const original {
            .datasource = "DS",
            .username = password,
            .password = password,
            .timeout = std::chrono::seconds { 5 },
        };
        CHECK(SqlConnectionDataSource::FromConnectionString(original.ToConnectionString()) == original);
    }
}
