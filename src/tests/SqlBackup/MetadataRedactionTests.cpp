// SPDX-License-Identifier: Apache-2.0
//
// Focused coverage for the credential redaction applied to the
// `original_connection_string` field written into a backup's metadata.json.
//
// The redaction is exercised through `RedactConnectionStringSecrets()` rather
// than through `CreateMetadata()`: the latter opens a real connection, so
// feeding it a connection string carrying a bogus password attribute breaks
// authentication on every DBMS that actually authenticates (PostgreSQL and
// SQL Server), while only appearing to work on SQLite, which ignores `PWD`.
// Testing the pure function keeps the coverage database-independent.

#include <Lightweight/SqlBackup/SqlBackup.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>

using Lightweight::SqlBackup::RedactConnectionStringSecrets;

TEST_CASE("SqlBackup: RedactConnectionStringSecrets masks PWD", "[SqlBackup][metadata][security]")
{
    auto const redacted =
        RedactConnectionStringSecrets("Driver={ODBC Driver 18 for SQL Server};UID=SA;PWD=topsecret;DATABASE=test");

    CHECK(redacted == "Driver={ODBC Driver 18 for SQL Server};UID=SA;PWD=***;DATABASE=test");
}

TEST_CASE("SqlBackup: RedactConnectionStringSecrets masks Password= case-insensitively", "[SqlBackup][metadata][security]")
{
    CHECK(RedactConnectionStringSecrets("Server=localhost;password=hunter2;Database=test")
          == "Server=localhost;password=***;Database=test");
    CHECK(RedactConnectionStringSecrets("Server=localhost;pWd=hunter2") == "Server=localhost;pWd=***");
}

TEST_CASE("SqlBackup: RedactConnectionStringSecrets masks a leading password attribute", "[SqlBackup][metadata][security]")
{
    CHECK(RedactConnectionStringSecrets("PWD=topsecret;Server=localhost") == "PWD=***;Server=localhost");
    CHECK(RedactConnectionStringSecrets("PWD=topsecret") == "PWD=***");
}

TEST_CASE("SqlBackup: RedactConnectionStringSecrets leaves non-secret attributes untouched",
          "[SqlBackup][metadata][security]")
{
    // No password at all — the string must round-trip byte-exact.
    CHECK(RedactConnectionStringSecrets("DRIVER=SQLite3;Database=test.db") == "DRIVER=SQLite3;Database=test.db");
    // `PWD`/`Password` must only match at an attribute boundary, never inside
    // another attribute's name or value.
    CHECK(RedactConnectionStringSecrets("Server=localhost;MyPWD=keepme") == "Server=localhost;MyPWD=keepme");
    CHECK(RedactConnectionStringSecrets("Database=PasswordVault") == "Database=PasswordVault");
    CHECK(RedactConnectionStringSecrets("").empty());
}

TEST_CASE("SqlBackup: RedactConnectionStringSecrets masks brace-quoted passwords whole", "[SqlBackup][metadata][security]")
{
    // ODBC allows `{...}` around an attribute value so it may contain `;`.
    // Scanning for the next `;` without honouring braces used to emit
    // `PWD=***;ss}` — half the password in cleartext inside the archive.
    CHECK(RedactConnectionStringSecrets("PWD={pa;ss}") == "PWD=***");
    CHECK(RedactConnectionStringSecrets("UID=SA;PWD={pa;ss};Database=test") == "UID=SA;PWD=***;Database=test");
    // `}}` is an escaped literal brace, so the value continues past it.
    CHECK(RedactConnectionStringSecrets("PWD={pa}};ss};Database=test") == "PWD=***;Database=test");
    // An unterminated brace swallows the rest of the string — nothing after it
    // may leak either.
    CHECK(RedactConnectionStringSecrets("PWD={pa;ss") == "PWD=***");
}

TEST_CASE("SqlBackup: RedactConnectionStringSecrets is not fooled by a `;` inside another brace-quoted value",
          "[SqlBackup][metadata][security]")
{
    // The `;` lives inside the driver's brace-quoted name, so `PWD` is still
    // the second attribute and must be redacted.
    CHECK(RedactConnectionStringSecrets("Driver={Weird;Driver};PWD=topsecret") == "Driver={Weird;Driver};PWD=***");
    // ... and a `PWD=` that is merely part of another attribute's quoted value
    // is not an attribute of its own, so it must survive untouched.
    CHECK(RedactConnectionStringSecrets("Driver={x;PWD=notreally};UID=SA") == "Driver={x;PWD=notreally};UID=SA");
}

TEST_CASE("SqlBackup: RedactConnectionStringSecrets tolerates whitespace around the attribute name",
          "[SqlBackup][metadata][security]")
{
    // A driver manager accepts blanks after the `;` separator and around the
    // key; the redaction must not treat them as "not at an attribute boundary"
    // and let the password through.
    CHECK(RedactConnectionStringSecrets("Server=localhost; PWD=topsecret") == "Server=localhost; PWD=***");
    CHECK(RedactConnectionStringSecrets("Server=localhost;\tPassword=topsecret") == "Server=localhost;\tPassword=***");
    CHECK(RedactConnectionStringSecrets("Server=localhost; PWD =topsecret") == "Server=localhost; PWD =***");
    CHECK(RedactConnectionStringSecrets(" PWD=topsecret") == " PWD=***");
    // Whitespace must not turn a non-secret key into a secret one.
    CHECK(RedactConnectionStringSecrets("Server=localhost; MyPWD=keepme") == "Server=localhost; MyPWD=keepme");
}

TEST_CASE("SqlBackup: RedactConnectionStringSecrets copies malformed segments through unchanged",
          "[SqlBackup][metadata][security]")
{
    // Segments without a `KEY=` pair must not disturb the attributes around
    // them (and must not cause the scan to lose its attribute boundaries).
    CHECK(RedactConnectionStringSecrets("garbage;PWD=topsecret") == "garbage;PWD=***");
    CHECK(RedactConnectionStringSecrets(";;PWD=topsecret;;") == ";;PWD=***;;");
    CHECK(RedactConnectionStringSecrets("Server=localhost;") == "Server=localhost;");
}
